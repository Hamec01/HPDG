#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "AudioFeatureMap.h"
#include "LaneEvidenceMap.h"
#include "PercussiveHarmonicSeparator.h"
#include "SampleAnalysisResult.h"

namespace bbg
{
class LaneEventInferer
{
public:
    LaneEvidenceMap infer(const AudioFeatureMap& featureMap,
                          const SampleAnalysisResult& summary,
                          const SpectralAnalysis& spectral,
                          const OnsetDetectionResult& onsets,
                          const PercussiveHarmonicSeparation& separation) const
    {
        LaneEvidenceMap evidence;
        evidence.bars = featureMap.bars;
        evidence.stepsPerBar = featureMap.stepsPerBar;
        evidence.steps.resize(featureMap.steps.size());

        if (featureMap.steps.empty() || summary.detectedBpm <= 20.0 || spectral.frames.empty())
            return evidence;

        std::vector<float> kickRaw(featureMap.steps.size(), 0.0f);
        std::vector<float> snareRaw(featureMap.steps.size(), 0.0f);
        std::vector<float> hatRaw(featureMap.steps.size(), 0.0f);
        std::vector<float> openHatRaw(featureMap.steps.size(), 0.0f);
        std::vector<float> percRaw(featureMap.steps.size(), 0.0f);
        std::vector<float> bassRaw(featureMap.steps.size(), 0.0f);

        for (size_t stepIndex = 0; stepIndex < featureMap.steps.size(); ++stepIndex)
        {
            const auto aggregate = aggregateFramesForStep(static_cast<int>(stepIndex), summary, spectral, onsets, separation);
            const auto& feature = featureMap.steps[stepIndex];
            auto& step = evidence.steps[stepIndex];

            step.isStrongBeat = feature.isStrongBeat;
            step.isWeakBeat = feature.isWeakBeat;
            step.nearPhraseBoundary = feature.nearPhraseBoundary;

            step.lowEnergy = std::clamp(0.58f * feature.low + 0.42f * aggregate.lowEnergy, 0.0f, 1.0f);
            step.midEnergy = std::clamp(0.58f * feature.mid + 0.42f * aggregate.midEnergy, 0.0f, 1.0f);
            step.highEnergy = std::clamp(0.58f * feature.high + 0.42f * aggregate.highEnergy, 0.0f, 1.0f);

            step.onsetBroadband = std::clamp(0.45f * feature.onset + 0.55f * aggregate.onsetBroadband, 0.0f, 1.0f);
            step.onsetLow = std::clamp(0.30f * feature.onset + 0.70f * aggregate.onsetLow, 0.0f, 1.0f);
            step.onsetMid = std::clamp(0.30f * feature.onset + 0.70f * aggregate.onsetMid, 0.0f, 1.0f);
            step.onsetHigh = std::clamp(0.30f * feature.onset + 0.70f * aggregate.onsetHigh, 0.0f, 1.0f);

            step.transientSharpness = std::clamp(0.55f * aggregate.onsetBroadband + 0.25f * aggregate.onsetHigh + 0.20f * feature.accent,
                                                 0.0f,
                                                 1.0f);
            step.sustain = std::clamp(0.45f * aggregate.sustain + 0.35f * aggregate.harmonic + 0.20f * feature.energy,
                                      0.0f,
                                      1.0f);
            step.harmonicStrength = std::clamp(0.65f * aggregate.harmonic + 0.35f * (1.0f - aggregate.onsetBroadband), 0.0f, 1.0f);
            step.percussiveStrength = std::clamp(0.68f * aggregate.percussive + 0.32f * aggregate.onsetBroadband, 0.0f, 1.0f);

            const int stepInBar = evidence.stepsPerBar > 0 ? static_cast<int>(stepIndex) % evidence.stepsPerBar : 0;
            const float kickPrior = step.isStrongBeat ? 1.0f : (stepInBar == 14 || stepInBar == 15 ? 0.45f : 0.15f);
            const float snarePrior = (stepInBar == 4 || stepInBar == 12) ? 1.0f : (step.isWeakBeat ? 0.42f : 0.12f);
            const float hatPrior = (stepInBar % 2 == 0) ? 0.8f : 0.55f;
            const float offbeatPrior = (!step.isStrongBeat && !step.isWeakBeat) ? 0.75f : 0.25f;

            kickRaw[stepIndex] = 0.34f * step.lowEnergy
                + 0.24f * step.onsetLow
                + 0.16f * step.percussiveStrength
                + 0.14f * step.onsetBroadband
                + 0.12f * kickPrior;

            snareRaw[stepIndex] = 0.28f * step.midEnergy
                + 0.24f * step.onsetMid
                + 0.16f * step.percussiveStrength
                + 0.18f * step.transientSharpness
                + 0.14f * snarePrior;

            hatRaw[stepIndex] = 0.34f * step.highEnergy
                + 0.24f * step.onsetHigh
                + 0.18f * step.percussiveStrength
                + 0.12f * step.transientSharpness
                + 0.12f * hatPrior;

            openHatRaw[stepIndex] = 0.24f * step.highEnergy
                + 0.16f * step.onsetHigh
                + 0.22f * step.sustain
                + 0.18f * step.harmonicStrength
                + 0.20f * (step.nearPhraseBoundary ? 1.0f : 0.0f);

            percRaw[stepIndex] = 0.24f * step.midEnergy
                + 0.16f * step.highEnergy
                + 0.18f * step.onsetMid
                + 0.16f * step.onsetHigh
                + 0.12f * step.percussiveStrength
                + 0.14f * offbeatPrior;

            bassRaw[stepIndex] = 0.34f * step.lowEnergy
                + 0.18f * step.onsetLow
                + 0.22f * step.sustain
                + 0.18f * step.harmonicStrength
                + 0.08f * kickPrior;

            if (step.percussiveStrength > step.harmonicStrength && step.sustain < 0.25f)
                bassRaw[stepIndex] *= 0.82f;

            if (step.sustain < 0.20f)
                openHatRaw[stepIndex] *= 0.78f;
        }

        normalize(kickRaw);
        normalize(snareRaw);
        normalize(hatRaw);
        normalize(openHatRaw);
        normalize(percRaw);
        normalize(bassRaw);

        for (size_t index = 0; index < evidence.steps.size(); ++index)
        {
            evidence.steps[index].kickProbability = kickRaw[index];
            evidence.steps[index].snareProbability = snareRaw[index];
            evidence.steps[index].hatProbability = hatRaw[index];
            evidence.steps[index].openHatProbability = openHatRaw[index];
            evidence.steps[index].percProbability = percRaw[index];
            evidence.steps[index].bassProbability = bassRaw[index];
        }

        return evidence;
    }

private:
    struct StepFrameAggregate
    {
        float lowEnergy = 0.0f;
        float midEnergy = 0.0f;
        float highEnergy = 0.0f;
        float onsetBroadband = 0.0f;
        float onsetLow = 0.0f;
        float onsetMid = 0.0f;
        float onsetHigh = 0.0f;
        float percussive = 0.0f;
        float harmonic = 0.0f;
        float sustain = 0.0f;
    };

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

