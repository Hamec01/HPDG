#include "RapPhrasePlanner.h"

namespace bbg
{
std::vector<RapPhraseRole> RapPhrasePlanner::createPlan(int bars, float density, std::mt19937& rng)
{
    const int count = bars < 1 ? 1 : bars;
    std::vector<RapPhraseRole> plan(static_cast<size_t>(count), RapPhraseRole::Base);

    if (count == 1)
        return plan;

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    const float varChance = 0.22f + density * 0.26f;

    for (int bar = 1; bar < count; ++bar)
    {
        if (bar == count - 1)
        {
            plan[static_cast<size_t>(bar)] = RapPhraseRole::Ending;
            continue;
        }

        if (chance(rng) < varChance)
            plan[static_cast<size_t>(bar)] = RapPhraseRole::Variation;
    }

    return plan;
}

float RapPhrasePlanner::roleVariationStrength(RapPhraseRole role)
{
    switch (role)
    {
        case RapPhraseRole::Base: return 0.0f;
        case RapPhraseRole::Variation: return 0.42f;
        case RapPhraseRole::Ending: return 0.55f;
        default: return 0.0f;
    }
}
} // namespace bbg
