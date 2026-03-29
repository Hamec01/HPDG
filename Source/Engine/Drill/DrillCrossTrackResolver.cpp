#include "DrillCrossTrackResolver.h"

#include <algorithm>
#include <iterator>

#include "../../Core/TrackSemantics.h"

namespace bbg
{
namespace
{
TrackState* findTrack(PatternProject& project, TrackType type)
{
    for (auto& track : project.tracks)
    {
        if (track.type == type)
            return &track;
    }

    return nullptr;
}

bool stepHasNote(const TrackState* track, int step, bool nonGhostOnly = false)
{
    if (track == nullptr)
        return false;

    return std::any_of(track->notes.begin(), track->notes.end(), [step, nonGhostOnly](const NoteEvent& n)
    {
        return n.step == step && (!nonGhostOnly || !n.isGhost);
    });
}

int eventWindowIndex(int step)
{
    const int stepInBar = ((step % 16) + 16) % 16;
    return std::clamp(stepInBar / 4, 0, 3);
}

bool isStrongSubEvent(const NoteEvent& note)
{
    return (note.step % 16) >= 10 && (note.length >= 3 || note.velocity >= 112);
}
}

void DrillCrossTrackResolver::resolve(PatternProject& project,
                                      const DrillStyleProfile& style,
                                      const DrillGrooveBlueprint& blueprint,
                                      const std::unordered_set<TrackType>& mutableTracks) const
{
    const auto styleSpec = getDrillStyleSpec(style.substyle);

    auto* snare = findTrack(project, TrackType::Snare);
    auto* clap = findTrack(project, TrackType::ClapGhostSnare);
    auto* kick = findTrack(project, TrackType::Kick);
    auto* sub = findTrack(project, TrackType::Sub808);
    auto* openHat = findTrack(project, TrackType::OpenHat);
    auto* hatFx = findTrack(project, TrackType::HatFX);

    // Enforce snare-protection windows against heavy top/bass clutter.
    for (auto& track : project.tracks)
    {
        if (mutableTracks.count(track.type) == 0 || track.locked)
            continue;

        if (!bbg::isHatFamily(track.type) && track.type != TrackType::Sub808 && track.type != TrackType::GhostKick)
            continue;

        track.notes.erase(std::remove_if(track.notes.begin(), track.notes.end(), [&](const NoteEvent& note)
        {
            const auto* slot = blueprint.slotAt(note.step);
            if (slot == nullptr || !slot->snareProtection)
                return false;

            const bool keepSnareLayer = (track.type == TrackType::ClapGhostSnare) && note.isGhost;
            if (keepSnareLayer)
                return false;

            if (track.type == TrackType::OpenHat || track.type == TrackType::HatFX)
                return true;
            if (track.type == TrackType::Perc && note.velocity > 72)
                return true;
            if (track.type == TrackType::Sub808 && (note.step % 16) >= 13)
                return true;

            return false;
        }), track.notes.end());
    }

    // Reserve one dominant major event per phrase-edge micro window.
    for (int step = 0; step < static_cast<int>(blueprint.slots.size()); ++step)
    {
        const auto* slot = blueprint.slotAt(step);
        if (slot == nullptr || slot->phraseEdgeWeight < 0.7f)
            continue;

        const bool hasOpenHat = stepHasNote(openHat, step, true);
        const bool hasHatFx = stepHasNote(hatFx, step, false);
        const bool hasSub = stepHasNote(sub, step, false);
        const bool hasKick = stepHasNote(kick, step, true);

        int majorCount = 0;
        majorCount += hasOpenHat ? 1 : 0;
        majorCount += hasHatFx ? 1 : 0;
        majorCount += hasSub ? 1 : 0;
        majorCount += hasKick ? 1 : 0;

        if (majorCount <= 1)
            continue;

        // Priority: keep sub or kick pulse, then one top event.
        if (hasSub && hasKick)
        {
            if (openHat != nullptr)
            {
                openHat->notes.erase(std::remove_if(openHat->notes.begin(), openHat->notes.end(), [step](const NoteEvent& n) { return n.step == step; }), openHat->notes.end());
            }
            if (hatFx != nullptr)
            {
                hatFx->notes.erase(std::remove_if(hatFx->notes.begin(), hatFx->notes.end(), [step](const NoteEvent& n) { return n.step == step; }), hatFx->notes.end());
            }
            continue;
        }

        if (hasHatFx && hasOpenHat)
        {
            if (hatFx != nullptr)
            {
                hatFx->notes.erase(std::remove_if(hatFx->notes.begin(), hatFx->notes.end(), [step](const NoteEvent& n) { return n.step == step; }), hatFx->notes.end());
            }
        }
    }

    // Enforce shared major-event budget per bar and per two-bar window.
    const int bars = std::max(1, project.params.bars);
    auto majorCountInBar = [&](int bar)
    {
        const int start = bar * 16;
        const int end = start + 16;
        int count = 0;

        auto countTrack = [&](const TrackState* track, bool gateToEdges)
        {
            if (track == nullptr)
                return;

            for (const auto& n : track->notes)
            {
                if (n.step < start || n.step >= end)
                    continue;
                if (gateToEdges && (n.step % 16) < 10)
                    continue;
                ++count;
            }
        };

        countTrack(hatFx, false);
        countTrack(openHat, false);
        countTrack(kick, true);
        countTrack(sub, true);
        return count;
    };

    auto trimOneLatestInBar = [&](TrackState* track, int bar, bool edgeOnly)
    {
        if (track == nullptr)
            return false;

        const int start = bar * 16;
        const int end = start + 16;
        for (auto it = track->notes.rbegin(); it != track->notes.rend(); ++it)
        {
            if (it->step < start || it->step >= end)
                continue;
            if (edgeOnly && (it->step % 16) < 10)
                continue;

            track->notes.erase(std::next(it).base());
            return true;
        }
        return false;
    };

    for (int bar = 0; bar < bars; ++bar)
    {
        int guard = 64;
        while (majorCountInBar(bar) > styleSpec.maxMajorEventsPerBar && guard-- > 0)
        {
            if (trimOneLatestInBar(hatFx, bar, false))
                continue;
            if (trimOneLatestInBar(openHat, bar, false))
                continue;
            if (trimOneLatestInBar(sub, bar, true))
                continue;
            if (!trimOneLatestInBar(kick, bar, true))
                break;
        }
    }

    for (int bar = 0; bars >= 2 && bar < bars; bar += 2)
    {
        const int first = bar;
        const int second = std::min(bars - 1, bar + 1);
        int guard = 64;
        while ((majorCountInBar(first) + majorCountInBar(second)) > styleSpec.maxMajorEventsPer2Bars && guard-- > 0)
        {
            if (trimOneLatestInBar(hatFx, second, false) || trimOneLatestInBar(hatFx, first, false))
                continue;
            if (trimOneLatestInBar(openHat, second, false) || trimOneLatestInBar(openHat, first, false))
                continue;
            if (trimOneLatestInBar(sub, second, true) || trimOneLatestInBar(sub, first, true))
                continue;
            if (!trimOneLatestInBar(kick, second, true) && !trimOneLatestInBar(kick, first, true))
                break;
        }
    }

    // Enforce one dominant major event in each 1/4-bar window.
    for (int bar = 0; bar < bars; ++bar)
    {
        for (int window = 0; window < 4; ++window)
        {
            const auto inWindow = [&](const NoteEvent& n)
            {
                return n.step / 16 == bar && eventWindowIndex(n.step) == window;
            };

            const bool kickPresent = kick != nullptr && std::any_of(kick->notes.begin(), kick->notes.end(), inWindow);
            const bool subPresent = sub != nullptr && std::any_of(sub->notes.begin(), sub->notes.end(), [&](const NoteEvent& n)
            {
                return inWindow(n) && isStrongSubEvent(n);
            });
            const bool openPresent = openHat != nullptr && std::any_of(openHat->notes.begin(), openHat->notes.end(), inWindow);
            const bool fxPresent = hatFx != nullptr && std::any_of(hatFx->notes.begin(), hatFx->notes.end(), inWindow);

            int majorCount = 0;
            majorCount += kickPresent ? 1 : 0;
            majorCount += subPresent ? 1 : 0;
            majorCount += openPresent ? 1 : 0;
            majorCount += fxPresent ? 1 : 0;
            if (majorCount <= 1)
                continue;

            const bool keepKick = kickPresent;
            const bool keepSub = !keepKick && subPresent;
            const bool keepOpen = !keepKick && !keepSub && openPresent;

            if (hatFx != nullptr && (keepKick || keepSub || keepOpen))
            {
                hatFx->notes.erase(std::remove_if(hatFx->notes.begin(), hatFx->notes.end(), [&](const NoteEvent& n)
                {
                    return inWindow(n);
                }), hatFx->notes.end());
            }

            if (openHat != nullptr && (keepKick || keepSub))
            {
                openHat->notes.erase(std::remove_if(openHat->notes.begin(), openHat->notes.end(), [&](const NoteEvent& n)
                {
                    return inWindow(n);
                }), openHat->notes.end());
            }

            if (sub != nullptr && keepKick)
            {
                sub->notes.erase(std::remove_if(sub->notes.begin(), sub->notes.end(), [&](const NoteEvent& n)
                {
                    return inWindow(n) && isStrongSubEvent(n);
                }), sub->notes.end());
            }
        }
    }

    // Simplify 808 if top layer is dense in a bar.
    if (sub != nullptr && !sub->locked && mutableTracks.count(sub->type) != 0)
    {
        for (int bar = 0; bar < bars; ++bar)
        {
            int topHits = 0;
            for (TrackType type : { TrackType::HiHat, TrackType::HatFX, TrackType::OpenHat, TrackType::Perc })
            {
                if (const auto* t = findTrack(project, type); t != nullptr)
                {
                    for (const auto& n : t->notes)
                    {
                        if (n.step / 16 == bar)
                            ++topHits;
                    }
                }
            }

            if (topHits < styleSpec.denseTopHitsThresholdPerBar)
                continue;

            bool firstInBar = true;
            for (auto& n : sub->notes)
            {
                if (n.step / 16 != bar)
                    continue;

                if (firstInBar)
                {
                    n.length = std::max(n.length, style.substyle == DrillSubstyle::DarkDrill ? 6 : 4);
                    firstInBar = false;
                }
                else
                {
                    n.velocity = std::max(style.sub808VelocityMin, n.velocity - 12);
                    if ((n.step % 16) > 10)
                        n.length = std::min(n.length, 2);
                }
            }
        }
    }

    // Keep kick off strong snare hits.
    if (kick != nullptr && !kick->locked && mutableTracks.count(kick->type) != 0)
    {
        kick->notes.erase(std::remove_if(kick->notes.begin(), kick->notes.end(), [&](const NoteEvent& n)
        {
            if (!(stepHasNote(snare, n.step, true) || stepHasNote(clap, n.step, true)))
                return false;

            const auto* slot = blueprint.slotAt(n.step);
            if (slot == nullptr)
                return true;

            // Keep structural anchors when conflict is intentional; trim mainly support clutter.
            if (slot->kickEventType == DrillKickEventType::AnchorKick)
                return false;
            return true;
        }), kick->notes.end());
    }
}
} // namespace bbg
