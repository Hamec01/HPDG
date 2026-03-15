#include "DrillSnareGenerator.h"

#include <algorithm>

#include "../../Core/TrackRegistry.h"
#include "../TempoInterpretation.h"

namespace bbg
{
namespace
{
float tensionGhostChance(DrillSubstyle substyle)
{
    switch (substyle)
    {
        case DrillSubstyle::BrooklynDrill: return 0.48f;
        case DrillSubstyle::NYDrill: return 0.34f;
        case DrillSubstyle::DarkDrill: return 0.2f;
        case DrillSubstyle::UKDrill:
        default: return 0.28f;
    }
}

float endingAccentChance(DrillSubstyle substyle)
{
    switch (substyle)
    {
        case DrillSubstyle::BrooklynDrill: return 0.46f;
        case DrillSubstyle::NYDrill: return 0.34f;
        case DrillSubstyle::DarkDrill: return 0.16f;
        case DrillSubstyle::UKDrill:
        default: return 0.24f;
    }
}
}

void DrillSnareGenerator::generate(TrackState& track,
                                   const GeneratorParams& params,
                                   const DrillStyleProfile& style,
                                   const std::vector<DrillPhraseRole>& phrase,
                                   std::mt19937& rng) const
{
    track.notes.clear();
    const auto* info = TrackRegistry::find(track.type);
    const int pitch = info != nullptr ? info->defaultMidiNote : 38;

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    std::uniform_int_distribution<int> vel(style.snareVelocityMin, style.snareVelocityMax);

    const auto tempoBand = selectTempoBand(params.bpm, params, 120.0f, 140.0f, 100.0f, 130.0f);
    const bool halfTimeAware = tempoBand != TempoBand::Base;
    const int bars = std::max(1, params.bars);
    const auto substyle = style.substyle;
    const float tensionChance = tensionGhostChance(substyle);
    const float endingChance = endingAccentChance(substyle);
    for (int bar = 0; bar < bars; ++bar)
    {
        const auto role = bar < static_cast<int>(phrase.size()) ? phrase[static_cast<size_t>(bar)] : DrillPhraseRole::Base;

        if (halfTimeAware)
        {
            // Fast drill keeps halftime snare weight around beat 3.
            track.notes.push_back({ pitch, bar * 16 + 8, 1, vel(rng), 0, false });

            if (substyle == DrillSubstyle::BrooklynDrill && chance(rng) < 0.3f)
                track.notes.push_back({ pitch, bar * 16 + 11, 1, std::max(style.snareVelocityMin, vel(rng) - 7), 0, false });
            else if (substyle == DrillSubstyle::NYDrill && chance(rng) < 0.2f)
                track.notes.push_back({ pitch, bar * 16 + 12, 1, std::max(style.snareVelocityMin, vel(rng) - 9), 0, false });
            else if (substyle == DrillSubstyle::DarkDrill && role == DrillPhraseRole::Ending && chance(rng) < 0.12f)
                track.notes.push_back({ pitch, bar * 16 + 14, 1, std::max(style.snareVelocityMin, vel(rng) - 13), 0, true });

            if (role == DrillPhraseRole::Tension && chance(rng) < tensionChance)
                track.notes.push_back({ pitch, bar * 16 + 7, 1, std::max(style.snareVelocityMin, vel(rng) - 12), 0, true });

            float edgeChance = endingChance;
            if (tempoBand == TempoBand::Fast)
                edgeChance += 0.08f;
            if (role == DrillPhraseRole::Ending && chance(rng) < std::clamp(edgeChance, 0.0f, 0.92f))
                track.notes.push_back({ pitch, bar * 16 + 15, 1, std::max(style.snareVelocityMin, vel(rng) - 8), 0, false });
        }
        else
        {
            track.notes.push_back({ pitch, bar * 16 + 4, 1, vel(rng), 0, false });
            track.notes.push_back({ pitch, bar * 16 + 12, 1, vel(rng), 0, false });
            if (role == DrillPhraseRole::Tension && chance(rng) < tensionChance)
                track.notes.push_back({ pitch, bar * 16 + 11, 1, std::max(style.snareVelocityMin, vel(rng) - 10), 0, true });
        }
    }
}
} // namespace bbg
