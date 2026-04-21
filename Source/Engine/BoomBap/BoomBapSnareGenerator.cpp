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
        case BoomBapSubstyle::Classic: return 0.34f;
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

int classicGhostPriority(const NoteEvent& note, int bars)
{
    const int step = ((note.step % 16) + 16) % 16;
    const int bar = note.step / 16;
    int score = note.velocity;

    if (bar == bars - 1)
        score += 120;
    if (step == 11)
        score += 80;
    else if (step == 3)
        score += 70;
    else if (step == 15)
        score += 55;
    else if (step == 10 || step == 14)
        score += 30;

    return score;
}

void pruneClassicGhostSnares(TrackState& track, int bars)
{
    std::vector<NoteEvent> anchors;
    std::vector<NoteEvent> ghosts;
    anchors.reserve(track.notes.size());
    ghosts.reserve(track.notes.size());

    for (const auto& note : track.notes)
    {
        if (note.isGhost)
            ghosts.push_back(note);
        else
            anchors.push_back(note);
    }

    const int maxGhosts = std::max(1, (std::max(1, bars) + 3) / 4);
    std::stable_sort(ghosts.begin(), ghosts.end(), [bars](const NoteEvent& left, const NoteEvent& right)
    {
        const int leftScore = classicGhostPriority(left, bars);
        const int rightScore = classicGhostPriority(right, bars);
        if (leftScore != rightScore)
            return leftScore > rightScore;
        return left.step < right.step;
    });

    if (static_cast<int>(ghosts.size()) > maxGhosts)
        ghosts.resize(static_cast<size_t>(maxGhosts));

    anchors.insert(anchors.end(), ghosts.begin(), ghosts.end());
    std::sort(anchors.begin(), anchors.end(), [](const NoteEvent& left, const NoteEvent& right)
    {
        if (left.step != right.step)
            return left.step < right.step;
        if (left.isGhost != right.isGhost)
            return !left.isGhost;
        return left.velocity > right.velocity;
    });
    track.notes = std::move(anchors);
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
        const bool classicTightGhosts = style.substyle == BoomBapSubstyle::Classic;
        bool classicSupportHitUsed = false;
        int beat2Late = sampleLateTicks(feel.beat2LateTicks, feel.beat2LateSpread, rng);
        int beat4Late = sampleLateTicks(feel.beat4LateTicks, feel.beat4LateSpread, rng);

        if (feel.avoidStraightBackbeat && std::abs(beat2Late - beat4Late) < 2)
            beat4Late += beat4Late >= beat2Late ? 2 : -2;

        if (feel.dryBackbeat)
        {
            beat2Late = std::max(1, static_cast<int>(beat2Late * 0.82f));
            beat4Late = std::max(1, static_cast<int>(beat4Late * 0.82f));
        }

        if (classicTightGhosts)
        {
            beat2Late = std::clamp(beat2Late, 6, 14);
            beat4Late = std::clamp(beat4Late, 8, 16);
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

            float dragChance = std::clamp(feel.dragProbability + BoomBapPhrasePlanner::roleVariationStrength(role) * 0.12f, 0.0f, 0.42f);
            if (classicTightGhosts)
                dragChance = std::min(dragChance, 0.12f);

            if (chance(rng) < dragChance)
            {
                track.notes.push_back({ pitch, bar * 16 + 10, 1, std::max(style.ghostVelocityMin, ghostVel(rng) - 2), std::max(1, beat4Late / 3), true });
                if (classicTightGhosts)
                    classicSupportHitUsed = true;
            }
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

        if (classicTightGhosts)
        {
            ghostChance = std::min(ghostChance, role == PhraseRole::Ending ? 0.07f : 0.035f);
            ghostBefore2Chance = std::min(ghostBefore2Chance, role == PhraseRole::Ending ? 0.06f : 0.025f);
            ghostBefore4Chance = std::min(ghostBefore4Chance, role == PhraseRole::Ending ? 0.07f : 0.035f);
            if (role == PhraseRole::Base)
            {
                ghostChance *= 0.45f;
                ghostBefore2Chance *= 0.38f;
                ghostBefore4Chance *= 0.42f;
            }
        }

        if (role == PhraseRole::Ending && style.substyle == BoomBapSubstyle::BoomBapGold)
            track.notes.push_back({ pitch, bar * 16 + 14, 1, std::max(style.snareVelocityMin, velDist(rng) - 8), std::max(1, beat4Late / 2), true });

        float fillChance = std::clamp(feel.fillHitProbability, 0.0f, 0.45f);
        if (classicTightGhosts)
            fillChance = std::min(fillChance, role == PhraseRole::Ending ? 0.08f : 0.02f);

        if (role == PhraseRole::Ending && chance(rng) < fillChance)
        {
            if (!classicTightGhosts || !classicSupportHitUsed)
            {
                track.notes.push_back({ pitch, bar * 16 + 15, 1, std::max(style.snareVelocityMin, velDist(rng) - 6), std::max(1, beat4Late / 2), true });
                if (classicTightGhosts)
                    classicSupportHitUsed = true;
            }
        }

        if (halfTimeAware)
        {
            if (chance(rng) < ghostChance && (!classicTightGhosts || !classicSupportHitUsed))
            {
                track.notes.push_back({ pitch, bar * 16 + 7, 1, ghostVel(rng), std::max(1, beat4Late / 3), true });
                if (classicTightGhosts)
                    classicSupportHitUsed = true;
            }
        }
        else if (classicTightGhosts)
        {
            if (!classicSupportHitUsed)
            {
                const float before2Gate = std::max(ghostChance, ghostBefore2Chance);
                const float before4Gate = std::max(ghostChance, ghostBefore4Chance);
                const bool wantBefore2 = chance(rng) < before2Gate;
                const bool wantBefore4 = chance(rng) < before4Gate;

                if (wantBefore2 && wantBefore4)
                {
                    const float total = before2Gate + before4Gate;
                    const bool pickBefore2 = total <= 0.0f || chance(rng) < (before2Gate / total);
                    track.notes.push_back({ pitch,
                                            bar * 16 + (pickBefore2 ? 3 : 11),
                                            1,
                                            ghostVel(rng),
                                            std::max(1, (pickBefore2 ? beat2Late : beat4Late) / 3),
                                            true });
                }
                else if (wantBefore2)
                {
                    track.notes.push_back({ pitch, bar * 16 + 3, 1, ghostVel(rng), std::max(1, beat2Late / 3), true });
                }
                else if (wantBefore4)
                {
                    track.notes.push_back({ pitch, bar * 16 + 11, 1, ghostVel(rng), std::max(1, beat4Late / 3), true });
                }
            }
        }
        else
        {
            if (chance(rng) < std::max(ghostChance, ghostBefore2Chance))
                track.notes.push_back({ pitch, bar * 16 + 3, 1, ghostVel(rng), std::max(1, beat2Late / 3), true });
            if (chance(rng) < std::max(ghostChance, ghostBefore4Chance))
                track.notes.push_back({ pitch, bar * 16 + 11, 1, ghostVel(rng), std::max(1, beat4Late / 3), true });
        }
    }

    if (style.substyle == BoomBapSubstyle::Classic)
        pruneClassicGhostSnares(track, bars);
}
} // namespace bbg
