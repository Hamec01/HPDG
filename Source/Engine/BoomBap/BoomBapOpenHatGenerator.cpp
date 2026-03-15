#include "BoomBapOpenHatGenerator.h"

#include <algorithm>

#include "BoomBapPatternLibrary.h"
#include "../../Core/TrackRegistry.h"

namespace bbg
{
void BoomBapOpenHatGenerator::generate(TrackState& track,
                                       const TrackState& hatTrack,
                                       const GeneratorParams& params,
                                       const BoomBapStyleProfile& style,
                                       const std::vector<PhraseRole>& phraseRoles,
                                       std::mt19937& rng) const
{
    track.notes.clear();

    const auto* info = TrackRegistry::find(track.type);
    const int pitch = info != nullptr ? info->defaultMidiNote : 46;

    const auto& preset = chooseOpenHatPreset(style.substyle, rng);

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    std::uniform_int_distribution<int> vel(style.openHatVelocityMin, style.openHatVelocityMax);

    const float density = std::clamp(params.densityAmount * style.openHatDensityBias, 0.0f, 1.0f);

    for (const auto& hat : hatTrack.notes)
    {
        const int bar = hat.step / 16;
        const auto role = bar < static_cast<int>(phraseRoles.size()) ? phraseRoles[static_cast<size_t>(bar)] : PhraseRole::Base;

        const bool endOfHalfBar = (hat.step % 8 == 7);
        const bool afterKickPulse = (hat.step % 4 == 1);

        float gate = preset.eventProbability * style.openHatChance * (0.25f + density * 0.75f);
        if (preset.barEndBias && endOfHalfBar)
            gate *= 1.45f;
        if (preset.afterKickBias && afterKickPulse)
            gate *= 1.35f;
        if (role == PhraseRole::Ending)
            gate *= (style.substyle == BoomBapSubstyle::BoomBapGold ? 1.42f : 1.25f);
        if (style.substyle == BoomBapSubstyle::RussianUnderground)
            gate *= 0.78f;

        if (chance(rng) > std::clamp(gate, 0.01f, 0.75f))
            continue;

        track.notes.push_back({ pitch, hat.step, (role == PhraseRole::Ending) ? 2 : 1, vel(rng), hat.microOffset, false });
    }
}
} // namespace bbg
