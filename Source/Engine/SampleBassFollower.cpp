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

    const double secondsPerTick = 60.0 / (bpm * TimingGrid::PPQ);
    const double sampleEnd = harmony.bass.back().endSeconds;
    const double loopLength = sampleEnd - originSeconds;

    auto notes = sub->notes;
    for (auto& note : notes)
    {
        // Composed from the sample already (bass mode [2]): its pitch is the sample's.
        if (note.semanticRole.contains("sample_line"))
        {
            ++report.notesFollowed;
            continue;
        }
        double seconds = originSeconds + note.gridTick * secondsPerTick;
        if (seconds >= sampleEnd && loopLength > 0.1) // pattern longer than the sample: it loops
            seconds = originSeconds + std::fmod(seconds - originSeconds, loopLength);

        const auto* segment = harmony.segmentAt(seconds);
        if (segment == nullptr || segment->midiNote < 0 || segment->confidence < kMinBassConfidence)
        {
            ++report.notesKept;
            continue;
        }

        // The sample's pitch class, in the octave nearest to the note the genre engine chose:
        // keeps each genre's register (Boom Bap bass G1-B2, trap 808 around the key root) and
        // the engine's octave jumps.
        const int pitchClass = segment->midiNote % 12;
        const int shift = ((pitchClass - note.pitch % 12) % 12 + 12 + 6) % 12 - 6; // -6..+5 semitones
        note.pitch = juce::jlimit(24, 60, note.pitch + shift);
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
