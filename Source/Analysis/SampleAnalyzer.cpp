#include "SampleAnalyzer.h"

#include <cstdint>
#include <cmath>

namespace bbg
{
namespace
{
constexpr int kMinBarsForHinting = 2;

double applyTempoHandling(double bpm, SampleAnalysisRequest::TempoHandling handling)
{
    double out = juce::jlimit(60.0, 180.0, bpm);

    switch (handling)
    {
        case SampleAnalysisRequest::TempoHandling::PreferHalfTime:
            if (out >= 120.0)
                out *= 0.5;
            break;

        case SampleAnalysisRequest::TempoHandling::PreferDoubleTime:
            if (out <= 95.0)
                out *= 2.0;
            break;

        case SampleAnalysisRequest::TempoHandling::KeepDetected:
            break;

        case SampleAnalysisRequest::TempoHandling::Auto:
        default:
            // Hip-hop oriented auto-fit: fold very fast estimates to half-time first.
            if (out >= 150.0)
                out *= 0.5;
            break;
    }

    return juce::jlimit(60.0, 180.0, out);
}
}

SampleAnalysisResult SampleAnalyzer::analyzeBuffer(const juce::AudioBuffer<float>& input,
                                                   double sampleRate,
                                                   const SampleAnalysisRequest& request,
                                                   double hostBpm) const
{
    SampleAnalysisResult result;

    std::vector<float> mono;
    featureExtractor.downmixToMono(input, mono, request.downmixToMono);
    featureExtractor.normalizeWorkingLevel(mono);

    const bool hostTempoUsable = request.useHostTempoIfAvailable && hostBpm > 20.0;
    const double bpmForGrid = hostTempoUsable ? hostBpm : 120.0;

    result.bpmFromHost = hostTempoUsable;
    result.bpmReliable = hostTempoUsable;

    featureExtractor.computeStepFeatures(mono, sampleRate, bpmForGrid, request, result);

    if (!result.valid)
        return result;

    if (!hostTempoUsable && request.detectTempoFromFile)
    {
        const double estimated = estimateBpmFromEnergy(result.energyPerStep, result.stepsPerBar, bpmForGrid);
        result.detectedBpm = applyTempoHandling(estimated, request.tempoHandling);
        result.bpmReliable = estimated > 0.0;
    }

    return result;
}

SampleAnalysisResult SampleAnalyzer::analyzeAudioFile(const juce::File& file,
                                                      const SampleAnalysisRequest& request,
                                                      double hostBpm,
                                                      juce::String* errorMessage) const
{
    if (!file.existsAsFile())
    {
        if (errorMessage != nullptr)
            *errorMessage = "Audio file does not exist.";
        return {};
    }

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (reader == nullptr)
    {
        if (errorMessage != nullptr)
            *errorMessage = "Unsupported audio file format.";
        return {};
    }

    const int64_t lengthSamples = static_cast<int64_t>(reader->lengthInSamples);
    if (lengthSamples <= 0)
    {
        if (errorMessage != nullptr)
            *errorMessage = "Audio file is empty.";
        return {};
    }

    const int channels = juce::jlimit(1, 2, static_cast<int>(reader->numChannels));
    const int samplesToRead = static_cast<int>(juce::jmin<int64_t>(lengthSamples,
                                                                   static_cast<int64_t>(reader->sampleRate * 60.0 * 4.0)));
    juce::AudioBuffer<float> buffer(channels, samplesToRead);

    if (!reader->read(&buffer, 0, samplesToRead, 0, true, channels > 1))
    {
        if (errorMessage != nullptr)
            *errorMessage = "Failed to read audio file samples.";
        return {};
    }

    auto result = analyzeBuffer(buffer, reader->sampleRate, request, hostBpm);
    if (result.valid && result.analyzedBars < kMinBarsForHinting)
        result.phraseBoundaryBars.clear();

    if (errorMessage != nullptr && !result.valid)
        *errorMessage = "Analysis completed with invalid result.";

    return result;
}

AudioFeatureMap SampleAnalyzer::buildFeatureMap(const SampleAnalysisResult& result) const
{
    AudioFeatureMap map;
    if (!result.valid)
        return map;

    map.bars = result.analyzedBars;
    map.stepsPerBar = result.stepsPerBar;
    map.barDensity = result.densityPerBar;
    map.steps.resize(static_cast<size_t>(result.totalSteps));

    for (int i = 0; i < result.totalSteps; ++i)
    {
        auto& step = map.steps[static_cast<size_t>(i)];
        step.energy = result.energyPerStep[static_cast<size_t>(i)];
        step.accent = result.accentPerStep[static_cast<size_t>(i)];
        step.onset = result.onsetStrengthPerStep[static_cast<size_t>(i)];
        step.low = result.lowBandPerStep[static_cast<size_t>(i)];
        step.mid = result.midBandPerStep[static_cast<size_t>(i)];
        step.high = result.highBandPerStep[static_cast<size_t>(i)];

        const int stepInBar = i % juce::jmax(1, result.stepsPerBar);
        step.isStrongBeat = (stepInBar % 4 == 0);
        step.isWeakBeat = (stepInBar % 2 == 0) && !step.isStrongBeat;
    }

    for (const int boundaryBar : result.phraseBoundaryBars)
    {
        const int start = juce::jmax(0, boundaryBar * result.stepsPerBar - 2);
        const int end = juce::jmin(result.totalSteps, boundaryBar * result.stepsPerBar + 3);
        for (int i = start; i < end; ++i)
            map.steps[static_cast<size_t>(i)].nearPhraseBoundary = true;
    }

    return map;
}

double SampleAnalyzer::estimateBpmFromEnergy(const std::vector<float>& energyPerStep,
                                             int stepsPerBar,
                                             double fallbackBpm) const
{
    if (energyPerStep.size() < static_cast<size_t>(stepsPerBar * 2))
        return fallbackBpm;

    // A simple periodicity probe over bar-sized lags keeps MVP deterministic.
    const int maxLag = juce::jlimit(stepsPerBar, stepsPerBar * 4, static_cast<int>(energyPerStep.size() / 2));
    float bestScore = -1.0f;
    int bestLag = stepsPerBar;

    for (int lag = juce::jmax(2, stepsPerBar / 2); lag <= maxLag; ++lag)
    {
        float score = 0.0f;
        for (size_t i = static_cast<size_t>(lag); i < energyPerStep.size(); ++i)
            score += energyPerStep[i] * energyPerStep[i - static_cast<size_t>(lag)];

        if (score > bestScore)
        {
            bestScore = score;
            bestLag = lag;
        }
    }

    if (bestLag <= 0)
        return fallbackBpm;

    const double barsPerMinute = fallbackBpm / 4.0;
    const double lagRatio = static_cast<double>(stepsPerBar) / static_cast<double>(bestLag);
    const double estimated = juce::jlimit(60.0, 180.0, barsPerMinute * 4.0 * lagRatio);
    return estimated;
}
} // namespace bbg
