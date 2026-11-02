#include "HamloLinkPublisher.h"

#include <cstring>

namespace bbg
{
namespace
{
namespace link = hamlo::link;

int substyleOf(const GeneratorParams& p)
{
    switch (p.genre)
    {
        case GenreType::BoomBap: return p.boombapSubstyle;
        case GenreType::Rap: return p.rapSubstyle;
        case GenreType::Trap: return p.trapSubstyle;
        case GenreType::Drill: return p.drillSubstyle;
        case GenreType::DnB: return p.dnbSubstyle;
        case GenreType::Techno: return p.technoSubstyle;
        case GenreType::Atmospheric: return p.atmosphericSubstyle;
    }
    return 0;
}

void copyText(char* dest, int capacity, const juce::String& text)
{
    std::memset(dest, 0, static_cast<size_t>(capacity));
    const auto utf8 = text.toRawUTF8();
    std::memcpy(dest, utf8, juce::jmin(static_cast<size_t>(capacity - 1), std::strlen(utf8)));
}

int roleIdFor(link::Snapshot& s, int& roles, const juce::String& role)
{
    if (role.isEmpty())
        return 0;
    for (int i = 1; i < roles; ++i)
        if (role == link::roleName(s, i))
            return i;
    if (roles >= link::kMaxRoles)
        return 0; // more than 31 roles: the rest unnamed
    copyText(s.roleNames[roles], link::kRoleChars, role);
    return roles++;
}
} // namespace

void fillLinkSnapshot(const PatternProject& project, link::Snapshot& s)
{
    const auto& p = project.params;
    s.bpm = p.bpm;
    s.genre = static_cast<std::int32_t>(p.genre);
    s.substyle = substyleOf(p);
    s.swingPercent = p.swingPercent;
    s.timing = p.timingAmount;
    s.humanize = p.humanizeAmount;
    s.density = p.densityAmount;
    s.velocityAmount = p.velocityAmount;
    s.intensity = project.intensity;
    s.keyRoot = p.keyRoot;
    s.scaleMode = p.scaleMode;
    s.bars = p.bars;
    s.phraseLengthBars = project.phraseLengthBars;
    s.seed = p.seed;
    s.generationCounter = project.generationCounter;
    s.mutationCounter = project.mutationCounter;

    bool anySolo = false;
    for (const auto& t : project.tracks)
        anySolo = anySolo || (t.enabled && t.solo);
    const int totalTicks = juce::jmax(1, p.bars) * 4 * link::kPpq;
    int count = 0, roles = 1;
    std::memset(s.events, 0, sizeof(s.events));
    std::memset(s.roleNames, 0, sizeof(s.roleNames));
    for (const auto& t : project.tracks)
    {
        if (!t.enabled || t.muted || (anySolo && !t.solo))
            continue;
        const auto lane = static_cast<std::uint8_t>(t.runtimeTrackType.value_or(t.type));
        for (const auto& n : t.notes)
        {
            if (count >= link::kMaxEvents)
                break;
            auto& e = s.events[count++];
            e.lane = lane;
            e.velocity = static_cast<std::uint8_t>(juce::jlimit(1, 127, n.velocity));
            e.tick = juce::jlimit(0, totalTicks - 1, n.startTick());
            e.lengthTicks = juce::jmax(1, n.lengthTicks);
            const auto role = n.semanticRole.toLowerCase();
            e.flags = static_cast<std::uint8_t>((n.isGhost ? link::Ghost : 0) | (n.isSlide ? link::Slide : 0)
                                                | (role.contains("fill") ? link::Fill : 0) | (role.contains("accent") ? link::Accent : 0));
            e.roleId = static_cast<std::uint8_t>(roleIdFor(s, roles, n.semanticRole));
        }
    }
    s.eventCount = count;
}

HamloLinkPublisher::HamloLinkPublisher(Hooks h)
    : hooks(std::move(h)), scratch(std::make_unique<link::Snapshot>()), last(std::make_unique<link::Snapshot>())
{
    link::initialise(*last);
    link::removeDeadSlots(juce::Time::currentTimeMillis());
    startTimerHz(4);
}

HamloLinkPublisher::~HamloLinkPublisher()
{
    stopTimer();
    close();
}

void HamloLinkPublisher::close()
{
    if (slot == nullptr)
        return;
    slot->beat(0); // closed: readers see it at once
    slot.reset();
    link::slotFile(currentId).deleteFile();
}

juce::File HamloLinkPublisher::getSlotFile() const
{
    return currentId != 0 ? link::slotFile(currentId) : juce::File();
}

void HamloLinkPublisher::timerCallback()
{
    tick(juce::Time::currentTimeMillis());
}

void HamloLinkPublisher::tick(juce::int64 nowMs)
{
    auto id = hooks.getInstanceId ? hooks.getInstanceId() : 0;
    if (id != currentId || slot == nullptr)
    {
        close();
        // A new id, or one already alive in another instance (the DAW duplicated the track and its state): a fresh one.
        bool taken = id == 0;
        if (!taken)
            if (auto other = link::SharedSlot::openForReading(link::slotFile(id)))
            {
                if (other->read(*scratch) == link::Status::Ok)
                    taken = link::check(*scratch, nowMs) == link::Status::Ok;
            }
        if (taken)
        {
            juce::Random random;
            do
                id = static_cast<std::uint64_t>(random.nextInt64());
            while (id == 0);
            if (hooks.setInstanceId)
                hooks.setInstanceId(id);
        }
        currentId = id;
        slot = link::SharedSlot::openForWriting(link::slotFile(id));
        written = false;
        if (slot == nullptr)
            return; // no slot (disk, rights): try again next tick
    }

    const auto revision = hooks.revision ? hooks.revision() : 0u;
    if (!written || revision != lastRevision)
    {
        link::initialise(*scratch);
        scratch->instanceId = currentId;
        copyText(scratch->instanceName, link::kNameChars,
                 "HPDG " + juce::String::toHexString(static_cast<juce::int64>(currentId & 0xffff)).paddedLeft('0', 4).toUpperCase());
        if (hooks.fill)
            hooks.fill(*scratch);
        lastRevision = revision;
        if (!written || !link::samePayload(*scratch, *last))
        {
            slot->write(*scratch, nowMs);
            std::swap(scratch, last);
            lastWriteMs = nowMs;
            written = true;
            return;
        }
    }
    if (nowMs - lastWriteMs >= link::kHeartbeatMs)
    {
        slot->beat(nowMs);
        lastWriteMs = nowMs;
    }
}
} // namespace bbg
