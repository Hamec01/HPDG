#include "DrillPhrasePlanner.h"

namespace bbg
{
std::vector<DrillPhraseRole> DrillPhrasePlanner::createPlan(int bars, float density, std::mt19937& rng)
{
    const int count = bars < 1 ? 1 : bars;
    std::vector<DrillPhraseRole> plan(static_cast<size_t>(count), DrillPhraseRole::Base);
    if (count == 1)
        return plan;

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    const float tensionChance = 0.28f + density * 0.26f;

    for (int bar = 1; bar < count; ++bar)
    {
        if (bar == count - 1)
        {
            plan[static_cast<size_t>(bar)] = DrillPhraseRole::Ending;
            continue;
        }

        if (chance(rng) < tensionChance)
            plan[static_cast<size_t>(bar)] = DrillPhraseRole::Tension;
    }

    return plan;
}

float DrillPhrasePlanner::roleVariationStrength(DrillPhraseRole role)
{
    switch (role)
    {
        case DrillPhraseRole::Base: return 0.0f;
        case DrillPhraseRole::Tension: return 0.5f;
        case DrillPhraseRole::Ending: return 0.62f;
        default: return 0.0f;
    }
}
} // namespace bbg
