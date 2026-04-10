#pragma once

#include <algorithm>
#include <unordered_set>
#include <vector>

#include "../Core/PatternProject.h"

namespace bbg
{
class StepHintWeighter
{
public:
    static float weightForLaneStep(const SampleAwareGenerationContext& context, TrackType lane, int step)
    {
        const auto* weights = weightsForLane(context.hints, lane);
        if (weights == nullptr || weights->empty())
            return 0.0f;

        const size_t index = static_cast<size_t>(std::max(0, step)) % weights->size();
        return (*weights)[index];
    }

    static void applyToProject(PatternProject& project, const std::unordered_set<TrackType>& mutableTracks)
    {
        const auto& context = project.sampleContext;
        if (!context.enabled)
            return;

        const bool hasHints = !context.hints.kickStepWeights.empty()
            || !context.hints.snareStepWeights.empty()
            || !context.hints.hatStepWeights.empty()
            || !context.hints.bassStepWeights.empty();
        const bool hasTranscription = !context.transcription.drumEvents.empty() || !context.transcription.bassEvents.empty();
        if (!hasHints && !hasTranscription)
            return;

        for (auto& track : project.tracks)
        {
            if (mutableTracks.count(track.type) == 0 || !track.enabled || track.locked)
                continue;

            auto noteShouldDrop = [&](const NoteEvent& note)
            {
                const float hintWeight = weightForLaneStep(context, track.type, note.step);
                const float eventConfidence = transcriptionConfidenceForLaneStep(context.transcription, track.type, note.step);
                const bool anchor = isAnchorStep(track.type, note.step);

                if (anchor)
                    return false;
                if (eventConfidence >= 0.35f)
                    return false;
                if (context.preferCopyDrums || (track.type == TrackType::Sub808 && context.preferCopyBass))
                    return hintWeight < 0.10f;

                return hintWeight < 0.08f;
            };

            track.notes.erase(std::remove_if(track.notes.begin(), track.notes.end(), noteShouldDrop), track.notes.end());

            for (auto& note : track.notes)
            {
                const float hintWeight = std::max(weightForLaneStep(context, track.type, note.step),
                                                  transcriptionConfidenceForLaneStep(context.transcription, track.type, note.step));
                const float gain = std::clamp(0.82f + 0.44f * hintWeight, 0.70f, 1.34f);
                note.velocity = std::clamp(static_cast<int>(std::round(static_cast<float>(note.velocity) * gain)), 1, 127);

                if (context.preferCopyDrums && hintWeight > 0.74f)
                    note.microOffset = static_cast<int>(std::round(static_cast<float>(note.microOffset) * 0.35f));
                if (context.preferCopyBass && track.type == TrackType::Sub808 && hintWeight > 0.72f)
                    note.microOffset = static_cast<int>(std::round(static_cast<float>(note.microOffset) * 0.25f));
            }

            const auto appendMissingEvents = [&](const std::vector<TranscribedEvent>& events, bool preferCopy)
            {
                for (const auto& event : events)
                {
                    if (!matchesLane(track.type, event.lane))
                        continue;

                    const float hintWeight = weightForLaneStep(context, track.type, event.step);
                    if (!preferCopy && event.confidence < 0.76f && hintWeight < 0.84f)
                        continue;
                    if (containsStep(track.notes, event.step, event.pitch))
                        continue;

                    NoteEvent note;
                    note.pitch = event.pitch;
                    note.step = event.step;
                    note.length = std::max(1, event.lengthSteps);
                    note.velocity = std::clamp(event.velocity, 1, 127);
                    note.isGhost = event.ghost;
                    note.semanticRole = preferCopy ? "sample_copy" : "sample_hint";
                    track.notes.push_back(note);
                }
            };

            appendMissingEvents(context.transcription.drumEvents, context.preferCopyDrums);
            appendMissingEvents(context.transcription.bassEvents, context.preferCopyBass && track.type == TrackType::Sub808);

            dedupeAndSort(track.notes);
        }
    }

private:
    static const std::vector<float>* weightsForLane(const GenerationHints& hints, TrackType lane)
    {
        switch (lane)
        {
            case TrackType::Kick:
            case TrackType::GhostKick:
                return &hints.kickStepWeights;
            case TrackType::Snare:
            case TrackType::ClapGhostSnare:
                return &hints.snareStepWeights;
            case TrackType::HiHat:
            case TrackType::Ride:
                return &hints.hatStepWeights;
            case TrackType::OpenHat:
            case TrackType::Cymbal:
                return &hints.openHatStepWeights;
            case TrackType::Perc:
            case TrackType::HatFX:
                return &hints.percStepWeights;
            case TrackType::Sub808:
                return &hints.bassStepWeights;
            default:
                return nullptr;
        }
    }

    static bool isAnchorStep(TrackType lane, int step)
    {
        const int stepInBar = ((step % 16) + 16) % 16;
        switch (lane)
        {
            case TrackType::Kick:
            case TrackType::GhostKick:
            case TrackType::Sub808:
                return stepInBar == 0 || stepInBar == 8;
            case TrackType::Snare:
            case TrackType::ClapGhostSnare:
                return stepInBar == 4 || stepInBar == 12;
            case TrackType::HiHat:
                return (stepInBar % 2) == 0;
            default:
                return false;
        }
    }

    static float transcriptionConfidenceForLaneStep(const SampleTranscription& transcription, TrackType lane, int step)
    {
        float best = 0.0f;
        const auto capture = [&](const TranscribedEvent& event)
        {
            if (!matchesLane(lane, event.lane) || event.step != step)
                return;
            best = std::max(best, event.confidence);
        };

        for (const auto& event : transcription.drumEvents)
            capture(event);
        for (const auto& event : transcription.bassEvents)
            capture(event);
        return best;
    }

    static bool matchesLane(TrackType requested, TrackType provided)
    {
        if (requested == provided)
            return true;
        if ((requested == TrackType::Kick || requested == TrackType::GhostKick)
            && (provided == TrackType::Kick || provided == TrackType::GhostKick))
            return true;
        if ((requested == TrackType::Snare || requested == TrackType::ClapGhostSnare)
            && (provided == TrackType::Snare || provided == TrackType::ClapGhostSnare))
            return true;
        return false;
    }

    static bool containsStep(const std::vector<NoteEvent>& notes, int step, int pitch)
    {
        return std::any_of(notes.begin(), notes.end(), [&](const NoteEvent& note)
        {
            return note.step == step && note.pitch == pitch;
        });
    }

    static void dedupeAndSort(std::vector<NoteEvent>& notes)
    {
        std::sort(notes.begin(), notes.end(), [](const NoteEvent& left, const NoteEvent& right)
        {
            if (left.step != right.step)
                return left.step < right.step;
            if (left.pitch != right.pitch)
                return left.pitch < right.pitch;
            return left.velocity > right.velocity;
        });

        notes.erase(std::unique(notes.begin(), notes.end(), [](const NoteEvent& left, const NoteEvent& right)
        {
            return left.step == right.step && left.pitch == right.pitch;
        }), notes.end());
    }
};
} // namespace bbg