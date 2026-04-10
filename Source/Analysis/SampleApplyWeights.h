#pragma once

#include <juce_core/juce_core.h>

#include "SampleAnalysisRequest.h"
#include "SampleApplyMode.h"

namespace bbg
{
struct SampleApplyWeights
{
    float extractedDrumsWeight = 0.60f;
    float generatedDrumsWeight = 0.60f;
    float extractedBassWeight = 0.65f;
    float generatedBassWeight = 0.55f;
    float genreFillAmount = 0.35f;
    bool exactCopy = false;
};

SampleApplyWeights makeSampleApplyWeights(SampleApplyMode mode, AnalysisMode analysisMode);
juce::String describeSampleApplyWeights(const SampleApplyWeights& weights);
} // namespace bbg