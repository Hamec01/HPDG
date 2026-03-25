#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include "AudioFeatureMap.h"
#include "FeatureExtractor.h"
#include "SampleAnalysisRequest.h"
#include "SampleAnalysisResult.h"

namespace bbg
{
class SampleAnalyzer
{
public:
    SampleAnalysisResult analyzeBuffer(const juce::AudioBuffer<float>& input,
                                       double sampleRate,
                                       const SampleAnalysisRequest& request,
                                       double hostBpm) const;

    SampleAnalysisResult analyzeAudioFile(const juce::File& file,
                                          const SampleAnalysisRequest& request,
                                          double hostBpm,
                                          juce::String* errorMessage = nullptr) const;

    AudioFeatureMap buildFeatureMap(const SampleAnalysisResult& result) const;

private:
    double estimateBpmFromEnergy(const std::vector<float>& energyPerStep,
                                 int stepsPerBar,
                                 double fallbackBpm) const;

    FeatureExtractor featureExtractor;
};
} // namespace bbg
