#pragma once

#include <random>
#include <vector>

namespace bbg
{
enum class PhraseRole
{
    Base = 0,
    Variation,
    Contrast,
    Ending
};

class BoomBapPhrasePlanner
{
public:
    static std::vector<PhraseRole> createPlan(int bars, float variationAmount, std::mt19937& rng);
    static float roleVariationStrength(PhraseRole role);
};
} // namespace bbg
