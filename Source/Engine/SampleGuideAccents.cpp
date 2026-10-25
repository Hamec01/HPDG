#include "SampleGuideAccents.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "../Core/ProjectLaneAccess.h"
#include "../Core/ProjectStateController.h"
#include "../Core/TimingGrid.h"
#include "Trap/TrapAlgebraEngine.h"
#include "Trap/TrapTempoContext.h"

namespace bbg
{
namespace
{
constexpr float kMinKickLikeness = 0.35f;
constexpr int kMaxGridDistanceTicks = 70;     // peaks further off the 1/16 grid are ignored
constexpr int kKeepAwayTicks = TimingGrid::Sixteenth / 2;

bool isBackbeatSlot(GenreType genre, int stepInBar)
{
    // Drum & Bass: the snare on 2 / 4 and the 16th right after it (a kick there stumbles the
    // 2-step at 170+ BPM).
    if (genre == GenreType::DnB)
        return stepInBar == 4 || stepInBar == 5 || stepInBar == 12 || stepInBar == 13;
    // Trap / Drill: half-time snare on beat 3. Boom Bap / Rap: snare on beats 2 and 4.
    if (genre == GenreType::Trap || genre == GenreType::Drill)
        return stepInBar == 8;
    return stepInBar == 4 || stepInBar == 12;
}

// Trap / Drill: an added kick never sits a 16th next to a kick (a stumbling double at 140-180 BPM:
// "the kick hammers", maintainer 2026-10-14); the loop wraps (step 16 against the next bar's 1).
int kickKeepAwayTicks(GenreType genre)
{
    return genre == GenreType::Trap || genre == GenreType::Drill ? TimingGrid::Sixteenth : kKeepAwayTicks;
}

// Trap: the kicks Guide adds obey the engine's own kick grammar (they used to land on positions its
// repair removes - "the kick does not fit the beat", maintainer 2026-10-25). A tonal sample's low
// attack is weak evidence of a kick, so it needs a stronger peak and plays under the engine's kicks.
constexpr float kTrapTonalMinKickLikeness = 0.55f;
constexpr float kTrapTonalDrumLoopConfidence = 0.35f;
constexpr int kTrapAccentVelocityBelowKicks = 8;

struct TrapKickGrid
{
    TrapTempoContext tempo;
    int styleTicksPerHostBar = 64;

