#pragma once

#include <random>
#include <vector>

namespace bbg
{
enum class DrillPhraseRole
{
    Base = 0,
    Statement = 0,
    Response,
    Tension,
    Release,
    Ending
};

struct DrillPhraseBarPlan
{
    DrillPhraseRole role = DrillPhraseRole::Statement;
    float energyLevel = 0.5f;
    float densityBudget = 0.5f;
    float variationBudget = 0.5f;
    bool isTransition = false;
    bool isEnding = false;
};

struct DrillPhrasePlan
{
    std::vector<DrillPhraseBarPlan> bars;
};

class DrillPhrasePlanner
{
public:
    static DrillPhrasePlan createDetailedPlan(int bars, float density, std::mt19937& rng);
    static std::vector<DrillPhraseRole> createPlan(int bars, float density, std::mt19937& rng);
    static float roleVariationStrength(DrillPhraseRole role);
};
} // namespace bbg
