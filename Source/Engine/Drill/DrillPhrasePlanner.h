#pragma once

#include <array>
#include <vector>

#include "../../Core/PatternProject.h"

namespace bbg
{
enum class DrillPhraseBarRole
{
    Statement = 0,
    Response,
    Lift,
    Release
};

enum class DrillHatDensityIntent
{
    Sparse = 0,
    Medium,
    Dense
};

enum class DrillSupportAccentIntent
{
    None = 0,
    Light,
    Push,
    Drag
};

enum class DrillKickDensityIntent
{
    Sparse = 0,
    Medium,
    Push
};

enum class DrillLowEndIntent
{
    Hold = 0,
    Anchor,
    Move,
    Release
};

struct DrillBarAnchorMap
{
    std::array<int, 6> hatCarrierSteps { { -1, -1, -1, -1, -1, -1 } };
    std::array<int, 4> kickAnchorSteps { { -1, -1, -1, -1 } };
    std::array<int, 2> snareAnchorSteps { { -1, -1 } };
    std::array<int, 4> supportAccentSteps { { -1, -1, -1, -1 } };
    std::array<int, 4> lowEndAnchorSteps { { -1, -1, -1, -1 } };
};

struct DrillPhraseBarPlan
{
    int barIndex = 0;
    DrillPhraseBarRole role = DrillPhraseBarRole::Statement;
    bool isPhraseStart = false;
    bool isPhraseEnd = false;
    bool isStrongBar = false;
    bool isWeakBar = false;
    DrillBarAnchorMap anchorMap;
    DrillHatDensityIntent hatDensity = DrillHatDensityIntent::Medium;
    DrillSupportAccentIntent supportAccent = DrillSupportAccentIntent::Light;
    DrillKickDensityIntent kickDensity = DrillKickDensityIntent::Medium;
    DrillLowEndIntent lowEnd = DrillLowEndIntent::Anchor;
};

struct DrillPhrasePlan
{
    int phraseSpanBars = 1;
    int substyleIndex = 0;
    std::vector<DrillPhraseBarPlan> bars;
    juce::String summary;
};

juce::String toString(DrillPhraseBarRole role);
juce::String toString(DrillHatDensityIntent intent);
juce::String toString(DrillSupportAccentIntent intent);
juce::String toString(DrillKickDensityIntent intent);
juce::String toString(DrillLowEndIntent intent);

class DrillPhrasePlanner
{
public:
    static DrillPhrasePlan buildPlan(const PatternProject& project);
};
} // namespace bbg