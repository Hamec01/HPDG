#include "SampleBassFollower.h"

#include <cmath>

#include "../Core/ProjectLaneAccess.h"
#include "../Core/ProjectStateController.h"
#include "../Core/TimingGrid.h"

namespace bbg
{
SampleBassFollowReport SampleBassFollower::apply(PatternProject& project,
                                                 const SampleHarmony& harmony,
                                                 double bpm,
                                                 double originSeconds)
{
    SampleBassFollowReport report;
    report.keyApplied = shouldApplyKey(harmony)
        && project.params.keyRoot == harmony.keyRoot
        && project.params.scaleMode == harmony.scaleMode;

    auto* sub = ProjectLaneAccess::findTrackState(project, TrackType::Sub808);
    if (!harmony.valid || harmony.bass.empty() || sub == nullptr || sub->locked || sub->notes.empty() || bpm <= 20.0)
        return report;

    const int root = juce::jlimit(0, 11, project.params.keyRoot);
    const int baseRootPitch = juce::jlimit(24, 60, 36 + root);
    const double secondsPerTick = 60.0 / (bpm * TimingGrid::PPQ);
    const double sampleEnd = harmony.bass.back().endSeconds;
    const double loopLength = sampleEnd - originSeconds;

    auto notes = sub->notes;
    for (auto& note : notes)
    {
        double seconds = originSeconds + note.gridTick * secondsPerTick;
        if (seconds >= sampleEnd && loopLength > 0.1) // pattern longer than the sample: it loops
            seconds = originSeconds + std::fmod(seconds - originSeconds, loopLength);

        const auto* segment = harmony.segmentAt(seconds);
        if (segment == nullptr || segment->midiNote < 0 || segment->confidence < kMinBassConfidence)
        {
            ++report.notesKept;
            continue;
        }

        const int pitchClass = segment->midiNote % 12;
        int pitch = baseRootPitch + (pitchClass - root + 12) % 12;
        if (note.pitch - baseRootPitch >= 12) // keep the engine's octave jump
            pitch += 12;
        note.pitch = juce::jlimit(24, 60, pitch);
        note.semanticRole = note.semanticRole.isEmpty() ? juce::String("sample_bass") : note.semanticRole + "|sample_bass";
        ++report.notesFollowed;
    }

    if (report.notesFollowed > 0)
        ProjectStateController::setTrackNotes(project, TrackType::Sub808, notes);
    return report;
}

juce::String describeSampleBassFollowReport(const SampleBassFollowReport& report, const SampleHarmony& harmony)
{
    return "Sample bass: key " + harmony.keyName() + " (" + juce::String(harmony.keyConfidence, 2) + ")"
        + (report.keyApplied ? " applied" : " not applied")
        + " | 808 notes following sample bass " + juce::String(report.notesFollowed)
        + " | kept engine pitch " + juce::String(report.notesKept);
}
} // namespace bbg
