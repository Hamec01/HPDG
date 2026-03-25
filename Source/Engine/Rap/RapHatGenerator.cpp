#include "RapHatGenerator.h"

#include <algorithm>

#include "../../Core/PatternProject.h"
#include "../../Core/TrackRegistry.h"
#include "RapStyleSpec.h"
#include "../TempoInterpretation.h"

namespace bbg
{
namespace
{
float oddStepGateForStyle(RapSubstyle substyle)
{
    switch (substyle)
    {
        case RapSubstyle::GermanStreetRap: return 0.14f;
        case RapSubstyle::RussianRap: return 0.18f;
        case RapSubstyle::EastCoast: return 0.22f;
        case RapSubstyle::HardcoreRap: return 0.16f;
        case RapSubstyle::WestCoast: return 0.24f;
        case RapSubstyle::DirtySouthClassic: return 0.26f;
        case RapSubstyle::RnBRap: return 0.28f;
        default: return 0.22f;
    }
}

float supportAccentWeight(const StyleInfluenceState& styleInfluence)
{
    return std::clamp(styleInfluence.supportAccentWeight, 0.65f, 1.45f);
}

float supportLaneActivityWeight(const StyleInfluenceState& styleInfluence)
{
    return std::clamp(laneBiasFor(styleInfluence, TrackType::OpenHat).activityWeight, 0.6f, 1.4f);
}
}

void RapHatGenerator::generate(TrackState& track,
                               const GeneratorParams& params,
                               const RapStyleProfile& style,
                               const StyleInfluenceState& styleInfluence,
                               const std::vector<RapPhraseRole>& phraseRoles,
                               std::mt19937& rng) const
{
    track.notes.clear();

    const auto* info = TrackRegistry::find(track.type);
    const int pitch = info != nullptr ? info->defaultMidiNote : 42;

    const int bars = std::max(1, params.bars);
    const auto spec = getRapStyleSpec(style.substyle);
    const auto tempoBand = selectTempoBand(params.bpm, params, 120.0f, 140.0f, 100.0f, 128.0f);
    float tempoScale = 1.0f;
    if (tempoBand == TempoBand::Elevated)
        tempoScale = 0.78f;
    else if (tempoBand == TempoBand::Fast)
        tempoScale = 0.62f;
    const float density = std::clamp(rapMix(spec.hatDensityMin, spec.hatDensityMax, params.densityAmount) * tempoScale, 0.08f, 1.0f);
    const float oddGateBase = oddStepGateForStyle(style.substyle);
    const float supportAccent = supportAccentWeight(styleInfluence);
    const float supportLaneActivity = supportLaneActivityWeight(styleInfluence);

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    std::uniform_int_distribution<int> vel(style.hatVelocityMin, style.hatVelocityMax);
    std::uniform_int_distribution<int> ghostVel(30, 54);

    for (int bar = 0; bar < bars; ++bar)
    {
        const auto role = bar < static_cast<int>(phraseRoles.size()) ? phraseRoles[static_cast<size_t>(bar)] : RapPhraseRole::Base;
        const float roleBoost = RapPhrasePlanner::roleVariationStrength(role);

        for (int step = 0; step < 16; ++step)
        {
            bool place = false;
            if ((step % 2) == 0)
                place = chance(rng) < std::clamp(((tempoBand == TempoBand::Base ? 0.78f : 0.68f) + density * 0.22f) * (0.92f + supportLaneActivity * 0.08f), 0.50f, 0.995f);
            else
                place = chance(rng) < std::clamp((oddGateBase * density + roleBoost * 0.10f) * supportAccent * supportLaneActivity, 0.02f, 0.68f);

            if (!place)
                continue;

            int v = vel(rng);
            if ((step % 4) == 0)
                v = std::min(style.hatVelocityMax, v + 6);
            if (role == RapPhraseRole::Ending && step >= 12)
                v = std::min(style.hatVelocityMax, v + 4);

            const bool ghost = (step % 2) == 1 && chance(rng) < 0.24f;
            if (ghost)
                v = std::min(v, ghostVel(rng));

            track.notes.push_back({ pitch, bar * 16 + step, 1, v, 0, ghost });
        }
    }
}
} // namespace bbg
