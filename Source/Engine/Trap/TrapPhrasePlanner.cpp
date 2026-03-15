#include "TrapPhrasePlanner.h"

namespace bbg
{
std::vector<TrapPhraseRole> TrapPhrasePlanner::createPlan(int bars, float density, std::mt19937& rng)
{
    const int count = bars < 1 ? 1 : bars;
    std::vector<TrapPhraseRole> plan(static_cast<size_t>(count), TrapPhraseRole::Base);
    if (count == 1)
        return plan;

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    const float liftChance = 0.22f + density * 0.24f;

    for (int bar = 1; bar < count; ++bar)
    {
        if (bar == count - 1)
        {
            plan[static_cast<size_t>(bar)] = TrapPhraseRole::Ending;
            continue;
        }

        if (chance(rng) < liftChance)
            plan[static_cast<size_t>(bar)] = TrapPhraseRole::Lift;
    }

    return plan;
}

float TrapPhrasePlanner::roleVariationStrength(TrapPhraseRole role)
{
    switch (role)
    {
        case TrapPhraseRole::Base: return 0.0f;
        case TrapPhraseRole::Lift: return 0.42f;
        case TrapPhraseRole::Ending: return 0.58f;
        default: return 0.0f;
    }
}
} // namespace bbg
