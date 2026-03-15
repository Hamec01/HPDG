#include "BoomBapHatGenerator.h"

#include <algorithm>

#include "BoomBapPatternLibrary.h"
#include "../../Core/TrackRegistry.h"
#include "../TempoInterpretation.h"

namespace bbg
{
namespace
{
float carrierGateScale(BoomBapSubstyle substyle)
{
    switch (substyle)
    {
        case BoomBapSubstyle::BoomBapGold: return 1.10f;
        case BoomBapSubstyle::Jazzy: return 1.08f;
        case BoomBapSubstyle::RussianUnderground: return 0.8f;
        case BoomBapSubstyle::LofiRap: return 0.82f;
        case BoomBapSubstyle::Aggressive: return 0.9f;
        default: return 1.0f;
    }
}
}

void BoomBapHatGenerator::generate(TrackState& track,
                                   const GeneratorParams& params,
                                   const BoomBapStyleProfile& style,
                                   const std::vector<PhraseRole>& phraseRoles,
                                   std::mt19937& rng) const
{
    track.notes.clear();

    const auto* info = TrackRegistry::find(track.type);
    const int pitch = info != nullptr ? info->defaultMidiNote : 42;

    const auto& pattern = chooseHatPatternProfile(style.substyle, params.densityAmount, rng);

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    std::uniform_int_distribution<int> microDist(-style.hatTimingMaxTicks / 2, style.hatTimingMaxTicks / 2);
    const auto tempoBand = selectTempoBand(params.bpm, params, 120.0f, 140.0f, 98.0f, 126.0f);

    const int bars = std::max(1, params.bars);
    float tempoScale = 1.0f;
    if (tempoBand == TempoBand::Elevated)
        tempoScale = 0.78f;
    else if (tempoBand == TempoBand::Fast)
        tempoScale = 0.62f;
    const float baseDensity = std::clamp(params.densityAmount * style.hatDensityBias * tempoScale, 0.0f, 1.0f);

    for (int bar = 0; bar < bars; ++bar)
    {
        const auto role = bar < static_cast<int>(phraseRoles.size()) ? phraseRoles[static_cast<size_t>(bar)] : PhraseRole::Base;
        const float roleVar = BoomBapPhrasePlanner::roleVariationStrength(role) * style.barVariationAmount;

        for (int step = 0; step < 16; ++step)
        {
            int active = pattern.activeSteps[static_cast<size_t>(step)];
            if (active == 0)
                continue;

            int finalStep = step;
            if (role == PhraseRole::Ending)
            {
                const int substitution = pattern.substitutionSteps[static_cast<size_t>(step)];
                if (substitution >= 0 && chance(rng) < 0.55f)
                    finalStep = substitution;
            }

            float gate = std::clamp(0.30f + baseDensity * 0.55f + roleVar * 0.2f, 0.18f, 1.0f);
            gate = std::clamp(gate * carrierGateScale(style.substyle), 0.12f, 1.0f);

            if (style.substyle == BoomBapSubstyle::LofiRap)
            {
                if ((step % 2) == 0)
                    gate = std::clamp(0.72f + baseDensity * 0.20f, 0.62f, 0.94f);
                else
                    gate = std::clamp(0.04f + baseDensity * 0.22f + roleVar * 0.08f, 0.02f, 0.30f);
            }

            if (tempoBand != TempoBand::Base && (step % 2) == 1)
                gate = std::clamp(gate * (tempoBand == TempoBand::Fast ? 0.52f : 0.68f), 0.08f, 1.0f);
            if (role == PhraseRole::Ending && style.substyle == BoomBapSubstyle::BoomBapGold && step >= 12)
                gate = std::clamp(gate + 0.12f, 0.12f, 1.0f);
            if (chance(rng) > gate)
                continue;

            const int accent = pattern.accentMap[static_cast<size_t>(step)];
            const int velocity = std::clamp(accent, style.hatVelocityMin, style.hatVelocityMax);

            track.notes.push_back({
                pitch,
                bar * 16 + finalStep,
                1,
                velocity,
                microDist(rng),
                false
            });
        }
    }
}
} // namespace bbg
