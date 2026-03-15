#pragma once

#include <random>
#include <vector>

namespace bbg
{
enum class RapPhraseRole
{
    Base = 0,
    Variation,
    Ending
};

class RapPhrasePlanner
{
public:
    static std::vector<RapPhraseRole> createPlan(int bars, float density, std::mt19937& rng);
    static float roleVariationStrength(RapPhraseRole role);
};
} // namespace bbg
