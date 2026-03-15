#include "BoomBapPercGenerator.h"

#include <algorithm>

#include "BoomBapPatternLibrary.h"
#include "../../Core/TrackRegistry.h"

namespace bbg
{
void BoomBapPercGenerator::generate(TrackState& track,
                                    const GeneratorParams& params,
                                    const BoomBapStyleProfile& style,
                                    const std::vector<PhraseRole>& phraseRoles,
                                    std::mt19937& rng) const
{
    track.notes.clear();

    const auto* info = TrackRegistry::find(track.type);
    const int pitch = info != nullptr ? info->defaultMidiNote : 50;

    const auto& preset = choosePercPreset(style.substyle, rng);

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    std::uniform_int_distribution<int> velocity(style.percVelocityMin, style.percVelocityMax);
    std::uniform_int_distribution<int> micro(-style.ghostTimingMaxTicks / 2, style.ghostTimingMaxTicks);

    const int bars = std::max(1, params.bars);
    const float density = std::clamp(params.densityAmount * style.percDensityBias, 0.0f, 1.0f);
    std::vector<int> barEvents(static_cast<size_t>(bars), 0);

    for (int bar = 0; bar < bars; ++bar)
    {
        const auto role = bar < static_cast<int>(phraseRoles.size()) ? phraseRoles[static_cast<size_t>(bar)] : PhraseRole::Base;

        for (int step = 0; step < 16; ++step)
        {
            const bool candidate = (step % 4 == 2) || (step % 8 == 5) || (role == PhraseRole::Ending && step >= 12);
            if (!candidate)
                continue;

            if (barEvents[static_cast<size_t>(bar)] >= preset.maxEventsPerBar)
                continue;

            float gate = preset.eventProbability * style.percChance * (0.15f + density * 0.85f);
            if (preset.bar4Bias && bar == bars - 1)
                gate *= 1.5f;
            if (role == PhraseRole::Contrast)
                gate *= 1.15f;
            if (role == PhraseRole::Ending && style.substyle == BoomBapSubstyle::BoomBapGold)
                gate *= 1.2f;
            if (style.substyle == BoomBapSubstyle::RussianUnderground)
                gate *= 0.7f;

            if (chance(rng) > std::clamp(gate, 0.01f, 0.65f))
                continue;

            track.notes.push_back({ pitch, bar * 16 + step, 1, velocity(rng), micro(rng), false });
            ++barEvents[static_cast<size_t>(bar)];
        }
    }
}
} // namespace bbg
