#include "DrillKickGenerator.h"

#include <algorithm>

#include "../../Core/TrackRegistry.h"
#include "../TempoInterpretation.h"

namespace bbg
{
namespace
{
float substyleTempoDensityScale(DrillSubstyle substyle)
{
    switch (substyle)
    {
        case DrillSubstyle::BrooklynDrill: return 0.96f;
        case DrillSubstyle::NYDrill: return 0.9f;
        case DrillSubstyle::DarkDrill: return 0.66f;
        case DrillSubstyle::UKDrill:
        default: return 0.78f;
    }
}
}

void DrillKickGenerator::generate(TrackState& track,
                                  const GeneratorParams& params,
                                  const DrillStyleProfile& style,
                                  const std::vector<DrillPhraseRole>& phrase,
                                  std::mt19937& rng) const
{
    track.notes.clear();
    const auto* info = TrackRegistry::find(track.type);
    const int pitch = info != nullptr ? info->defaultMidiNote : 36;

    const int bars = std::max(1, params.bars);
    const auto tempoBand = selectTempoBand(params.bpm, params, 120.0f, 140.0f, 100.0f, 130.0f);
    const bool halfTimeAware = tempoBand != TempoBand::Base;
    float tempoDensityScale = 1.0f;
    if (halfTimeAware)
    {
        tempoDensityScale = substyleTempoDensityScale(style.substyle);
        if (tempoBand == TempoBand::Fast)
            tempoDensityScale *= 0.94f;
    }
    const float density = std::clamp(params.densityAmount * style.kickDensityBias * tempoDensityScale, 0.15f, 1.0f);

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    std::uniform_int_distribution<int> vel(style.kickVelocityMin, style.kickVelocityMax);

    for (int bar = 0; bar < bars; ++bar)
    {
        const auto role = bar < static_cast<int>(phrase.size()) ? phrase[static_cast<size_t>(bar)] : DrillPhraseRole::Base;
        const float roleBoost = DrillPhrasePlanner::roleVariationStrength(role);

        if (halfTimeAware)
        {
            // Half-time drill at high BPM: wider gaps and off-beat pressure.
            if (chance(rng) < 0.92f)
                track.notes.push_back({ pitch, bar * 16 + 0, 1, vel(rng), 0, false });

            if (style.substyle == DrillSubstyle::DarkDrill)
            {
                float darkGate = 0.22f + density * 0.2f + roleBoost * 0.1f;
                if (tempoBand == TempoBand::Fast)
                    darkGate += 0.04f;
                if (chance(rng) < std::clamp(darkGate, 0.06f, 0.7f))
                    track.notes.push_back({ pitch, bar * 16 + 10, 1, vel(rng), 0, false });
                if (role == DrillPhraseRole::Ending && chance(rng) < std::clamp(0.2f + density * 0.16f, 0.04f, 0.54f))
                    track.notes.push_back({ pitch, bar * 16 + 15, 1, vel(rng), 0, false });
            }
            else if (style.substyle == DrillSubstyle::UKDrill)
            {
                if (chance(rng) < std::clamp(0.28f + density * 0.26f + roleBoost * 0.12f, 0.1f, 0.78f))
                    track.notes.push_back({ pitch, bar * 16 + 6, 1, vel(rng), 0, false });
                if (chance(rng) < std::clamp(0.34f + density * 0.2f + roleBoost * 0.14f, 0.1f, 0.82f))
                    track.notes.push_back({ pitch, bar * 16 + 10, 1, vel(rng), 0, false });
                if (role == DrillPhraseRole::Ending && chance(rng) < std::clamp(0.28f + density * 0.16f, 0.06f, 0.66f))
                    track.notes.push_back({ pitch, bar * 16 + 15, 1, vel(rng), 0, false });
            }
            else if (style.substyle == DrillSubstyle::NYDrill)
            {
                if (chance(rng) < std::clamp(0.32f + density * 0.28f + roleBoost * 0.14f, 0.1f, 0.84f))
                    track.notes.push_back({ pitch, bar * 16 + 6, 1, vel(rng), 0, false });
                if (chance(rng) < std::clamp(0.38f + density * 0.24f + roleBoost * 0.16f, 0.12f, 0.88f))
                    track.notes.push_back({ pitch, bar * 16 + 10, 1, vel(rng), 0, false });
                if (chance(rng) < std::clamp(0.22f + density * 0.2f + roleBoost * 0.12f, 0.06f, 0.72f))
                    track.notes.push_back({ pitch, bar * 16 + 13, 1, vel(rng), 0, false });
                if (role == DrillPhraseRole::Ending && chance(rng) < std::clamp(0.32f + density * 0.16f, 0.08f, 0.7f))
                    track.notes.push_back({ pitch, bar * 16 + 15, 1, vel(rng), 0, false });
            }
            else // BrooklynDrill
            {
                float brook6 = 0.38f + density * 0.32f + roleBoost * 0.14f;
                if (tempoBand == TempoBand::Fast)
                    brook6 += 0.06f;
                if (chance(rng) < std::clamp(brook6, 0.14f, 0.94f))
                    track.notes.push_back({ pitch, bar * 16 + 6, 1, vel(rng), 0, false });
                if (chance(rng) < std::clamp(0.44f + density * 0.26f + roleBoost * 0.16f, 0.14f, 0.92f))
                    track.notes.push_back({ pitch, bar * 16 + 10, 1, vel(rng), 0, false });
                if (chance(rng) < std::clamp(0.34f + density * 0.24f + roleBoost * 0.16f, 0.1f, 0.86f))
                    track.notes.push_back({ pitch, bar * 16 + 13, 1, vel(rng), 0, false });
                if (role == DrillPhraseRole::Ending && chance(rng) < std::clamp(0.4f + density * 0.2f, 0.1f, 0.78f))
                    track.notes.push_back({ pitch, bar * 16 + 15, 1, vel(rng), 0, false });
            }
        }
        else
        {
            if (chance(rng) < 0.88f)
                track.notes.push_back({ pitch, bar * 16 + 0, 1, vel(rng), 0, false });
            if (chance(rng) < std::clamp(0.44f + density * 0.28f + roleBoost * 0.12f, 0.2f, 0.95f))
                track.notes.push_back({ pitch, bar * 16 + 3, 1, vel(rng), 0, false });
            if (chance(rng) < std::clamp(0.36f + density * 0.26f + roleBoost * 0.14f, 0.16f, 0.9f))
                track.notes.push_back({ pitch, bar * 16 + 7, 1, vel(rng), 0, false });
            if (chance(rng) < std::clamp(0.40f + density * 0.3f + roleBoost * 0.18f, 0.18f, 0.94f))
                track.notes.push_back({ pitch, bar * 16 + 10, 1, vel(rng), 0, false });
            if (chance(rng) < std::clamp(0.30f + density * 0.28f + roleBoost * 0.2f, 0.12f, 0.9f))
                track.notes.push_back({ pitch, bar * 16 + 14, 1, vel(rng), 0, false });
        }
    }
}
} // namespace bbg
