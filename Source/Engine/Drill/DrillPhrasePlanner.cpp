#include "DrillPhrasePlanner.h"

namespace bbg
{
DrillPhrasePlan DrillPhrasePlanner::createDetailedPlan(int bars, float density, std::mt19937& rng)
{
    const int count = bars < 1 ? 1 : bars;
    DrillPhrasePlan plan;
    plan.bars.resize(static_cast<size_t>(count));
    if (count == 1)
    {
        auto& bar = plan.bars.front();
        bar.role = DrillPhraseRole::Ending;
        bar.energyLevel = 0.52f;
        bar.densityBudget = 0.48f;
        bar.variationBudget = 0.44f;
        bar.isEnding = true;
        return plan;
    }

    const float densityNorm = std::clamp(density, 0.0f, 1.0f);

    for (int bar = 0; bar < count; ++bar)
    {
        auto& out = plan.bars[static_cast<size_t>(bar)];

        if (bar == count - 1)
        {
            out.role = DrillPhraseRole::Ending;
            out.energyLevel = std::clamp(0.72f + densityNorm * 0.18f, 0.0f, 1.0f);
            out.densityBudget = std::clamp(0.58f + densityNorm * 0.18f, 0.0f, 1.0f);
            out.variationBudget = 0.84f;
            out.isTransition = true;
            out.isEnding = true;
            continue;
        }

        const int phase = bar % 4;
        if (phase == 1)
            out.role = DrillPhraseRole::Response;
        else if (phase == 3 || bar == count - 2)
            out.role = DrillPhraseRole::Tension;
        else
            out.role = DrillPhraseRole::Statement;

        switch (out.role)
        {
            case DrillPhraseRole::Statement:
                out.energyLevel = std::clamp(0.46f + densityNorm * 0.20f, 0.0f, 1.0f);
                out.densityBudget = std::clamp(0.42f + densityNorm * 0.24f, 0.0f, 1.0f);
                out.variationBudget = 0.34f;
                out.isTransition = false;
                break;
            case DrillPhraseRole::Response:
                out.energyLevel = std::clamp(0.40f + densityNorm * 0.14f, 0.0f, 1.0f);
                out.densityBudget = std::clamp(0.30f + densityNorm * 0.20f, 0.0f, 1.0f);
                out.variationBudget = 0.26f;
                out.isTransition = false;
                break;
            case DrillPhraseRole::Tension:
                out.energyLevel = std::clamp(0.62f + densityNorm * 0.18f, 0.0f, 1.0f);
                out.densityBudget = std::clamp(0.50f + densityNorm * 0.24f, 0.0f, 1.0f);
                out.variationBudget = 0.58f;
                out.isTransition = true;
                break;
            default:
                break;
        }
    }

    (void)rng;
    return plan;
}

std::vector<DrillPhraseRole> DrillPhrasePlanner::createPlan(int bars, float density, std::mt19937& rng)
{
    const auto detailed = createDetailedPlan(bars, density, rng);
    std::vector<DrillPhraseRole> out;
    out.reserve(detailed.bars.size());
    for (const auto& bar : detailed.bars)
        out.push_back(bar.role);
    return out;
}

float DrillPhrasePlanner::roleVariationStrength(DrillPhraseRole role)
{
    switch (role)
    {
        case DrillPhraseRole::Statement: return 0.0f;
        case DrillPhraseRole::Response: return 0.22f;
        case DrillPhraseRole::Tension: return 0.5f;
        case DrillPhraseRole::Release: return 0.22f;
        case DrillPhraseRole::Ending: return 0.62f;
        default: return 0.0f;
    }
}
} // namespace bbg
