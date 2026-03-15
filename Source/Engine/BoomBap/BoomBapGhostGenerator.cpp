#include "BoomBapGhostGenerator.h"

#include <algorithm>

#include "BoomBapPatternLibrary.h"
#include "../../Core/TrackRegistry.h"

namespace bbg
{
void BoomBapGhostGenerator::generateGhostKick(TrackState& ghostKickTrack,
                                              const TrackState& kickTrack,
                                              const BoomBapStyleProfile& style,
                                              float density,
                                              const std::vector<PhraseRole>& phraseRoles,
                                              std::mt19937& rng) const
{
    ghostKickTrack.notes.clear();

    const auto* info = TrackRegistry::find(ghostKickTrack.type);
    const int pitch = info != nullptr ? info->defaultMidiNote : 35;

    const auto& preset = chooseGhostPreset(style.substyle, rng);

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    std::uniform_int_distribution<int> vel(style.ghostVelocityMin, style.ghostVelocityMax);
    std::uniform_int_distribution<int> offset(1, style.ghostTimingMaxTicks);

    std::vector<int> perBarCount(static_cast<size_t>(std::max(1, static_cast<int>(phraseRoles.size()))), 0);

    for (const auto& hit : kickTrack.notes)
    {
        const int bar = hit.step / 16;
        const auto role = bar < static_cast<int>(phraseRoles.size()) ? phraseRoles[static_cast<size_t>(bar)] : PhraseRole::Base;
        const float roleBoost = 1.0f + BoomBapPhrasePlanner::roleVariationStrength(role) * 0.4f;

        float gate = std::clamp((preset.pickupProbability + style.ghostKickChance * 0.5f)
                                      * (0.25f + density * style.ghostDensityBias)
                                      * roleBoost,
                                      0.02f,
                                      0.6f);
        if (style.substyle == BoomBapSubstyle::RussianUnderground)
            gate *= 0.72f;
        else if (style.substyle == BoomBapSubstyle::BoomBapGold && role == PhraseRole::Ending)
            gate = std::clamp(gate + 0.10f, 0.02f, 0.7f);

        if (bar < static_cast<int>(perBarCount.size()) && perBarCount[static_cast<size_t>(bar)] >= preset.maxEventsPerBar)
            continue;

        if (chance(rng) > gate)
            continue;

        int pickupStep = std::max(0, hit.step - 1);
        if (preset.barEndBias && chance(rng) < 0.5f)
            pickupStep = bar * 16 + 14;

        ghostKickTrack.notes.push_back({ pitch, pickupStep, 1, vel(rng), offset(rng), true });
        if (bar < static_cast<int>(perBarCount.size()))
            ++perBarCount[static_cast<size_t>(bar)];
    }
}

void BoomBapGhostGenerator::generateClapLayer(TrackState& clapTrack,
                                              const TrackState& snareTrack,
                                              const BoomBapStyleProfile& style,
                                              const std::vector<PhraseRole>& phraseRoles,
                                              std::mt19937& rng) const
{
    clapTrack.notes.clear();

    const auto* info = TrackRegistry::find(clapTrack.type);
    const int pitch = info != nullptr ? info->defaultMidiNote : 39;

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    std::uniform_int_distribution<int> vel(style.clapVelocityMin, style.clapVelocityMax);
    std::uniform_int_distribution<int> late(std::max(2, style.clapLateTicks / 2), std::max(4, style.clapLateTicks));
    const auto& feel = chooseSnareFeelProfile(style.substyle, 0.5f, rng);

    for (const auto& snare : snareTrack.notes)
    {
        const int bar = snare.step / 16;
        const auto role = bar < static_cast<int>(phraseRoles.size()) ? phraseRoles[static_cast<size_t>(bar)] : PhraseRole::Base;
        float gate = std::clamp(style.clapLayerChance * feel.clapLayerProbability
                                    + BoomBapPhrasePlanner::roleVariationStrength(role) * 0.12f,
                                0.25f,
                                1.0f);
        if (style.substyle == BoomBapSubstyle::RussianUnderground)
            gate = std::max(0.45f, gate - 0.14f);
        else if (style.substyle == BoomBapSubstyle::LofiRap)
            gate = std::min(gate, 0.58f);

        if (chance(rng) > gate)
            continue;

        clapTrack.notes.push_back({ pitch, snare.step, 1, vel(rng), snare.microOffset + late(rng), false });
    }
}
} // namespace bbg
