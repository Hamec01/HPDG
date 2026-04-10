#pragma once

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

#include <juce_core/juce_core.h>

#include "LaneEvidenceMap.h"
#include "SampleAnalysisResult.h"
#include "SampleTranscription.h"

namespace bbg
{
struct BasslineInferenceResult
{
    std::vector<TranscribedEvent> events;
    std::vector<float> stepWeights;
    bool likelyPresent = false;
};

class BasslineInferer
{
public:
    BasslineInferenceResult infer(const std::vector<float>& mono,
                                  double sampleRate,
                                  const SampleAnalysisResult& summary,
                                  const LaneEvidenceMap& evidence) const
    {
        BasslineInferenceResult result;
        result.stepWeights.resize(evidence.steps.size(), 0.0f);

        if (mono.empty() || sampleRate <= 1000.0 || evidence.steps.empty() || summary.detectedBpm <= 20.0)
            return result;

        for (size_t index = 0; index < evidence.steps.size(); ++index)
        {
            const float current = evidence.steps[index].bassProbability;
            const float prev = index > 0 ? evidence.steps[index - 1].bassProbability : current;
            const float next = index + 1 < evidence.steps.size() ? evidence.steps[index + 1].bassProbability : current;
            result.stepWeights[index] = std::clamp(0.64f * current + 0.18f * prev + 0.18f * next, 0.0f, 1.0f);
        }

        normalize(result.stepWeights);

        const float meanWeight = result.stepWeights.empty()
            ? 0.0f
            : std::accumulate(result.stepWeights.begin(), result.stepWeights.end(), 0.0f) / static_cast<float>(result.stepWeights.size());

        result.likelyPresent = meanWeight > 0.26f
            || std::any_of(result.stepWeights.begin(), result.stepWeights.end(), [](float value) { return value > 0.64f; });

        if (!result.likelyPresent)
            return result;

        int step = 0;
        while (step < static_cast<int>(result.stepWeights.size()))
        {
            const float weight = result.stepWeights[static_cast<size_t>(step)];
            const bool strongStart = weight > 0.54f;
            const bool softLocalPeak = weight > 0.42f
                && weight >= (step > 0 ? result.stepWeights[static_cast<size_t>(step - 1)] : weight)
                && weight >= (step + 1 < static_cast<int>(result.stepWeights.size()) ? result.stepWeights[static_cast<size_t>(step + 1)] : weight);

            if (!strongStart && !softLocalPeak)
            {
                ++step;
                continue;
            }

            const int startStep = step;
            int endStep = step;
            float peakWeight = weight;
            while (endStep + 1 < static_cast<int>(result.stepWeights.size())
                   && result.stepWeights[static_cast<size_t>(endStep + 1)] > 0.28f)
            {
                ++endStep;
                peakWeight = std::max(peakWeight, result.stepWeights[static_cast<size_t>(endStep)]);
            }

            const int pitch = estimatePitchFromSegment(mono, sampleRate, summary, startStep, endStep + 1);
            const int velocity = std::clamp(static_cast<int>(std::round(68.0f + peakWeight * 50.0f)), 52, 122);

            TranscribedEvent event;
            event.lane = TrackType::Sub808;
            event.step = startStep;
            event.lengthSteps = std::max(1, endStep - startStep + 1);
            event.velocity = velocity;
            event.pitch = pitch;
            event.confidence = peakWeight;
            event.ghost = false;
            result.events.push_back(event);

            step = endStep + 1;
        }

        return result;
    }

private:
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

    static int estimatePitchFromSegment(const std::vector<float>& mono,
                                        double sampleRate,
                                        const SampleAnalysisResult& summary,
                                        int startStep,
                                        int endStepExclusive)
    {
        const double stepSeconds = 240.0 / (summary.detectedBpm * static_cast<double>(std::max(1, summary.stepsPerBar)));
        const int startSample = std::clamp(static_cast<int>(std::floor(static_cast<double>(startStep) * stepSeconds * sampleRate)),
                                           0,
                                           static_cast<int>(mono.size()));
        const int endSample = std::clamp(static_cast<int>(std::ceil(static_cast<double>(endStepExclusive) * stepSeconds * sampleRate)),
                                         startSample,
                                         static_cast<int>(mono.size()));

        if (endSample - startSample < 128)
            return 36;

        std::vector<float> windowed;
        windowed.reserve(static_cast<size_t>(endSample - startSample));
        float mean = 0.0f;
        for (int sampleIndex = startSample; sampleIndex < endSample; ++sampleIndex)
            mean += mono[static_cast<size_t>(sampleIndex)];
        mean /= static_cast<float>(std::max(1, endSample - startSample));

        for (int sampleIndex = startSample; sampleIndex < endSample; ++sampleIndex)
            windowed.push_back(mono[static_cast<size_t>(sampleIndex)] - mean);

        const int minLag = std::max(1, static_cast<int>(sampleRate / 220.0));
        const int maxLag = std::min(static_cast<int>(windowed.size()) / 2, static_cast<int>(sampleRate / 40.0));
        if (maxLag <= minLag)
            return 36;

        float bestScore = 0.0f;
        int bestLag = 0;
        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            double numerator = 0.0;
            double lhsEnergy = 0.0;
            double rhsEnergy = 0.0;

            for (size_t index = static_cast<size_t>(lag); index < windowed.size(); ++index)
            {
                const float lhs = windowed[index];
                const float rhs = windowed[index - static_cast<size_t>(lag)];
                numerator += static_cast<double>(lhs) * static_cast<double>(rhs);
                lhsEnergy += static_cast<double>(lhs) * static_cast<double>(lhs);
                rhsEnergy += static_cast<double>(rhs) * static_cast<double>(rhs);
            }

            if (lhsEnergy <= 1.0e-9 || rhsEnergy <= 1.0e-9)
                continue;

            const float normalized = static_cast<float>(numerator / std::sqrt(lhsEnergy * rhsEnergy));
            if (normalized > bestScore)
            {
                bestScore = normalized;
                bestLag = lag;
            }
        }

        if (bestLag <= 0 || bestScore < 0.12f)
            return 36;

        const double frequency = sampleRate / static_cast<double>(bestLag);
        const double midi = 69.0 + 12.0 * std::log2(frequency / 440.0);
        return juce::jlimit(24, 60, static_cast<int>(std::round(midi)));
    }
};
} // namespace bbg