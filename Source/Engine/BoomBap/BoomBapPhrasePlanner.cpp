#include "BoomBapPhrasePlanner.h"

#include <algorithm>

namespace bbg
{
namespace
{
struct PhrasePlanProfile
{
    float variationBar2 = 0.72f;
    float contrastMid = 0.32f;
    float miniEnding = 0.36f;
    float strippedBar = 0.12f;
    float responseBar = 0.28f;
    float densityDrop = 0.22f;
};

PhrasePlanProfile profileForSubstyle(BoomBapSubstyle substyle)
{
    switch (substyle)
    {
        case BoomBapSubstyle::Classic: return { 0.70f, 0.30f, 0.36f, 0.08f, 0.30f, 0.18f };
        case BoomBapSubstyle::Dusty: return { 0.48f, 0.22f, 0.28f, 0.30f, 0.20f, 0.34f };
        case BoomBapSubstyle::Jazzy: return { 0.78f, 0.42f, 0.44f, 0.10f, 0.56f, 0.16f };
        case BoomBapSubstyle::Aggressive: return { 0.64f, 0.24f, 0.52f, 0.04f, 0.22f, 0.10f };
        case BoomBapSubstyle::LaidBack: return { 0.58f, 0.26f, 0.34f, 0.24f, 0.28f, 0.26f };
        case BoomBapSubstyle::BoomBapGold: return { 0.74f, 0.34f, 0.58f, 0.08f, 0.34f, 0.16f };
        case BoomBapSubstyle::RussianUnderground: return { 0.44f, 0.16f, 0.30f, 0.34f, 0.14f, 0.30f };
        case BoomBapSubstyle::LofiRap: return { 0.36f, 0.12f, 0.24f, 0.44f, 0.12f, 0.40f };
        default: return {};
    }
}
} // namespace

std::vector<PhraseRole> BoomBapPhrasePlanner::createPlan(int bars, float variationAmount, std::mt19937& rng)
{
    return createPlan(bars, variationAmount, BoomBapSubstyle::Classic, 0.5f, rng);
}

std::vector<PhraseRole> BoomBapPhrasePlanner::createPlan(int bars,
                                                          float variationAmount,
                                                          BoomBapSubstyle substyle,
                                                          float density,
                                                          std::mt19937& rng)
{
    const int count = std::max(1, bars);
    std::vector<PhraseRole> roles(static_cast<size_t>(count), PhraseRole::Base);

    if (count == 1)
        return roles;

    const auto profile = profileForSubstyle(substyle);
    const float var = std::clamp(variationAmount, 0.0f, 1.0f);
    const float den = std::clamp(density, 0.0f, 1.0f);
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

        const bool isBar2 = bar == 1;
        const bool phrasePivot = ((bar + 1) % 4) == 0;
        const bool midBar = bar > 1 && bar < count - 1;

        float pVariation = profile.variationBar2 + var * 0.22f - (den < 0.40f ? 0.08f : 0.0f);
        float pContrast = profile.contrastMid + var * 0.20f + (den > 0.65f ? 0.08f : 0.0f);
        float pMiniEnding = profile.miniEnding + var * 0.24f;
        float pStripped = profile.strippedBar + (den < 0.45f ? 0.10f : -0.04f);
        float pResponse = profile.responseBar + var * 0.20f;
        float pDensityDrop = profile.densityDrop + (den > 0.68f ? 0.12f : 0.0f);

        if (isBar2 && chance(rng) < std::clamp(pVariation, 0.02f, 0.96f))
        {
            roles[static_cast<size_t>(bar)] = PhraseRole::Variation;
            continue;
        }

        if (phrasePivot && chance(rng) < std::clamp(pMiniEnding, 0.04f, 0.95f))
        {
            roles[static_cast<size_t>(bar)] = PhraseRole::Ending;
            continue;
        }

        if (midBar && chance(rng) < std::clamp(pDensityDrop * 0.8f + pStripped * 0.4f, 0.03f, 0.86f))
        {
            roles[static_cast<size_t>(bar)] = PhraseRole::Contrast;
            continue;
        }

        if (midBar && chance(rng) < std::clamp(pResponse, 0.04f, 0.92f))
        {
            roles[static_cast<size_t>(bar)] = PhraseRole::Variation;
            continue;
        }

        if (midBar && chance(rng) < std::clamp(pContrast, 0.03f, 0.9f))
        {
            roles[static_cast<size_t>(bar)] = PhraseRole::Contrast;
            continue;
        }

        roles[static_cast<size_t>(bar)] = PhraseRole::Base;
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