    // Host grid tick -> (style bar, tick 0-63 in it).
    std::pair<int, int> styleBarAndLocal(int hostTick) const
    {
        const int styleTick = static_cast<int>(static_cast<long long>(hostTick) * styleTicksPerHostBar / TimingGrid::TicksPerBar4_4);
        return { styleTick / 64, styleTick % 64 };
    }
    bool allowed(int hostTick) const
    {
        return TrapAlgebraEngine::isMainKickTickAllowed(styleBarAndLocal(hostTick).second, tempo.doubleTime);
    }
    // The engine's cap per style bar (TrapAlgebraEngine::repair).
    static int maxKicks(int styleBar) { return styleBar % 4 == 3 ? 4 : 3; }
};

int medianKickVelocity(const std::vector<NoteEvent>& notes)
{
    std::vector<int> velocities;
    for (const auto& note : notes)
        if (!note.isGhost)
            velocities.push_back(note.velocity);
    if (velocities.empty())
        return 110;
    std::nth_element(velocities.begin(), velocities.begin() + static_cast<long>(velocities.size() / 2), velocities.end());
    return velocities[velocities.size() / 2];
}

int loopDistance(int a, int b, int loopTicks)
{
    const int d = std::abs(a - b) % loopTicks;
    return std::min(d, loopTicks - d);
}

bool hasNoteNear(const TrackState* state, int tick, int distance)
{
    if (state == nullptr)
        return false;
    return std::any_of(state->notes.begin(), state->notes.end(), [&](const NoteEvent& note)
    {
        return std::abs(note.gridTick - tick) <= distance;
    });
}
}

SampleGuideAccentReport SampleGuideAccents::apply(PatternProject& project,
                                                  const DrumBreakAnalysis& analysis,
                                                  double gridBpm,
                                                  double gridOriginSeconds,
                                                  float reactivity)
{
    SampleGuideAccentReport report;
    auto* kick = ProjectLaneAccess::findTrackState(project, TrackType::Kick);
    if (!analysis.valid || analysis.onsetTimes.empty() || kick == nullptr || kick->locked)
        return report;

    // The grid is decided by the analyzer (sample tempo for trusted drum loops, otherwise the
    // session tempo with beat 1 at the start of the file) and shared with the 808 bass follower.
    report.usedSampleTempo = std::abs(gridBpm - analysis.bpm) < 0.01;
    report.bpm = gridBpm;
    report.originSeconds = gridOriginSeconds;
    if (report.bpm <= 20.0)
        return report;

    const int loopTicks = juce::jmax(1, project.params.bars) * TimingGrid::TicksPerBar4_4;
    const double ticksPerSecond = report.bpm / 60.0 * TimingGrid::PPQ;
    const auto* snare = ProjectLaneAccess::findTrackState(project, TrackType::Snare);
    const bool trap = project.params.genre == GenreType::Trap;
    const int intensity = std::clamp(project.intensity, 1, 5);
    TrapKickGrid trapGrid;
    if (trap)
    {
        trapGrid.tempo = resolveTrapTempo(project.params.bpm, project.params.bars);
        trapGrid.styleTicksPerHostBar = static_cast<int>(64.0 * trapGrid.tempo.clockMultiplier);
    }
    const float minKickLikeness = trap && analysis.drumLoopConfidence < kTrapTonalDrumLoopConfidence
                                      ? kTrapTonalMinKickLikeness
                                      : kMinKickLikeness;

    std::vector<std::pair<float, int>> candidates; // kick-likeness, grid tick
    for (size_t onset = 0; onset < analysis.onsetTimes.size() && onset < analysis.onsetLaneLevels.size(); ++onset)
    {
        const float kickLikeness = analysis.onsetLaneLevels[onset][0];
        if (kickLikeness < minKickLikeness)
            continue;

        const int tick = static_cast<int>(std::lround((analysis.onsetTimes[onset] - report.originSeconds) * ticksPerSecond));
        if (tick < -kMaxGridDistanceTicks || tick >= loopTicks)
            continue;

        const int slot = static_cast<int>(std::lround(static_cast<double>(tick) / TimingGrid::Sixteenth)) * TimingGrid::Sixteenth;
        if (std::abs(tick - slot) > kMaxGridDistanceTicks || slot < 0 || slot >= loopTicks)
            continue;

        const int stepInBar = (slot / TimingGrid::Sixteenth) % 16;
        if (isBackbeatSlot(project.params.genre, stepInBar)
            || hasNoteNear(snare, slot, kKeepAwayTicks)
            || hasNoteNear(kick, slot, kKeepAwayTicks))
            continue;
        // Trap: only where the engine itself puts a kick; Intensity 1 keeps the lane on quarter
        // notes (LaneStrength::primitive), so an added kick stays on them too.
        if (trap && (!trapGrid.allowed(slot) || (intensity == 1 && slot % TimingGrid::TicksPerBeat != 0)))
            continue;

        candidates.emplace_back(kickLikeness, slot);
    }

    report.candidatePeaks = static_cast<int>(candidates.size());
    if (candidates.empty())
        return report;

    std::sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) { return left.first > right.first; });

    // Trap Intensity 1 / 2 write a simplified kick: one accent per bar at most.
    const int maxPerBar = trap && intensity <= 2 ? 1 : (reactivity >= 0.6f ? 2 : 1);
    const int pitch = kick->notes.empty() ? 36 : kick->notes.front().pitch;
    const int trapVelocityCap = medianKickVelocity(kick->notes) - kTrapAccentVelocityBelowKicks;
    std::map<int, int> perBar;
    std::map<int, int> kicksPerStyleBar;
    if (trap)
        for (const auto& note : kick->notes)
            if (!note.isGhost)
                ++kicksPerStyleBar[trapGrid.styleBarAndLocal(note.gridTick).first];
    auto notes = kick->notes;
    for (const auto& [likeness, slot] : candidates)
    {
        const int bar = slot / TimingGrid::TicksPerBar4_4;
        if (perBar[bar] >= maxPerBar)
            continue;
        const int keepAway = kickKeepAwayTicks(project.params.genre);
        if (std::any_of(notes.begin(), notes.end(), [&](const NoteEvent& note) { return loopDistance(note.gridTick, slot, loopTicks) <= keepAway; }))
            continue;
        const int styleBar = trap ? trapGrid.styleBarAndLocal(slot).first : 0;
        if (trap && kicksPerStyleBar[styleBar] >= TrapKickGrid::maxKicks(styleBar))
            continue;

        NoteEvent note;
        note.pitch = pitch;
        note.gridTick = slot;
        note.lengthTicks = TimingGrid::Sixteenth;
        note.velocity = juce::jlimit(1, 127, static_cast<int>(std::lround(88.0f + 32.0f * std::min(1.0f, likeness))));
        if (trap)
            note.velocity = juce::jlimit(1, 127, std::min(note.velocity, trapVelocityCap));
        note.semanticRole = "sample_accent";
        notes.push_back(note);
        if (trap)
            ++kicksPerStyleBar[styleBar];
        ++perBar[bar];
        ++report.addedKicks;
    }

    if (report.addedKicks > 0)
    {
        std::sort(notes.begin(), notes.end(), [](const NoteEvent& left, const NoteEvent& right) { return left.gridTick < right.gridTick; });
        ProjectStateController::setTrackNotes(project, TrackType::Kick, notes);
    }

    return report;
}

juce::String describeSampleGuideAccentReport(const SampleGuideAccentReport& report)
{
    return "Sample guide accents: " + juce::String(report.addedKicks) + " kick(s) from "
        + juce::String(report.candidatePeaks) + " peak(s) | grid "
        + juce::String(report.bpm, 2) + " bpm (" + (report.usedSampleTempo ? "sample tempo" : "project tempo") + ")";
}
} // namespace bbg
