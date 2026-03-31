#include "BoomBapHatGenerator.h"

#include <algorithm>

#include "BoomBapPatternLibrary.h"
#include "../../Core/PatternProject.h"
#include "../../Core/TrackRegistry.h"
#include "../HiResTiming.h"
#include "../TempoInterpretation.h"

namespace bbg
{
namespace
{
const BoomBapBarBlueprint* blueprintBarAt(const BoomBapGrooveBlueprint& blueprint, int bar)
{
    if (bar < 0 || bar >= static_cast<int>(blueprint.bars.size()))
        return nullptr;

    return &blueprint.bars[static_cast<size_t>(bar)];
}

float carrierGateScale(BoomBapSubstyle substyle)
{
    switch (substyle)
    {
        case BoomBapSubstyle::BoomBapGold: return 1.10f;
        case BoomBapSubstyle::Jazzy: return 1.08f;
        case BoomBapSubstyle::RussianUnderground: return 0.8f;
        case BoomBapSubstyle::LofiRap: return 0.82f;
        case BoomBapSubstyle::Aggressive: return 0.9f;
        default: return 1.0f;
    }
}

int swingTicksFromBlueprint(const BoomBapBarBlueprint* bar)
{
    if (bar == nullptr)
        return 0;

    const float normalized = std::clamp((bar->swingStrength - 0.45f) / 0.35f, 0.0f, 1.0f);
    return static_cast<int>(2.0f + normalized * 10.0f);
}

float syncopationGateScale(const BoomBapBarBlueprint* bar, int step)
{
    if (bar == nullptr)
        return 1.0f;

    if ((step % 2) == 0)
        return std::clamp(0.74f + bar->hatSyncopation * 0.52f, 0.62f, 1.24f);

    return std::clamp(0.48f + bar->hatSyncopation * 0.96f, 0.26f, 1.34f);
}

float hiHatActivityWeight(const StyleInfluenceState& styleInfluence)
{
    return std::clamp(laneBiasFor(styleInfluence, TrackType::HiHat).activityWeight, 0.5f, 1.5f);
}

float supportAccentWeight(const StyleInfluenceState& styleInfluence)
{
    return std::clamp(styleInfluence.supportAccentWeight, 0.6f, 1.5f);
}

struct ReferenceBoomBapHatFeel
{
    bool available = false;
    float noteDensity = 0.0f;
    float carrierRatio = 0.0f;
    float supportRatio = 0.0f;
    float gapRatio = 0.0f;
    float looseRatio = 0.0f;
    std::array<float, 8> slotWeight {};
};

ReferenceBoomBapHatFeel buildReferenceBoomBapHatFeel(const StyleInfluenceState& styleInfluence, int bar)
{
    ReferenceBoomBapHatFeel feel;
    int contributingBars = 0;
    float totalNotes = 0.0f;
    float carrierNotes = 0.0f;
    float supportNotes = 0.0f;
    float looseNotes = 0.0f;
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
            ++contributingBars;
            std::array<bool, 8> occupiedSlots {};

            for (const auto& note : barMap.notes)
            {
                const int step32 = std::clamp(HiResTiming::quantizeTicks(note.tickInBar, HiResTiming::kTicks1_32) / HiResTiming::kTicks1_32,
                                              0,
                                              31);
                const int step16 = std::clamp(step32 / 2, 0, 15);
                const int slot = std::clamp(step16 / 2, 0, 7);
                occupiedSlots[static_cast<size_t>(slot)] = true;
                feel.slotWeight[static_cast<size_t>(slot)] += 1.0f;
                totalNotes += 1.0f;
                if ((step16 % 4) == 0)
                    carrierNotes += 1.0f;
                else
                    supportNotes += 1.0f;
                if ((step32 % 4) == 1 || (step32 % 4) == 3)
                    looseNotes += 1.0f;
            }

            for (size_t i = 0; i < occupiedSlots.size(); ++i)
                if (!occupiedSlots[i])
                    emptySlots += 1.0f;
        }
    }

