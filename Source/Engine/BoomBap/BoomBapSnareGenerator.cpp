#include "BoomBapSnareGenerator.h"

#include <algorithm>

#include "BoomBapPatternLibrary.h"
#include "../../Core/TrackRegistry.h"
#include "../TempoInterpretation.h"

namespace bbg
{
namespace
{
float substyleGhostScale(BoomBapSubstyle substyle)
{
    switch (substyle)
    {
        case BoomBapSubstyle::BoomBapGold: return 1.18f;
        case BoomBapSubstyle::Jazzy: return 1.12f;
        case BoomBapSubstyle::RussianUnderground: return 0.72f;
        case BoomBapSubstyle::LofiRap: return 0.64f;
        case BoomBapSubstyle::Aggressive: return 0.8f;
        default: return 1.0f;
    }
}
}

void BoomBapSnareGenerator::generate(TrackState& track,
                                     const GeneratorParams& params,
                                     const BoomBapStyleProfile& style,
                                     const std::vector<PhraseRole>& phraseRoles,
                                     std::mt19937& rng) const
{
    track.notes.clear();

    const auto* info = TrackRegistry::find(track.type);
    const int pitch = info != nullptr ? info->defaultMidiNote : 38;

    const auto& feel = chooseSnareFeelProfile(style.substyle, params.densityAmount, rng);

    std::uniform_int_distribution<int> velDist(style.snareVelocityMin, style.snareVelocityMax);
    std::uniform_int_distribution<int> beat2Late(std::max(2, feel.beat2LateTicks / 2), std::max(4, feel.beat2LateTicks));
    std::uniform_int_distribution<int> beat4Late(std::max(2, feel.beat4LateTicks / 2), std::max(4, feel.beat4LateTicks));
    std::uniform_int_distribution<int> ghostVel(style.ghostVelocityMin, style.ghostVelocityMax);
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    const auto tempoBand = selectTempoBand(params.bpm, params, 120.0f, 140.0f, 98.0f, 126.0f);
    const bool halfTimeAware = tempoBand != TempoBand::Base;

    const int bars = std::max(1, params.bars);
    for (int bar = 0; bar < bars; ++bar)
    {
        const auto role = bar < static_cast<int>(phraseRoles.size()) ? phraseRoles[static_cast<size_t>(bar)] : PhraseRole::Base;

        if (halfTimeAware)
        {
            track.notes.push_back({ pitch, bar * 16 + 8, 1, velDist(rng), beat4Late(rng), false });
            if (tempoBand == TempoBand::Fast && role == PhraseRole::Ending && chance(rng) < 0.24f)
                track.notes.push_back({ pitch, bar * 16 + 15, 1, std::max(style.snareVelocityMin, velDist(rng) - 10), std::max(1, beat4Late(rng) / 2), true });
        }
        else
        {
            track.notes.push_back({ pitch, bar * 16 + 4, 1, velDist(rng), beat2Late(rng), false });
            track.notes.push_back({ pitch, bar * 16 + 12, 1, velDist(rng), beat4Late(rng), false });
        }

        float ghostChance = std::clamp((feel.ghostSnareProbability + style.ghostSnareChance)
                                             * (0.4f + params.densityAmount * 0.6f)
                                             * (1.0f + BoomBapPhrasePlanner::roleVariationStrength(role) * 0.5f),
                                             0.0f,
                                             0.5f);
        ghostChance = std::clamp(ghostChance * substyleGhostScale(style.substyle), 0.0f, 0.56f);
        if (halfTimeAware)
            ghostChance *= 0.66f;

        if (role == PhraseRole::Ending && style.substyle == BoomBapSubstyle::BoomBapGold)
            track.notes.push_back({ pitch, bar * 16 + 14, 1, std::max(style.snareVelocityMin, velDist(rng) - 8), std::max(1, beat4Late(rng) / 2), true });

        if (halfTimeAware)
        {
            if (chance(rng) < ghostChance)
                track.notes.push_back({ pitch, bar * 16 + 7, 1, ghostVel(rng), beat4Late(rng) / 3, true });
        }
        else
        {
            if (chance(rng) < ghostChance)
                track.notes.push_back({ pitch, bar * 16 + 3, 1, ghostVel(rng), beat2Late(rng) / 3, true });
            if (chance(rng) < ghostChance)
                track.notes.push_back({ pitch, bar * 16 + 11, 1, ghostVel(rng), beat4Late(rng) / 3, true });
        }
    }
}
} // namespace bbg
