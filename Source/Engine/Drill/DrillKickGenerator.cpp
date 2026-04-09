#include "DrillKickGenerator.h"

#include <algorithm>
#include <array>

#include "../../Core/TrackRegistry.h"

namespace bbg
{
namespace
{
struct PlannedKickNote
{
    int stepInBar = 0;
    int velocity = 108;
    juce::String semanticRole;
    int priority = 0;
    bool preserve = false;
};

struct ReferenceDrillKickFeel
{
    bool available = false;
    float density = 0.0f;
    float anchorRatio = 0.0f;
    float supportRatio = 0.0f;
    float pickupRatio = 0.0f;
    std::array<float, 16> presence {};
    std::array<float, 16> velocitySum {};
    std::array<float, 16> velocityWeight {};
};

struct PrimaryReferenceKickPattern
{
    bool available = false;
    std::vector<ReferenceKickNote> notes;
};

int maxKickEvents(const DrillPhraseBarPlan& bar)
{
    switch (bar.role)
    {
        case DrillPhraseBarRole::Lift:
            return (bar.kickDensity == DrillKickDensityIntent::Push && bar.lowEnd == DrillLowEndIntent::Move) ? 3 : 2;
        case DrillPhraseBarRole::Release:
            return bar.kickDensity == DrillKickDensityIntent::Push ? 3 : 2;
        case DrillPhraseBarRole::Statement:
            return bar.kickDensity == DrillKickDensityIntent::Sparse ? 1 : 2;
        case DrillPhraseBarRole::Response:
        default:
            return 2;
    }
}

bool collidesWithSnare(const DrillPhraseBarPlan& bar, int stepInBar)
{
    for (const int snareStep : bar.anchorMap.snareAnchorSteps)
    {
        if (snareStep >= 0 && snareStep == stepInBar)
            return true;
    }

    return false;
}

bool isAnchorLikeKickStep(int stepInBar)
{
    return stepInBar == 0;
}

bool isPickupLikeKickStep(int stepInBar)
{
    return stepInBar >= 12;
}

juce::String semanticRoleForKickStep(int stepInBar)
{
    if (isAnchorLikeKickStep(stepInBar))
        return "drill_kick_anchor";
    if (isPickupLikeKickStep(stepInBar))
        return "drill_kick_pickup";
    return "drill_kick_support";
}

int referenceVelocityForKickStep(const ReferenceDrillKickFeel& feel, int stepInBar, int fallbackVelocity)
{
    const int clampedStep = std::clamp(stepInBar, 0, 15);
    const float weight = feel.velocityWeight[static_cast<size_t>(clampedStep)];
    if (weight <= 0.0f)
        return fallbackVelocity;

    const float average = feel.velocitySum[static_cast<size_t>(clampedStep)] / weight;
    return std::clamp(static_cast<int>(std::round((average + static_cast<float>(fallbackVelocity)) * 0.5f)),
                      72,
                      122);
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

ReferenceDrillKickFeel buildReferenceDrillKickFeel(const StyleInfluenceState& styleInfluence,
                                                   int bar,
                                                   int selectionSeed)
{
    ReferenceDrillKickFeel feel;
    if (!styleInfluence.referenceKickCorpus.available || styleInfluence.referenceKickCorpus.variants.empty())
        return feel;

    int contributingBars = 0;
    float totalNotes = 0.0f;
    float anchors = 0.0f;
    float supports = 0.0f;
    float pickups = 0.0f;

    const auto startIndex = rotatedReferenceVariantStartIndex(styleInfluence.referenceKickCorpus.variants.size(), selectionSeed, bar);
    for (size_t offset = 0; offset < styleInfluence.referenceKickCorpus.variants.size(); ++offset)
    {
        const auto& variant = styleInfluence.referenceKickCorpus.variants[(startIndex + offset) % styleInfluence.referenceKickCorpus.variants.size()];
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
            feel.velocitySum[static_cast<size_t>(step)] += static_cast<float>(note.velocity);
            feel.velocityWeight[static_cast<size_t>(step)] += 1.0f;
            if (isAnchorLikeKickStep(step))
                anchors += 1.0f;
            else if (isPickupLikeKickStep(step))
                pickups += 1.0f;
            else
                supports += 1.0f;
        }

        break;
    }

    if (contributingBars <= 0)
        return feel;

    const float invBars = 1.0f / static_cast<float>(contributingBars);
    for (auto& value : feel.presence)
        value *= invBars;
    for (auto& value : feel.velocitySum)
        value *= invBars;
    for (auto& value : feel.velocityWeight)
        value *= invBars;

    feel.available = true;
    feel.density = std::clamp((totalNotes * invBars) / 4.0f, 0.0f, 1.0f);
    feel.anchorRatio = totalNotes > 0.0f ? anchors / totalNotes : 0.0f;
    feel.supportRatio = totalNotes > 0.0f ? supports / totalNotes : 0.0f;
    feel.pickupRatio = totalNotes > 0.0f ? pickups / totalNotes : 0.0f;
    return feel;
}

void sortAndUniqueReferenceKickNotes(std::vector<ReferenceKickNote>& notes)
{
    std::sort(notes.begin(), notes.end(), [](const ReferenceKickNote& lhs, const ReferenceKickNote& rhs)
    {
        if (lhs.step16 != rhs.step16)
            return lhs.step16 < rhs.step16;
        return lhs.velocity > rhs.velocity;
    });

    notes.erase(std::unique(notes.begin(), notes.end(), [](const ReferenceKickNote& lhs, const ReferenceKickNote& rhs)
    {
        return lhs.step16 == rhs.step16;
    }), notes.end());
}

PrimaryReferenceKickPattern buildPrimaryReferenceKickPattern(const StyleInfluenceState& styleInfluence,
                                                             int bar,
                                                             int selectionSeed)
{
    PrimaryReferenceKickPattern pattern;
    if (!styleInfluence.referenceKickCorpus.available || styleInfluence.referenceKickCorpus.variants.empty())
        return pattern;

    const auto startIndex = rotatedReferenceVariantStartIndex(styleInfluence.referenceKickCorpus.variants.size(), selectionSeed, bar);
    for (size_t offset = 0; offset < styleInfluence.referenceKickCorpus.variants.size(); ++offset)
    {
        const auto& variant = styleInfluence.referenceKickCorpus.variants[(startIndex + offset) % styleInfluence.referenceKickCorpus.variants.size()];
        if (!variant.available || variant.barPatterns.empty())
            continue;

        const int sourceBars = std::max(1, variant.sourceBars > 0 ? variant.sourceBars : static_cast<int>(variant.barPatterns.size()));
        const int normalizedBar = ((bar % sourceBars) + sourceBars) % sourceBars;
        if (normalizedBar < 0 || normalizedBar >= static_cast<int>(variant.barPatterns.size()))
            continue;

        pattern.notes = variant.barPatterns[static_cast<size_t>(normalizedBar)].notes;
        sortAndUniqueReferenceKickNotes(pattern.notes);
        pattern.available = !pattern.notes.empty();
        if (pattern.available)
            return pattern;
    }

    return pattern;
}

bool isPlannedKickAnchor(const DrillPhraseBarPlan& bar, int stepInBar)
{
    for (const int anchorStep : bar.anchorMap.kickAnchorSteps)
        if (anchorStep == stepInBar)
            return true;
    return false;
}

bool isLowEndAnchor(const DrillPhraseBarPlan& bar, int stepInBar)
{
    for (const int anchorStep : bar.anchorMap.lowEndAnchorSteps)
        if (anchorStep == stepInBar)
            return true;
    return false;
}

void addKickCandidate(std::vector<PlannedKickNote>& notes,
                      int stepInBar,
                      int velocity,
                      const juce::String& semanticRole,
                      int priority,
                      bool preserve)
{
    auto existing = std::find_if(notes.begin(), notes.end(), [stepInBar](const PlannedKickNote& note)
    {
        return note.stepInBar == stepInBar;
    });

    if (existing == notes.end())
    {
        notes.push_back({ stepInBar, velocity, semanticRole, priority, preserve });
        return;
    }

    if (preserve && !existing->preserve)
        existing->preserve = true;
    if (priority > existing->priority)
        existing->priority = priority;
    if (velocity > existing->velocity)
        existing->velocity = velocity;
    if (existing->semanticRole != "drill_kick_anchor" && semanticRole == "drill_kick_anchor")
        existing->semanticRole = semanticRole;
    else if (existing->semanticRole == "drill_kick_support" && semanticRole == "drill_kick_pickup")
        existing->semanticRole = semanticRole;
}

void addReferenceRetentionCandidates(std::vector<PlannedKickNote>& notes,
                                     const PrimaryReferenceKickPattern& referencePattern,
                                     const DrillPhraseBarPlan& bar,
                                     int maxEvents,
                                     int primaryAnchorStep)
{
    if (!referencePattern.available || referencePattern.notes.empty() || maxEvents <= 0)
        return;

    struct RankedReferenceStep
    {
        ReferenceKickNote note;
        float score = 0.0f;
    };

    std::vector<RankedReferenceStep> ranked;
    ranked.reserve(referencePattern.notes.size());

    bool hasPrimaryAnchor = false;
    for (const auto& note : referencePattern.notes)
    {
        const int stepInBar = std::clamp(note.step16, 0, 15);
        if (stepInBar == primaryAnchorStep)
            hasPrimaryAnchor = true;
        if (collidesWithSnare(bar, stepInBar))
            continue;

        float score = isAnchorLikeKickStep(stepInBar) ? 90.0f : (isPickupLikeKickStep(stepInBar) ? 76.0f : 68.0f);
        score += static_cast<float>(std::clamp(note.velocity, 1, 127)) * 0.22f;
        if (isPlannedKickAnchor(bar, stepInBar))
            score += 12.0f;
        if (isLowEndAnchor(bar, stepInBar))
            score += 8.0f;
        if (bar.lowEnd == DrillLowEndIntent::Move && !isPickupLikeKickStep(stepInBar))
            score += 6.0f;
        if (bar.role == DrillPhraseBarRole::Release && isPickupLikeKickStep(stepInBar))
            score += 10.0f;

        ranked.push_back({ { stepInBar, note.velocity }, score });
    }

    if (ranked.empty())
        return;

    std::sort(ranked.begin(), ranked.end(), [](const RankedReferenceStep& lhs, const RankedReferenceStep& rhs)
    {
        if (lhs.score != rhs.score)
            return lhs.score > rhs.score;
        if (lhs.note.velocity != rhs.note.velocity)
            return lhs.note.velocity > rhs.note.velocity;
        return lhs.note.step16 < rhs.note.step16;
    });

    const int reservedForAnchor = hasPrimaryAnchor ? 0 : 1;
    const int availableSlots = std::max(0, maxEvents - reservedForAnchor);
    if (availableSlots <= 0)
        return;

    const int targetRetained = std::min(availableSlots,
                                        std::max(1, static_cast<int>(std::round(static_cast<float>(availableSlots) * 0.75f))));

    for (int index = 0; index < targetRetained && index < static_cast<int>(ranked.size()); ++index)
    {
        const auto& note = ranked[static_cast<size_t>(index)].note;
        const int stepInBar = std::clamp(note.step16, 0, 15);
        addKickCandidate(notes,
                         stepInBar,
                         std::clamp(note.velocity, 72, 122),
                         semanticRoleForKickStep(stepInBar),
                         144 - index,
                         true);
    }
}
} // namespace

void DrillKickGenerator::generate(TrackState& kickTrack,
                                  const PatternProject& project,
                                  const DrillPhrasePlan& phrasePlan,
                                  const TrackState* snareTrack,
                                  std::mt19937& rng) const
{
    juce::ignoreUnused(project, snareTrack);

    kickTrack.notes.clear();
    kickTrack.subProfile = "Main";
    kickTrack.laneRole = "drill_kick";

    const auto* info = TrackRegistry::find(TrackType::Kick);
    const int pitch = info != nullptr ? info->defaultMidiNote : 36;
    std::uniform_int_distribution<int> anchorVelocity(100, 118);
    std::uniform_int_distribution<int> supportVelocity(76, 104);
    std::uniform_int_distribution<int> pickupVelocity(82, 108);
    std::uniform_int_distribution<int> priorityJitter(0, 7);

    for (const auto& bar : phrasePlan.bars)
    {
        const int barStart = bar.barIndex * 16;
        const int maxEvents = maxKickEvents(bar);
        const auto referencePattern = buildPrimaryReferenceKickPattern(project.styleInfluence, bar.barIndex, project.params.seed);
        const auto referenceFeel = buildReferenceDrillKickFeel(project.styleInfluence, bar.barIndex, project.params.seed);
        std::vector<PlannedKickNote> plannedNotes;
        plannedNotes.reserve(6);

        const int primaryAnchorStep = bar.anchorMap.kickAnchorSteps[0] >= 0 ? bar.anchorMap.kickAnchorSteps[0] : 0;
        addReferenceRetentionCandidates(plannedNotes, referencePattern, bar, maxEvents, primaryAnchorStep);

        for (size_t index = 0; index < bar.anchorMap.kickAnchorSteps.size(); ++index)
        {
            const int stepInBar = bar.anchorMap.kickAnchorSteps[index];
            if (stepInBar < 0 || collidesWithSnare(bar, stepInBar))
                continue;

            const juce::String semanticRole = semanticRoleForKickStep(stepInBar);
            const int fallbackVelocity = semanticRole == "drill_kick_anchor"
                ? anchorVelocity(rng)
                : (semanticRole == "drill_kick_pickup" ? pickupVelocity(rng) : supportVelocity(rng));
            int priority = index == 0 ? 140 : 118 - static_cast<int>(index) * 6;
            if (referenceFeel.available)
            {
                const float presence = referenceFeel.presence[static_cast<size_t>(stepInBar)];
                if (presence > 0.0f)
                    priority += static_cast<int>(std::round(presence * 14.0f));
            }

            addKickCandidate(plannedNotes,
                             stepInBar,
                             referenceFeel.available ? referenceVelocityForKickStep(referenceFeel, stepInBar, fallbackVelocity) : fallbackVelocity,
                             semanticRole,
                             priority + priorityJitter(rng),
                             index == 0);
        }

        std::sort(plannedNotes.begin(), plannedNotes.end(), [](const PlannedKickNote& lhs, const PlannedKickNote& rhs)
        {
            if (lhs.preserve != rhs.preserve)
                return lhs.preserve > rhs.preserve;
            if (lhs.priority != rhs.priority)
                return lhs.priority > rhs.priority;
            if (lhs.velocity != rhs.velocity)
                return lhs.velocity > rhs.velocity;
            return lhs.stepInBar < rhs.stepInBar;
        });

        std::array<bool, 16> usedSteps {};
        int added = 0;
        for (const auto& note : plannedNotes)
        {
            if (added >= maxEvents || note.stepInBar < 0 || note.stepInBar >= 16 || usedSteps[static_cast<size_t>(note.stepInBar)])
                continue;

            usedSteps[static_cast<size_t>(note.stepInBar)] = true;
            kickTrack.notes.push_back({ pitch, barStart + note.stepInBar, 1, note.velocity, 0, false, note.semanticRole, false, false, false });
            ++added;
        }

    }

    std::sort(kickTrack.notes.begin(), kickTrack.notes.end(), [](const NoteEvent& lhs, const NoteEvent& rhs)
    {
        if (lhs.step != rhs.step)
            return lhs.step < rhs.step;
        return lhs.velocity > rhs.velocity;
    });
    kickTrack.notes.erase(std::unique(kickTrack.notes.begin(), kickTrack.notes.end(), [](const NoteEvent& lhs, const NoteEvent& rhs)
    {
        return lhs.step == rhs.step;
    }), kickTrack.notes.end());
}
} // namespace bbg