    if (!feel.available && styleInfluence.referenceHatSkeleton.available && !styleInfluence.referenceHatSkeleton.barMaps.empty())
    {
        const auto& skeleton = styleInfluence.referenceHatSkeleton;
        const int sourceBars = std::max(1, skeleton.sourceBars > 0 ? skeleton.sourceBars : static_cast<int>(skeleton.barMaps.size()));
        const int normalizedBar = ((bar % sourceBars) + sourceBars) % sourceBars;
        if (normalizedBar >= 0 && normalizedBar < static_cast<int>(skeleton.barMaps.size()))
        {
            const auto& barMap = skeleton.barMaps[static_cast<size_t>(normalizedBar)];
            ++contributingBars;
            std::array<bool, 8> occupiedSlots {};

            for (const int step16 : barMap.backboneSteps16)
            {
                const int slot = std::clamp(step16 / 2, 0, 7);
                occupiedSlots[static_cast<size_t>(slot)] = true;
                feel.slotWeight[static_cast<size_t>(slot)] += 0.8f;
                totalNotes += 0.8f;
                if ((step16 % 4) == 0)
                    carrierNotes += 0.8f;
                else
                    supportNotes += 0.5f;
            }

            for (const int step32 : barMap.motionSteps32)
            {
                const int step16 = std::clamp(step32 / 2, 0, 15);
                const int slot = std::clamp(step16 / 2, 0, 7);
                occupiedSlots[static_cast<size_t>(slot)] = true;
                feel.slotWeight[static_cast<size_t>(slot)] += 0.55f;
                totalNotes += 0.65f;
                supportNotes += 0.65f;
                if ((step32 % 4) == 1 || (step32 % 4) == 3)
                    looseNotes += 0.55f;
            }

            for (size_t i = 0; i < occupiedSlots.size(); ++i)
                if (!occupiedSlots[i])
                    emptySlots += 1.0f;
        }
    }

    if (contributingBars <= 0)
        return feel;

    feel.available = true;
    const float invBars = 1.0f / static_cast<float>(contributingBars);
    for (auto& value : feel.slotWeight)
        value *= invBars;
    feel.noteDensity = totalNotes * invBars;
    feel.carrierRatio = totalNotes > 0.0f ? carrierNotes / totalNotes : 0.0f;
    feel.supportRatio = totalNotes > 0.0f ? supportNotes / totalNotes : 0.0f;
    feel.gapRatio = emptySlots / (static_cast<float>(contributingBars) * 8.0f);
    feel.looseRatio = totalNotes > 0.0f ? looseNotes / totalNotes : 0.0f;
    return feel;
}

float referenceCarrierBias(const ReferenceBoomBapHatFeel& feel)
{
    if (!feel.available)
        return 1.0f;
    return std::clamp(0.88f + feel.carrierRatio * 0.42f - std::max(0.0f, feel.gapRatio - 0.5f) * 0.12f, 0.78f, 1.24f);
}

float referenceSupportBias(const ReferenceBoomBapHatFeel& feel)
{
    if (!feel.available)
        return 1.0f;
    return std::clamp(0.86f + feel.supportRatio * 0.34f + std::max(0.0f, feel.noteDensity - 8.0f) * 0.018f, 0.76f, 1.22f);
}

float referenceLoosenessBias(const ReferenceBoomBapHatFeel& feel)
{
    if (!feel.available)
        return 1.0f;
    return std::clamp(0.88f + feel.looseRatio * 0.34f + feel.gapRatio * 0.26f, 0.8f, 1.22f);
}

