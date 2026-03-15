#include "RapSnareGenerator.h"

#include <algorithm>

#include "../../Core/TrackRegistry.h"
#include "RapStyleSpec.h"
#include "../TempoInterpretation.h"

namespace bbg
{
namespace
{
std::pair<int, int> snareLateRange(RapSubstyle substyle)
{
    switch (substyle)
    {
        case RapSubstyle::GermanStreetRap: return { -2, 6 };
        case RapSubstyle::HardcoreRap: return { -2, 6 };
        case RapSubstyle::DirtySouthClassic: return { 0, 6 };
        case RapSubstyle::EastCoast: return { 0, 8 };
        case RapSubstyle::WestCoast: return { 2, 10 };
        case RapSubstyle::RussianRap: return { 0, 10 };
        case RapSubstyle::RnBRap: return { 4, 14 };
        default: return { 0, 8 };
    }
}
}

void RapSnareGenerator::generate(TrackState& track,
                                 const GeneratorParams& params,
                                 const RapStyleProfile& style,
                                 const std::vector<RapPhraseRole>& phraseRoles,
                                 std::mt19937& rng) const
{
    track.notes.clear();

    const auto* info = TrackRegistry::find(track.type);
    const int pitch = info != nullptr ? info->defaultMidiNote : 38;

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    std::uniform_int_distribution<int> vel(style.snareVelocityMin, style.snareVelocityMax);
    std::uniform_int_distribution<int> ghostVel(style.ghostVelocityMin, style.ghostVelocityMax);
    const auto late = snareLateRange(style.substyle);
    std::uniform_int_distribution<int> lateDist(late.first, late.second);
    const auto spec = getRapStyleSpec(style.substyle);
    const auto tempoBand = selectTempoBand(params.bpm, params, 120.0f, 140.0f, 100.0f, 128.0f);
    const bool halfTimeAware = tempoBand != TempoBand::Base;

    const int bars = std::max(1, params.bars);
    for (int bar = 0; bar < bars; ++bar)
    {
        const auto role = bar < static_cast<int>(phraseRoles.size()) ? phraseRoles[static_cast<size_t>(bar)] : RapPhraseRole::Base;

        if (halfTimeAware)
        {
            track.notes.push_back({ pitch, bar * 16 + 8, 1, vel(rng), lateDist(rng), false });
            if (tempoBand == TempoBand::Fast && role == RapPhraseRole::Ending && chance(rng) < 0.3f)
                track.notes.push_back({ pitch, bar * 16 + 15, 1, std::max(style.snareVelocityMin, vel(rng) - 8), lateDist(rng) / 2, false });
        }
        else
        {
            track.notes.push_back({ pitch, bar * 16 + 4, 1, vel(rng), lateDist(rng), false });
            track.notes.push_back({ pitch, bar * 16 + 12, 1, vel(rng), lateDist(rng), false });
        }

        float ghostChance = style.ghostSnareChance * rapMix(spec.clapGhostDensityMin, spec.clapGhostDensityMax, params.densityAmount);
        if (role == RapPhraseRole::Variation)
            ghostChance += 0.04f;
        if (role == RapPhraseRole::Ending)
            ghostChance += 0.06f;
        if (halfTimeAware)
            ghostChance *= 0.72f;
        if (style.substyle == RapSubstyle::GermanStreetRap)
            ghostChance *= 0.62f;

        if (halfTimeAware)
        {
            if (chance(rng) < std::clamp(ghostChance, 0.01f, 0.24f))
                track.notes.push_back({ pitch, bar * 16 + 7, 1, ghostVel(rng), std::max(1, lateDist(rng) / 2), true });
        }
        else
        {
            if (chance(rng) < std::clamp(ghostChance, 0.02f, 0.32f))
                track.notes.push_back({ pitch, bar * 16 + 3, 1, ghostVel(rng), std::max(1, lateDist(rng) / 2), true });
            if (chance(rng) < std::clamp(ghostChance - 0.01f, 0.01f, 0.28f))
                track.notes.push_back({ pitch, bar * 16 + 11, 1, ghostVel(rng), std::max(1, lateDist(rng) / 2), true });
        }
    }
}
} // namespace bbg
