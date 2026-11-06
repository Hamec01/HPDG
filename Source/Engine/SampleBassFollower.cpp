#include "SampleBassFollower.h"

#include <cmath>
#include <array>
#include <algorithm>
#include <vector>

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
    if (project.params.genre == GenreType::Trap && project.params.trapSubstyle == 5)
    {
        // Luxury carries a melodic phrase. Replacing every pitch class with a detected
        // sample note flattened its contour and produced arbitrary octave changes.
        // Move the whole contour with a stable, duration-weighted harmonic root per bar.
        constexpr auto marker = "|luxury_base_pitch=";
        const auto originalPitch = [marker](const NoteEvent& n)
        {
            const int at=n.semanticRole.indexOf(marker);
            return at>=0 ? n.semanticRole.substring(at+juce::String(marker).length()).getIntValue() : n.pitch;
        };
        const int barTicks = 4 * TimingGrid::PPQ;
        for (auto& note : notes)
        {
            const int tag = note.semanticRole.indexOf(marker);
            const int basePitch = originalPitch(note);
            // Store the unadapted pitch so applying Guide repeatedly cannot accumulate transposition.
            if (tag < 0) note.semanticRole += juce::String(marker)+juce::String(basePitch);
            const double barStart = originSeconds + (note.gridTick/barTicks)*barTicks*secondsPerTick;
            const double barDuration = barTicks*secondsPerTick;
            double start = barStart;
            if (start >= sampleEnd && loopLength > .1)
                start = originSeconds + std::fmod(start-originSeconds,loopLength);
            std::array<double,12> weights {};
            for (const auto& segment : harmony.bass)
            {
                if (segment.midiNote < 0 || segment.confidence < .6f) continue;
                const double overlap = std::max(0.0,std::min(start+barDuration,segment.endSeconds)-std::max(start,segment.startSeconds));
                weights[static_cast<size_t>(segment.midiNote%12)] += overlap*segment.confidence;
            }
            const auto best = std::max_element(weights.begin(),weights.end());
            if (*best < barDuration*.15)
            {
                note.pitch=basePitch; ++report.notesKept; continue;
            }
            const int root = static_cast<int>(std::distance(weights.begin(),best));
            int shift = root-project.params.keyRoot;
            int low=127,high=0;
            for (const auto& other:notes)
                if (other.gridTick/barTicks==note.gridTick/barTicks)
                { low=std::min(low,originalPitch(other)); high=std::max(high,originalPitch(other)); }
            while (low+shift<24) shift+=12;
            while (high+shift>59) shift-=12;
            note.pitch=basePitch+shift; ++report.notesFollowed;
        }
        if (report.notesFollowed > 0)
            ProjectStateController::setTrackNotes(project,TrackType::Sub808,notes);
        return report;
    }
    const bool dark = project.params.genre == GenreType::Trap && project.params.trapSubstyle == 1;
    // Current-pass evidence, rather than a role tag left by an earlier guidance pass.
    std::vector<bool> supportedPitch(dark ? notes.size() : 0, false);
    for (size_t index = 0; index < notes.size(); ++index)
    {
        auto& note = notes[index];
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
        if (!dark || !note.semanticRole.contains("sample_bass"))
            note.semanticRole = note.semanticRole.isEmpty() ? juce::String("sample_bass") : note.semanticRole + "|sample_bass";
        if (dark)
            supportedPitch[index] = true;
        ++report.notesFollowed;
    }

    if (dark)
        // Work backwards so a short chain of pickups reaches the same supported target.
        for (size_t next = notes.size(); next > 1; --next)
        {
            const size_t index = next - 2;
            auto& pickup = notes[index];
            auto& target = notes[index + 1];
            const int distance = target.gridTick - pickup.gridTick;
            if (!pickup.semanticRole.contains("bass_pickup") || pickup.semanticRole.contains("sample_line")
                || distance <= 0 || distance > TimingGrid::Sixteenth || !supportedPitch[index + 1])
                continue;
            // Anticipate the following chord on a short rearticulation. Keep the genre's
            // timing/gates/velocity; the analyzer contributes pitch, not a copied rhythm.
            pickup.pitch = target.pitch;
            pickup.glideToNext = false;
            target.isSlide = false;
            if (!pickup.semanticRole.contains("sample_bass"))
                pickup.semanticRole += "|sample_bass";
            if (!supportedPitch[index])
            {
                ++report.notesFollowed;
                --report.notesKept;
            }
            supportedPitch[index] = true;
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
