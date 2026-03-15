#include "BoomBapLaneActivation.h"

#include <algorithm>

namespace bbg
{
namespace
{
float styleLaneBias(BoomBapSubstyle substyle, float defaultBias, float strongBias)
{
    switch (substyle)
    {
        case BoomBapSubstyle::Aggressive: return strongBias;
        case BoomBapSubstyle::LofiRap: return defaultBias * 0.72f;
        case BoomBapSubstyle::RussianUnderground: return defaultBias * 0.82f;
        default: return defaultBias;
    }
}

bool decideActivation(float probability, std::mt19937& rng)
{
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    return chance(rng) < std::clamp(probability, 0.02f, 0.98f);
}
} // namespace

BoomBapLaneActivation decideLaneActivation(const GeneratorParams& params,
                                           const BoomBapStyleProfile& style,
                                           const BoomBapBarBlueprint& bar,
                                           std::mt19937& rng)
{
    BoomBapLaneActivation lane;

    if (bar.stripToCore)
    {
        lane.useOpenHat = false;
        lane.useRide = false;
        lane.usePerc = false;
        lane.useGhostKick = false;
        lane.useClapGhostSnare = false;
        lane.useCymbal = false;
        return lane;
    }

    const float density = std::clamp(params.densityAmount, 0.0f, 1.0f);
    const float energyScale = 0.55f + bar.kickSupportAmount * 0.45f;
    const float endingBoost = bar.endLiftAmount * 0.24f;

    const float clapP = styleLaneBias(style.substyle, style.laneClapActivity, 0.92f)
        * (0.70f + density * 0.25f + bar.lateBackbeatAmount * 0.15f);
    const float ghostP = styleLaneBias(style.substyle, style.laneGhostKickActivity, 0.88f)
        * (0.60f + energyScale * 0.45f);
    const float openHatP = styleLaneBias(style.substyle, style.laneOpenHatActivity, 0.84f)
        * (0.48f + bar.hatActivity * 0.55f + endingBoost);
    const float percP = styleLaneBias(style.substyle, style.lanePercActivity, 0.86f)
        * (0.44f + bar.hatSyncopation * 0.72f + endingBoost * 0.4f);
    const float rideP = styleLaneBias(style.substyle, style.laneRideActivity, 0.52f)
        * (0.32f + bar.hatActivity * 0.78f + endingBoost);
    const float cymbalP = styleLaneBias(style.substyle, style.laneCymbalActivity, 0.46f)
        * (bar.role == PhraseRole::Ending ? 0.96f : 0.06f);

    lane.useClapGhostSnare = bar.allowClapLayer && decideActivation(clapP, rng);
    lane.useGhostKick = bar.allowGhostKick && decideActivation(ghostP, rng);
    lane.useOpenHat = bar.allowOpenHat && decideActivation(openHatP, rng);
    lane.usePerc = bar.allowPerc && decideActivation(percP, rng);
    lane.useRide = bar.allowRide && decideActivation(rideP, rng);
    lane.useCymbal = (bar.role == PhraseRole::Ending) && decideActivation(cymbalP, rng);

    if (style.substyle == BoomBapSubstyle::LofiRap)
    {
        lane.useRide = false;
        lane.useCymbal = false;
        lane.useClapGhostSnare = lane.useClapGhostSnare && (bar.role == PhraseRole::Ending);
        lane.useGhostKick = lane.useGhostKick && (bar.kickSupportAmount > 0.65f);
    }
    else if (style.substyle == BoomBapSubstyle::RussianUnderground)
    {
        lane.useRide = false;
        lane.useCymbal = false;
        lane.usePerc = lane.usePerc && (bar.hatSyncopation > 0.48f);
        lane.useClapGhostSnare = lane.useClapGhostSnare && (bar.role == PhraseRole::Ending);
    }
    else if (style.substyle == BoomBapSubstyle::Aggressive)
    {
        lane.useGhostKick = bar.allowGhostKick && decideActivation(std::min(0.96f, ghostP + 0.12f), rng);
        lane.usePerc = bar.allowPerc && decideActivation(std::min(0.96f, percP + 0.08f), rng);
    }
    else if (style.substyle == BoomBapSubstyle::Dusty || style.substyle == BoomBapSubstyle::LaidBack)
    {
        lane.useRide = lane.useRide && decideActivation(0.42f, rng);
        lane.usePerc = lane.usePerc && decideActivation(0.68f, rng);
    }

    return lane;
}

BoomBapLaneActivationPlan buildBoomBapLaneActivation(const GeneratorParams& params,
                                                     const BoomBapStyleProfile& style,
                                                     const BoomBapGrooveBlueprint& blueprint,
                                                     std::mt19937& rng)
{
    BoomBapLaneActivationPlan plan;
    plan.bars.reserve(blueprint.bars.size());

    for (const auto& bar : blueprint.bars)
    {
        const auto lane = decideLaneActivation(params, style, bar, rng);

        plan.anyRide = plan.anyRide || lane.useRide;
        plan.anyCymbal = plan.anyCymbal || lane.useCymbal;
        plan.bars.push_back(lane);
    }

    if (plan.bars.empty())
        plan.bars.push_back({});

    return plan;
}
} // namespace bbg
