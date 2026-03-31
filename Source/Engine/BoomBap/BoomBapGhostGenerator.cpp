#include "BoomBapGhostGenerator.h"

#include <algorithm>

#include "BoomBapPatternLibrary.h"
#include "../../Core/PatternProject.h"
#include "../../Core/TrackRegistry.h"

namespace bbg
{
namespace
{
struct ReferenceBoomBapSupportFeel
{
    bool available = false;
    float kickSupportRatio = 0.0f;
    float hatSupportRatio = 0.0f;
    float gapRatio = 0.0f;
};

ReferenceBoomBapSupportFeel buildReferenceBoomBapSupportFeel(const StyleInfluenceState& styleInfluence, int bar)
{
    ReferenceBoomBapSupportFeel feel;
    int hatBars = 0;
    float hatNotes = 0.0f;
    float hatSupport = 0.0f;
    float emptySlots = 0.0f;

    if (styleInfluence.referenceHatCorpus.available && !styleInfluence.referenceHatCorpus.variants.empty())
    {
        for (const auto& variant : styleInfluence.referenceHatCorpus.variants)
        {
            if (!variant.available || variant.barMaps.empty())
                continue;
            const int sourceBars = std::max(1, variant.sourceBars > 0 ? variant.sourceBars : static_cast<int>(variant.barMaps.size()));
            const int normalizedBar = ((bar % sourceBars) + sourceBars) % sourceBars;
            if (normalizedBar < 0 || normalizedBar >= static_cast<int>(variant.barMaps.size()))
                continue;

            const auto& barMap = variant.barMaps[static_cast<size_t>(normalizedBar)];
            ++hatBars;
            std::array<bool, 8> occupiedSlots {};
            for (const auto& note : barMap.notes)
            {
                const int step16 = std::clamp(note.tickInBar / 120, 0, 15);
                occupiedSlots[static_cast<size_t>(step16 / 2)] = true;
                hatNotes += 1.0f;
                if ((step16 % 2) == 1)
                    hatSupport += 1.0f;
            }
            for (size_t i = 0; i < occupiedSlots.size(); ++i)
                if (!occupiedSlots[i])
                    emptySlots += 1.0f;
        }
    }

    int kickBars = 0;
    float kickNotes = 0.0f;
    float kickSupport = 0.0f;
    if (styleInfluence.referenceKickCorpus.available && !styleInfluence.referenceKickCorpus.variants.empty())
    {
        for (const auto& variant : styleInfluence.referenceKickCorpus.variants)
        {
            if (!variant.available || variant.barPatterns.empty())
                continue;
            const int sourceBars = std::max(1, variant.sourceBars > 0 ? variant.sourceBars : static_cast<int>(variant.barPatterns.size()));
            const int normalizedBar = ((bar % sourceBars) + sourceBars) % sourceBars;
            if (normalizedBar < 0 || normalizedBar >= static_cast<int>(variant.barPatterns.size()))
                continue;

            const auto& pattern = variant.barPatterns[static_cast<size_t>(normalizedBar)];
            ++kickBars;
            kickNotes += static_cast<float>(pattern.notes.size());
            for (const auto& note : pattern.notes)
                if (note.step16 != 0 && note.step16 != 8)
                    kickSupport += 1.0f;
        }
    }

    if (hatBars <= 0 && kickBars <= 0)
        return feel;

    feel.available = true;
    feel.hatSupportRatio = hatNotes > 0.0f ? hatSupport / hatNotes : 0.0f;
    feel.kickSupportRatio = kickNotes > 0.0f ? kickSupport / kickNotes : 0.0f;
    feel.gapRatio = hatBars > 0 ? emptySlots / (static_cast<float>(hatBars) * 8.0f) : 0.0f;
    return feel;
}
}

void BoomBapGhostGenerator::generateGhostKick(TrackState& ghostKickTrack,
                                              const TrackState& kickTrack,
                                              const BoomBapStyleProfile& style,
                                              const StyleInfluenceState& styleInfluence,
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
        const auto referenceFeel = buildReferenceBoomBapSupportFeel(styleInfluence, bar);
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
        if (referenceFeel.available)
            gate *= std::clamp(0.86f + referenceFeel.kickSupportRatio * 0.28f + referenceFeel.gapRatio * 0.12f, 0.74f, 1.22f);

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
                                              const StyleInfluenceState& styleInfluence,
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
        const auto referenceFeel = buildReferenceBoomBapSupportFeel(styleInfluence, bar);
        const auto role = bar < static_cast<int>(phraseRoles.size()) ? phraseRoles[static_cast<size_t>(bar)] : PhraseRole::Base;
        float gate = std::clamp(style.clapLayerChance * feel.clapLayerProbability
                                    + BoomBapPhrasePlanner::roleVariationStrength(role) * 0.12f,
                                0.25f,
                                1.0f);
        if (style.substyle == BoomBapSubstyle::RussianUnderground)
            gate = std::max(0.45f, gate - 0.14f);
        else if (style.substyle == BoomBapSubstyle::LofiRap)
            gate = std::min(gate, 0.58f);
        if (referenceFeel.available)
            gate *= std::clamp(0.88f + referenceFeel.kickSupportRatio * 0.2f + referenceFeel.hatSupportRatio * 0.14f, 0.78f, 1.18f);

        if (chance(rng) > gate)
            continue;

        clapTrack.notes.push_back({ pitch, snare.step, 1, vel(rng), snare.microOffset + late(rng), false });
    }
}
} // namespace bbg
