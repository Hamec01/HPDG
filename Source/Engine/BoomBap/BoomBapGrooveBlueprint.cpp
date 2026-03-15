#include "BoomBapGrooveBlueprint.h"

#include <algorithm>
#include <array>

namespace bbg
{
namespace
{
float roleBaseEnergy(PhraseRole role)
{
    switch (role)
    {
        case PhraseRole::Base: return 0.56f;
        case PhraseRole::Variation: return 0.68f;
        case PhraseRole::Contrast: return 0.48f;
        case PhraseRole::Ending: return 0.78f;
        default: return 0.56f;
    }
}

struct SubstyleBehavior
{
    float kickSupportBase = 0.52f;
    float hatActivityBase = 0.58f;
    float hatSyncBase = 0.42f;
    float swingBase = 0.56f;
    float lateBackbeatBase = 0.55f;
    float loosenessBase = 0.34f;
    float gritBase = 0.30f;
    float stripCoreBase = 0.12f;
};

SubstyleBehavior behaviorForSubstyle(BoomBapSubstyle substyle)
{
    switch (substyle)
    {
        case BoomBapSubstyle::Classic: return { 0.56f, 0.62f, 0.44f, 0.57f, 0.56f, 0.30f, 0.24f, 0.08f };
        case BoomBapSubstyle::Dusty: return { 0.40f, 0.46f, 0.38f, 0.58f, 0.62f, 0.48f, 0.54f, 0.24f };
        case BoomBapSubstyle::Jazzy: return { 0.48f, 0.64f, 0.62f, 0.64f, 0.66f, 0.44f, 0.28f, 0.10f };
        case BoomBapSubstyle::Aggressive: return { 0.68f, 0.72f, 0.36f, 0.52f, 0.42f, 0.20f, 0.44f, 0.05f };
        case BoomBapSubstyle::LaidBack: return { 0.44f, 0.52f, 0.42f, 0.60f, 0.72f, 0.46f, 0.22f, 0.20f };
        case BoomBapSubstyle::BoomBapGold: return { 0.58f, 0.66f, 0.50f, 0.59f, 0.58f, 0.26f, 0.18f, 0.08f };
        case BoomBapSubstyle::RussianUnderground: return { 0.54f, 0.42f, 0.30f, 0.54f, 0.60f, 0.40f, 0.62f, 0.30f };
        case BoomBapSubstyle::LofiRap: return { 0.36f, 0.40f, 0.26f, 0.54f, 0.64f, 0.52f, 0.36f, 0.38f };
        default: return {};
    }
}

float roleLift(PhraseRole role)
{
    switch (role)
    {
        case PhraseRole::Variation: return 0.08f;
        case PhraseRole::Contrast: return -0.10f;
        case PhraseRole::Ending: return 0.16f;
        case PhraseRole::Base:
        default:
            return 0.0f;
    }
}

float tempoInterpretationBias(const GeneratorParams& params, bool halfTimeReference)
{
    if (params.tempoInterpretationMode == 2)
        return 0.14f;
    if (params.tempoInterpretationMode == 1)
        return -0.04f;
    return halfTimeReference ? 0.08f : 0.0f;
}

float deterministicSeedDrift(int seed, int bar, int salt)
{
    const unsigned int x = static_cast<unsigned int>((seed + 31 * bar + 17 * salt) * 1103515245u + 12345u);
    const float normalized = static_cast<float>(x % 1000u) / 999.0f;
    return (normalized - 0.5f) * 0.18f;
}
} // namespace

BoomBapGrooveBlueprint buildBoomBapGrooveBlueprint(const GeneratorParams& params,
                                                   const BoomBapStyleProfile& style,
                                                   const std::vector<PhraseRole>& phrasePlan,
                                                   bool halfTimeReference,
                                                   std::mt19937& rng)
{
    BoomBapGrooveBlueprint blueprint;
    blueprint.phraseLengthBars = std::max(1, params.bars);
    blueprint.halfTimeReference = halfTimeReference;

    if (phrasePlan.empty())
    {
        blueprint.bars.push_back({});
        return blueprint;
    }

    const auto behavior = behaviorForSubstyle(style.substyle);
    const float density = std::clamp(params.densityAmount, 0.0f, 1.0f);
    const int bars = std::max(1, params.bars);
    std::uniform_real_distribution<float> drift(-0.06f, 0.06f);

    blueprint.bars.reserve(phrasePlan.size());
    float hatAccum = 0.0f;

    for (size_t i = 0; i < phrasePlan.size(); ++i)
    {
        const PhraseRole role = phrasePlan[i];
        BoomBapBarBlueprint bar;
        bar.barIndex = static_cast<int>(i);
        bar.role = role;

        const float roleBump = roleLift(role);
        const float densityBump = (density - 0.5f) * 0.22f;
        const float tempoBias = tempoInterpretationBias(params, halfTimeReference);
        const float seedDrift = deterministicSeedDrift(params.seed, static_cast<int>(i), 3) + drift(rng);

        bar.kickSupportAmount = std::clamp(behavior.kickSupportBase + roleBump * 0.8f + densityBump + seedDrift * 0.5f, 0.08f, 0.96f);
        bar.hatActivity = std::clamp(behavior.hatActivityBase + roleBump * 0.55f + densityBump * 1.2f + seedDrift * 0.7f, 0.05f, 0.98f);
        bar.hatSyncopation = std::clamp(behavior.hatSyncBase + roleBump * 0.5f + seedDrift * 0.8f, 0.04f, 0.95f);

        const float baseSwing = std::clamp(style.swingPercent / 100.0f, 0.44f, 0.72f);
        bar.swingStrength = std::clamp(baseSwing + behavior.swingBase * 0.2f + tempoBias + style.blueprintSwingBias + roleBump * 0.35f, 0.35f, 0.86f);
        bar.lateBackbeatAmount = std::clamp(behavior.lateBackbeatBase + roleBump * 0.4f + tempoBias * 0.45f + seedDrift * 0.3f, 0.08f, 0.95f);
        bar.endLiftAmount = std::clamp((role == PhraseRole::Ending ? 0.78f : 0.28f) + deterministicSeedDrift(params.seed, static_cast<int>(i), 5) * 0.22f, 0.0f, 1.0f);
        bar.looseness = std::clamp(behavior.loosenessBase + roleBump * 0.24f + tempoBias * 0.25f + deterministicSeedDrift(params.seed, static_cast<int>(i), 7), 0.05f, 0.96f);
        bar.grit = std::clamp(behavior.gritBase + roleBump * 0.18f + deterministicSeedDrift(params.seed, static_cast<int>(i), 11), 0.0f, 1.0f);

        const float stripChance = behavior.stripCoreBase
            + (role == PhraseRole::Contrast ? 0.2f : 0.0f)
            + (role == PhraseRole::Ending ? -0.08f : 0.0f)
            + (density < 0.45f ? 0.12f : -0.04f)
            + (bars >= 8 && ((static_cast<int>(i) % 4) == 2) ? 0.08f : 0.0f);
        bar.stripToCore = std::clamp(stripChance, 0.0f, 0.9f) > std::clamp(0.45f + drift(rng), 0.1f, 0.8f);

        bar.strongBackbeat = bar.lateBackbeatAmount > 0.34f;

        const bool ending = role == PhraseRole::Ending;
        const bool contrast = role == PhraseRole::Contrast;
        bar.allowRide = !bar.stripToCore
            && style.rideCarrierPreference > 0.08f
            && (ending || bar.hatActivity > 0.62f)
            && style.substyle != BoomBapSubstyle::LofiRap
            && style.substyle != BoomBapSubstyle::RussianUnderground;
        bar.allowOpenHat = !bar.stripToCore
            && (ending || bar.endLiftAmount > 0.52f || bar.hatActivity > 0.66f)
            && style.substyle != BoomBapSubstyle::RussianUnderground;
        bar.allowPerc = !bar.stripToCore
            && (!contrast || density > 0.58f)
            && bar.hatSyncopation > 0.2f;
        bar.allowGhostKick = !bar.stripToCore
            && (bar.kickSupportAmount > 0.4f)
            && style.substyle != BoomBapSubstyle::LofiRap;
        bar.allowClapLayer = !bar.stripToCore
            && style.substyle != BoomBapSubstyle::LofiRap
            && style.substyle != BoomBapSubstyle::RussianUnderground
            && (bar.strongBackbeat || ending);

        hatAccum += bar.hatActivity;
        blueprint.bars.push_back(bar);
    }

    blueprint.averageEnergy = hatAccum / static_cast<float>(blueprint.bars.size());
    return blueprint;
}
} // namespace bbg
