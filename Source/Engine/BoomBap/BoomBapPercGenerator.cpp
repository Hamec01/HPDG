#include "BoomBapPercGenerator.h"

#include <algorithm>

#include "BoomBapPatternLibrary.h"
#include "../../Core/PatternProject.h"
#include "../../Core/TrackRegistry.h"

namespace bbg
{
namespace
{
struct ReferenceBoomBapPercFeel
{
    bool available = false;
    float supportRatio = 0.0f;
    float gapRatio = 0.0f;
};

ReferenceBoomBapPercFeel buildReferenceBoomBapPercFeel(const StyleInfluenceState& styleInfluence, int bar)
{
    ReferenceBoomBapPercFeel feel;
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

void BoomBapPercGenerator::generate(TrackState& track,
                                    const GeneratorParams& params,
                                    const BoomBapStyleProfile& style,
                                    const StyleInfluenceState& styleInfluence,
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
        const auto referenceFeel = buildReferenceBoomBapPercFeel(styleInfluence, bar);

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
            if (referenceFeel.available)
                gate *= std::clamp(0.88f + referenceFeel.supportRatio * 0.22f + referenceFeel.gapRatio * 0.1f, 0.76f, 1.18f);

            if (chance(rng) > std::clamp(gate, 0.01f, 0.65f))
                continue;

            track.notes.push_back({ pitch, bar * 16 + step, 1, velocity(rng), micro(rng), false });
            ++barEvents[static_cast<size_t>(bar)];
        }
    }
}
} // namespace bbg
