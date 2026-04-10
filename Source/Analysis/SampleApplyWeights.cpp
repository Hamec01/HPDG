#include "SampleApplyWeights.h"

namespace bbg
{
SampleApplyWeights makeSampleApplyWeights(SampleApplyMode mode, AnalysisMode analysisMode)
{
    SampleApplyWeights weights;

    switch (mode)
    {
        case SampleApplyMode::GenreFirst:
            weights.extractedDrumsWeight = 0.32f;
            weights.generatedDrumsWeight = 0.88f;
            weights.extractedBassWeight = 0.40f;
            weights.generatedBassWeight = 0.82f;
            weights.genreFillAmount = 0.42f;
            break;

        case SampleApplyMode::SampleFirst:
            weights.extractedDrumsWeight = 0.84f;
            weights.generatedDrumsWeight = 0.28f;
            weights.extractedBassWeight = 0.90f;
            weights.generatedBassWeight = 0.22f;
            weights.genreFillAmount = 0.18f;
            break;

        case SampleApplyMode::ExactCopy:
            weights.extractedDrumsWeight = 1.0f;
            weights.generatedDrumsWeight = 0.0f;
            weights.extractedBassWeight = 1.0f;
            weights.generatedBassWeight = 0.0f;
            weights.genreFillAmount = 0.0f;
            weights.exactCopy = true;
            break;

        case SampleApplyMode::Blend:
        default:
            weights.extractedDrumsWeight = 0.58f;
            weights.generatedDrumsWeight = 0.62f;
            weights.extractedBassWeight = 0.66f;
            weights.generatedBassWeight = 0.56f;
            weights.genreFillAmount = 0.30f;
            break;
    }

    if (!weights.exactCopy && analysisMode == AnalysisMode::ExtractFromSample)
    {
        weights.extractedDrumsWeight = juce::jlimit(0.0f, 1.0f, weights.extractedDrumsWeight + 0.08f);
        weights.generatedDrumsWeight = juce::jlimit(0.0f, 1.0f, weights.generatedDrumsWeight - 0.08f);
        weights.extractedBassWeight = juce::jlimit(0.0f, 1.0f, weights.extractedBassWeight + 0.08f);
        weights.generatedBassWeight = juce::jlimit(0.0f, 1.0f, weights.generatedBassWeight - 0.08f);
        weights.genreFillAmount = juce::jlimit(0.0f, 1.0f, weights.genreFillAmount - 0.06f);
    }

    return weights;
}

juce::String describeSampleApplyWeights(const SampleApplyWeights& weights)
{
    juce::String line;
    line << "drums sample " << juce::String(weights.extractedDrumsWeight, 2)
         << " / genre " << juce::String(weights.generatedDrumsWeight, 2)
         << " | bass sample " << juce::String(weights.extractedBassWeight, 2)
         << " / genre " << juce::String(weights.generatedBassWeight, 2)
         << " | fill " << juce::String(weights.genreFillAmount, 2);

    if (weights.exactCopy)
        line << " | exact";

    return line;
}
} // namespace bbg