#pragma once

#include <algorithm>
#include <vector>

#include "GenerationHints.h"
#include "LaneEvidenceMap.h"
#include "SampleAnalysisResult.h"
#include "SampleTranscription.h"

namespace bbg
{
class GenerationHintsBuilder
{
public:
    GenerationHints build(const LaneEvidenceMap& evidence,
                          const SampleTranscription& transcription,
                          const SampleAnalysisResult& summary) const
    {
        GenerationHints hints;
        const size_t totalSteps = evidence.steps.size();

        hints.kickStepWeights = buildWeights(evidence, transcription, TrackType::Kick, [](const StepEvidence& step) { return step.kickProbability; });
        hints.snareStepWeights = buildWeights(evidence, transcription, TrackType::Snare, [](const StepEvidence& step) { return step.snareProbability; });
        hints.hatStepWeights = buildWeights(evidence, transcription, TrackType::HiHat, [](const StepEvidence& step) { return step.hatProbability; });
        hints.openHatStepWeights = buildWeights(evidence, transcription, TrackType::OpenHat, [](const StepEvidence& step) { return step.openHatProbability; });
        hints.percStepWeights = buildWeights(evidence, transcription, TrackType::Perc, [](const StepEvidence& step) { return step.percProbability; });
        hints.bassStepWeights = buildWeights(evidence, transcription, TrackType::Sub808, [](const StepEvidence& step) { return step.bassProbability; });

        for (size_t index = 0; index < totalSteps; ++index)
        {
            const int step = static_cast<int>(index);
            const int stepInBar = evidence.stepsPerBar > 0 ? step % evidence.stepsPerBar : step;

            if ((stepInBar == 0 || stepInBar == 8) && hints.kickStepWeights[index] > 0.68f)
                hints.preferredKickAnchors.push_back(step);
            if ((stepInBar == 4 || stepInBar == 12) && hints.snareStepWeights[index] > 0.64f)
                hints.preferredSnareAnchors.push_back(step);
        }

        hints.phraseBoundaries = summary.phraseBoundaryBars;
        hints.drumsLikelyPresent = transcription.hasDetectedDrums
            || hasStrongWeight(hints.kickStepWeights)
            || hasStrongWeight(hints.snareStepWeights)
            || hasStrongWeight(hints.hatStepWeights);
        hints.bassLikelyPresent = transcription.hasDetectedBass || hasStrongWeight(hints.bassStepWeights);
        return hints;
    }

private:
    template <typename ProbabilityFn>
    static std::vector<float> buildWeights(const LaneEvidenceMap& evidence,
                                           const SampleTranscription& transcription,
                                           TrackType lane,
                                           ProbabilityFn probability)
    {
        std::vector<float> weights(evidence.steps.size(), 0.0f);
        for (size_t index = 0; index < evidence.steps.size(); ++index)
        {
            const auto& current = evidence.steps[index];
            const float prev = index > 0 ? probability(evidence.steps[index - 1]) : probability(current);
            const float next = index + 1 < evidence.steps.size() ? probability(evidence.steps[index + 1]) : probability(current);
            const float transcriptionBoost = transcriptionConfidenceAtStep(transcription, lane, static_cast<int>(index));

            float weight = 0.60f * probability(current)
                + 0.16f * prev
                + 0.16f * next
                + 0.08f * transcriptionBoost;

            const int stepInBar = evidence.stepsPerBar > 0 ? static_cast<int>(index) % evidence.stepsPerBar : static_cast<int>(index);
            if ((lane == TrackType::Kick || lane == TrackType::Sub808) && (stepInBar == 0 || stepInBar == 8))
                weight += 0.08f;
            if (lane == TrackType::Snare && (stepInBar == 4 || stepInBar == 12))
                weight += 0.10f;
            if ((lane == TrackType::OpenHat || lane == TrackType::Perc) && current.nearPhraseBoundary)
                weight += 0.08f;

            weights[index] = std::clamp(weight, 0.0f, 1.0f);
        }

        normalize(weights);
        return weights;
    }

    static void normalize(std::vector<float>& values)
    {
        float peak = 0.0f;
        for (const float value : values)
            peak = std::max(peak, value);

        if (peak < 1.0e-6f)
            return;

        const float invPeak = 1.0f / peak;
        for (auto& value : values)
            value = std::clamp(value * invPeak, 0.0f, 1.0f);
    }

    static float transcriptionConfidenceAtStep(const SampleTranscription& transcription, TrackType lane, int step)
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

    static bool matchesLane(TrackType requested, TrackType eventLane)
    {
        if (requested == eventLane)
            return true;
        if ((requested == TrackType::Kick || requested == TrackType::GhostKick)
            && (eventLane == TrackType::Kick || eventLane == TrackType::GhostKick))
            return true;
        if ((requested == TrackType::Snare || requested == TrackType::ClapGhostSnare)
            && (eventLane == TrackType::Snare || eventLane == TrackType::ClapGhostSnare))
            return true;
        return false;
    }

    static bool hasStrongWeight(const std::vector<float>& weights)
    {
        return std::any_of(weights.begin(), weights.end(), [](float value)
        {
            return value > 0.62f;
        });
    }
};
} // namespace bbg