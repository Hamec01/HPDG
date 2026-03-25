#pragma once

#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

#include "SampleAnalysisRequest.h"
#include "SampleAnalysisResult.h"

namespace bbg
{
class FeatureExtractor
{
public:
    void downmixToMono(const juce::AudioBuffer<float>& input,
                       std::vector<float>& mono,
                       bool forceDownmix) const;

    void normalizeWorkingLevel(std::vector<float>& mono) const;

    void computeStepFeatures(const std::vector<float>& mono,
                             double sampleRate,
                             double bpm,
                             const SampleAnalysisRequest& request,
                             SampleAnalysisResult& result) const;

private:
    static void normalizeVector(std::vector<float>& values);
};
} // namespace bbg
