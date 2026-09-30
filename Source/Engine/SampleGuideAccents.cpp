#include "SampleGuideAccents.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "../Core/ProjectLaneAccess.h"
#include "../Core/ProjectStateController.h"
#include "../Core/TimingGrid.h"

namespace bbg
{
namespace
{
constexpr float kMinKickLikeness = 0.35f;
constexpr int kMaxGridDistanceTicks = 70;     // peaks further off the 1/16 grid are ignored
constexpr int kKeepAwayTicks = TimingGrid::Sixteenth / 2;

bool isBackbeatSlot(GenreType genre, int stepInBar)
{
    // Trap / Drill: half-time snare on beat 3. Boom Bap / Rap: snare on beats 2 and 4.
    if (genre == GenreType::Trap || genre == GenreType::Drill)
        return stepInBar == 8;
    return stepInBar == 4 || stepInBar == 12;
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

    std::vector<std::pair<float, int>> candidates; // kick-likeness, grid tick
    for (size_t onset = 0; onset < analysis.onsetTimes.size() && onset < analysis.onsetLaneLevels.size(); ++onset)
    {
        const float kickLikeness = analysis.onsetLaneLevels[onset][0];
        if (kickLikeness < kMinKickLikeness)
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

        candidates.emplace_back(kickLikeness, slot);
    }

    report.candidatePeaks = static_cast<int>(candidates.size());
    if (candidates.empty())
        return report;

    std::sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) { return left.first > right.first; });

    const int maxPerBar = reactivity >= 0.6f ? 2 : 1;
    const int pitch = kick->notes.empty() ? 36 : kick->notes.front().pitch;
    std::map<int, int> perBar;
    auto notes = kick->notes;
    for (const auto& [likeness, slot] : candidates)
    {
        const int bar = slot / TimingGrid::TicksPerBar4_4;
        if (perBar[bar] >= maxPerBar)
            continue;
        if (std::any_of(notes.begin(), notes.end(), [&](const NoteEvent& note) { return std::abs(note.gridTick - slot) <= kKeepAwayTicks; }))
            continue;

        NoteEvent note;
        note.pitch = pitch;
        note.gridTick = slot;
        note.lengthTicks = TimingGrid::Sixteenth;
        note.velocity = juce::jlimit(1, 127, static_cast<int>(std::lround(88.0f + 32.0f * std::min(1.0f, likeness))));
        note.semanticRole = "sample_accent";
        notes.push_back(note);
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
