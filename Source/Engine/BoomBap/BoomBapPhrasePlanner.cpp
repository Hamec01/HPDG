#include "BoomBapPhrasePlanner.h"

namespace bbg
{
std::vector<PhraseRole> BoomBapPhrasePlanner::createPlan(int bars, float variationAmount, std::mt19937& rng)
{
    const int count = bars < 1 ? 1 : bars;
    std::vector<PhraseRole> roles(static_cast<size_t>(count), PhraseRole::Base);

    if (count == 1)
        return roles;

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);

    for (int bar = 0; bar < count; ++bar)
    {
        if (bar == 0)
        {
            roles[static_cast<size_t>(bar)] = PhraseRole::Base;
            continue;
        }

        if (bar == count - 1)
        {
            roles[static_cast<size_t>(bar)] = PhraseRole::Ending;
            continue;
        }

        if (bar == 1)
        {
            roles[static_cast<size_t>(bar)] = chance(rng) < (0.78f + variationAmount * 0.18f)
                ? PhraseRole::Variation
                : PhraseRole::Base;
            continue;
        }

        if ((bar + 1) % 4 == 0)
        {
            roles[static_cast<size_t>(bar)] = chance(rng) < (0.62f + variationAmount * 0.28f)
                ? PhraseRole::Ending
                : PhraseRole::Contrast;
            continue;
        }

        roles[static_cast<size_t>(bar)] = chance(rng) < (0.40f + variationAmount * 0.40f)
            ? PhraseRole::Contrast
            : PhraseRole::Base;
    }

    return roles;
}

float BoomBapPhrasePlanner::roleVariationStrength(PhraseRole role)
{
    switch (role)
    {
        case PhraseRole::Base: return 0.0f;
        case PhraseRole::Variation: return 0.35f;
        case PhraseRole::Contrast: return 0.6f;
        case PhraseRole::Ending: return 0.75f;
        default: return 0.0f;
    }
}
} // namespace bbg
