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
        case BoomBapSubstyle::LofiRap: return defaultBias * 0.52f;
        case BoomBapSubstyle::RussianUnderground: return defaultBias * 0.56f;
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

    if (style.substyle == BoomBapSubstyle::Classic)
    {
        const bool phraseLift = bar.role == PhraseRole::Variation || bar.role == PhraseRole::Ending;
        lane.useClapGhostSnare = bar.allowClapLayer
            && phraseLift
            && decideActivation(clapP * (bar.role == PhraseRole::Ending ? 0.42f : 0.24f), rng);
        lane.useGhostKick = bar.allowGhostKick
            && decideActivation(ghostP * (bar.role == PhraseRole::Ending ? 0.52f : 0.36f), rng);
        lane.useOpenHat = bar.allowOpenHat
            && (bar.role == PhraseRole::Ending || bar.endLiftAmount > 0.66f)
            && decideActivation(openHatP * 0.42f, rng);
        lane.usePerc = bar.allowPerc
            && phraseLift
            && decideActivation(percP * 0.36f, rng);
        lane.useRide = false;
        lane.useCymbal = (bar.role == PhraseRole::Ending) && decideActivation(cymbalP * 0.40f, rng);
        return lane;
    }

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
        lane.useOpenHat = lane.useOpenHat
            && bar.role == PhraseRole::Ending
            && decideActivation(0.16f, rng);
        lane.useClapGhostSnare = lane.useClapGhostSnare
            && bar.role == PhraseRole::Ending
            && decideActivation(0.14f, rng);
        lane.useGhostKick = lane.useGhostKick
            && bar.kickSupportAmount > 0.68f
            && decideActivation(0.18f, rng);
        lane.usePerc = lane.usePerc
            && (bar.role == PhraseRole::Ending || bar.hatSyncopation > 0.62f)
            && decideActivation(0.22f, rng);
    }
    else if (style.substyle == BoomBapSubstyle::RussianUnderground)
    {
        lane.useRide = false;
        lane.useCymbal = false;
        lane.useOpenHat = false;
        lane.useGhostKick = lane.useGhostKick
            && bar.kickSupportAmount > 0.55f
            && decideActivation(0.22f, rng);
        lane.usePerc = lane.usePerc
            && (bar.role == PhraseRole::Ending || bar.hatSyncopation > 0.55f)
            && decideActivation(0.28f, rng);
        lane.useClapGhostSnare = lane.useClapGhostSnare
            && bar.role == PhraseRole::Ending
            && decideActivation(0.18f, rng);
    }
    else if (style.substyle == BoomBapSubstyle::Aggressive)
    {
        lane.useGhostKick = bar.allowGhostKick && decideActivation(std::min(0.96f, ghostP + 0.12f), rng);
        lane.usePerc = bar.allowPerc && decideActivation(std::min(0.96f, percP + 0.08f), rng);
    }
    else if (style.substyle == BoomBapSubstyle::Dusty)
    {
        const bool phraseLift = bar.role == PhraseRole::Variation || bar.role == PhraseRole::Ending;
        lane.useClapGhostSnare = lane.useClapGhostSnare && decideActivation(phraseLift ? 0.52f : 0.30f, rng);
        lane.useGhostKick = lane.useGhostKick && decideActivation(0.42f + bar.kickSupportAmount * 0.18f, rng);
        lane.useOpenHat = lane.useOpenHat && decideActivation(bar.role == PhraseRole::Ending ? 0.46f : 0.28f, rng);
        lane.usePerc = lane.usePerc && decideActivation(phraseLift ? 0.58f : 0.42f, rng);
        lane.useRide = lane.useRide && decideActivation((bar.hatActivity > 0.58f || phraseLift) ? 0.62f : 0.42f, rng);
        lane.useCymbal = false;
    }
    else if (style.substyle == BoomBapSubstyle::Jazzy)
    {
        const bool phraseLift = bar.role == PhraseRole::Variation || bar.role == PhraseRole::Ending;
        lane.useRide = bar.allowRide && decideActivation(phraseLift ? 0.98f : 0.90f, rng);
        lane.useClapGhostSnare = lane.useClapGhostSnare && decideActivation(phraseLift ? 0.72f : 0.52f, rng);
        lane.useGhostKick = lane.useGhostKick && decideActivation(0.32f + bar.kickSupportAmount * 0.20f, rng);
        lane.useOpenHat = lane.useOpenHat && decideActivation(bar.role == PhraseRole::Ending ? 0.42f : 0.18f, rng);
        lane.usePerc = lane.usePerc && decideActivation(phraseLift ? 0.74f : 0.58f, rng);
        lane.useCymbal = (bar.role == PhraseRole::Ending) && decideActivation(cymbalP * 0.48f, rng);
    }
    else if (style.substyle == BoomBapSubstyle::LaidBack)
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
