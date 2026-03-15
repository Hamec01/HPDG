#pragma once

#include <random>
#include <vector>

namespace bbg
{
enum class DrillPhraseRole
{
    Base = 0,
    Tension,
    Ending
};

class DrillPhrasePlanner
{
public:
    static std::vector<DrillPhraseRole> createPlan(int bars, float density, std::mt19937& rng);
    static float roleVariationStrength(DrillPhraseRole role);
};
} // namespace bbg
