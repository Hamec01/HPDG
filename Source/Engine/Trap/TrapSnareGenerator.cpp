#include "TrapSnareGenerator.h"

#include <algorithm>

#include "../../Core/TrackRegistry.h"
#include "../TempoInterpretation.h"

namespace bbg
{
namespace
{
float endingChanceForSubstyle(TrapSubstyle substyle)
{
    switch (substyle)
    {
        case TrapSubstyle::ATLClassic: return 0.34f;
        case TrapSubstyle::DarkTrap: return 0.18f;
        case TrapSubstyle::CloudTrap: return 0.24f;
        case TrapSubstyle::RageTrap: return 0.46f;
        case TrapSubstyle::MemphisTrap: return 0.36f;
        case TrapSubstyle::LuxuryTrap: return 0.26f;
        default: return 0.3f;
    }
}
}

void TrapSnareGenerator::generate(TrackState& track,
                                  const GeneratorParams& params,
                                  const TrapStyleProfile& style,
                                  const std::vector<TrapPhraseRole>& phrase,
                                  std::mt19937& rng) const
{
    track.notes.clear();

    const auto* info = TrackRegistry::find(track.type);
    const int pitch = info != nullptr ? info->defaultMidiNote : 38;

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    std::uniform_int_distribution<int> vel(style.snareVelocityMin, style.snareVelocityMax);
    const auto tempoBand = selectTempoBand(params.bpm, params, 120.0f, 145.0f, 100.0f, 132.0f);
    const bool highTempoHalfTime = tempoBand != TempoBand::Base;
    const float endingChance = endingChanceForSubstyle(style.substyle);

    const int bars = std::max(1, params.bars);
    for (int bar = 0; bar < bars; ++bar)
    {
        const auto role = bar < static_cast<int>(phrase.size()) ? phrase[static_cast<size_t>(bar)] : TrapPhraseRole::Base;

        if (highTempoHalfTime)
        {
            track.notes.push_back({ pitch, bar * 16 + 8, 1, vel(rng), 0, false });
            if (style.substyle == TrapSubstyle::RageTrap && chance(rng) < 0.3f)
                track.notes.push_back({ pitch, bar * 16 + 11, 1, std::max(style.snareVelocityMin, vel(rng) - 8), 0, false });
            if (style.substyle == TrapSubstyle::MemphisTrap && chance(rng) < 0.24f)
                track.notes.push_back({ pitch, bar * 16 + 14, 1, std::max(style.snareVelocityMin, vel(rng) - 12), 0, true });
        }
        else
        {
            track.notes.push_back({ pitch, bar * 16 + 4, 1, vel(rng), 0, false });
            track.notes.push_back({ pitch, bar * 16 + 12, 1, vel(rng), 0, false });
        }

        float edgeChance = endingChance;
        if (tempoBand == TempoBand::Fast)
            edgeChance += 0.06f;
        if (role == TrapPhraseRole::Ending && chance(rng) < std::clamp(edgeChance, 0.0f, 0.92f))
            track.notes.push_back({ pitch, bar * 16 + 15, 1, std::max(style.snareVelocityMin, vel(rng) - 8), 0, false });
    }
}
} // namespace bbg
