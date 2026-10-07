#pragma once

// Shared pattern metrics of the quality tools (GenerationQualityLab, ScorerAudit): the same
// numbers for every genre, computed on the written PatternProject notes.

#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

#include "../Source/Core/PatternProject.h"
#include "../Source/Core/ProjectLaneAccess.h"

namespace bbg::quality
{
constexpr int kStep = 240;     // 1/16
constexpr int kBar = 3840;

std::vector<int> backbeatSteps(GenreType genre)
{
    if (genre == GenreType::Trap)
        return { 8 };
    return { 4, 12 };
}

const std::vector<TrackType>& drumLanes()
{
    static const std::vector<TrackType> lanes { TrackType::Kick, TrackType::GhostKick, TrackType::Snare, TrackType::ClapGhostSnare,
                                                TrackType::HiHat, TrackType::HatFX, TrackType::OpenHat, TrackType::Ride,
                                                TrackType::Cymbal, TrackType::Perc };
    return lanes;
}

float metricWeight(int step)
{
    step = ((step % 16) + 16) % 16;
    if (step == 0) return 1.0f;
    if (step == 8) return 0.75f;
    if (step % 4 == 0) return 0.6f;
    if (step % 2 == 0) return 0.35f;
    return 0.15f;
}

struct Metrics
{
    // kick
    float kicksPerBar = 0, kickDownbeatRate = 0, kickStrongBeatRatio = 0, kickSyncopation = 0, kickConsecutiveRate = 0;
    float kickSnareCollisionsPerBar = 0, kickUniqueBarRatio = 0;
    // snare
    float backbeatCoverage = 0, snareGhostRate = 0, extraSnaresPerBar = 0, ghostLouderThanAnchor = 0, snareInvalidPerBar = 0;
    // hats
    float hatsPerBar = 0, hatEighthCoverage = 0, hatMaxGapSteps = 0, hatRollNotesPerBar = 0, openClosedCollisions = 0, openHatsPerBar = 0;
    // phrase
    float barSimilarity = 0, lastBarDifference = 0, identicalBarRate = 0;
    // dynamics
    float velocityStd = 0, identicalVelocityRate = 0;
    // timing
    float anchorTimingMeanAbs = 0, secondaryTimingMeanAbs = 0, timingOutlierRate = 0;
    // bass
    float bassNotesPerBar = 0, bassOnKickRate = 0;
    // overall
    float eventsPerBar = 0, syncopation = 0, bpm = 0;
    int hardFailures = 0;
    juce::String failure;
    double ms = 0;
    juce::String skeleton;      // drums, 32nd resolution, no velocity / offset
    juce::String kickSkeleton;
    juce::String snareSkeleton;   // Snare + ClapGhost, 32nd resolution
    juce::String hatSkeleton;     // HiHat + OpenHat + HatFX
    juce::String otherSkeleton;   // GhostKick, Ride, Cymbal, Perc
    juce::String firstTwoBars;    // all drums of bars 1-2 (the groove core)
    juce::String firstTwoKick;
    juce::String kickBarsText;    // kick 16ths per bar, "0 6 10|0 7 10 13|..." (reference comparison)
    juce::String hatBarsText;     // closed-hat 16ths per bar, same format
    std::set<std::pair<int, int>> skeletonSet;
};

std::vector<NoteEvent> notesOf(const PatternProject& project, TrackType lane)
{
    const auto* state = ProjectLaneAccess::findTrackState(project, lane);
    return state != nullptr && state->enabled && !state->muted ? state->notes : std::vector<NoteEvent> {};
}

// The 16th a note belongs to (a 32nd at x.5 stays in step x: rounding moved the last 32nd of
// a fill onto the next bar's downbeat and reported "snare on 1").
int stepOf(const NoteEvent& n) { return n.gridTick >= 0 ? n.gridTick / kStep : 0; }
bool onStep(const NoteEvent& n) { return n.gridTick % kStep == 0; }

Metrics measure(const PatternProject& project, GenreType genre)
{
    Metrics m;
    const int bars = std::max(1, project.params.bars);
    const float fb = static_cast<float>(bars);
    m.bpm = project.params.bpm;

    const auto kick = notesOf(project, TrackType::Kick);
    const auto snare = notesOf(project, TrackType::Snare);
    const auto hats = notesOf(project, TrackType::HiHat);
    const auto open = notesOf(project, TrackType::OpenHat);
    const auto bass = notesOf(project, TrackType::Sub808);

    // --- kick
    std::vector<std::set<int>> kickBars(static_cast<size_t>(bars));
    for (const auto& n : kick)
        if (!n.isGhost && stepOf(n) / 16 < bars)
            kickBars[static_cast<size_t>(stepOf(n) / 16)].insert(stepOf(n) % 16);
    int kicks = 0, downbeats = 0, strong = 0, consecutive = 0;
    for (const auto& bar : kickBars)
    {
        kicks += static_cast<int>(bar.size());
        downbeats += bar.count(0) > 0 ? 1 : 0;
        for (const int s : bar)
        {
            strong += s % 4 == 0 ? 1 : 0;
            consecutive += bar.count(s + 1) > 0 ? 1 : 0;
        }
    }
    m.kicksPerBar = kicks / fb;
    m.kickDownbeatRate = downbeats / fb;
    m.kickStrongBeatRatio = kicks > 0 ? strong / static_cast<float>(kicks) : 0.0f;
    m.kickConsecutiveRate = kicks > 0 ? consecutive / static_cast<float>(kicks) : 0.0f;
    std::set<std::set<int>> uniqueKickBars(kickBars.begin(), kickBars.end());
    m.kickUniqueBarRatio = uniqueKickBars.size() / fb;

    // LHL syncopation of one lane set per bar.
    auto syncopationOf = [&](const std::vector<std::set<int>>& lanes)
    {
        float total = 0.0f;
        for (const auto& on : lanes)
            for (const int n : on)
            {
                int r = n + 1;
                while (r < 16 && metricWeight(r) <= metricWeight(n))
                    ++r;
                const bool silent = r >= 16 ? false : on.count(r) == 0;
                if (silent)
                    total += metricWeight(r) - metricWeight(n);
            }
        return total / fb;
    };
    m.kickSyncopation = syncopationOf(kickBars);

    // --- snare
    std::vector<std::set<int>> snareBars(static_cast<size_t>(bars));
    int ghosts = 0, anchors = 0, ghostLoud = 0, invalid = 0;
    const auto backbeat = backbeatSteps(genre);
    for (const auto& n : snare)
    {
        const int s = stepOf(n);
        if (s / 16 >= bars)
            continue;
        if (n.isGhost)
        {
            ++ghosts;
            // the anchor of the same bar
            int anchorVelocity = 0;
            for (const auto& a : snare)
                if (!a.isGhost && stepOf(a) / 16 == s / 16)
                    anchorVelocity = std::max(anchorVelocity, a.velocity);
            ghostLoud += anchorVelocity > 0 && n.velocity >= anchorVelocity ? 1 : 0;
            continue;
        }
        ++anchors;
        snareBars[static_cast<size_t>(s / 16)].insert(s % 16);
        if (genre != GenreType::Trap && onStep(n) && (s % 16 == 0 || s % 16 == 8))
            ++invalid;
    }
    int covered = 0;
    for (const auto& bar : snareBars)
        for (const int b : backbeat)
            covered += bar.count(b) > 0 ? 1 : 0;
    m.backbeatCoverage = covered / (fb * static_cast<float>(backbeat.size()));
    m.snareGhostRate = (ghosts + anchors) > 0 ? ghosts / static_cast<float>(ghosts + anchors) : 0.0f;
    m.extraSnaresPerBar = std::max(0.0f, (anchors - covered) / fb);
    m.ghostLouderThanAnchor = static_cast<float>(ghostLoud);
    m.snareInvalidPerBar = invalid / fb;
    for (int bar = 0; bar < bars; ++bar)
        for (const int s : kickBars[static_cast<size_t>(bar)])
            m.kickSnareCollisionsPerBar += snareBars[static_cast<size_t>(bar)].count(s) > 0 ? 1.0f : 0.0f;
    m.kickSnareCollisionsPerBar /= fb;

    // --- hats
    std::vector<std::set<int>> hatBars(static_cast<size_t>(bars));
    int rollNotes = 0;
    for (const auto& n : hats)
    {
        if (n.gridTick % kStep != 0)
            ++rollNotes;
        if (stepOf(n) / 16 < bars)
            hatBars[static_cast<size_t>(stepOf(n) / 16)].insert(stepOf(n) % 16);
    }
    rollNotes += static_cast<int>(notesOf(project, TrackType::HatFX).size());
    std::set<int> openSteps;
    for (const auto& n : open)
        openSteps.insert(stepOf(n));
    m.hatsPerBar = hats.size() / fb;
    m.openHatsPerBar = open.size() / fb;
    m.hatRollNotesPerBar = rollNotes / fb;
    int eighths = 0, maxGap = 0;
    for (int bar = 0; bar < bars; ++bar)
    {
        const auto& on = hatBars[static_cast<size_t>(bar)];
        for (int s = 0; s < 16; s += 2)
            eighths += (on.count(s) > 0 || on.count(s + 1) > 0 || openSteps.count(bar * 16 + s) > 0) ? 1 : 0;
    }
    std::vector<int> allHat;
    for (const auto& n : hats)
        allHat.push_back(stepOf(n));
    for (const auto& n : open)
        allHat.push_back(stepOf(n));
    std::sort(allHat.begin(), allHat.end());
    allHat.erase(std::unique(allHat.begin(), allHat.end()), allHat.end());
    for (size_t i = 0; i < allHat.size(); ++i)
    {
        const int next = i + 1 < allHat.size() ? allHat[i + 1] : allHat.front() + bars * 16;
        maxGap = std::max(maxGap, next - allHat[i]);
    }
    if (allHat.empty())
        maxGap = bars * 16;
    for (const auto& bar : hatBars)
    {
        bool first = true;
        for (const int s : bar)
        {
            m.hatBarsText << (first ? "" : " ") << s;
            first = false;
        }
        m.hatBarsText << "|";
    }
    m.hatEighthCoverage = eighths / (fb * 8.0f);
    m.hatMaxGapSteps = static_cast<float>(maxGap);
    for (const auto& n : hats)
        m.openClosedCollisions += openSteps.count(stepOf(n)) > 0 ? 1.0f : 0.0f;

    // --- whole drum skeleton: phrase similarity, duplicates
    std::vector<std::set<std::pair<int, int>>> barSets(static_cast<size_t>(bars));
    int events = 0;
    std::vector<int> velocities;
    int identicalVelocityPairs = 0, velocityPairs = 0;
    double anchorAbs = 0, secondaryAbs = 0;
    int anchorCount = 0, secondaryCount = 0, outliers = 0;
    std::vector<std::set<int>> allLaneBars(static_cast<size_t>(bars));
    for (const auto lane : drumLanes())
    {
        const auto notes = notesOf(project, lane);
        int previousVelocity = -1;
        for (const auto& n : notes)
        {
            const int s32 = static_cast<int>(std::lround(n.gridTick / 120.0));
            const int bar = s32 / 32;
            if (bar >= bars)
                continue;
            ++events;
            barSets[static_cast<size_t>(bar)].insert({ static_cast<int>(lane), s32 % 32 });
            m.skeletonSet.insert({ static_cast<int>(lane), s32 });
            velocities.push_back(n.velocity);
            if (previousVelocity >= 0)
            {
                ++velocityPairs;
                identicalVelocityPairs += previousVelocity == n.velocity ? 1 : 0;
            }
            previousVelocity = n.velocity;
            const bool anchor = (lane == TrackType::Kick || lane == TrackType::Snare) && !n.isGhost;
            const int offset = std::abs(n.timingOffsetTicks);
            (anchor ? anchorAbs : secondaryAbs) += offset;
            (anchor ? anchorCount : secondaryCount) += 1;
            outliers += offset > 60 ? 1 : 0;
            if (lane != TrackType::Cymbal && lane != TrackType::HatFX)
                allLaneBars[static_cast<size_t>(bar)].insert(stepOf(n) % 16 + 16 * static_cast<int>(lane));
        }
    }
    m.eventsPerBar = events / fb;
    float jaccardSum = 0.0f;
    int identical = 0;
    for (int bar = 1; bar < bars; ++bar)
    {
        const auto& a = barSets[static_cast<size_t>(bar - 1)];
        const auto& b = barSets[static_cast<size_t>(bar)];
        int inter = 0;
        for (const auto& x : a)
            inter += b.count(x) > 0 ? 1 : 0;
        const int uni = static_cast<int>(a.size() + b.size()) - inter;
        jaccardSum += uni > 0 ? inter / static_cast<float>(uni) : 1.0f;
        identical += a == b ? 1 : 0;
    }
    m.barSimilarity = bars > 1 ? jaccardSum / (fb - 1.0f) : 1.0f;
    m.identicalBarRate = bars > 1 ? identical / (fb - 1.0f) : 1.0f;
    if (bars > 1)
    {
        const auto& a = barSets.front();
        const auto& b = barSets.back();
        int inter = 0;
        for (const auto& x : a)
            inter += b.count(x) > 0 ? 1 : 0;
        const int uni = static_cast<int>(a.size() + b.size()) - inter;
        m.lastBarDifference = uni > 0 ? 1.0f - inter / static_cast<float>(uni) : 0.0f;
    }
    if (!velocities.empty())
    {
        double mean = 0;
        for (const int v : velocities)
            mean += v;
        mean /= velocities.size();
        double var = 0;
        for (const int v : velocities)
            var += (v - mean) * (v - mean);
        m.velocityStd = static_cast<float>(std::sqrt(var / velocities.size()));
    }
    m.identicalVelocityRate = velocityPairs > 0 ? identicalVelocityPairs / static_cast<float>(velocityPairs) : 0.0f;
    m.anchorTimingMeanAbs = anchorCount > 0 ? static_cast<float>(anchorAbs / anchorCount) : 0.0f;
    m.secondaryTimingMeanAbs = secondaryCount > 0 ? static_cast<float>(secondaryAbs / secondaryCount) : 0.0f;
    m.timingOutlierRate = events > 0 ? outliers / static_cast<float>(events) : 0.0f;
    m.syncopation = syncopationOf(allLaneBars.empty() ? kickBars : kickBars) + syncopationOf(snareBars) + 0.5f * syncopationOf(hatBars);

    // --- bass
    std::set<int> kickSteps;
    for (const auto& n : kick)
        kickSteps.insert(stepOf(n));
    int onKick = 0;
    for (const auto& n : bass)
        onKick += kickSteps.count(stepOf(n)) > 0 ? 1 : 0;
    m.bassNotesPerBar = bass.size() / fb;
    m.bassOnKickRate = bass.empty() ? 0.0f : onKick / static_cast<float>(bass.size());

    // --- skeleton strings (duplicate detection)
    for (const auto& [lane, s32] : m.skeletonSet)
    {
        m.skeleton << lane << ":" << s32 << " ";
        const auto type = static_cast<TrackType>(lane);
        auto& part = (type == TrackType::Snare || type == TrackType::ClapGhostSnare) ? m.snareSkeleton
                   : (type == TrackType::HiHat || type == TrackType::OpenHat || type == TrackType::HatFX) ? m.hatSkeleton
                   : type == TrackType::Kick ? m.kickSkeleton
                   : m.otherSkeleton;
        if (type != TrackType::Kick)
            part << lane << ":" << s32 << " ";
        if (s32 < 64)
        {
            m.firstTwoBars << lane << ":" << s32 << " ";
            if (type == TrackType::Kick)
                m.firstTwoKick << s32 << " ";
        }
    }
    for (const auto& bar : kickBars)
    {
        for (const int s : bar)
        {
            m.kickSkeleton << s << ",";
            m.kickBarsText << (m.kickBarsText.isEmpty() || m.kickBarsText.endsWith("|") ? "" : " ") << s;
        }
        m.kickSkeleton << "|";
        m.kickBarsText << "|";
    }

    // --- critical structural failures (baseline counts, not gates)
    juce::StringArray fails;
    if (genre == GenreType::BoomBap || genre == GenreType::DnB)
    {
        if (m.backbeatCoverage < 0.999f) fails.add("backbeat missing");
        if (m.kicksPerBar > (genre == GenreType::DnB ? 5.0f : 6.0f)) fails.add("kick spam");
        if (m.ghostLouderThanAnchor > 0) fails.add("ghost louder than anchor");
        if (m.snareInvalidPerBar > 0) fails.add("snare on 1/3");
    }
    else if (genre == GenreType::Trap)
    {
        if (m.backbeatCoverage < 0.999f) fails.add("snare on 3 missing");
        int kickOnSnare = 0;
        for (const auto& bar : kickBars)
            kickOnSnare += bar.count(8) > 0 ? 1 : 0;
        if (kickOnSnare > bars / 2) fails.add("kick on protected snare");
        if (hats.empty()) fails.add("no hat carrier");
    }
    else if (genre == GenreType::Techno)
    {
        int missing = 0;
        for (int bar = 0; bar + 1 < bars || (bars == 1 && bar == 0); ++bar)
            for (int beat = 0; beat < 4; ++beat)
                missing += kickBars[static_cast<size_t>(bar)].count(beat * 4) == 0 ? 1 : 0;
        if (missing > 0) fails.add("kick axis broken");
        int percOnKick = 0;
        for (const auto& n : notesOf(project, TrackType::Perc))
            percOnKick += kickSteps.count(stepOf(n)) > 0 ? 1 : 0;
        if (percOnKick > 0) fails.add("perc on kick");
    }
    if (m.bassOnKickRate > 0.0f && genre == GenreType::Techno)
        fails.add("bass on kick");
    m.hardFailures = fails.size();
    m.failure = fails.joinIntoString(";");
    return m;
}

struct Stat
{
    std::vector<double> values;
    void add(double v) { values.push_back(v); }
    double mean() const
    {
        double s = 0;
        for (const double v : values)
            s += v;
        return values.empty() ? 0.0 : s / values.size();
    }
    double pct(double q) const
    {
        if (values.empty())
            return 0.0;
        auto v = values;
        std::sort(v.begin(), v.end());
        return v[std::min(v.size() - 1, static_cast<size_t>(std::lround(q * (v.size() - 1))))];
    }
};
} // namespace bbg::quality
