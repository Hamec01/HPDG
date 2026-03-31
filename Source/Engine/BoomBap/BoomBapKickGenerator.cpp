#include "BoomBapKickGenerator.h"

#include <algorithm>

#include "BoomBapPatternLibrary.h"
#include "../../Core/PatternProject.h"
#include "../../Core/TrackRegistry.h"
#include "../TempoInterpretation.h"

namespace bbg
{
namespace
{
float substyleSupportScale(BoomBapSubstyle substyle)
{
    switch (substyle)
    {
        case BoomBapSubstyle::BoomBapGold: return 1.15f;
        case BoomBapSubstyle::RussianUnderground: return 0.84f;
        case BoomBapSubstyle::LofiRap: return 0.72f;
        case BoomBapSubstyle::Aggressive: return 1.10f;
        case BoomBapSubstyle::Dusty: return 0.92f;
        default: return 1.0f;
    }
}

float probabilityForRole(KickHitRole role, float density, float roleVar, float styleBias)
{
    const float base = role == KickHitRole::Anchor ? 0.98f : role == KickHitRole::Support ? 0.42f : 0.24f;
    const float dense = role == KickHitRole::Anchor ? 0.0f : role == KickHitRole::Support ? 0.45f : 0.38f;
    const float variation = role == KickHitRole::Anchor ? -0.04f : roleVar * 0.28f;
    return std::clamp(base + dense * density * styleBias + variation, 0.05f, 1.0f);
}

const KickTemplateDefinition* pickTemplate(BoomBapSubstyle substyle,
                                           float density,
                                           PhraseRole role,
                                           std::mt19937& rng)
{
    auto candidates = findMatchingKickTemplates(substyle, density, role);
    if (candidates.empty())
        return nullptr;

    std::uniform_int_distribution<int> pick(0, static_cast<int>(candidates.size() - 1));
    return candidates[static_cast<size_t>(pick(rng))];
}

float identityRetention(PhraseRole role)
{
    switch (role)
    {
        case PhraseRole::Base: return 0.92f;
        case PhraseRole::Variation: return 0.74f;
        case PhraseRole::Contrast: return 0.62f;
        case PhraseRole::Ending: return 0.84f;
        default: return 0.78f;
    }
}

float kickActivityWeight(const StyleInfluenceState& styleInfluence)
{
    return std::clamp(laneBiasFor(styleInfluence, TrackType::Kick).activityWeight, 0.45f, 1.6f);
}

float supportAccentWeight(const StyleInfluenceState& styleInfluence)
{
    return std::clamp(styleInfluence.supportAccentWeight, 0.6f, 1.5f);
}

struct ReferenceBoomBapKickFeel
{
    bool available = false;
    float density = 0.0f;
    float anchorRatio = 0.0f;
    float supportRatio = 0.0f;
    float punctuationRatio = 0.0f;
    std::array<float, 16> presence {};
};

ReferenceBoomBapKickFeel buildReferenceBoomBapKickFeel(const StyleInfluenceState& styleInfluence, int bar)
{
    ReferenceBoomBapKickFeel feel;
    if (!styleInfluence.referenceKickCorpus.available || styleInfluence.referenceKickCorpus.variants.empty())
        return feel;

    int contributingBars = 0;
    float totalNotes = 0.0f;
    float anchors = 0.0f;
    float supports = 0.0f;
    float punctuation = 0.0f;

    for (const auto& variant : styleInfluence.referenceKickCorpus.variants)
    {
        if (!variant.available || variant.barPatterns.empty())
            continue;

        const int sourceBars = std::max(1, variant.sourceBars > 0 ? variant.sourceBars : static_cast<int>(variant.barPatterns.size()));
        const int normalizedBar = ((bar % sourceBars) + sourceBars) % sourceBars;
        if (normalizedBar < 0 || normalizedBar >= static_cast<int>(variant.barPatterns.size()))
            continue;

        const auto& pattern = variant.barPatterns[static_cast<size_t>(normalizedBar)];
        ++contributingBars;
        totalNotes += static_cast<float>(pattern.notes.size());
        for (const auto& note : pattern.notes)
        {
            const int step = std::clamp(note.step16, 0, 15);
            feel.presence[static_cast<size_t>(step)] += 1.0f;
            if (step == 0 || step == 8)
                anchors += 1.0f;
            else if (step >= 11)
                punctuation += 1.0f;
            else
                supports += 1.0f;
        }
    }

    if (contributingBars <= 0)
        return feel;

    feel.available = true;
    const float invBars = 1.0f / static_cast<float>(contributingBars);
    for (auto& value : feel.presence)
        value *= invBars;
    feel.density = std::clamp((totalNotes * invBars) / 4.0f, 0.0f, 1.0f);
    feel.anchorRatio = totalNotes > 0.0f ? anchors / totalNotes : 0.0f;
    feel.supportRatio = totalNotes > 0.0f ? supports / totalNotes : 0.0f;
    feel.punctuationRatio = totalNotes > 0.0f ? punctuation / totalNotes : 0.0f;
    return feel;
}
} // namespace

void BoomBapKickGenerator::generate(TrackState& track,
                                    const GeneratorParams& params,
                                    const BoomBapStyleProfile& style,
                                    const StyleInfluenceState& styleInfluence,
                                    const std::vector<PhraseRole>& phraseRoles,
                                    std::mt19937& rng) const
{
    track.notes.clear();

    const auto* info = TrackRegistry::find(track.type);
    const int pitch = info != nullptr ? info->defaultMidiNote : 36;

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    std::uniform_int_distribution<int> velDist(style.kickVelocityMin, style.kickVelocityMax);
    std::uniform_int_distribution<int> microDist(-style.kickTimingMaxTicks / 2, style.kickTimingMaxTicks);

    const int bars = std::max(1, params.bars);
    const auto tempoBand = selectTempoBand(params.bpm, params, 120.0f, 140.0f, 98.0f, 126.0f);
    float tempoScale = 1.0f;
    if (tempoBand == TempoBand::Elevated)
        tempoScale = 0.8f;
    else if (tempoBand == TempoBand::Fast)
        tempoScale = 0.64f;
    const float density = std::clamp(params.densityAmount * style.kickDensityBias * tempoScale * kickActivityWeight(styleInfluence), 0.0f, 1.0f);
    const float supportAccent = supportAccentWeight(styleInfluence);

    const auto* identityTemplate = pickTemplate(style.substyle, density, PhraseRole::Base, rng);
    if (identityTemplate == nullptr)
        return;

    std::array<int, 8> identityAnchors {};
    int identityAnchorCount = 0;
    for (int i = 0; i < static_cast<int>(identityTemplate->steps.size()); ++i)
    {
        const int step = identityTemplate->steps[static_cast<size_t>(i)];
        if (step < 0)
            continue;

        if (identityTemplate->roles[static_cast<size_t>(i)] != KickHitRole::Anchor)
            continue;

        if (identityAnchorCount < static_cast<int>(identityAnchors.size()))
            identityAnchors[static_cast<size_t>(identityAnchorCount++)] = step;
    }

    if (identityAnchorCount == 0)
    {
        identityAnchors[0] = 0;
        identityAnchors[1] = 8;
        identityAnchorCount = 2;
    }

    for (int bar = 0; bar < bars; ++bar)
    {
        const auto role = bar < static_cast<int>(phraseRoles.size()) ? phraseRoles[static_cast<size_t>(bar)] : PhraseRole::Base;
        const auto referenceFeel = buildReferenceBoomBapKickFeel(styleInfluence, bar);
        const float roleVar = BoomBapPhrasePlanner::roleVariationStrength(role) * style.barVariationAmount;
        const float retainIdentity = identityRetention(role);
        const auto* barTemplate = pickTemplate(style.substyle, density, role, rng);
        if (barTemplate == nullptr)
            barTemplate = identityTemplate;

        std::array<bool, 16> usedSteps {};

        for (int i = 0; i < identityAnchorCount; ++i)
        {
            int stepInBar = identityAnchors[static_cast<size_t>(i)];
            if (stepInBar < 0 || stepInBar > 15 || usedSteps[static_cast<size_t>(stepInBar)])
                continue;

            usedSteps[static_cast<size_t>(stepInBar)] = true;

            float keepChance = std::clamp(0.94f + retainIdentity * 0.08f, 0.75f, 1.0f);
            if (tempoBand == TempoBand::Fast)
                keepChance = std::clamp(keepChance - 0.08f, 0.6f, 1.0f);
            if (referenceFeel.available)
                keepChance = std::clamp(keepChance * std::clamp(0.88f + referenceFeel.presence[static_cast<size_t>(stepInBar)] * 0.36f + referenceFeel.anchorRatio * 0.18f,
                                                                0.78f,
                                                                1.28f),
                                        0.6f,
                                        1.0f);

            if (chance(rng) > keepChance)
                continue;

            int step = bar * 16 + stepInBar;
            track.notes.push_back({
                pitch,
                step,
                1,
                velDist(rng),
                microDist(rng),
                false
            });
        }

        for (int i = 0; i < static_cast<int>(barTemplate->steps.size()); ++i)
        {
            const int stepInBar = barTemplate->steps[static_cast<size_t>(i)];
            if (stepInBar < 0)
                continue;

            if (stepInBar > 15 || usedSteps[static_cast<size_t>(stepInBar)])
                continue;

            const auto hitRole = barTemplate->roles[static_cast<size_t>(i)];
            const float profileScale = substyleSupportScale(style.substyle);
            float keepChance = probabilityForRole(hitRole, density, roleVar, style.kickDensityBias);
            if (hitRole != KickHitRole::Anchor)
                keepChance = std::clamp(keepChance * profileScale * retainIdentity * supportAccent, 0.05f, 1.0f);
            if (referenceFeel.available)
            {
                const float presence = referenceFeel.presence[static_cast<size_t>(stepInBar)];
                if (hitRole == KickHitRole::Anchor)
                    keepChance *= std::clamp(0.88f + presence * 0.38f + referenceFeel.anchorRatio * 0.18f, 0.78f, 1.28f);
                else if (hitRole == KickHitRole::Pickup)
                    keepChance *= std::clamp(0.86f + presence * 0.28f + referenceFeel.punctuationRatio * 0.24f, 0.72f, 1.3f);
                else
                    keepChance *= std::clamp(0.86f + presence * 0.3f + referenceFeel.supportRatio * 0.18f, 0.72f, 1.24f);
            }
            if (tempoBand != TempoBand::Base && hitRole != KickHitRole::Anchor)
                keepChance = std::clamp(keepChance * (tempoBand == TempoBand::Fast ? 0.58f : 0.72f), 0.03f, 1.0f);

            if (role == PhraseRole::Ending && hitRole == KickHitRole::Pickup)
                keepChance = std::clamp(keepChance + (style.substyle == BoomBapSubstyle::BoomBapGold ? 0.16f : 0.08f), 0.05f, 1.0f);
            if (referenceFeel.available && role == PhraseRole::Ending && stepInBar >= 11)
                keepChance = std::clamp(keepChance + referenceFeel.punctuationRatio * 0.12f, 0.05f, 1.0f);

            if (chance(rng) > keepChance)
                continue;

            int step = bar * 16 + stepInBar;
            if (tempoBand != TempoBand::Base && stepInBar == 8)
                step = bar * 16 + (chance(rng) < 0.65f ? 10 : 9);

            if (role == PhraseRole::Ending && hitRole == KickHitRole::Pickup && chance(rng) < 0.4f)
                step = std::min(step + 1, bar * 16 + 15);

            usedSteps[static_cast<size_t>(stepInBar)] = true;
            track.notes.push_back({
                pitch,
                step,
                1,
                velDist(rng),
                microDist(rng),
                false
            });
        }
    }
}
} // namespace bbg
