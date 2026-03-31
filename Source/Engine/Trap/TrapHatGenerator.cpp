#include "TrapHatGenerator.h"

#include <algorithm>
#include <array>

#include "../../Core/PatternProject.h"
#include "../../Core/TrackRegistry.h"
#include "../HiResTiming.h"
#include "../TempoInterpretation.h"

namespace bbg
{
namespace
{
struct TrapHatMotifTemplate
{
    std::array<float, 8> slotBias {};
    std::array<float, 8> ghostBias {};
    std::array<float, 8> tripletBias {};
};

TrapHatMotifTemplate buildMotifTemplate(TrapSubstyle substyle, std::mt19937& rng)
{
    TrapHatMotifTemplate motif;
    std::uniform_real_distribution<float> spread(0.0f, 1.0f);

    for (int i = 0; i < 8; ++i)
    {
        float slot = 0.88f + spread(rng) * 0.32f;
        if ((i % 2) == 0)
            slot += 0.08f;

        float ghost = 0.88f + spread(rng) * 0.26f;
        float trip = 0.84f + spread(rng) * 0.32f;

        if (substyle == TrapSubstyle::DarkTrap || substyle == TrapSubstyle::LuxuryTrap)
        {
            if ((i % 4) == 1)
                slot *= 0.86f;
            trip *= 0.88f;
        }
        else if (substyle == TrapSubstyle::RageTrap)
        {
            if ((i % 4) == 3)
                slot *= 1.1f;
            ghost *= 1.08f;
            trip *= 1.1f;
        }

        motif.slotBias[static_cast<size_t>(i)] = std::clamp(slot, 0.72f, 1.34f);
        motif.ghostBias[static_cast<size_t>(i)] = std::clamp(ghost, 0.74f, 1.3f);
        motif.tripletBias[static_cast<size_t>(i)] = std::clamp(trip, 0.72f, 1.34f);
    }

    return motif;
}

float motifLookup(const std::array<float, 8>& values,
                  int slot,
                  int bar,
                  TrapPhraseRole role)
{
    int index = slot;
    if (role == TrapPhraseRole::Lift)
        index = (slot + 1) % 8;
    else if (role == TrapPhraseRole::Ending)
        index = (slot + 2) % 8;
    else if ((bar & 1) == 1)
        index = (slot + 7) % 8;

    return values[static_cast<size_t>(index)];
}

float tripletBias(TrapSubstyle substyle)
{
    switch (substyle)
    {
        case TrapSubstyle::ATLClassic: return 1.0f;
        case TrapSubstyle::DarkTrap: return 0.72f;
        case TrapSubstyle::CloudTrap: return 1.08f;
        case TrapSubstyle::RageTrap: return 1.06f;
        case TrapSubstyle::MemphisTrap: return 1.14f;
        case TrapSubstyle::LuxuryTrap: return 0.86f;
        default: return 1.0f;
    }
}

float baseCarrierBias(TrapSubstyle substyle)
{
    switch (substyle)
    {
        case TrapSubstyle::ATLClassic: return 1.0f;
        case TrapSubstyle::DarkTrap: return 0.84f;
        case TrapSubstyle::CloudTrap: return 0.9f;
        case TrapSubstyle::RageTrap: return 1.14f;
        case TrapSubstyle::MemphisTrap: return 0.94f;
        case TrapSubstyle::LuxuryTrap: return 0.88f;
        default: return 1.0f;
    }
}

int carrierDivisionForSpec(const TrapStyleSpec& spec, float density)
{
    switch (spec.hatDensityMode)
    {
        case TrapHatDensityMode::Sparse:
            return density > 0.56f ? HiResTiming::kTicks1_16 : HiResTiming::kTicks1_8;
        case TrapHatDensityMode::Dense:
            return HiResTiming::kTicks1_16;
        case TrapHatDensityMode::Medium:
        default:
            return density > 0.66f ? HiResTiming::kTicks1_16 : HiResTiming::kTicks1_8;
    }
}

int maxBaseNotesPerBar(const TrapStyleSpec& spec)
{
    switch (spec.hatDensityMode)
    {
        case TrapHatDensityMode::Sparse: return 14;
        case TrapHatDensityMode::Dense: return 32;
        case TrapHatDensityMode::Medium:
        default:
            return 22;
    }
}

int accentVelocity(std::mt19937& rng)
{
    std::uniform_int_distribution<int> accentVel(85, 96);
    return accentVel(rng);
}

int baseVelocity(std::mt19937& rng)
{
    std::uniform_int_distribution<int> baseVel(58, 84);
    return baseVel(rng);
}

int ghostVelocity(std::mt19937& rng)
{
    std::uniform_int_distribution<int> ghostVel(35, 55);
    return ghostVel(rng);
}

int carrierJitter(TrapSubstyle substyle,
                  TrapPhraseRole role,
                  bool isBeatAnchor,
                  std::mt19937& rng)
{
    int minJ = -3;
    int maxJ = 4;

    switch (substyle)
    {
        case TrapSubstyle::ATLClassic: minJ = -3; maxJ = 4; break;
        case TrapSubstyle::DarkTrap: minJ = -2; maxJ = 4; break;
        case TrapSubstyle::CloudTrap: minJ = -4; maxJ = 5; break;
        case TrapSubstyle::RageTrap: minJ = -2; maxJ = 3; break;
        case TrapSubstyle::MemphisTrap: minJ = -4; maxJ = 6; break;
        case TrapSubstyle::LuxuryTrap: minJ = -2; maxJ = 2; break;
        default: break;
    }

    if (isBeatAnchor)
    {
        minJ = std::max(minJ, -2);
        maxJ = std::min(maxJ, 2);
    }

    if (role == TrapPhraseRole::Ending)
    {
        minJ = std::max(minJ, -2);
        maxJ = std::min(maxJ + 1, 5);
    }

    std::uniform_int_distribution<int> dist(minJ, maxJ);
    return dist(rng);
}

int smoothCarrierVelocity(int desired,
                         int previous,
                         bool isBeatAnchor,
                         TrapSubstyle substyle)
{
    if (previous < 0)
        return desired;

    int downLimit = previous - 18;
    int upLimit = previous + 16;
    if (substyle == TrapSubstyle::LuxuryTrap)
    {
        downLimit = previous - 12;
        upLimit = previous + 10;
    }
    else if (substyle == TrapSubstyle::MemphisTrap)
    {
        downLimit = previous - 22;
        upLimit = previous + 20;
    }

    int smoothed = std::clamp(desired, downLimit, upLimit);
    if (isBeatAnchor)
        smoothed = std::max(smoothed, previous - 2);

    return smoothed;
}

float hiHatActivityWeight(const StyleInfluenceState& styleInfluence)
{
    return std::clamp(laneBiasFor(styleInfluence, TrackType::HiHat).activityWeight, 0.55f, 1.55f);
}

float bounceWeight(const StyleInfluenceState& styleInfluence)
{
    return std::clamp(styleInfluence.bounceWeight, 0.65f, 1.5f);
}

struct ReferenceTrapHatFeel
{
    bool available = false;
    float noteDensity = 0.0f;
    float subdivision32Ratio = 0.0f;
    float tripletRatio = 0.0f;
    float burstRatio = 0.0f;
    float gapRatio = 0.0f;
    float anchorRatio = 0.0f;
    std::array<float, 8> slotWeight {};
};

ReferenceTrapHatFeel buildReferenceTrapHatFeel(const StyleInfluenceState& styleInfluence, int bar)
{
    ReferenceTrapHatFeel feel;

    int contributingBars = 0;
    float totalNotes = 0.0f;
    float total32 = 0.0f;
    float totalTriplets = 0.0f;
    float totalBursts = 0.0f;
    float totalAnchors = 0.0f;
    float totalGapSlots = 0.0f;

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
                const int slot = std::clamp(step32 / 4, 0, 7);
                occupiedSlots[static_cast<size_t>(slot)] = true;
                feel.slotWeight[static_cast<size_t>(slot)] += 1.0f;
                totalNotes += 1.0f;
                if ((step32 % 2) == 1)
                    total32 += 1.0f;
                if ((step32 % 8) == 0)
                    totalAnchors += 1.0f;
            }

