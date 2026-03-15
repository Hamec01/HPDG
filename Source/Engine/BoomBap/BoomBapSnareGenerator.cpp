#include "BoomBapSnareGenerator.h"

#include <algorithm>
#include <cmath>

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

int sampleLateTicks(int center, int spread, std::mt19937& rng)
{
    const int minValue = std::max(1, center - std::max(1, spread));
    const int maxValue = std::max(minValue + 1, center + std::max(1, spread));
    std::uniform_int_distribution<int> dist(minValue, maxValue);
    return dist(rng);
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

    const int snareVelMax = feel.dryBackbeat ? std::max(style.snareVelocityMin + 4, style.snareVelocityMax - 8) : style.snareVelocityMax;
    std::uniform_int_distribution<int> velDist(style.snareVelocityMin, snareVelMax);
    std::uniform_int_distribution<int> ghostVel(style.ghostVelocityMin, style.ghostVelocityMax);
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    const auto tempoBand = selectTempoBand(params.bpm, params, 120.0f, 140.0f, 98.0f, 126.0f);
    const bool halfTimeAware = tempoBand != TempoBand::Base;

    const int bars = std::max(1, params.bars);
    for (int bar = 0; bar < bars; ++bar)
    {
        const auto role = bar < static_cast<int>(phraseRoles.size()) ? phraseRoles[static_cast<size_t>(bar)] : PhraseRole::Base;
        int beat2Late = sampleLateTicks(feel.beat2LateTicks, feel.beat2LateSpread, rng);
        int beat4Late = sampleLateTicks(feel.beat4LateTicks, feel.beat4LateSpread, rng);

        if (feel.avoidStraightBackbeat && std::abs(beat2Late - beat4Late) < 2)
            beat4Late += beat4Late >= beat2Late ? 2 : -2;

        if (feel.dryBackbeat)
        {
            beat2Late = std::max(1, static_cast<int>(beat2Late * 0.82f));
            beat4Late = std::max(1, static_cast<int>(beat4Late * 0.82f));
        }

        if (halfTimeAware)
        {
            track.notes.push_back({ pitch, bar * 16 + 8, 1, velDist(rng), beat4Late, false });
            if (tempoBand == TempoBand::Fast && role == PhraseRole::Ending && chance(rng) < 0.24f)
                track.notes.push_back({ pitch, bar * 16 + 15, 1, std::max(style.snareVelocityMin, velDist(rng) - 10), std::max(1, beat4Late / 2), true });

            if (style.substyle == BoomBapSubstyle::Jazzy && chance(rng) < std::clamp(feel.dragProbability + 0.06f, 0.02f, 0.4f))
                track.notes.push_back({ pitch, bar * 16 + 6, 1, std::max(style.ghostVelocityMin, ghostVel(rng) - 4), std::max(1, beat4Late / 3), true });
        }
        else
        {
            track.notes.push_back({ pitch, bar * 16 + 4, 1, velDist(rng), beat2Late, false });
            track.notes.push_back({ pitch, bar * 16 + 12, 1, velDist(rng), beat4Late, false });

            if (chance(rng) < std::clamp(feel.dragProbability + BoomBapPhrasePlanner::roleVariationStrength(role) * 0.12f, 0.0f, 0.42f))
                track.notes.push_back({ pitch, bar * 16 + 10, 1, std::max(style.ghostVelocityMin, ghostVel(rng) - 2), std::max(1, beat4Late / 3), true });
        }

        float ghostChance = std::clamp((feel.ghostSnareProbability + style.ghostSnareChance)
                                             * (0.4f + params.densityAmount * 0.6f)
                                             * (1.0f + BoomBapPhrasePlanner::roleVariationStrength(role) * 0.5f),
                                             0.0f,
                                             0.5f);
        ghostChance = std::clamp(ghostChance * substyleGhostScale(style.substyle), 0.0f, 0.56f);
        if (halfTimeAware)
            ghostChance *= 0.66f;

        float ghostBefore2Chance = std::clamp(feel.ghostBefore2Probability * (0.55f + params.densityAmount * 0.45f), 0.0f, 0.5f);
        float ghostBefore4Chance = std::clamp(feel.ghostBefore4Probability * (0.55f + params.densityAmount * 0.45f), 0.0f, 0.5f);

        if (style.substyle == BoomBapSubstyle::LofiRap)
        {
            ghostChance = std::min(ghostChance, 0.22f);
            ghostBefore2Chance *= 0.78f;
            ghostBefore4Chance *= 0.78f;
        }
        else if (style.substyle == BoomBapSubstyle::RussianUnderground)
        {
            ghostChance = std::min(ghostChance, 0.18f);
            ghostBefore2Chance *= 0.60f;
            ghostBefore4Chance *= 0.60f;
        }
        else if (style.substyle == BoomBapSubstyle::Jazzy)
        {
            ghostBefore2Chance = std::max(ghostBefore2Chance, 0.16f);
            ghostBefore4Chance = std::max(ghostBefore4Chance, 0.18f);
        }

        if (role == PhraseRole::Ending && style.substyle == BoomBapSubstyle::BoomBapGold)
            track.notes.push_back({ pitch, bar * 16 + 14, 1, std::max(style.snareVelocityMin, velDist(rng) - 8), std::max(1, beat4Late / 2), true });

        if (role == PhraseRole::Ending && chance(rng) < std::clamp(feel.fillHitProbability, 0.0f, 0.45f))
            track.notes.push_back({ pitch, bar * 16 + 15, 1, std::max(style.snareVelocityMin, velDist(rng) - 6), std::max(1, beat4Late / 2), true });

        if (halfTimeAware)
        {
            if (chance(rng) < ghostChance)
                track.notes.push_back({ pitch, bar * 16 + 7, 1, ghostVel(rng), std::max(1, beat4Late / 3), true });
        }
        else
        {
            if (chance(rng) < std::max(ghostChance, ghostBefore2Chance))
                track.notes.push_back({ pitch, bar * 16 + 3, 1, ghostVel(rng), std::max(1, beat2Late / 3), true });
            if (chance(rng) < std::max(ghostChance, ghostBefore4Chance))
                track.notes.push_back({ pitch, bar * 16 + 11, 1, ghostVel(rng), std::max(1, beat4Late / 3), true });
        }
    }
}
} // namespace bbg
