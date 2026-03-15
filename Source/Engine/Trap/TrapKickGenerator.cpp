#include "TrapKickGenerator.h"

#include <algorithm>

#include "../../Core/TrackRegistry.h"
#include "../TempoInterpretation.h"

namespace bbg
{
namespace
{
float densityScaleForSubstyle(TrapSubstyle substyle)
{
    switch (substyle)
    {
        case TrapSubstyle::ATLClassic: return 1.0f;
        case TrapSubstyle::DarkTrap: return 0.78f;
        case TrapSubstyle::CloudTrap: return 0.86f;
        case TrapSubstyle::RageTrap: return 1.18f;
        case TrapSubstyle::MemphisTrap: return 0.94f;
        case TrapSubstyle::LuxuryTrap: return 0.9f;
        default: return 1.0f;
    }
}
}

void TrapKickGenerator::generate(TrackState& track,
                                 const GeneratorParams& params,
                                 const TrapStyleProfile& style,
                                 const std::vector<TrapPhraseRole>& phrase,
                                 std::mt19937& rng) const
{
    track.notes.clear();

    const auto* info = TrackRegistry::find(track.type);
    const int pitch = info != nullptr ? info->defaultMidiNote : 36;
    const int bars = std::max(1, params.bars);
    const auto tempoBand = selectTempoBand(params.bpm, params, 120.0f, 145.0f, 100.0f, 132.0f);
    float tempoScale = 1.0f;
    if (tempoBand == TempoBand::Elevated)
        tempoScale = 0.86f;
    else if (tempoBand == TempoBand::Fast)
        tempoScale = 0.74f;
    const float density = std::clamp(params.densityAmount * style.kickDensityBias * densityScaleForSubstyle(style.substyle) * tempoScale, 0.12f, 1.0f);

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    std::uniform_int_distribution<int> vel(style.kickVelocityMin, style.kickVelocityMax);
    const bool halfTimeAware = tempoBand != TempoBand::Base;

    for (int bar = 0; bar < bars; ++bar)
    {
        const auto role = bar < static_cast<int>(phrase.size()) ? phrase[static_cast<size_t>(bar)] : TrapPhraseRole::Base;
        const float roleBoost = TrapPhrasePlanner::roleVariationStrength(role);

        track.notes.push_back({ pitch, bar * 16 + 0, 1, vel(rng), 0, false });

        if (style.substyle == TrapSubstyle::DarkTrap)
        {
            if (chance(rng) < 0.82f)
                track.notes.push_back({ pitch, bar * 16 + (halfTimeAware ? 10 : 8), 1, vel(rng), 0, false });
            if (chance(rng) < std::clamp(0.22f + density * 0.24f + roleBoost * 0.1f, 0.08f, 0.72f))
                track.notes.push_back({ pitch, bar * 16 + (halfTimeAware ? 11 : 10), 1, vel(rng), 0, false });
            if (role == TrapPhraseRole::Ending && chance(rng) < 0.26f)
                track.notes.push_back({ pitch, bar * 16 + 15, 1, std::max(style.kickVelocityMin, vel(rng) - 7), 0, false });
            continue;
        }
        if (chance(rng) < (style.substyle == TrapSubstyle::RageTrap ? (halfTimeAware ? 0.9f : 0.97f) : (halfTimeAware ? 0.84f : 0.92f)))
            track.notes.push_back({ pitch, bar * 16 + (halfTimeAware ? 10 : 8), 1, vel(rng), 0, false });

        if (chance(rng) < std::clamp((halfTimeAware ? 0.24f : 0.30f) + density * 0.34f + roleBoost * 0.08f, 0.10f, 0.94f))
            track.notes.push_back({ pitch, bar * 16 + 6, 1, vel(rng), 0, false });
        if (chance(rng) < std::clamp((halfTimeAware ? 0.20f : 0.26f) + density * 0.30f + roleBoost * 0.14f, 0.10f, 0.92f))
            track.notes.push_back({ pitch, bar * 16 + (halfTimeAware ? 11 : 10), 1, vel(rng), 0, false });

        float lateGate = 0.20f + density * 0.28f + roleBoost * 0.2f;
        if (style.substyle == TrapSubstyle::RageTrap)
            lateGate += 0.14f;
        else if (style.substyle == TrapSubstyle::CloudTrap || style.substyle == TrapSubstyle::LuxuryTrap)
            lateGate -= 0.06f;
        if (chance(rng) < std::clamp(lateGate, 0.06f, 0.9f))
            track.notes.push_back({ pitch, bar * 16 + 14, 1, vel(rng), 0, false });

        if (style.substyle == TrapSubstyle::RageTrap && chance(rng) < std::clamp(0.28f + density * 0.2f, 0.1f, 0.72f))
            track.notes.push_back({ pitch, bar * 16 + 12, 1, vel(rng), 0, false });

        if (role == TrapPhraseRole::Ending && chance(rng) < 0.38f)
            track.notes.push_back({ pitch, bar * 16 + 15, 1, std::max(style.kickVelocityMin, vel(rng) - 6), 0, false });
    }
}
} // namespace bbg
