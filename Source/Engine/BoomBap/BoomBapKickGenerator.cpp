#include "BoomBapKickGenerator.h"

#include <algorithm>
#include <cmath>

#include "BoomBapPatternLibrary.h"
#include "../../Core/PatternProject.h"
#include "../../Core/TrackRegistry.h"
#include "../TempoInterpretation.h"

namespace bbg
{
namespace
{
constexpr float kStyleLabReferenceBlend = 0.40f;

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
    float anchorVelocity = 0.0f;
    float supportVelocity = 0.0f;
    float punctuationVelocity = 0.0f;
    std::array<float, 16> presence {};
    std::array<float, 16> velocitySum {};
    std::array<float, 16> velocityWeight {};
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
    float anchorVelocityTotal = 0.0f;
    float supportVelocityTotal = 0.0f;
    float punctuationVelocityTotal = 0.0f;
    float anchorVelocityWeight = 0.0f;
    float supportVelocityWeight = 0.0f;
    float punctuationVelocityWeight = 0.0f;

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
            const float velocity = static_cast<float>(std::clamp(note.velocity, 1, 127));
            feel.presence[static_cast<size_t>(step)] += 1.0f;
            feel.velocitySum[static_cast<size_t>(step)] += velocity;
            feel.velocityWeight[static_cast<size_t>(step)] += 1.0f;
            if (step == 0 || step == 8)
            {
                anchors += 1.0f;
                anchorVelocityTotal += velocity;
                anchorVelocityWeight += 1.0f;
            }
            else if (step >= 11)
            {
                punctuation += 1.0f;
                punctuationVelocityTotal += velocity;
                punctuationVelocityWeight += 1.0f;
            }
            else
            {
                supports += 1.0f;
                supportVelocityTotal += velocity;
                supportVelocityWeight += 1.0f;
            }
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
    feel.anchorVelocity = anchorVelocityWeight > 0.0f ? anchorVelocityTotal / anchorVelocityWeight : 0.0f;
    feel.supportVelocity = supportVelocityWeight > 0.0f ? supportVelocityTotal / supportVelocityWeight : 0.0f;
    feel.punctuationVelocity = punctuationVelocityWeight > 0.0f ? punctuationVelocityTotal / punctuationVelocityWeight : 0.0f;
    return feel;
}

int blendedReferenceKickVelocity(const ReferenceBoomBapKickFeel& feel,
                                 int stepInBar,
                                 KickHitRole role,
                                 int fallbackVelocity,
                                 const BoomBapStyleProfile& style)
{
    if (!feel.available)
        return fallbackVelocity;

    float target = static_cast<float>(fallbackVelocity);
    const float laneAverage = role == KickHitRole::Anchor
        ? feel.anchorVelocity
        : (role == KickHitRole::Pickup ? feel.punctuationVelocity : feel.supportVelocity);

    if (laneAverage > 0.0f)
        target = target * (1.0f - kStyleLabReferenceBlend) + laneAverage * kStyleLabReferenceBlend;

    const int clampedStep = std::clamp(stepInBar, 0, 15);
    const float stepWeight = feel.velocityWeight[static_cast<size_t>(clampedStep)];
    if (stepWeight > 0.0f)
        target = target * (1.0f - kStyleLabReferenceBlend) + (feel.velocitySum[static_cast<size_t>(clampedStep)] / stepWeight) * kStyleLabReferenceBlend;

    return std::clamp(static_cast<int>(std::round(target)), style.kickVelocityMin, style.kickVelocityMax);
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
            if (style.substyle == BoomBapSubstyle::Classic && (stepInBar == 4 || stepInBar == 12))
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
            int velocity = velDist(rng);
            if (referenceFeel.available)
                velocity = blendedReferenceKickVelocity(referenceFeel, stepInBar, KickHitRole::Anchor, velocity, style);
            if (style.substyle == BoomBapSubstyle::Classic)
                velocity = std::clamp(velocity + 2, style.kickVelocityMin, style.kickVelocityMax);

            int microOffset = microDist(rng);
            if (style.substyle == BoomBapSubstyle::Classic)
                microOffset = std::clamp(microOffset, -4, 8);

            track.notes.push_back({
                pitch,
                step,
                1,
                velocity,
                microOffset,
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
            if (style.substyle == BoomBapSubstyle::Classic && (stepInBar == 4 || stepInBar == 12))
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
            if (style.substyle == BoomBapSubstyle::Classic && hitRole != KickHitRole::Anchor)
            {
                if (hitRole == KickHitRole::Support)
                    keepChance = std::min(keepChance, role == PhraseRole::Ending ? 0.52f : 0.44f);
                else
                    keepChance = std::min(keepChance, role == PhraseRole::Ending ? 0.34f : 0.20f);

                if (role == PhraseRole::Base)
                    keepChance = std::clamp(keepChance * 0.72f, 0.03f, 1.0f);

                if (hitRole == KickHitRole::Pickup && role != PhraseRole::Ending && stepInBar >= 13)
                    keepChance = std::clamp(keepChance * 0.50f, 0.03f, 1.0f);
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
            int velocity = velDist(rng);
            if (referenceFeel.available)
                velocity = blendedReferenceKickVelocity(referenceFeel, stepInBar, hitRole, velocity, style);
            if (style.substyle == BoomBapSubstyle::Classic && hitRole != KickHitRole::Pickup)
                velocity = std::clamp(velocity + 2, style.kickVelocityMin, style.kickVelocityMax);

            int microOffset = microDist(rng);
            if (style.substyle == BoomBapSubstyle::Classic)
            {
                if (hitRole == KickHitRole::Anchor)
                    microOffset = std::clamp(microOffset, -4, 8);
                else
                    microOffset = std::clamp(microOffset, -8, 14);
            }

            track.notes.push_back({
                pitch,
                step,
                1,
                velocity,
                microOffset,
                false
            });
        }
    }
}
} // namespace bbg
