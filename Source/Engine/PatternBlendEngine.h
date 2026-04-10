#pragma once

#include <unordered_set>

#include <juce_core/juce_core.h>

#include "ExtractPatternBuilder.h"
#include "../Analysis/SampleApplyWeights.h"
#include "../Core/PatternProject.h"

namespace bbg
{
struct PatternBlendReport
{
    int copiedLaneCount = 0;
    int blendedLaneCount = 0;
    int clearedLaneCount = 0;
    bool exactCopy = false;
    std::unordered_set<TrackType> changedTracks;
};

juce::String describePatternBlendReport(const PatternBlendReport& report);

class PatternBlendEngine
{
public:
    static PatternBlendReport apply(PatternProject& project,
                                    const ExtractedPatternData& extracted,
                                    const SampleApplyWeights& weights);
};
} // namespace bbg