            for (const auto& cluster : variant.rollClusters)
            {
                if (cluster.barIndex != normalizedBar)
                    continue;
                totalBursts += cluster.noteCount >= 4 ? 1.0f : 0.6f;
            }
            for (const auto& cluster : variant.tripletClusters)
            {
                if (cluster.barIndex != normalizedBar)
                    continue;
                totalTriplets += cluster.noteCount >= 3 ? 1.0f : 0.6f;
            }

            for (size_t i = 0; i < occupiedSlots.size(); ++i)
                if (!occupiedSlots[i])
                    totalGapSlots += 1.0f;
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
                totalNotes += 1.0f;
                totalAnchors += (step16 % 4) == 0 ? 1.0f : 0.0f;
            }
            for (const int step32 : barMap.motionSteps32)
            {
                const int slot = std::clamp(step32 / 4, 0, 7);
                occupiedSlots[static_cast<size_t>(slot)] = true;
                feel.slotWeight[static_cast<size_t>(slot)] += 0.55f;
                totalNotes += 0.7f;
                if ((step32 % 2) == 1)
                    total32 += 0.7f;
            }
            for (size_t i = 0; i < occupiedSlots.size(); ++i)
                if (!occupiedSlots[i])
                    totalGapSlots += 1.0f;

            for (const auto& cluster : skeleton.rollClusters)
                if (cluster.barIndex == normalizedBar)
                    totalBursts += cluster.noteCount >= 4 ? 1.0f : 0.6f;
            for (const auto& cluster : skeleton.tripletClusters)
                if (cluster.barIndex == normalizedBar)
                    totalTriplets += cluster.noteCount >= 3 ? 1.0f : 0.6f;
        }
    }

    if (contributingBars <= 0)
        return feel;

    feel.available = true;
    const float invBars = 1.0f / static_cast<float>(contributingBars);
    for (auto& slot : feel.slotWeight)
        slot *= invBars;
    feel.noteDensity = totalNotes * invBars;
    feel.subdivision32Ratio = totalNotes > 0.0f ? total32 / totalNotes : 0.0f;
    feel.tripletRatio = totalTriplets * invBars;
    feel.burstRatio = totalBursts * invBars;
    feel.gapRatio = totalGapSlots / (static_cast<float>(contributingBars) * 8.0f);
    feel.anchorRatio = totalNotes > 0.0f ? totalAnchors / totalNotes : 0.0f;
    return feel;
}

