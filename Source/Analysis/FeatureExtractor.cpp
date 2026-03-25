#include "FeatureExtractor.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace bbg
{
namespace
{
constexpr int kDefaultStepsPerBar = 16;

float computeMean(const std::vector<float>& values)
{
    if (values.empty())
        return 0.0f;

    const float sum = std::accumulate(values.begin(), values.end(), 0.0f);
    return sum / static_cast<float>(values.size());
}
}

void FeatureExtractor::downmixToMono(const juce::AudioBuffer<float>& input,
                                     std::vector<float>& mono,
                                     bool forceDownmix) const
{
    mono.clear();

    const int numChannels = input.getNumChannels();
    const int numSamples = input.getNumSamples();
    if (numChannels <= 0 || numSamples <= 0)
        return;

    mono.resize(static_cast<size_t>(numSamples), 0.0f);

    if (numChannels == 1 || !forceDownmix)
    {
        const auto* src = input.getReadPointer(0);
        std::copy(src, src + numSamples, mono.begin());
        return;
    }

    const auto* left = input.getReadPointer(0);
    const auto* right = input.getReadPointer(1);
    for (int i = 0; i < numSamples; ++i)
        mono[static_cast<size_t>(i)] = 0.5f * (left[i] + right[i]);
}

void FeatureExtractor::normalizeWorkingLevel(std::vector<float>& mono) const
{
    if (mono.empty())
        return;

    float peak = 0.0f;
    for (const float sample : mono)
        peak = juce::jmax(peak, std::abs(sample));

    if (peak < 1.0e-6f)
        return;

    const float targetPeak = 0.95f;
    const float gain = targetPeak / peak;
    for (float& sample : mono)
        sample *= gain;
}

void FeatureExtractor::computeStepFeatures(const std::vector<float>& mono,
                                           double sampleRate,
                                           double bpm,
                                           const SampleAnalysisRequest& request,
                                           SampleAnalysisResult& result) const
{
    result.valid = false;
    result.sampleRate = sampleRate;
    result.detectedBpm = bpm;
    result.stepsPerBar = kDefaultStepsPerBar;
    result.analyzedBars = juce::jmax(1, request.barsToCapture);
    result.totalSteps = result.analyzedBars * result.stepsPerBar;

    if (mono.empty() || sampleRate <= 1000.0 || bpm <= 20.0)
        return;

    const double stepSeconds = 240.0 / (bpm * static_cast<double>(result.stepsPerBar));
    const int samplesPerStep = juce::jmax(1, static_cast<int>(std::round(stepSeconds * sampleRate)));
    const int totalSteps = juce::jmax(1, juce::jmin(result.totalSteps,
                                                    static_cast<int>(mono.size()) / juce::jmax(1, samplesPerStep)));

    result.totalSteps = totalSteps;
    result.analyzedBars = juce::jmax(1, totalSteps / result.stepsPerBar);

    result.energyPerStep.assign(static_cast<size_t>(totalSteps), 0.0f);
    result.onsetStrengthPerStep.assign(static_cast<size_t>(totalSteps), 0.0f);
    result.accentPerStep.assign(static_cast<size_t>(totalSteps), 0.0f);
    result.lowBandPerStep.assign(static_cast<size_t>(totalSteps), 0.0f);
    result.midBandPerStep.assign(static_cast<size_t>(totalSteps), 0.0f);
    result.highBandPerStep.assign(static_cast<size_t>(totalSteps), 0.0f);

    const float lowAlpha = std::exp(-2.0f * juce::MathConstants<float>::pi * (120.0f / static_cast<float>(sampleRate)));
    float lowState = 0.0f;

    for (int step = 0; step < totalSteps; ++step)
    {
        const int start = step * samplesPerStep;
        const int end = juce::jmin(static_cast<int>(mono.size()), start + samplesPerStep);
        if (end <= start)
            continue;

        float fullEnergy = 0.0f;
        float lowEnergy = 0.0f;
        float highEnergy = 0.0f;

        for (int i = start; i < end; ++i)
        {
            const float x = mono[static_cast<size_t>(i)];
            const float absX = std::abs(x);

            lowState = lowAlpha * lowState + (1.0f - lowAlpha) * x;
            const float low = std::abs(lowState);
            const float high = std::abs(x - lowState);

            fullEnergy += absX;
            lowEnergy += low;
            highEnergy += high;
        }

        const float invCount = 1.0f / static_cast<float>(end - start);
        const float energy = fullEnergy * invCount;
        const float low = lowEnergy * invCount;
        const float high = highEnergy * invCount;
        const float mid = juce::jmax(0.0f, energy - (0.55f * low + 0.45f * high));

        result.energyPerStep[static_cast<size_t>(step)] = energy;
        result.lowBandPerStep[static_cast<size_t>(step)] = low;
        result.midBandPerStep[static_cast<size_t>(step)] = mid;
        result.highBandPerStep[static_cast<size_t>(step)] = high;
    }

    normalizeVector(result.energyPerStep);
    normalizeVector(result.lowBandPerStep);
    normalizeVector(result.midBandPerStep);
    normalizeVector(result.highBandPerStep);

    for (int i = 1; i < totalSteps; ++i)
    {
        const float delta = result.energyPerStep[static_cast<size_t>(i)]
            - result.energyPerStep[static_cast<size_t>(i - 1)];
        result.onsetStrengthPerStep[static_cast<size_t>(i)] = juce::jmax(0.0f, delta);
    }

    normalizeVector(result.onsetStrengthPerStep);

    for (int i = 0; i < totalSteps; ++i)
    {
        const float energy = result.energyPerStep[static_cast<size_t>(i)];
        const float onset = result.onsetStrengthPerStep[static_cast<size_t>(i)];
        const float localPrev = result.energyPerStep[static_cast<size_t>(juce::jmax(0, i - 1))];
        const float localNext = result.energyPerStep[static_cast<size_t>(juce::jmin(totalSteps - 1, i + 1))];
        const float localPeak = juce::jmax(0.0f, energy - 0.5f * (localPrev + localNext));

        result.accentPerStep[static_cast<size_t>(i)] = 0.45f * onset + 0.40f * localPeak + 0.15f * energy;
    }

    normalizeVector(result.accentPerStep);

    result.densityPerBar.assign(static_cast<size_t>(result.analyzedBars), 0.0f);
    for (int bar = 0; bar < result.analyzedBars; ++bar)
    {
        const int startStep = bar * result.stepsPerBar;
        const int endStep = juce::jmin(totalSteps, startStep + result.stepsPerBar);

        int active = 0;
        for (int step = startStep; step < endStep; ++step)
        {
            if (result.onsetStrengthPerStep[static_cast<size_t>(step)] > 0.24f
                || result.energyPerStep[static_cast<size_t>(step)] > 0.44f)
            {
                ++active;
            }
        }

        const int span = juce::jmax(1, endStep - startStep);
        result.densityPerBar[static_cast<size_t>(bar)] = static_cast<float>(active) / static_cast<float>(span);
    }

    if (request.detectPhraseHints)
    {
        for (int bar = 1; bar < result.analyzedBars; ++bar)
        {
            const float prevDensity = result.densityPerBar[static_cast<size_t>(bar - 1)];
            const float currDensity = result.densityPerBar[static_cast<size_t>(bar)];
            const float drop = prevDensity - currDensity;
            const bool phraseAligned = (bar % 4 == 0);
            if (drop > 0.22f || (phraseAligned && std::abs(drop) > 0.12f))
                result.phraseBoundaryBars.push_back(bar);
        }
    }

    const float meanDensity = computeMean(result.densityPerBar);
    const float meanOnset = computeMean(result.onsetStrengthPerStep);

    result.sparse = meanDensity < 0.24f;
    result.dense = meanDensity > 0.62f;
    result.transientRich = meanOnset > 0.34f;
    result.loopLike = result.analyzedBars >= 4 && std::abs(result.densityPerBar.front() - result.densityPerBar.back()) < 0.08f;
    result.valid = true;
}

void FeatureExtractor::normalizeVector(std::vector<float>& values)
{
    if (values.empty())
        return;

    float peak = 0.0f;
    for (const float value : values)
        peak = juce::jmax(peak, std::abs(value));

    if (peak < 1.0e-6f)
        return;

    const float invPeak = 1.0f / peak;
    for (float& value : values)
        value *= invPeak;
}
} // namespace bbg
