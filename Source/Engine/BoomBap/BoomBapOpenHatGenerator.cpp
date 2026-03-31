#include "BoomBapOpenHatGenerator.h"

#include <algorithm>

#include "BoomBapPatternLibrary.h"
#include "../../Core/PatternProject.h"
#include "../../Core/TrackRegistry.h"

namespace bbg
{
namespace
{
struct ReferenceBoomBapHatSupportFeel
{
    bool available = false;
    float supportRatio = 0.0f;
    float gapRatio = 0.0f;
};

ReferenceBoomBapHatSupportFeel buildReferenceBoomBapHatSupportFeel(const StyleInfluenceState& styleInfluence, int bar)
{
    ReferenceBoomBapHatSupportFeel feel;
    const auto& corpus = styleInfluence.referenceHatCorpus;
    if (!corpus.available || corpus.variants.empty())
        return feel;

    int contributingBars = 0;
    float totalNotes = 0.0f;
    float supportNotes = 0.0f;
    float emptySlots = 0.0f;
    for (const auto& variant : corpus.variants)
    {
        if (!variant.available || variant.barMaps.empty())
            continue;
        const int sourceBars = std::max(1, variant.sourceBars > 0 ? variant.sourceBars : static_cast<int>(variant.barMaps.size()));
        const int normalizedBar = ((bar % sourceBars) + sourceBars) % sourceBars;
        if (normalizedBar < 0 || normalizedBar >= static_cast<int>(variant.barMaps.size()))
            continue;
        const auto& barMap = variant.barMaps[static_cast<size_t>(normalizedBar)];
        ++contributingBars;
        std::array<bool, 8> occupiedSlots {};
        for (const auto& note : barMap.notes)
        {
            const int step16 = std::clamp(note.tickInBar / 120, 0, 15);
            occupiedSlots[static_cast<size_t>(step16 / 2)] = true;
            totalNotes += 1.0f;
            if ((step16 % 4) != 0)
                supportNotes += 1.0f;
        }
        for (size_t i = 0; i < occupiedSlots.size(); ++i)
            if (!occupiedSlots[i])
                emptySlots += 1.0f;
    }

    if (contributingBars <= 0)
        return feel;
    feel.available = true;
    feel.supportRatio = totalNotes > 0.0f ? supportNotes / totalNotes : 0.0f;
    feel.gapRatio = emptySlots / (static_cast<float>(contributingBars) * 8.0f);
    return feel;
}
}

void BoomBapOpenHatGenerator::generate(TrackState& track,
                                       const TrackState& hatTrack,
                                       const GeneratorParams& params,
                                       const BoomBapStyleProfile& style,
                                       const StyleInfluenceState& styleInfluence,
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
        const auto referenceFeel = buildReferenceBoomBapHatSupportFeel(styleInfluence, bar);
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
        if (referenceFeel.available)
            gate *= std::clamp(0.88f + referenceFeel.supportRatio * 0.24f + referenceFeel.gapRatio * 0.12f, 0.76f, 1.2f);

        if (chance(rng) > std::clamp(gate, 0.01f, 0.75f))
            continue;

        track.notes.push_back({ pitch, hat.step, (role == PhraseRole::Ending) ? 2 : 1, vel(rng), hat.microOffset, false });
    }
}
} // namespace bbg
