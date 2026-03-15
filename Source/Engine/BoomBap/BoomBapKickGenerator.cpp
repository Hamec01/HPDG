#include "BoomBapKickGenerator.h"

#include <algorithm>

#include "BoomBapPatternLibrary.h"
#include "../../Core/TrackRegistry.h"
#include "../TempoInterpretation.h"

namespace bbg
{
namespace
{
float substyleSupportScale(BoomBapSubstyle substyle)
{
    switch (substyle)
    {
        case BoomBapSubstyle::BoomBapGold: return 1.15f;
        case BoomBapSubstyle::RussianUnderground: return 0.84f;
        case BoomBapSubstyle::LofiRap: return 0.72f;
        case BoomBapSubstyle::Aggressive: return 1.10f;
        case BoomBapSubstyle::Dusty: return 0.92f;
        default: return 1.0f;
    }
}

float probabilityForRole(KickHitRole role, float density, float roleVar, float styleBias)
{
    const float base = role == KickHitRole::Anchor ? 0.98f : role == KickHitRole::Support ? 0.42f : 0.24f;
    const float dense = role == KickHitRole::Anchor ? 0.0f : role == KickHitRole::Support ? 0.45f : 0.38f;
    const float variation = role == KickHitRole::Anchor ? -0.04f : roleVar * 0.28f;
    return std::clamp(base + dense * density * styleBias + variation, 0.05f, 1.0f);
}
} // namespace

void BoomBapKickGenerator::generate(TrackState& track,
                                    const GeneratorParams& params,
                                    const BoomBapStyleProfile& style,
                                    const std::vector<PhraseRole>& phraseRoles,
                                    std::mt19937& rng) const
{
    track.notes.clear();

    const auto* info = TrackRegistry::find(track.type);
    const int pitch = info != nullptr ? info->defaultMidiNote : 36;

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    std::uniform_int_distribution<int> velDist(style.kickVelocityMin, style.kickVelocityMax);
    std::uniform_int_distribution<int> microDist(-style.kickTimingMaxTicks / 2, style.kickTimingMaxTicks);

    const int bars = std::max(1, params.bars);
    const auto tempoBand = selectTempoBand(params.bpm, params, 120.0f, 140.0f, 98.0f, 126.0f);
    float tempoScale = 1.0f;
    if (tempoBand == TempoBand::Elevated)
        tempoScale = 0.8f;
    else if (tempoBand == TempoBand::Fast)
        tempoScale = 0.64f;
    const float density = std::clamp(params.densityAmount * style.kickDensityBias * tempoScale, 0.0f, 1.0f);

    const auto baseRole = phraseRoles.empty() ? PhraseRole::Base : phraseRoles.front();
    auto matching = findMatchingKickTemplates(style.substyle, density, baseRole);
    if (matching.empty())
        return;

    std::uniform_int_distribution<int> templatePick(0, static_cast<int>(matching.size() - 1));
    const auto* baseTemplate = matching[static_cast<size_t>(templatePick(rng))];

    for (int bar = 0; bar < bars; ++bar)
    {
        const auto role = bar < static_cast<int>(phraseRoles.size()) ? phraseRoles[static_cast<size_t>(bar)] : PhraseRole::Base;
        const float roleVar = BoomBapPhrasePlanner::roleVariationStrength(role) * style.barVariationAmount;

        for (int i = 0; i < static_cast<int>(baseTemplate->steps.size()); ++i)
        {
            const int stepInBar = baseTemplate->steps[static_cast<size_t>(i)];
            if (stepInBar < 0)
                continue;

            const auto hitRole = baseTemplate->roles[static_cast<size_t>(i)];
            const float profileScale = substyleSupportScale(style.substyle);
            float keepChance = probabilityForRole(hitRole, density, roleVar, style.kickDensityBias);
            if (hitRole != KickHitRole::Anchor)
                keepChance = std::clamp(keepChance * profileScale, 0.05f, 1.0f);
            if (tempoBand != TempoBand::Base && hitRole != KickHitRole::Anchor)
                keepChance = std::clamp(keepChance * (tempoBand == TempoBand::Fast ? 0.58f : 0.72f), 0.03f, 1.0f);

            if (role == PhraseRole::Ending && hitRole == KickHitRole::Pickup)
                keepChance = std::clamp(keepChance + (style.substyle == BoomBapSubstyle::BoomBapGold ? 0.16f : 0.08f), 0.05f, 1.0f);

            if (chance(rng) > keepChance)
                continue;

            int step = bar * 16 + stepInBar;
            if (tempoBand != TempoBand::Base && stepInBar == 8)
                step = bar * 16 + (chance(rng) < 0.65f ? 10 : 9);

            if (role == PhraseRole::Ending && hitRole == KickHitRole::Pickup && chance(rng) < 0.4f)
                step = std::min(step + 1, bar * 16 + 15);

            track.notes.push_back({
                pitch,
                step,
                1,
                velDist(rng),
                microDist(rng),
                false
            });
        }
    }
}
} // namespace bbg
