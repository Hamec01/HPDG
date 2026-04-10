#pragma once

#include <unordered_set>

#include <juce_core/juce_core.h>

#include "../Core/PatternProject.h"

namespace bbg
{
struct SubstyleRuleReport
{
    bool applied = false;
    int prunedNotes = 0;
    std::unordered_set<TrackType> changedTracks;
};

juce::String describeSubstyleRuleReport(const SubstyleRuleReport& report);

class SubstyleRuleEnforcer
{
public:
    static SubstyleRuleReport enforce(PatternProject& project);
};
} // namespace bbg