    static StepFrameAggregate aggregateFramesForStep(int step,
                                                     const SampleAnalysisResult& summary,
                                                     const SpectralAnalysis& spectral,
                                                     const OnsetDetectionResult& onsets,
                                                     const PercussiveHarmonicSeparation& separation)
    {
        StepFrameAggregate aggregate;

        if (spectral.frames.empty() || summary.stepsPerBar <= 0 || summary.detectedBpm <= 20.0)
            return aggregate;

        const double stepDurationSeconds = 240.0 / (summary.detectedBpm * static_cast<double>(summary.stepsPerBar));
        const double startTime = static_cast<double>(step) * stepDurationSeconds;
        const double endTime = startTime + stepDurationSeconds;
        const double frameCenterOffset = static_cast<double>(spectral.frameSize) * 0.5 / spectral.sampleRate;

        int count = 0;
        for (size_t frameIndex = 0; frameIndex < spectral.frames.size(); ++frameIndex)
        {
            const double frameStart = static_cast<double>(frameIndex * static_cast<size_t>(spectral.hopSize)) / spectral.sampleRate;
            const double frameCenter = frameStart + frameCenterOffset;
            if (frameCenter < startTime || frameCenter >= endTime)
                continue;

            const auto& spectralFrame = spectral.frames[frameIndex];
            const auto& onsetFrame = onsets.frames[frameIndex];
            const auto& separated = separation.frames[frameIndex];

            aggregate.lowEnergy += spectralFrame.lowEnergy;
            aggregate.midEnergy += spectralFrame.midEnergy;
            aggregate.highEnergy += spectralFrame.highEnergy;
            aggregate.onsetBroadband += onsetFrame.broadband;
            aggregate.onsetLow += onsetFrame.low;
            aggregate.onsetMid += onsetFrame.mid;
            aggregate.onsetHigh += onsetFrame.high;
            aggregate.percussive += separated.percussive;
            aggregate.harmonic += separated.harmonic;
            aggregate.sustain += separated.sustain;
            ++count;
        }

        if (count <= 0)
        {
            const double centerTime = startTime + 0.5 * stepDurationSeconds;
            const int nearest = std::clamp(static_cast<int>(std::round((centerTime * spectral.sampleRate) / static_cast<double>(spectral.hopSize))),
                                           0,
                                           static_cast<int>(spectral.frames.size()) - 1);

            const auto& spectralFrame = spectral.frames[static_cast<size_t>(nearest)];
            const auto& onsetFrame = onsets.frames[static_cast<size_t>(nearest)];
            const auto& separated = separation.frames[static_cast<size_t>(nearest)];

            aggregate.lowEnergy = spectralFrame.lowEnergy;
            aggregate.midEnergy = spectralFrame.midEnergy;
            aggregate.highEnergy = spectralFrame.highEnergy;
            aggregate.onsetBroadband = onsetFrame.broadband;
            aggregate.onsetLow = onsetFrame.low;
            aggregate.onsetMid = onsetFrame.mid;
            aggregate.onsetHigh = onsetFrame.high;
            aggregate.percussive = separated.percussive;
            aggregate.harmonic = separated.harmonic;
            aggregate.sustain = separated.sustain;
            return aggregate;
        }

        const float invCount = 1.0f / static_cast<float>(count);
        aggregate.lowEnergy *= invCount;
        aggregate.midEnergy *= invCount;
        aggregate.highEnergy *= invCount;
        aggregate.onsetBroadband *= invCount;
        aggregate.onsetLow *= invCount;
        aggregate.onsetMid *= invCount;
        aggregate.onsetHigh *= invCount;
        aggregate.percussive *= invCount;
        aggregate.harmonic *= invCount;
        aggregate.sustain *= invCount;
        return aggregate;
    }
};
} // namespace bbg