#include "RapHatGenerator.h"

#include <algorithm>

#include "../../Core/PatternProject.h"
#include "../../Core/TrackRegistry.h"
#include "RapStyleSpec.h"
#include "../HiResTiming.h"
#include "../TempoInterpretation.h"

namespace bbg
{
namespace
{
float oddStepGateForStyle(RapSubstyle substyle)
{
    switch (substyle)
    {
        case RapSubstyle::GermanStreetRap: return 0.14f;
        case RapSubstyle::RussianRap: return 0.18f;
        case RapSubstyle::EastCoast: return 0.22f;
        case RapSubstyle::HardcoreRap: return 0.16f;
        case RapSubstyle::WestCoast: return 0.24f;
        case RapSubstyle::DirtySouthClassic: return 0.26f;
        case RapSubstyle::RnBRap: return 0.28f;
        default: return 0.22f;
    }
}

float supportAccentWeight(const StyleInfluenceState& styleInfluence)
{
    return std::clamp(styleInfluence.supportAccentWeight, 0.65f, 1.45f);
}

float supportLaneActivityWeight(const StyleInfluenceState& styleInfluence)
{
    return std::clamp(laneBiasFor(styleInfluence, TrackType::OpenHat).activityWeight, 0.6f, 1.4f);
}

struct ReferenceRapHatFeel
{
    bool available = false;
    float noteDensity = 0.0f;
    float oddRatio = 0.0f;
    float gapRatio = 0.0f;
    float relaxedRatio = 0.0f;
    std::array<float, 8> slotWeight {};
};

ReferenceRapHatFeel buildReferenceRapHatFeel(const StyleInfluenceState& styleInfluence, int bar)
{
    ReferenceRapHatFeel feel;

    int contributingBars = 0;
    float totalNotes = 0.0f;
    float oddNotes = 0.0f;
    float relaxedNotes = 0.0f;
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
                if ((step16 % 2) == 1)
                    oddNotes += 1.0f;
                if ((step32 % 4) == 1 || (step32 % 4) == 3)
                    relaxedNotes += 1.0f;
            }
            for (size_t i = 0; i < occupiedSlots.size(); ++i)
                if (!occupiedSlots[i])
                    emptySlots += 1.0f;
        }
    }

    if (!feel.available
        && styleInfluence.referenceHatSkeleton.available
        && !styleInfluence.referenceHatSkeleton.barMaps.empty())
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
                if ((step16 % 2) == 1)
                    oddNotes += 0.5f;
            }
            for (const int step32 : barMap.motionSteps32)
            {
                const int step16 = std::clamp(step32 / 2, 0, 15);
                const int slot = std::clamp(step16 / 2, 0, 7);
                occupiedSlots[static_cast<size_t>(slot)] = true;
                feel.slotWeight[static_cast<size_t>(slot)] += 0.6f;
                totalNotes += 0.7f;
                if ((step16 % 2) == 1)
                    oddNotes += 0.7f;
                if ((step32 % 4) == 1 || (step32 % 4) == 3)
                    relaxedNotes += 0.6f;
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
    feel.oddRatio = totalNotes > 0.0f ? oddNotes / totalNotes : 0.0f;
    feel.gapRatio = emptySlots / (static_cast<float>(contributingBars) * 8.0f);
    feel.relaxedRatio = totalNotes > 0.0f ? relaxedNotes / totalNotes : 0.0f;
    return feel;
}

float referenceSubdivisionBias(const ReferenceRapHatFeel& feel)
{
    if (!feel.available)
        return 1.0f;
    return std::clamp(0.88f + feel.oddRatio * 0.46f + std::max(0.0f, feel.noteDensity - 8.0f) * 0.02f, 0.8f, 1.28f);
}

float referenceLoosenessBias(const ReferenceRapHatFeel& feel)
{
    if (!feel.available)
        return 1.0f;
    return std::clamp(0.9f + feel.relaxedRatio * 0.36f + feel.gapRatio * 0.22f, 0.82f, 1.24f);
}

float referenceGapBias(const ReferenceRapHatFeel& feel)
{
    if (!feel.available)
        return 1.0f;
    return std::clamp(0.86f + feel.gapRatio * 0.34f - std::max(0.0f, feel.noteDensity - 10.0f) * 0.025f, 0.72f, 1.16f);
}

float referenceBarMotionShape(const ReferenceRapHatFeel& feel, RapPhraseRole role, int bar)
{
    if (!feel.available)
        return role == RapPhraseRole::Ending ? 1.04f : 1.0f;

    float shape = 1.0f;
    if (feel.oddRatio > 0.34f)
        shape += (bar % 2) == 0 ? 0.04f : -0.02f;
    if (feel.gapRatio > 0.48f)
        shape += (bar % 2) == 1 ? -0.05f : 0.02f;
    if (role == RapPhraseRole::Variation)
        shape += feel.oddRatio > 0.3f ? 0.05f : 0.02f;
    if (role == RapPhraseRole::Ending)
        shape += feel.noteDensity > 8.5f ? 0.05f : 0.02f;
    if (role == RapPhraseRole::Base && feel.gapRatio > 0.52f)
        shape -= 0.04f;
    return std::clamp(shape, 0.86f, 1.16f);
}
}

