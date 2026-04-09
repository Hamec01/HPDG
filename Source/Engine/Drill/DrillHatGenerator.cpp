#include "DrillHatGenerator.h"

#include <algorithm>
#include <array>

#include "../../Core/TrackRegistry.h"
#include "../HiResTiming.h"
#include "../StyleDefaults.h"

namespace bbg
{
namespace
{
struct DrillHatSubstyleBias
{
    float densityBias = 1.0f;
    float tripletBias = 1.0f;
    float burstBias = 1.0f;
    float silenceBias = 1.0f;
    float transitionBias = 1.0f;
    int sparseMaxNotes = 8;
    int mediumMaxNotes = 11;
    int denseMaxNotes = 14;
};

struct DrillPhraseMotif
{
    std::array<float, 8> accentBias {};
    std::array<float, 8> subdivisionBias {};
    std::array<float, 8> tripletBias {};
    std::array<float, 8> silenceBias {};
};

struct PlannedHatNote
{
    int tick = 0;
    int velocity = 80;
    juce::String semanticRole;
    int priority = 0;
    bool preserve = false;
};

struct ReferenceDrillHatFeel
{
    bool available = false;
    float noteDensity = 0.0f;
    float tripletRatio = 0.0f;
    float burstRatio = 0.0f;
    float gapRatio = 0.0f;
    float anchorRatio = 0.0f;
    std::array<float, 8> slotWeight {};
    std::array<float, 32> stepWeight {};
    std::array<float, 32> stepVelocitySum {};
    std::array<float, 32> stepVelocityWeight {};
    std::array<float, 32> tripletStepWeight {};
    std::array<float, 32> burstStepWeight {};
};

struct ReferenceDrillHatPattern
{
    bool available = false;
    std::vector<ReferenceHatNote> notes;
};

float clampUnit(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

int positiveModulo(int value, int modulus)
{
    if (modulus <= 0)
        return 0;

    const int remainder = value % modulus;
    return remainder < 0 ? remainder + modulus : remainder;
}

size_t rotatedReferenceVariantStartIndex(size_t variantCount, int selectionSeed, int barIndex)
{
    if (variantCount == 0)
        return 0;

    return static_cast<size_t>(positiveModulo(selectionSeed + barIndex * 7, static_cast<int>(variantCount)));
}

float normalizedActivityWeight(const PatternProject& project)
{
    const float weight = std::clamp(laneBiasFor(project.styleInfluence, TrackType::HiHat).activityWeight, 0.55f, 1.6f);
    return clampUnit((weight - 0.55f) / 1.05f);
}

float normalizedStyleWeight(float value)
{
    return clampUnit(std::clamp(value, 0.0f, 2.0f) * 0.5f);
}

DrillHatSubstyleBias substyleBiasFor(int substyleIndex)
{
    juce::ignoreUnused(substyleIndex);
    DrillHatSubstyleBias bias;
    bias.densityBias = 1.0f;
    bias.tripletBias = 1.0f;
    bias.burstBias = 0.92f;
    bias.silenceBias = 0.90f;
    bias.transitionBias = 1.08f;
    bias.sparseMaxNotes = 8;
    bias.mediumMaxNotes = 11;
    bias.denseMaxNotes = 14;
    return bias;
}

DrillPhraseMotif buildPhraseMotif(std::mt19937& rng)
{
    DrillPhraseMotif motif;
    std::uniform_real_distribution<float> spread(0.0f, 1.0f);

    for (int slot = 0; slot < 8; ++slot)
    {
        float accent = 0.82f + spread(rng) * 0.30f;
        float subdivision = 0.68f + spread(rng) * 0.36f;
        float triplet = 0.64f + spread(rng) * 0.40f;
        float silence = 0.58f + spread(rng) * 0.38f;

        if ((slot % 4) == 0)
            accent += 0.16f;
        if ((slot % 2) == 1)
            subdivision += 0.10f;
        if (slot >= 5)
            triplet += 0.14f;
        if (slot == 1 || slot == 4)
            silence += 0.12f;

        motif.accentBias[static_cast<size_t>(slot)] = std::clamp(accent, 0.72f, 1.34f);
        motif.subdivisionBias[static_cast<size_t>(slot)] = std::clamp(subdivision, 0.62f, 1.36f);
        motif.tripletBias[static_cast<size_t>(slot)] = std::clamp(triplet, 0.58f, 1.38f);
        motif.silenceBias[static_cast<size_t>(slot)] = std::clamp(silence, 0.52f, 1.42f);
    }

    return motif;
}

float motifLookup(const std::array<float, 8>& table,
                  int slot,
                  int barIndex,
                  DrillPhraseBarRole role)
{
    int index = std::clamp(slot, 0, 7);
    if (role == DrillPhraseBarRole::Response)
        index = (index + 1) % 8;
    else if (role == DrillPhraseBarRole::Lift)
        index = (index + 2) % 8;
    else if (role == DrillPhraseBarRole::Release)
        index = (index + 3) % 8;
    else if ((barIndex & 1) == 1)
        index = (index + 7) % 8;

    return table[static_cast<size_t>(index)];
}

int maxNotesForDensity(DrillHatDensityIntent density, const DrillHatSubstyleBias& bias)
{
    switch (density)
    {
        case DrillHatDensityIntent::Sparse: return bias.sparseMaxNotes;
        case DrillHatDensityIntent::Dense: return bias.denseMaxNotes;
        case DrillHatDensityIntent::Medium:
        default:
            return bias.mediumMaxNotes;
    }
}

bool isTransitionCarrier(const DrillPhraseBarPlan& bar, int carrierStep)
{
    const int snareAnchor = bar.anchorMap.snareAnchorSteps[0];
    return bar.isPhraseEnd || bar.role == DrillPhraseBarRole::Lift || carrierStep >= 12
        || (snareAnchor >= 0 && carrierStep >= (snareAnchor - 3));
}

bool withinBar(int tick, int barStartTick)
{
    return tick >= barStartTick && tick < (barStartTick + HiResTiming::kTicksPerBar4_4);
}

int motionJitterTicks(const PatternProject& project,
                      bool transitionNote,
                      std::mt19937& rng)
{
    const float motion = clampUnit(0.34f * clampUnit(project.params.timingAmount)
                                   + 0.46f * normalizedStyleWeight(project.styleInfluence.hatMotionWeight));
    const int span = static_cast<int>(std::round(4.0f + 10.0f * motion));
    const int minJitter = transitionNote ? -2 : -span / 2;
    const int maxJitter = transitionNote ? span + 4 : span;
    std::uniform_int_distribution<int> dist(minJitter, maxJitter);
    return dist(rng);
}

int carrierVelocity(const PatternProject& project,
                    const DrillPhraseBarPlan& bar,
                    int slot,
                    int stepInBar,
                    const DrillPhraseMotif& motif,
                    std::mt19937& rng)
{
    const float accentWeight = normalizedStyleWeight(project.styleInfluence.drillHatAccentPatternWeight);
    const float accentBias = motifLookup(motif.accentBias, slot, bar.barIndex, bar.role);
    const bool strongBeat = (stepInBar % 4) == 0;
    std::uniform_int_distribution<int> spread(0, 5);
    float value = 76.0f + 12.0f * accentWeight + 6.0f * (accentBias - 0.8f);
    if (strongBeat)
        value += 6.0f;
    if (bar.role == DrillPhraseBarRole::Lift || bar.role == DrillPhraseBarRole::Release)
        value += 2.0f;

    return std::clamp(static_cast<int>(std::round(value)) + spread(rng), 72, 108);
}

int subdivisionVelocity(const PatternProject& project,
                        bool transitionNote,
                        std::mt19937& rng)
{
    const float accentWeight = normalizedStyleWeight(project.styleInfluence.drillHatAccentPatternWeight);
    const int base = transitionNote ? 70 : 58;
    const int max = transitionNote ? 90 : 78;
    std::uniform_int_distribution<int> spread(base, max);
    return std::clamp(spread(rng) + static_cast<int>(std::round(accentWeight * 4.0f)), 44, 96);
}

void addCandidate(std::vector<PlannedHatNote>& notes,
                  int tick,
                  int velocity,
                  const juce::String& semanticRole,
                  int priority,
                  bool preserve)
{
    notes.push_back({ tick, velocity, semanticRole, priority, preserve });
}

void dedupeByTick(std::vector<PlannedHatNote>& notes)
{
    std::sort(notes.begin(), notes.end(), [](const PlannedHatNote& lhs, const PlannedHatNote& rhs)
    {
        if (lhs.tick != rhs.tick)
            return lhs.tick < rhs.tick;
        if (lhs.preserve != rhs.preserve)
            return lhs.preserve > rhs.preserve;
        if (lhs.priority != rhs.priority)
            return lhs.priority > rhs.priority;
        return lhs.velocity > rhs.velocity;
    });

    notes.erase(std::unique(notes.begin(), notes.end(), [](const PlannedHatNote& lhs, const PlannedHatNote& rhs)
    {
        return lhs.tick == rhs.tick;
    }), notes.end());
}

void trimToMax(std::vector<PlannedHatNote>& notes, int maxNotes)
{
    if (static_cast<int>(notes.size()) <= maxNotes)
        return;

    std::vector<PlannedHatNote> preserve;
    std::vector<PlannedHatNote> extras;
    preserve.reserve(notes.size());
    extras.reserve(notes.size());

    for (const auto& note : notes)
    {
        if (note.preserve)
            preserve.push_back(note);
        else
            extras.push_back(note);
    }

    std::sort(extras.begin(), extras.end(), [](const PlannedHatNote& lhs, const PlannedHatNote& rhs)
    {
        if (lhs.priority != rhs.priority)
            return lhs.priority > rhs.priority;
        if (lhs.velocity != rhs.velocity)
            return lhs.velocity > rhs.velocity;
        return lhs.tick < rhs.tick;
    });

    const int remaining = std::max(0, maxNotes - static_cast<int>(preserve.size()));
    if (static_cast<int>(extras.size()) > remaining)
        extras.resize(static_cast<size_t>(remaining));

    notes = preserve;
    notes.insert(notes.end(), extras.begin(), extras.end());
    std::sort(notes.begin(), notes.end(), [](const PlannedHatNote& lhs, const PlannedHatNote& rhs)
    {
        if (lhs.tick != rhs.tick)
            return lhs.tick < rhs.tick;
        if (lhs.preserve != rhs.preserve)
            return lhs.preserve > rhs.preserve;
        return lhs.priority > rhs.priority;
    });

}

void accumulateReferenceHatStep(ReferenceDrillHatFeel& feel,
                                int step32,
                                int velocity,
                                float weight,
                                float& totalNotes,
                                float& totalAnchors,
                                std::array<bool, 8>& occupiedSlots)
{
    const int clampedStep = std::clamp(step32, 0, 31);
    const int slot = std::clamp(clampedStep / 4, 0, 7);

    occupiedSlots[static_cast<size_t>(slot)] = true;
    feel.slotWeight[static_cast<size_t>(slot)] += weight;
    feel.stepWeight[static_cast<size_t>(clampedStep)] += weight;
    feel.stepVelocitySum[static_cast<size_t>(clampedStep)] += static_cast<float>(velocity) * weight;
    feel.stepVelocityWeight[static_cast<size_t>(clampedStep)] += weight;
    totalNotes += weight;
    if ((clampedStep % 8) == 0)
        totalAnchors += weight;
}

void accumulateClusterWeight(std::array<float, 32>& weights,
                             const ReferenceHatCluster& cluster,
                             float amount)
{
    const int start = std::clamp(cluster.startStep32, 0, 31);
    const int end = std::clamp(cluster.endStep32, start, 31);
    for (int step = start; step <= end; ++step)
        weights[static_cast<size_t>(step)] += amount;
}

void sortAndUniqueReferenceNotes(std::vector<ReferenceHatNote>& notes)
{
    std::sort(notes.begin(), notes.end(), [](const ReferenceHatNote& lhs, const ReferenceHatNote& rhs)
    {
        if (lhs.tickInBar != rhs.tickInBar)
            return lhs.tickInBar < rhs.tickInBar;
        return lhs.velocity > rhs.velocity;
    });

    notes.erase(std::unique(notes.begin(), notes.end(), [](const ReferenceHatNote& lhs, const ReferenceHatNote& rhs)
    {
        return lhs.tickInBar == rhs.tickInBar;
    }), notes.end());
}

bool loadReferencePatternFromSkeleton(const ReferenceHatSkeleton& skeleton,
                                      int bar,
                                      ReferenceDrillHatPattern& pattern)
{
    if (!skeleton.available || skeleton.barMaps.empty())
        return false;

    const int sourceBars = std::max(1, skeleton.sourceBars > 0 ? skeleton.sourceBars : static_cast<int>(skeleton.barMaps.size()));
    const int normalizedBar = ((bar % sourceBars) + sourceBars) % sourceBars;
    if (normalizedBar < 0 || normalizedBar >= static_cast<int>(skeleton.barMaps.size()))
        return false;

    const auto& barMap = skeleton.barMaps[static_cast<size_t>(normalizedBar)];
    pattern.notes.clear();
    pattern.notes.reserve(barMap.notes.size() + barMap.backboneSteps16.size() + barMap.motionSteps32.size());

    for (const auto& note : barMap.notes)
        pattern.notes.push_back({ std::clamp(note.tickInBar, 0, HiResTiming::kTicksPerBar4_4 - 1), note.velocity });

    if (pattern.notes.empty())
    {
        for (const int step16 : barMap.backboneSteps16)
            pattern.notes.push_back({ std::clamp(step16 * HiResTiming::kTicks1_16, 0, HiResTiming::kTicksPerBar4_4 - 1), 96 });
        for (const int step32 : barMap.motionSteps32)
            pattern.notes.push_back({ std::clamp(step32 * HiResTiming::kTicks1_32, 0, HiResTiming::kTicksPerBar4_4 - 1), 84 });
    }

    sortAndUniqueReferenceNotes(pattern.notes);
    pattern.available = !pattern.notes.empty();
    return pattern.available;
}

ReferenceDrillHatPattern buildPrimaryReferenceHatPattern(const StyleInfluenceState& styleInfluence,
                                                         int bar,
                                                         int selectionSeed)
{
    ReferenceDrillHatPattern pattern;

    if (styleInfluence.referenceHatCorpus.available && !styleInfluence.referenceHatCorpus.variants.empty())
    {
        const auto startIndex = rotatedReferenceVariantStartIndex(styleInfluence.referenceHatCorpus.variants.size(), selectionSeed, bar);
        for (size_t offset = 0; offset < styleInfluence.referenceHatCorpus.variants.size(); ++offset)
        {
            const auto& variant = styleInfluence.referenceHatCorpus.variants[(startIndex + offset) % styleInfluence.referenceHatCorpus.variants.size()];
            if (loadReferencePatternFromSkeleton(variant, bar, pattern))
                return pattern;
        }
    }

    loadReferencePatternFromSkeleton(styleInfluence.referenceHatSkeleton, bar, pattern);
    return pattern;
}

ReferenceDrillHatFeel buildReferenceDrillHatFeel(const StyleInfluenceState& styleInfluence,
                                                 int bar,
                                                 int selectionSeed)
{
    ReferenceDrillHatFeel feel;

    int contributingBars = 0;
    float totalNotes = 0.0f;
    float totalTriplets = 0.0f;
    float totalBursts = 0.0f;
    float totalAnchors = 0.0f;
    float totalGapSlots = 0.0f;

    if (styleInfluence.referenceHatCorpus.available && !styleInfluence.referenceHatCorpus.variants.empty())
    {
        const auto startIndex = rotatedReferenceVariantStartIndex(styleInfluence.referenceHatCorpus.variants.size(), selectionSeed, bar);
        for (size_t offset = 0; offset < styleInfluence.referenceHatCorpus.variants.size(); ++offset)
        {
            const auto& variant = styleInfluence.referenceHatCorpus.variants[(startIndex + offset) % styleInfluence.referenceHatCorpus.variants.size()];
            if (!variant.available || variant.barMaps.empty())
                continue;

            const int sourceBars = std::max(1, variant.sourceBars > 0 ? variant.sourceBars : static_cast<int>(variant.barMaps.size()));
            const int normalizedBar = ((bar % sourceBars) + sourceBars) % sourceBars;
            if (normalizedBar < 0 || normalizedBar >= static_cast<int>(variant.barMaps.size()))
                continue;

            const auto& barMap = variant.barMaps[static_cast<size_t>(normalizedBar)];
            std::array<bool, 8> occupiedSlots {};
            ++contributingBars;

            for (const auto& note : barMap.notes)
            {
                const int step32 = std::clamp(HiResTiming::quantizeTicks(note.tickInBar, HiResTiming::kTicks1_32) / HiResTiming::kTicks1_32,
                                              0,
                                              31);
                accumulateReferenceHatStep(feel,
                                           step32,
                                           note.velocity,
                                           1.0f,
                                           totalNotes,
                                           totalAnchors,
                                           occupiedSlots);
            }

            for (const auto& cluster : variant.rollClusters)
            {
                if (cluster.barIndex != normalizedBar)
                    continue;

                const float weight = cluster.noteCount >= 4 ? 1.0f : 0.6f;
                totalBursts += weight;
                accumulateClusterWeight(feel.burstStepWeight, cluster, weight);
            }

            for (const auto& cluster : variant.tripletClusters)
            {
                if (cluster.barIndex != normalizedBar)
                    continue;

                const float weight = cluster.noteCount >= 3 ? 1.0f : 0.6f;
                totalTriplets += weight;
                accumulateClusterWeight(feel.tripletStepWeight, cluster, weight);
            }

            for (size_t slot = 0; slot < occupiedSlots.size(); ++slot)
                if (!occupiedSlots[slot])
                    totalGapSlots += 1.0f;

            break;
        }
    }

    if (contributingBars <= 0
        && styleInfluence.referenceHatSkeleton.available
        && !styleInfluence.referenceHatSkeleton.barMaps.empty())
    {
        const auto& skeleton = styleInfluence.referenceHatSkeleton;
        const int sourceBars = std::max(1, skeleton.sourceBars > 0 ? skeleton.sourceBars : static_cast<int>(skeleton.barMaps.size()));
        const int normalizedBar = ((bar % sourceBars) + sourceBars) % sourceBars;
        if (normalizedBar >= 0 && normalizedBar < static_cast<int>(skeleton.barMaps.size()))
        {
            const auto& barMap = skeleton.barMaps[static_cast<size_t>(normalizedBar)];
            std::array<bool, 8> occupiedSlots {};
            ++contributingBars;

            for (const int step16 : barMap.backboneSteps16)
                accumulateReferenceHatStep(feel,
                                           step16 * 2,
                                           96,
                                           0.9f,
                                           totalNotes,
                                           totalAnchors,
                                           occupiedSlots);

            for (const int step32 : barMap.motionSteps32)
                accumulateReferenceHatStep(feel,
                                           step32,
                                           84,
                                           0.72f,
                                           totalNotes,
                                           totalAnchors,
                                           occupiedSlots);

            for (const auto& cluster : skeleton.rollClusters)
            {
                if (cluster.barIndex != normalizedBar)
                    continue;

                const float weight = cluster.noteCount >= 4 ? 0.9f : 0.55f;
                totalBursts += weight;
                accumulateClusterWeight(feel.burstStepWeight, cluster, weight);
            }

            for (const auto& cluster : skeleton.tripletClusters)
            {
                if (cluster.barIndex != normalizedBar)
                    continue;

                const float weight = cluster.noteCount >= 3 ? 0.9f : 0.55f;
                totalTriplets += weight;
                accumulateClusterWeight(feel.tripletStepWeight, cluster, weight);
            }

            for (size_t slot = 0; slot < occupiedSlots.size(); ++slot)
                if (!occupiedSlots[slot])
                    totalGapSlots += 1.0f;
        }
    }

    if (contributingBars <= 0)
        return feel;

    const float invBars = 1.0f / static_cast<float>(contributingBars);
    for (auto& value : feel.slotWeight)
        value *= invBars;
    for (auto& value : feel.stepWeight)
        value *= invBars;
    for (auto& value : feel.stepVelocitySum)
        value *= invBars;
    for (auto& value : feel.stepVelocityWeight)
        value *= invBars;
    for (auto& value : feel.tripletStepWeight)
        value *= invBars;
    for (auto& value : feel.burstStepWeight)
        value *= invBars;

    feel.available = true;
    feel.noteDensity = totalNotes * invBars;
    feel.tripletRatio = totalTriplets * invBars;
    feel.burstRatio = totalBursts * invBars;
    feel.gapRatio = totalGapSlots / (static_cast<float>(contributingBars) * 8.0f);
    feel.anchorRatio = totalNotes > 0.0f ? totalAnchors / totalNotes : 0.0f;
    return feel;
}

float referenceHatStepPriority(const ReferenceDrillHatFeel& feel, int step32)
{
    const int clampedStep = std::clamp(step32, 0, 31);
    const int slot = std::clamp(clampedStep / 4, 0, 7);
    return feel.stepWeight[static_cast<size_t>(clampedStep)]
        + feel.tripletStepWeight[static_cast<size_t>(clampedStep)] * 0.34f
        + feel.burstStepWeight[static_cast<size_t>(clampedStep)] * 0.28f
        + feel.slotWeight[static_cast<size_t>(slot)] * 0.12f;
}

bool isExactCarrierTick(const DrillPhraseBarPlan& bar, int tickInBar)
{
    for (const int carrierStep : bar.anchorMap.hatCarrierSteps)
    {
        if (carrierStep < 0)
            continue;

        if (carrierStep * HiResTiming::kTicks1_16 == tickInBar)
            return true;
    }

    return false;
}

void addReferenceCopyCandidates(std::vector<PlannedHatNote>& notes,
                                const ReferenceDrillHatPattern& pattern,
                                const DrillPhraseBarPlan& bar,
                                int barStartTick)
{
    if (!pattern.available)
        return;

    for (const auto& note : pattern.notes)
    {
        const int localTick = std::clamp(note.tickInBar, 0, HiResTiming::kTicksPerBar4_4 - 1);
        const int absoluteTick = barStartTick + localTick;
        addCandidate(notes,
                     absoluteTick,
                     std::clamp(note.velocity, 42, 118),
                     "drill_hat_reference_copy",
                     isExactCarrierTick(bar, localTick) ? 132 : 126,
                     true);
    }
}

bool shouldCopyReferenceMostly(const ReferenceDrillHatPattern& pattern,
                               const ReferenceDrillHatFeel& feel)
{
    return pattern.available && !pattern.notes.empty() && (static_cast<int>(pattern.notes.size()) >= 6 || feel.noteDensity >= 7.5f);
}

int referenceVelocityForStep(const ReferenceDrillHatFeel& feel, int step32, int fallbackVelocity)
{
    const int clampedStep = std::clamp(step32, 0, 31);
    const float weight = feel.stepVelocityWeight[static_cast<size_t>(clampedStep)];
    if (weight <= 0.0f)
        return fallbackVelocity;

    const float average = feel.stepVelocitySum[static_cast<size_t>(clampedStep)] / weight;
    return std::clamp(static_cast<int>(std::round((average + static_cast<float>(fallbackVelocity)) * 0.5f)),
                      42,
                      108);
}

bool hasCandidateAtTick(const std::vector<PlannedHatNote>& notes, int tick)
{
    return std::any_of(notes.begin(), notes.end(), [tick](const PlannedHatNote& note)
    {
        return note.tick == tick;
    });
}

bool isFoundationHatSemantic(const juce::String& semanticRole)
{
    return semanticRole == "drill_hat_backbone" || semanticRole == "drill_hat_reference_copy";
}

bool hasReferenceCoverageNearTick(const std::vector<PlannedHatNote>& notes,
                                  int tick,
                                  int toleranceTicks)
{
    return std::any_of(notes.begin(), notes.end(), [&](const PlannedHatNote& note)
    {
        return note.semanticRole == "drill_hat_reference_copy"
            && std::abs(note.tick - tick) <= toleranceTicks;
    });
}

bool hasNearbyAnyActivity(const std::vector<PlannedHatNote>& notes,
                         int tick,
                         int toleranceTicks)
{
    return std::any_of(notes.begin(), notes.end(), [&](const PlannedHatNote& note)
    {
        return std::abs(note.tick - tick) <= toleranceTicks;
    });
}

bool hasNearbyProceduralActivity(const std::vector<PlannedHatNote>& notes,
                                 int tick,
                                 int toleranceTicks)
{
    return std::any_of(notes.begin(), notes.end(), [&](const PlannedHatNote& note)
    {
        if (isFoundationHatSemantic(note.semanticRole))
            return false;
        return std::abs(note.tick - tick) <= toleranceTicks;
    });
}

template <size_t Size>
bool clusterConflictsWithNearbyAnyActivity(const std::vector<PlannedHatNote>& notes,
                                           const std::array<int, Size>& ticks,
                                           int toleranceTicks)
{
    for (const int tick : ticks)
    {
        if (hasNearbyAnyActivity(notes, tick, toleranceTicks))
            return true;
    }

    return false;
}

bool isCarrierStep32(const DrillPhraseBarPlan& bar, int step32)
{
    for (const int carrierStep : bar.anchorMap.hatCarrierSteps)
        if (carrierStep >= 0 && carrierStep * 2 == step32)
            return true;

    return false;
}

void addReferenceDrivenCandidates(std::vector<PlannedHatNote>& notes,
                                  int& extraBudget,
                                  const ReferenceDrillHatFeel& feel,
                                  const DrillPhraseBarPlan& bar,
                                  int barStartTick,
                                  const PatternProject& project)
{
    if (!feel.available || extraBudget <= 0)
        return;

    constexpr int kHatLocalWindowTicks = 96;

    struct ReferenceCandidate
    {
        int step32 = 0;
        float score = 0.0f;
    };

    std::vector<ReferenceCandidate> candidates;
    candidates.reserve(12);
    for (int step32 = 0; step32 < 32; ++step32)
    {
        if (isCarrierStep32(bar, step32))
            continue;

        const float score = referenceHatStepPriority(feel, step32);
        if (score < 0.54f)
            continue;

        candidates.push_back({ step32, score });
    }

    std::sort(candidates.begin(), candidates.end(), [](const ReferenceCandidate& lhs, const ReferenceCandidate& rhs)
    {
        if (lhs.score != rhs.score)
            return lhs.score > rhs.score;
        return lhs.step32 < rhs.step32;
    });

    const int maxReferenceAdds = std::min(extraBudget, feel.noteDensity >= 9.0f ? 2 : 1);
    int added = 0;
    for (const auto& candidate : candidates)
    {
        if (extraBudget <= 0 || added >= maxReferenceAdds)
            break;

        const int tick = barStartTick + candidate.step32 * HiResTiming::kTicks1_32;
        if (!withinBar(tick, barStartTick)
            || hasCandidateAtTick(notes, tick)
            || hasNearbyAnyActivity(notes, tick, kHatLocalWindowTicks))
            continue;

        const bool preferTriplet = feel.tripletStepWeight[static_cast<size_t>(candidate.step32)]
            > feel.burstStepWeight[static_cast<size_t>(candidate.step32)]
            && feel.tripletStepWeight[static_cast<size_t>(candidate.step32)] > 0.15f;
        const bool preferBurst = !preferTriplet && feel.burstStepWeight[static_cast<size_t>(candidate.step32)] > 0.15f;
        const bool transitionLike = bar.isPhraseEnd
            || bar.role == DrillPhraseBarRole::Lift
            || bar.role == DrillPhraseBarRole::Release
            || candidate.step32 >= 24;
        const int slot = std::clamp(candidate.step32 / 4, 0, 7);
        const int fallbackVelocity = std::clamp(60
                                                    + static_cast<int>(std::round(normalizedStyleWeight(project.styleInfluence.drillHatAccentPatternWeight) * 12.0f))
                                                    + static_cast<int>(std::round(feel.slotWeight[static_cast<size_t>(slot)] * 4.0f)),
                                                44,
                                                98);

        addCandidate(notes,
                     tick,
                     referenceVelocityForStep(feel, candidate.step32, fallbackVelocity),
                     preferTriplet ? "drill_hat_triplet"
                                   : (preferBurst ? "drill_hat_burst"
                                                  : (transitionLike ? "drill_hat_transition" : "drill_hat_subdivision")),
                     preferTriplet ? 70 : (preferBurst ? 66 : (transitionLike ? 62 : 56)),
                     false);
        --extraBudget;
        ++added;
    }
}

void sortAndDedupeRenderedNotes(std::vector<NoteEvent>& notes)
{
    auto semanticPriority = [](const juce::String& semanticRole)
    {
        if (semanticRole == "drill_hat_reference_copy")
            return 6;
        if (semanticRole == "drill_hat_backbone")
            return 5;
        if (semanticRole == "drill_hat_transition")
            return 4;
        if (semanticRole == "drill_hat_triplet")
            return 3;
        if (semanticRole == "drill_hat_burst")
            return 2;
        return 1;
    };

    std::sort(notes.begin(), notes.end(), [&](const NoteEvent& lhs, const NoteEvent& rhs)
    {
        if (lhs.step != rhs.step)
            return lhs.step < rhs.step;
        if (lhs.microOffset != rhs.microOffset)
            return lhs.microOffset < rhs.microOffset;
        if (lhs.pitch != rhs.pitch)
            return lhs.pitch < rhs.pitch;
        const int lhsPriority = semanticPriority(lhs.semanticRole);
        const int rhsPriority = semanticPriority(rhs.semanticRole);
        if (lhsPriority != rhsPriority)
            return lhsPriority > rhsPriority;
        return lhs.velocity > rhs.velocity;
    });

    notes.erase(std::unique(notes.begin(), notes.end(), [](const NoteEvent& lhs, const NoteEvent& rhs)
    {
        return lhs.step == rhs.step && lhs.microOffset == rhs.microOffset && lhs.pitch == rhs.pitch;
    }), notes.end());
}
} // namespace

void DrillHatGenerator::generate(TrackState& track,
                                 const PatternProject& project,
                                 const DrillPhrasePlan& phrasePlan,
                                 std::mt19937& rng) const
{
    track.notes.clear();
    track.subProfile = "Main";
    track.laneRole = "drill_hat";

    const auto* trackInfo = TrackRegistry::find(TrackType::HiHat);
    const int pitch = trackInfo != nullptr ? trackInfo->defaultMidiNote : 42;
    const auto substyleBias = substyleBiasFor(phrasePlan.substyleIndex);
    const auto motif = buildPhraseMotif(rng);
    const auto& style = getGenreStyleDefaults(GenreType::Drill, phrasePlan.substyleIndex);
    const auto& laneDefaults = getLaneStyleDefaults(style, TrackType::HiHat);

    const float densityBase = clampUnit(0.28f
                                        + 0.32f * clampUnit(project.params.densityAmount)
                                        + 0.20f * normalizedActivityWeight(project)
                                        + 0.12f * normalizedStyleWeight(project.styleInfluence.drillHatDensityVariationWeight)
                                        + 0.08f * clampUnit(laneDefaults.densityBias * 0.5f));
    const float tripletBase = clampUnit(0.10f
                                        + 0.56f * normalizedStyleWeight(project.styleInfluence.drillHatTripletWeight));
    const float burstBase = clampUnit(0.08f
                                      + 0.46f * normalizedStyleWeight(project.styleInfluence.drillHatBurstWeight));
    const float gapBase = clampUnit(0.08f
                                    + 0.52f * normalizedStyleWeight(project.styleInfluence.drillHatGapIntentWeight));

    for (const auto& bar : phrasePlan.bars)
    {
        std::vector<PlannedHatNote> barNotes;
        barNotes.reserve(32);

        const int barStartTick = bar.barIndex * HiResTiming::kTicksPerBar4_4;
        const auto referencePattern = buildPrimaryReferenceHatPattern(project.styleInfluence, bar.barIndex, project.params.seed);
        const auto referenceFeel = buildReferenceDrillHatFeel(project.styleInfluence, bar.barIndex, project.params.seed);
        const float referenceDensity = referenceFeel.available ? clampUnit((referenceFeel.noteDensity - 4.0f) / 8.0f) : 0.0f;
        const float referenceTriplet = referenceFeel.available ? clampUnit(referenceFeel.tripletRatio) : 0.0f;
        const float referenceBurst = referenceFeel.available ? clampUnit(referenceFeel.burstRatio) : 0.0f;
        const float referenceGap = referenceFeel.available ? clampUnit(referenceFeel.gapRatio * 1.1f) : 0.0f;
        const int baseMaxNotes = std::min(substyleBias.denseMaxNotes,
                                          maxNotesForDensity(bar.hatDensity, substyleBias)
                                              + (referenceFeel.available && referenceFeel.noteDensity >= 9.0f ? 1 : 0));
        const int maxNotes = referencePattern.available
            ? std::max(baseMaxNotes, static_cast<int>(referencePattern.notes.size()))
            : baseMaxNotes;
        const bool copyReferenceMostly = shouldCopyReferenceMostly(referencePattern, referenceFeel);
        const float localDensityBase = clampUnit(densityBase + 0.12f * referenceDensity);
        const float localTripletBase = clampUnit(tripletBase + 0.24f * referenceTriplet);
        const float localBurstBase = clampUnit(burstBase + 0.20f * referenceBurst);
        const float localGapBase = clampUnit(gapBase * (referenceFeel.available
                                                            ? std::clamp(0.82f + referenceGap * 0.52f, 0.78f, 1.18f)
                                                            : 1.0f));

        addReferenceCopyCandidates(barNotes, referencePattern, bar, barStartTick);

        for (const int stepInBar : bar.anchorMap.hatCarrierSteps)
        {
            if (stepInBar < 0)
                continue;

            const int slot = std::clamp(stepInBar / 2, 0, 7);
            const int tick = barStartTick + stepInBar * HiResTiming::kTicks1_16;
            if (hasReferenceCoverageNearTick(barNotes, tick, HiResTiming::kTicks1_32))
                continue;

            addCandidate(barNotes,
                         tick,
                         carrierVelocity(project, bar, slot, stepInBar, motif, rng),
                         "drill_hat_backbone",
                         100,
                         true);
        }

        dedupeByTick(barNotes);
        int extraBudget = std::max(0, maxNotes - static_cast<int>(barNotes.size()));

        if (!copyReferenceMostly)
            addReferenceDrivenCandidates(barNotes, extraBudget, referenceFeel, bar, barStartTick, project);

        const bool allowReferenceFoundationVariation = copyReferenceMostly
            && (bar.role == DrillPhraseBarRole::Lift || bar.role == DrillPhraseBarRole::Release || bar.isPhraseEnd);
        const int maxProceduralAddsForBar = copyReferenceMostly ? (allowReferenceFoundationVariation ? 1 : 0) : maxNotes;
        int proceduralAddsInBar = 0;

        if (!copyReferenceMostly || allowReferenceFoundationVariation)
        {
            for (const int stepInBar : bar.anchorMap.hatCarrierSteps)
            {
                if (stepInBar < 0 || extraBudget <= 0 || proceduralAddsInBar >= maxProceduralAddsForBar)
                    continue;

                const int slot = std::clamp(stepInBar / 2, 0, 7);
                const int carrierTick = barStartTick + stepInBar * HiResTiming::kTicks1_16;
                const bool transitionCarrier = isTransitionCarrier(bar, stepInBar);
                const int localProximityWindow = copyReferenceMostly ? 72 : 96;
                bool carrierWindowOccupied = hasNearbyProceduralActivity(barNotes,
                                                                         carrierTick,
                                                                         HiResTiming::kTicks1_32);
                const float silenceIntent = motifLookup(motif.silenceBias, slot, bar.barIndex, bar.role) * (0.55f + 0.65f * localGapBase * substyleBias.silenceBias);
                if (copyReferenceMostly && !transitionCarrier)
                    continue;

                if (carrierWindowOccupied)
                    continue;

                if (silenceIntent > 0.84f && !transitionCarrier && (slot % 2) == 1)
                    continue;

                const float subdivisionScore = localDensityBase * substyleBias.densityBias * motifLookup(motif.subdivisionBias, slot, bar.barIndex, bar.role);
                if (extraBudget > 0 && subdivisionScore > (transitionCarrier ? 0.48f : 0.62f))
                {
                    int subdivisionTick = carrierTick + ((slot % 2) == 0 ? HiResTiming::kTicks1_32 : -HiResTiming::kTicks1_32);
                    if (!withinBar(subdivisionTick, barStartTick))
                        subdivisionTick = carrierTick + HiResTiming::kTicks1_32;

                    subdivisionTick += motionJitterTicks(project, transitionCarrier, rng);
                    if (withinBar(subdivisionTick, barStartTick)
                        && !hasNearbyAnyActivity(barNotes, subdivisionTick, localProximityWindow))
                    {
                        addCandidate(barNotes,
                                     subdivisionTick,
                                     subdivisionVelocity(project, transitionCarrier, rng),
                                     transitionCarrier ? "drill_hat_transition" : "drill_hat_subdivision",
                                     transitionCarrier ? 72 : 52,
                                     false);
                        --extraBudget;
                        ++proceduralAddsInBar;
                        carrierWindowOccupied = true;
                    }
                }

                if (((!copyReferenceMostly && extraBudget > 1)
                     || (copyReferenceMostly && extraBudget > 0 && proceduralAddsInBar < maxProceduralAddsForBar))
                    && transitionCarrier
                    && !carrierWindowOccupied)
                {
                    const float tripletScore = localTripletBase * substyleBias.tripletBias * motifLookup(motif.tripletBias, slot, bar.barIndex, bar.role) * substyleBias.transitionBias;
                    if (tripletScore > 0.58f)
                    {
                        std::array<int, 2> tripletTicks {
                            carrierTick + (stepInBar >= 12 ? -HiResTiming::kTicks1_24 : HiResTiming::kTicks1_24),
                            carrierTick + (stepInBar >= 12 ? HiResTiming::kTicks1_24 : HiResTiming::kTicks1_12)
                        };

                        if (!clusterConflictsWithNearbyAnyActivity(barNotes, tripletTicks, localProximityWindow))
                        {
                            bool addedTriplet = false;
                            for (int tick : tripletTicks)
                            {
                                if (extraBudget <= 0 || proceduralAddsInBar >= maxProceduralAddsForBar)
                                    break;

                                tick += motionJitterTicks(project, true, rng);
                                if (!withinBar(tick, barStartTick) || hasNearbyAnyActivity(barNotes, tick, localProximityWindow))
                                    continue;

                                addCandidate(barNotes,
                                             tick,
                                             subdivisionVelocity(project, true, rng),
                                             "drill_hat_triplet",
                                             68,
                                             false);
                                --extraBudget;
                                ++proceduralAddsInBar;
                                addedTriplet = true;
                                if (copyReferenceMostly)
                                    break;
                            }

                            carrierWindowOccupied = carrierWindowOccupied || addedTriplet;
                        }
                    }
                }

                if (!copyReferenceMostly && extraBudget > 1 && bar.hatDensity == DrillHatDensityIntent::Dense && transitionCarrier && !carrierWindowOccupied)
                {
                    const float burstScore = localBurstBase * substyleBias.burstBias * motifLookup(motif.subdivisionBias, slot, bar.barIndex, bar.role);
                    if (burstScore > 0.64f)
                    {
                        std::array<int, 2> burstTicks {
                            carrierTick + HiResTiming::kTicks1_64,
                            carrierTick + HiResTiming::kTicks1_32 + HiResTiming::kTicks1_64
                        };

                        if (!clusterConflictsWithNearbyAnyActivity(barNotes, burstTicks, localProximityWindow))
                        {
                            bool addedBurst = false;
                            for (int tick : burstTicks)
                            {
                                if (extraBudget <= 0)
                                    break;

                                tick += motionJitterTicks(project, true, rng);
                                if (!withinBar(tick, barStartTick) || hasNearbyAnyActivity(barNotes, tick, localProximityWindow))
                                    continue;

                                addCandidate(barNotes,
                                             tick,
                                             subdivisionVelocity(project, true, rng) - 4,
                                             "drill_hat_burst",
                                             64,
                                             false);
                                --extraBudget;
                                addedBurst = true;
                            }

                            carrierWindowOccupied = carrierWindowOccupied || addedBurst;
                        }
                    }
                }
            }
        }

        dedupeByTick(barNotes);
        trimToMax(barNotes, maxNotes);

        for (const auto& note : barNotes)
        {
            HiResTiming::addNoteAtTick(track, pitch, note.tick, note.velocity, false, phrasePlan.phraseSpanBars);
            track.notes.back().semanticRole = note.semanticRole;
        }
    }

    sortAndDedupeRenderedNotes(track.notes);
}
} // namespace bbg