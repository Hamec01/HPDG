// HPDG Link (HamloRevolution docs/HPDG_LINK.md, V12 on the HPDG side): the snapshot equals the project, the slot
// round trip, heartbeat and close, a duplicated instance id, the seqlock under a concurrent reader, version refusal.

#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>

#include <juce_events/juce_events.h>

#include "Core/PatternProject.h"
#include "Services/HamloLinkProtocol.h"
#include "Services/HamloLinkPublisher.h"

namespace bbg
{
namespace
{
namespace link = hamlo::link;

NoteEvent note(int tick, int offset, int velocity, const char* role = "", bool ghost = false)
{
    NoteEvent n;
    n.gridTick = tick;
    n.timingOffsetTicks = offset;
    n.velocity = velocity;
    n.semanticRole = role;
    n.isGhost = ghost;
    n.lengthTicks = 120;
    return n;
}

PatternProject testProject()
{
    auto p = createDefaultProject();
    p.params.bpm = 87.5f;
    p.params.genre = GenreType::Trap;
    p.params.trapSubstyle = 3;
    p.params.swingPercent = 61.0f;
    p.params.timingAmount = 0.25f;
    p.params.humanizeAmount = 0.4f;
    p.params.densityAmount = 0.7f;
    p.params.velocityAmount = 0.6f;
    p.params.bars = 4;
    p.params.seed = 1234;
    p.params.keyRoot = 9;
    p.params.scaleMode = 2;
    p.intensity = 4;
    p.phraseLengthBars = 4;
    p.generationCounter = 7;
    p.mutationCounter = 2;
    for (auto& t : p.tracks)
    {
        t.notes.clear();
        t.enabled = true;
        t.muted = false;
        t.solo = false;
        if (t.type == TrackType::HiHat)
            for (int i = 0; i < 16; ++i)
                t.notes.push_back(note(i * 240, i % 2 == 1 ? 18 : 0, 90 + i, "hat_roll"));
        if (t.type == TrackType::Snare)
        {
            t.notes.push_back(note(960, -4, 120, "backbeat"));
            t.notes.push_back(note(2880, 0, 118, "backbeat"));
            t.notes.push_back(note(3600, 0, 40, "fill", true));
        }
        if (t.type == TrackType::Kick)
            t.notes.push_back(note(0, 0, 127, "anchor_accent"));
        if (t.type == TrackType::Perc)
        {
            t.notes.push_back(note(480, 0, 70, "perc"));
            t.muted = true; // not heard: not published
        }
    }
    return p;
}

} // namespace

class HamloLinkTests final : public juce::UnitTest
{
public:
    HamloLinkTests() : juce::UnitTest("HamloLink", "HPDG") {}