float referenceSubdivisionBias(const ReferenceTrapHatFeel& feel)
{
    if (!feel.available)
        return 1.0f;
    return std::clamp(0.9f + feel.subdivision32Ratio * 0.55f + std::max(0.0f, feel.noteDensity - 8.0f) * 0.025f,
                      0.82f,
                      1.35f);
}

float referenceBounceBias(const ReferenceTrapHatFeel& feel)
{
    if (!feel.available)
        return 1.0f;
    return std::clamp(0.9f + feel.burstRatio * 0.2f + feel.tripletRatio * 0.16f + feel.subdivision32Ratio * 0.14f,
                      0.82f,
                      1.34f);
}

float referenceGapBias(const ReferenceTrapHatFeel& feel)
{
    if (!feel.available)
        return 1.0f;
    return std::clamp(0.86f + feel.gapRatio * 0.36f - std::max(0.0f, feel.noteDensity - 10.0f) * 0.02f,
                      0.74f,
                      1.18f);
}

float referenceBarMotionShape(const ReferenceTrapHatFeel& feel, TrapPhraseRole role, int bar)
{
    if (!feel.available)
        return role == TrapPhraseRole::Ending ? 1.04f : 1.0f;

    float shape = 1.0f;
    if (feel.burstRatio > 0.22f || feel.subdivision32Ratio > 0.42f)
        shape += (bar % 2) == 0 ? 0.05f : -0.02f;
    if (feel.gapRatio > 0.48f)
        shape += (bar % 2) == 1 ? -0.05f : 0.02f;
    if (role == TrapPhraseRole::Lift)
        shape += feel.burstRatio > 0.14f ? 0.06f : 0.02f;
    if (role == TrapPhraseRole::Ending)
        shape += feel.tripletRatio > 0.18f ? 0.07f : 0.03f;
    if (role == TrapPhraseRole::Base && feel.gapRatio > 0.5f)
        shape -= 0.05f;
    return std::clamp(shape, 0.84f, 1.18f);
}
}