float referenceBarShape(const ReferenceBoomBapHatFeel& feel, PhraseRole role, int bar)
{
    if (!feel.available)
        return role == PhraseRole::Ending ? 1.04f : 1.0f;

    float shape = 1.0f;
    if (feel.carrierRatio > 0.34f)
        shape += (bar % 2) == 0 ? 0.04f : -0.02f;
    if (feel.gapRatio > 0.48f)
        shape += (bar % 2) == 1 ? -0.05f : 0.02f;
    if (role == PhraseRole::Variation)
        shape += feel.supportRatio > 0.5f ? 0.04f : 0.02f;
    if (role == PhraseRole::Ending)
        shape += feel.noteDensity > 8.0f ? 0.05f : 0.02f;
    if (role == PhraseRole::Base && feel.gapRatio > 0.54f)
        shape -= 0.04f;
    return std::clamp(shape, 0.86f, 1.16f);
}
}

void BoomBapHatGenerator::generate(TrackState& track,
                                   const GeneratorParams& params,
                                   const BoomBapStyleProfile& style,
                                   const StyleInfluenceState& styleInfluence,
                                   const std::vector<PhraseRole>& phraseRoles,
                                   const BoomBapGrooveBlueprint& blueprint,
                                   std::mt19937& rng) const
{
    track.notes.clear();

    const auto* info = TrackRegistry::find(track.type);
    const int pitch = info != nullptr ? info->defaultMidiNote : 42;

    const auto& pattern = chooseHatPatternProfile(style.substyle, params.densityAmount, rng);

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    std::uniform_int_distribution<int> microDist(-style.hatTimingMaxTicks / 2, style.hatTimingMaxTicks / 2);
    std::uniform_real_distribution<float> microChance(0.0f, 1.0f);
    const auto tempoBand = selectTempoBand(params.bpm, params, 120.0f, 140.0f, 98.0f, 126.0f);

    const int bars = std::max(1, params.bars);
    float tempoScale = 1.0f;
    if (tempoBand == TempoBand::Elevated)
        tempoScale = 0.78f;
    else if (tempoBand == TempoBand::Fast)
        tempoScale = 0.62f;
    const float baseDensity = std::clamp(params.densityAmount * style.hatDensityBias * tempoScale * hiHatActivityWeight(styleInfluence), 0.0f, 1.0f);
    const float supportAccent = supportAccentWeight(styleInfluence);

    for (int bar = 0; bar < bars; ++bar)
    {
        const auto role = bar < static_cast<int>(phraseRoles.size()) ? phraseRoles[static_cast<size_t>(bar)] : PhraseRole::Base;
        const auto referenceFeel = buildReferenceBoomBapHatFeel(styleInfluence, bar);
        const float refCarrier = referenceCarrierBias(referenceFeel);
        const float refSupport = referenceSupportBias(referenceFeel);
        const float refLoose = referenceLoosenessBias(referenceFeel);
        const float barShape = referenceBarShape(referenceFeel, role, bar);
        const float roleVar = BoomBapPhrasePlanner::roleVariationStrength(role) * style.barVariationAmount;
        const auto* barBlueprint = blueprintBarAt(blueprint, bar);
        const float barHatActivity = barBlueprint != nullptr ? barBlueprint->hatActivity : 0.5f;
        const float barLooseness = barBlueprint != nullptr ? barBlueprint->looseness : 0.35f;
        const float barGrit = barBlueprint != nullptr ? barBlueprint->grit : 0.3f;
        const bool stripToCore = barBlueprint != nullptr && barBlueprint->stripToCore;
        const int barSwingTicks = swingTicksFromBlueprint(barBlueprint);

        for (int step = 0; step < 16; ++step)
        {
            int active = pattern.activeSteps[static_cast<size_t>(step)];
            if (active == 0)
                continue;

            const bool strongGridPoint = (step % 4) == 0;
            if (stripToCore && !strongGridPoint)
            {
                if ((step % 2) == 1)
                    continue;
                if (chance(rng) < 0.66f)
                    continue;
            }

            int finalStep = step;
            if (role == PhraseRole::Ending)
            {
                const int substitution = pattern.substitutionSteps[static_cast<size_t>(step)];
                const float endingLift = barBlueprint != nullptr ? barBlueprint->endLiftAmount : 0.4f;
                if (substitution >= 0 && chance(rng) < std::clamp(0.28f + endingLift * 0.55f, 0.2f, 0.9f))
                    finalStep = substitution;
            }

            float gate = std::clamp(0.24f + baseDensity * 0.52f + roleVar * 0.2f, 0.14f, 1.0f);
            gate *= std::clamp(0.55f + barHatActivity * 0.9f, 0.3f, 1.32f);
            gate *= syncopationGateScale(barBlueprint, step);
            gate = std::clamp(gate * carrierGateScale(style.substyle) * (0.9f + supportAccent * 0.1f), 0.12f, 1.0f);
            if (referenceFeel.available)
            {
                const int slot = std::clamp(step / 2, 0, 7);
                gate *= std::clamp(0.88f + referenceFeel.slotWeight[static_cast<size_t>(slot)] * 0.2f, 0.78f, 1.2f);
                gate *= barShape;
                if (strongGridPoint)
                    gate *= refCarrier;
                else
                    gate *= refSupport;
                if (!strongGridPoint && referenceFeel.gapRatio > 0.5f && referenceFeel.slotWeight[static_cast<size_t>(slot)] < 0.22f)
                    gate *= std::clamp((0.82f + (refLoose - 1.0f) * 0.35f), 0.68f, 1.0f);
            }

            if (style.substyle == BoomBapSubstyle::LofiRap)
            {
                if ((step % 2) == 0)
                    gate = std::clamp((0.72f + baseDensity * 0.20f) * (referenceFeel.available ? std::clamp(refCarrier, 0.86f, 1.12f) : 1.0f), 0.62f, 0.94f);
                else
                    gate = std::clamp((0.04f + baseDensity * 0.22f + roleVar * 0.08f) * (referenceFeel.available ? std::clamp(refSupport * 0.94f, 0.78f, 1.12f) : 1.0f), 0.02f, 0.30f);
            }

            if (tempoBand != TempoBand::Base && (step % 2) == 1)
                gate = std::clamp(gate * (tempoBand == TempoBand::Fast ? 0.52f : 0.68f), 0.08f, 1.0f);
            if (style.substyle == BoomBapSubstyle::Jazzy && (step % 2) == 1)
                gate = std::clamp(gate + 0.08f, 0.08f, 1.0f);
            if (role == PhraseRole::Ending && style.substyle == BoomBapSubstyle::BoomBapGold && step >= 12)
                gate = std::clamp(gate + 0.12f, 0.12f, 1.0f);
            if (chance(rng) > gate)
                continue;

            const int accent = pattern.accentMap[static_cast<size_t>(step)];
            int velocity = std::clamp(accent, style.hatVelocityMin, style.hatVelocityMax);
            if (barBlueprint != nullptr)
            {
                const float gritMul = 1.0f + (barGrit - 0.3f) * 0.26f;
                velocity = std::clamp(static_cast<int>(static_cast<float>(velocity) * gritMul), style.hatVelocityMin, style.hatVelocityMax);
            }

            const float looseScale = std::clamp((0.65f + barLooseness * 0.9f) * (referenceFeel.available ? refLoose : 1.0f), 0.4f, 1.5f);
            int microOffset = static_cast<int>(static_cast<float>(microDist(rng)) * looseScale);
            if ((finalStep % 2) == 1)
                microOffset += barSwingTicks;

            if (style.substyle == BoomBapSubstyle::Jazzy && (finalStep % 4) == 1 && microChance(rng) < 0.42f)
                microOffset += std::max(1, barSwingTicks / 2);

            if (barBlueprint != nullptr && finalStep >= 12)
                microOffset += static_cast<int>(barBlueprint->endLiftAmount * 4.0f);

            microOffset = std::clamp(microOffset, -24, 36);

            track.notes.push_back({
                pitch,
                bar * 16 + finalStep,
                1,
                velocity,
                microOffset,
                false
            });
        }
    }
}
} // namespace bbg