    void runTest() override
    {
        const auto project = testProject();

        beginTest("fillLinkSnapshot: every field from the project, the audible notes at their played ticks");
        {
            auto s = std::make_unique<link::Snapshot>();
            link::initialise(*s);
            fillLinkSnapshot(project, *s);
            expectEquals(s->bpm, 87.5f);
            expectEquals(s->genre, static_cast<std::int32_t>(link::Trap));
            expectEquals(s->substyle, 3);
            expectEquals(s->swingPercent, 61.0f);
            expectEquals(s->timing, 0.25f);
            expectEquals(s->humanize, 0.4f);
            expectEquals(s->density, 0.7f);
            expectEquals(s->velocityAmount, 0.6f);
            expectEquals(s->intensity, 4);
            expectEquals(s->keyRoot, 9);
            expectEquals(s->scaleMode, 2);
            expectEquals(s->bars, 4);
            expectEquals(s->phraseLengthBars, 4);
            expectEquals(s->seed, 1234);
            expectEquals(s->generationCounter, 7);
            expectEquals(s->mutationCounter, 2);
            expectEquals(s->eventCount, 16 + 3 + 1); // the muted perc lane left out
            int hats = 0, fills = 0, accents = 0, ghosts = 0;
            for (int i = 0; i < s->eventCount; ++i)
            {
                const auto& e = s->events[i];
                if (e.lane == link::HiHat)
                {
                    expectEquals(e.tick, hats * 240 + (hats % 2 == 1 ? 18 : 0), "hat tick");
                    expectEquals(static_cast<int>(e.velocity), 90 + hats);
                    expectEquals(link::roleName(*s, e.roleId), juce::String("hat_roll"));
                    ++hats;
                }
                if (e.lane == link::Snare && e.tick == 956)
                    expectEquals(link::roleName(*s, e.roleId), juce::String("backbeat"));
                fills += (e.flags & link::Fill) != 0;
                accents += (e.flags & link::Accent) != 0;
                ghosts += (e.flags & link::Ghost) != 0;
                expect(e.lane != link::Perc, "muted lane");
                expectEquals(e.lengthTicks, 120);
            }
            expectEquals(hats, 16);
            expectEquals(fills, 1);
            expectEquals(accents, 1);
            expectEquals(ghosts, 1);

            auto solo = project;
            for (auto& t : solo.tracks)
                t.solo = t.type == TrackType::Kick;
            link::initialise(*s);
            fillLinkSnapshot(solo, *s);
            expectEquals(s->eventCount, 1, "solo: only the kick");
        }

        beginTest("Publisher -> reader: the slot carries the snapshot exactly; listed with name, genre, tempo");
        std::uint64_t storedId = 0;
        std::uint32_t revision = 1;
        const auto hooksFor = [&](std::uint64_t& idStore)
        {
            HamloLinkPublisher::Hooks h;
            h.fill = [&project](link::Snapshot& s) { fillLinkSnapshot(project, s); };
            h.revision = [&revision] { return revision; };
            h.getInstanceId = [&idStore] { return idStore; };
            h.setInstanceId = [&idStore](std::uint64_t id) { idStore = id; };
            return h;
        };
        juce::File slotFile;
        std::unique_ptr<link::SharedSlot> keepOpen;
        {
            const auto now = juce::Time::currentTimeMillis();
            HamloLinkPublisher publisher(hooksFor(storedId));
            publisher.tick(now);
            expect(storedId != 0, "an instance id was made and stored");
            slotFile = publisher.getSlotFile();
            expect(slotFile.existsAsFile(), slotFile.getFullPathName());

            auto reader = link::SharedSlot::openForReading(slotFile);
            expect(reader != nullptr);
            auto got = std::make_unique<link::Snapshot>();
            expect(reader->read(*got) == link::Status::Ok);
            expect(link::check(*got, now) == link::Status::Ok);
            auto expected = std::make_unique<link::Snapshot>();
            link::initialise(*expected);
            expected->instanceId = storedId;
            fillLinkSnapshot(project, *expected);
            // the name is the publisher's; copy it so the payloads compare
            std::memcpy(expected->instanceName, got->instanceName, sizeof(expected->instanceName));
            expect(link::samePayload(*got, *expected), "payload");
            expectEquals(got->heartbeatMs, now);
            expect(juce::String(got->instanceName).startsWith("HPDG "), got->instanceName);

            bool listed = false;
            for (const auto& l : link::listInstances(now))
                if (l.instanceId == storedId)
                {
                    listed = l.status == link::Status::Ok && l.genre == link::Trap && l.bpm == 87.5f && l.name.startsWith("HPDG ");
                }
            expect(listed, "listInstances");

            beginTest("Heartbeat: no change = only the heartbeat moves (once a second); a change = a new payload");
            const auto seq = got->sequence;
            publisher.tick(now + 400);
            reader->read(*got);
            expectEquals(got->sequence, seq, "nothing written within the second");
            publisher.tick(now + 1100);
            reader->read(*got);
            expectEquals(got->heartbeatMs, now + 1100);
            expectEquals(got->sequence, seq + 2u);
            expect(link::samePayload(*got, *expected), "payload kept");
            expect(link::check(*got, now + 1100 + link::kAliveMs + 1) == link::Status::Stale, "stale after 2 s without a beat");

            ++revision; // same project: rebuilt but equal -> heartbeat only rules apply
            publisher.tick(now + 1200);
            reader->read(*got);
            expectEquals(got->sequence, seq + 2u, "an equal payload is not rewritten");

            beginTest("A duplicated instance (same stored id, the first alive): the second takes a new id");
            std::uint64_t copiedId = storedId;
            {
                HamloLinkPublisher twin(hooksFor(copiedId));
                twin.tick(juce::Time::currentTimeMillis());
                expect(copiedId != storedId && copiedId != 0, "new id");
                expect(twin.getSlotFile() != slotFile);
            }

            keepOpen = std::move(reader); // still mapped by a reader when the publisher goes
        }
        beginTest("Close: the publisher's end marks the slot closed (a reader sees it) and deletes the file");
        {
            auto got = std::make_unique<link::Snapshot>();
            expect(keepOpen != nullptr && keepOpen->read(*got) == link::Status::Ok);
            expectEquals(got->heartbeatMs, static_cast<std::int64_t>(0));
            expect(link::check(*got, juce::Time::currentTimeMillis()) == link::Status::Stale);
            keepOpen.reset();
            expect(!slotFile.existsAsFile(), "deleted");
        }

        beginTest("Seqlock: a reader thread never sees a half-written snapshot (20000 writes, 20000 reads)");
        {
            const auto file = link::slotFile(0x5eed5eed5eedULL);
            auto writer = link::SharedSlot::openForWriting(file);
            expect(writer != nullptr);
            auto a = std::make_unique<link::Snapshot>();
            auto b = std::make_unique<link::Snapshot>();
            for (auto* s : { a.get(), b.get() })
            {
                link::initialise(*s);
                const int marker = s == a.get() ? 111 : 222;
                s->eventCount = link::kMaxEvents;
                s->bpm = static_cast<float>(marker);
                for (auto& e : s->events)
                    e.tick = e.lengthTicks = marker;
            }
            writer->write(*a, 1);
            std::atomic<bool> stop { false };
            std::atomic<int> torn { 0 }, ok { 0 }, busy { 0 };
            std::thread readerThread([&]
            {
                auto r = link::SharedSlot::openForReading(file);
                auto s = std::make_unique<link::Snapshot>();
                for (int i = 0; i < 20000 && r != nullptr; ++i)
                {
                    if (r->read(*s) != link::Status::Ok)
                    {
                        ++busy;
                        continue;
                    }
                    ++ok;
                    const int marker = static_cast<int>(s->bpm);
                    for (const auto& e : s->events)
                        if (e.tick != marker || e.lengthTicks != marker)
                        {
                            ++torn;
                            break;
                        }
                }
                stop = true;
            });
            for (int i = 0; i < 20000 && !stop; ++i)
                writer->write(i % 2 == 0 ? *b : *a, i + 2);
            readerThread.join();
            logMessage("  reads ok " + juce::String(ok.load()) + ", busy " + juce::String(busy.load()) + ", torn " + juce::String(torn.load()));
            expectEquals(torn.load(), 0);
            expect(ok.load() > 0);
            writer.reset();
            file.deleteFile();
        }

        beginTest("Versions: a newer major is refused, a newer minor accepted, a foreign file is not a slot");
        {
            auto s = std::make_unique<link::Snapshot>();
            link::initialise(*s);
            s->heartbeatMs = 1000;
            expect(link::check(*s, 1500) == link::Status::Ok);
            s->versionMinor = link::kVersionMinor + 3;
            expect(link::check(*s, 1500) == link::Status::Ok);
            s->versionMajor = link::kVersionMajor + 1;
            expect(link::check(*s, 1500) == link::Status::NewerMajor);
            s->magic[0] = 'X';
            expect(link::check(*s, 1500) == link::Status::Missing);
            const auto junk = link::linkDirectory().getChildFile("hpdg-0000000000000bad.slot");
            junk.replaceWithText("not a slot");
            expect(link::SharedSlot::openForReading(junk) == nullptr, "too short");
            junk.deleteFile();
        }
    }
};

static HamloLinkTests hamloLinkTests;
} // namespace bbg

namespace
{
struct PrintingRunner final : juce::UnitTestRunner
{
    void logMessage(const juce::String& message) override { std::printf("%s\n", message.toRawUTF8()); }
};
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    PrintingRunner runner;
    runner.setAssertOnFailure(false);
    runner.runTestsInCategory("HPDG");
    int failures = 0, passes = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        failures += runner.getResult(i)->failures;
        passes += runner.getResult(i)->passes;
    }
    std::printf("HPDG_HamloLinkTests: %d passed, %d failed\n", passes, failures);
    return failures == 0 ? 0 : 1;
}
