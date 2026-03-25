#include "RapKickGenerator.h"

#include <algorithm>

#include "../../Core/PatternProject.h"
#include "../../Core/TrackRegistry.h"
#include "RapStyleSpec.h"
#include "../TempoInterpretation.h"

namespace bbg
{
namespace
{
struct KickZone
{
    int step = 0;
    float gate = 0.0f;
    bool anchor = false;
};

std::vector<KickZone> kickZonesForStyle(RapSubstyle substyle)
{
    switch (substyle)
    {
        case RapSubstyle::WestCoast:
        case RapSubstyle::RnBRap:
            return { { 0, 1.0f, true }, { 5, 0.52f, false }, { 8, 0.86f, true }, { 10, 0.52f, false }, { 13, 0.48f, false }, { 15, 0.42f, false } };
        case RapSubstyle::DirtySouthClassic:
            return { { 0, 1.0f, true }, { 4, 0.58f, false }, { 8, 0.90f, true }, { 10, 0.62f, false }, { 12, 0.52f, false }, { 14, 0.56f, false } };
        case RapSubstyle::GermanStreetRap:
            return { { 0, 1.0f, true }, { 4, 0.56f, false }, { 8, 0.90f, true }, { 10, 0.54f, false }, { 12, 0.48f, false }, { 14, 0.52f, false } };
        case RapSubstyle::RussianRap:
            return { { 0, 1.0f, true }, { 6, 0.52f, false }, { 8, 0.88f, true }, { 10, 0.54f, false }, { 14, 0.48f, false } };
        case RapSubstyle::HardcoreRap:
            return {
                { 0, 1.0f, true },
                { 4, 0.68f, false },
                { 8, 0.94f, true },
                { 10, 0.66f, false },
                { 12, 0.62f, false },
                { 14, 0.58f, false },
                { 2, 0.44f, false },
                { 6, 0.40f, false },
                { 15, 0.36f, false }
            };
        case RapSubstyle::EastCoast:
        case RapSubstyle::LofiRap:
        default:
            return { { 0, 1.0f, true }, { 6, 0.56f, false }, { 8, 0.90f, true }, { 10, 0.58f, false }, { 14, 0.50f, false }, { 3, 0.32f, false }, { 7, 0.28f, false }, { 15, 0.34f, false } };
    }
}

float supportAccentWeight(const StyleInfluenceState& styleInfluence)
{
    return std::clamp(styleInfluence.supportAccentWeight, 0.65f, 1.45f);
}
}

void RapKickGenerator::generate(TrackState& track,
                                const GeneratorParams& params,
                                const RapStyleProfile& style,
                                const StyleInfluenceState& styleInfluence,
                                const std::vector<RapPhraseRole>& phraseRoles,
                                std::mt19937& rng) const
{
    track.notes.clear();

    const auto* info = TrackRegistry::find(track.type);
    const int pitch = info != nullptr ? info->defaultMidiNote : 36;

    const int bars = std::max(1, params.bars);
    const auto spec = getRapStyleSpec(style.substyle);
    const auto tempoBand = selectTempoBand(params.bpm, params, 120.0f, 140.0f, 100.0f, 128.0f);
    float tempoScale = 1.0f;
    if (tempoBand == TempoBand::Elevated)
        tempoScale = 0.8f;
    else if (tempoBand == TempoBand::Fast)
        tempoScale = 0.68f;
    const float density = std::clamp(rapMix(spec.kickDensityMin, spec.kickDensityMax, params.densityAmount) * tempoScale,
                                     0.10f,
                                     1.0f);
    const float supportAccent = supportAccentWeight(styleInfluence);

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    std::uniform_int_distribution<int> vel(style.kickVelocityMin, style.kickVelocityMax);
    std::uniform_int_distribution<int> mainVel(74, 104);
    std::uniform_int_distribution<int> supportVel(45, 82);
    std::uniform_int_distribution<int> ghostVel(28, 58);

    const auto zones = kickZonesForStyle(style.substyle);

    for (int bar = 0; bar < bars; ++bar)
    {
        const auto role = bar < static_cast<int>(phraseRoles.size()) ? phraseRoles[static_cast<size_t>(bar)] : RapPhraseRole::Base;
        const float roleBoost = RapPhrasePlanner::roleVariationStrength(role);

        for (const auto& zone : zones)
        {
            float gate = zone.gate;
            if (!zone.anchor)
                gate = std::clamp(gate * (0.52f + density * 0.72f + roleBoost * 0.16f) * supportAccent, 0.06f, 0.94f);

            if (tempoBand != TempoBand::Base && !zone.anchor)
                gate = std::clamp(gate * (tempoBand == TempoBand::Fast ? 0.70f : 0.84f), 0.05f, 0.9f);

            if (chance(rng) > gate)
                continue;

            int v = zone.anchor ? std::clamp(mainVel(rng), style.kickVelocityMin, style.kickVelocityMax)
                                : std::clamp(supportVel(rng), style.kickVelocityMin - 10, style.kickVelocityMax);
            track.notes.push_back({ pitch, bar * 16 + zone.step, 1, v, 0, false });
        }

        if (chance(rng) < std::clamp(rapMix(spec.ghostKickDensityMin, spec.ghostKickDensityMax, params.densityAmount) * supportAccent, 0.02f, 0.30f))
            track.notes.push_back({ pitch, bar * 16 + (style.substyle == RapSubstyle::DirtySouthClassic ? 12 : 7), 1, ghostVel(rng), 0, true });

        if (role == RapPhraseRole::Ending && chance(rng) < (style.substyle == RapSubstyle::GermanStreetRap ? 0.18f : 0.32f))
            track.notes.push_back({ pitch, bar * 16 + 15, 1, std::max(style.kickVelocityMin - 8, supportVel(rng)), 0, true });
    }
}
} // namespace bbg
