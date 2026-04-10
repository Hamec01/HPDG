#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include "AudioFeatureMap.h"
#include "BasslineInferer.h"
#include "FeatureExtractor.h"
#include "GenerationHintsBuilder.h"
#include "LaneEventInferer.h"
#include "OnsetDetector.h"
#include "PercussiveHarmonicSeparator.h"
#include "SampleAnalysisBundle.h"
#include "SampleAnalysisRequest.h"
#include "SampleAnalysisResult.h"
#include "SampleTranscriber.h"
#include "STFTAnalyzer.h"

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

    SampleAnalysisBundle analyzeBufferExtended(const juce::AudioBuffer<float>& input,
                                               double sampleRate,
                                               const SampleAnalysisRequest& request,
                                               double hostBpm) const;

    SampleAnalysisBundle analyzeAudioFileExtended(const juce::File& file,
                                                  const SampleAnalysisRequest& request,
                                                  double hostBpm,
                                                  juce::String* errorMessage = nullptr) const;

    AudioFeatureMap buildFeatureMap(const SampleAnalysisResult& result) const;

private:
    double estimateBpmFromEnergy(const std::vector<float>& energyPerStep,
                                 int stepsPerBar,
                                 double fallbackBpm) const;

    SampleAnalysisResult analyzePreparedMono(const std::vector<float>& mono,
                                             double sampleRate,
                                             const SampleAnalysisRequest& request,
                                             double hostBpm) const;

    FeatureExtractor featureExtractor;
    STFTAnalyzer stftAnalyzer;
    OnsetDetector onsetDetector;
    PercussiveHarmonicSeparator percussiveHarmonicSeparator;
    LaneEventInferer laneEventInferer;
    BasslineInferer basslineInferer;
    GenerationHintsBuilder hintsBuilder;
    SampleTranscriber sampleTranscriber;
};
} // namespace bbg
