#pragma once

#include <random>
#include <vector>

namespace bbg
{
enum class TrapPhraseRole
{
    Base = 0,
    Lift,
    Ending
};

class TrapPhrasePlanner
{
public:
    static std::vector<TrapPhraseRole> createPlan(int bars, float density, std::mt19937& rng);
    static float roleVariationStrength(TrapPhraseRole role);
};
} // namespace bbg