void TrapHatGenerator::generate(TrackState& track,
                                const GeneratorParams& params,
                                const TrapStyleProfile& style,
                                const StyleInfluenceState& styleInfluence,
                                const std::vector<TrapPhraseRole>& phrase,
                                std::mt19937& rng) const
{
    track.notes.clear();

    const auto* info = TrackRegistry::find(track.type);
    const int pitch = info != nullptr ? info->defaultMidiNote : 42;
    const auto spec = getTrapStyleSpec(style.substyle);

    const int bars = std::max(1, params.bars);
    const auto tempoBand = selectTempoBand(params.bpm, params, 120.0f, 145.0f, 100.0f, 132.0f);
    float tempoScale = 1.0f;
    if (tempoBand == TempoBand::Elevated)
        tempoScale = 0.82f;
    else if (tempoBand == TempoBand::Fast)
        tempoScale = 0.68f;
    const float unclampedDensity = params.densityAmount * style.hatDensityBias * tempoScale;
    const float hatActivity = hiHatActivityWeight(styleInfluence);
    const float density = std::clamp(unclampedDensity * hatActivity, spec.hatDensityMin, spec.hatDensityMax);
    const float triBias = tripletBias(style.substyle);
    const float bounce = bounceWeight(styleInfluence);
    const float carrierBias = baseCarrierBias(style.substyle) * (0.9f + bounce * 0.1f);
    const auto motif = buildMotifTemplate(style.substyle, rng);

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);

    int carrierDivision = carrierDivisionForSpec(spec, density);
    if (hatActivity <= 0.85f)
        carrierDivision = HiResTiming::kTicks1_8;
    else if (hatActivity >= 1.18f && spec.hatDensityMode != TrapHatDensityMode::Sparse)
        carrierDivision = HiResTiming::kTicks1_16;
    if (tempoBand != TempoBand::Base && style.substyle != TrapSubstyle::RageTrap)
        carrierDivision = HiResTiming::kTicks1_8;

    const int move32 = HiResTiming::kTicks1_32;
    const int tripletDiv = HiResTiming::kTicks1_24;

    for (int bar = 0; bar < bars; ++bar)
    {
        const auto role = bar < static_cast<int>(phrase.size()) ? phrase[static_cast<size_t>(bar)] : TrapPhraseRole::Base;
        const auto referenceFeel = buildReferenceTrapHatFeel(styleInfluence, bar);
        const float refSubdivision = referenceSubdivisionBias(referenceFeel);
        const float refBounce = referenceBounceBias(referenceFeel);
        const float refGap = referenceGapBias(referenceFeel);
        const float barMotionShape = referenceBarMotionShape(referenceFeel, role, bar);
        const float roleBoost = TrapPhrasePlanner::roleVariationStrength(role);
        const int barStart = bar * HiResTiming::kTicksPerBar4_4;
        int notesInBar = 0;
        const int maxNotes = std::max(6,
                          static_cast<int>(std::round(static_cast<float>(maxBaseNotesPerBar(spec))
                                      * std::clamp((0.55f + density * 0.55f + (bounce - 1.0f) * 0.25f)
                                               * std::clamp(0.94f + (refSubdivision - 1.0f) * 0.5f + (refBounce - 1.0f) * 0.4f, 0.86f, 1.18f),
                                               0.42f,
                                               1.45f))));
        int lastCarrierVel = -1;

        for (int t = 0; t < HiResTiming::kTicksPerBar4_4; t += carrierDivision)
        {
            if (notesInBar >= maxNotes)
                break;

            const int stepInBar = t / HiResTiming::kTicks1_16;
            const bool isBeatAnchor = (t % HiResTiming::kTicks1_8) == 0;
            const int motifSlot = std::clamp(stepInBar / 2, 0, 7);
            const float slotReference = referenceFeel.available
                ? std::clamp(0.84f + referenceFeel.slotWeight[static_cast<size_t>(motifSlot)] * 0.28f, 0.76f, 1.26f)
                : 1.0f;
            float gate = 0.62f + density * 0.3f;
            if (isBeatAnchor)
                gate += 0.18f;
            if (role == TrapPhraseRole::Lift)
                gate += 0.06f;
            if (role == TrapPhraseRole::Ending && stepInBar >= 12)
                gate += roleBoost * 0.08f;
            gate *= carrierBias;
            gate *= motifLookup(motif.slotBias, motifSlot, bar, role);
            gate *= slotReference;
            gate *= barMotionShape;

            if (!isBeatAnchor && carrierDivision == HiResTiming::kTicks1_16)
                gate *= std::clamp(refSubdivision, 0.86f, 1.28f);
            if (!isBeatAnchor && (stepInBar % 2) == 1)
                gate *= std::clamp(0.9f + (refBounce - 1.0f) * 0.75f, 0.82f, 1.22f);

            // Sparse styles intentionally leave breathing pockets.
            if (spec.hatDensityMode == TrapHatDensityMode::Sparse && !isBeatAnchor && (stepInBar % 4) == 2)
                gate *= 0.64f;

            if (!isBeatAnchor && referenceFeel.available && referenceFeel.gapRatio > 0.42f && referenceFeel.slotWeight[static_cast<size_t>(motifSlot)] < 0.22f)
                gate *= std::clamp(refGap * 0.86f, 0.68f, 1.0f);

            // Keep tiny omissions in the carrier so HatFX can become the energy lane.
            if (!isBeatAnchor && chance(rng) < 0.07f)
                gate *= 0.4f;

            if (chance(rng) > std::clamp(gate, 0.2f, 0.99f))
                continue;

            int v = baseVelocity(rng);
            if (isBeatAnchor)
                v = std::max(v, accentVelocity(rng));

            if (!isBeatAnchor && carrierDivision == HiResTiming::kTicks1_16)
            {
                // Light hand-alternation feel to avoid robotic same-level runs.
                v += ((stepInBar & 1) == 0) ? 2 : -4;
            }

            v = smoothCarrierVelocity(v, lastCarrierVel, isBeatAnchor, style.substyle);
            v = std::clamp(v, style.hatVelocityMin, style.hatVelocityMax);
            lastCarrierVel = v;

            HiResTiming::addNoteAtTick(track,
                                       pitch,
                                       barStart + t + carrierJitter(style.substyle, role, isBeatAnchor, rng),
                                       v,
                                       false,
                                       bars);
            ++notesInBar;

            // Small doubles keep the base moving without turning it into a burst lane.
            float tinyDoubleGate = (0.05f + density * 0.1f) * (0.86f + bounce * 0.14f);
            if (spec.hatDensityMode == TrapHatDensityMode::Dense)
                tinyDoubleGate += 0.04f;
            tinyDoubleGate *= motifLookup(motif.ghostBias, motifSlot, bar, role);
            tinyDoubleGate *= std::clamp(refSubdivision * 0.9f + (refBounce - 1.0f) * 0.35f, 0.7f, 1.32f);
            if (referenceFeel.available && referenceFeel.gapRatio > 0.5f && referenceFeel.slotWeight[static_cast<size_t>(motifSlot)] < 0.24f)
                tinyDoubleGate *= 0.74f;
            if (chance(rng) < std::clamp(tinyDoubleGate, 0.01f, 0.26f))
            {
                const int tick = barStart + t + move32;
                HiResTiming::addNoteAtTick(track,
                                           pitch,
                                           tick,
                                           std::clamp(ghostVelocity(rng), 28, std::min(72, style.hatVelocityMax)),
                                           true,
                                           bars);
                ++notesInBar;
            }

            // Light triplet glides belong to base for Cloud/Memphis flavor, not full rolls.
            float triGate = (0.03f + density * 0.07f) * triBias;
            if (role == TrapPhraseRole::Lift)
                triGate += 0.05f;
            triGate *= motifLookup(motif.tripletBias, motifSlot, bar, role);
            triGate *= std::clamp(0.88f + referenceFeel.tripletRatio * 0.26f + (barMotionShape - 1.0f) * 0.55f, 0.72f, 1.34f);
            if (referenceFeel.available && referenceFeel.gapRatio > 0.54f && referenceFeel.tripletRatio < 0.12f)
                triGate *= 0.7f;
            if (chance(rng) < std::clamp(triGate, 0.0f, 0.24f))
            {
                const int tick = barStart + HiResTiming::quantizeTicks(t + tripletDiv, tripletDiv);
                HiResTiming::addNoteAtTick(track,
                                           pitch,
                                           tick,
                                           std::clamp(ghostVelocity(rng), 28, std::min(72, style.hatVelocityMax)),
                                           true,
                                           bars);
                ++notesInBar;
            }
        }

        // Phrase punctuation in base lane stays short and readable.
        if (role == TrapPhraseRole::Ending && notesInBar < maxNotes)
        {
            const int punctTick = barStart + HiResTiming::kTicksPerBar4_4 - HiResTiming::kTicks1_16;
            HiResTiming::addNoteAtTick(track,
                                       pitch,
                                       punctTick,
                                       std::clamp(accentVelocity(rng), style.hatVelocityMin, style.hatVelocityMax),
                                       false,
                                       bars);
        }
    }
}
} // namespace bbg