void RapHatGenerator::generate(TrackState& track,
                               const GeneratorParams& params,
                               const RapStyleProfile& style,
                               const StyleInfluenceState& styleInfluence,
                               const std::vector<RapPhraseRole>& phraseRoles,
                               std::mt19937& rng) const
{
    track.notes.clear();

    const auto* info = TrackRegistry::find(track.type);
    const int pitch = info != nullptr ? info->defaultMidiNote : 42;

    const int bars = std::max(1, params.bars);
    const auto spec = getRapStyleSpec(style.substyle);
    const auto tempoBand = selectTempoBand(params.bpm, params, 120.0f, 140.0f, 100.0f, 128.0f);
    float tempoScale = 1.0f;
    if (tempoBand == TempoBand::Elevated)
        tempoScale = 0.78f;
    else if (tempoBand == TempoBand::Fast)
        tempoScale = 0.62f;
    const float density = std::clamp(rapMix(spec.hatDensityMin, spec.hatDensityMax, params.densityAmount) * tempoScale, 0.08f, 1.0f);
    const float oddGateBase = oddStepGateForStyle(style.substyle);
    const float supportAccent = supportAccentWeight(styleInfluence);
    const float supportLaneActivity = supportLaneActivityWeight(styleInfluence);

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    std::uniform_int_distribution<int> vel(style.hatVelocityMin, style.hatVelocityMax);
    std::uniform_int_distribution<int> ghostVel(30, 54);

    for (int bar = 0; bar < bars; ++bar)
    {
        const auto role = bar < static_cast<int>(phraseRoles.size()) ? phraseRoles[static_cast<size_t>(bar)] : RapPhraseRole::Base;
        const auto referenceFeel = buildReferenceRapHatFeel(styleInfluence, bar);
        const float refSubdivision = referenceSubdivisionBias(referenceFeel);
        const float refLoose = referenceLoosenessBias(referenceFeel);
        const float refGap = referenceGapBias(referenceFeel);
        const float barShape = referenceBarMotionShape(referenceFeel, role, bar);
        const float roleBoost = RapPhrasePlanner::roleVariationStrength(role);

        for (int step = 0; step < 16; ++step)
        {
            bool place = false;
            if ((step % 2) == 0)
            {
                const int slot = std::clamp(step / 2, 0, 7);
                float gate = ((tempoBand == TempoBand::Base ? 0.78f : 0.68f) + density * 0.22f) * (0.92f + supportLaneActivity * 0.08f);
                if (referenceFeel.available)
                {
                    gate *= std::clamp(0.9f + referenceFeel.slotWeight[static_cast<size_t>(slot)] * 0.18f, 0.82f, 1.18f);
                    gate *= std::clamp(0.94f + (refLoose - 1.0f) * 0.25f, 0.86f, 1.08f);
                    gate *= barShape;
                }
                place = chance(rng) < std::clamp(gate, 0.50f, 0.995f);
            }
            else
            {
                const int slot = std::clamp(step / 2, 0, 7);
                float gate = (oddGateBase * density + roleBoost * 0.10f) * supportAccent * supportLaneActivity;
                if (referenceFeel.available)
                {
                    gate *= std::clamp(0.88f + referenceFeel.slotWeight[static_cast<size_t>(slot)] * 0.26f, 0.76f, 1.22f);
                    gate *= refSubdivision;
                    gate *= std::clamp(0.92f + (refLoose - 1.0f) * 0.45f, 0.82f, 1.14f);
                    gate *= barShape;
                    if (referenceFeel.gapRatio > 0.5f && referenceFeel.slotWeight[static_cast<size_t>(slot)] < 0.2f)
                        gate *= std::clamp(refGap * 0.86f, 0.66f, 1.0f);
                }
                place = chance(rng) < std::clamp(gate, 0.02f, 0.68f);
            }

            if (!place)
                continue;

            int v = vel(rng);
            if ((step % 4) == 0)
                v = std::min(style.hatVelocityMax, v + 6);
            if (role == RapPhraseRole::Ending && step >= 12)
                v = std::min(style.hatVelocityMax, v + 4);

            const bool ghost = (step % 2) == 1 && chance(rng) < 0.24f;
            if (ghost)
                v = std::min(v, ghostVel(rng));

            track.notes.push_back({ pitch, bar * 16 + step, 1, v, 0, ghost });
        }
    }
}
} // namespace bbg
