#include "Drill808Generator.h"

#include <algorithm>
#include <array>
#include <optional>

#include "../../Core/Sub808Types.h"

namespace bbg
{
namespace
{
struct ReferenceDrillKickFeel
{
    bool available = false;
    float density = 0.0f;
    float supportRatio = 0.0f;
    float pickupRatio = 0.0f;
    std::array<float, 16> presence {};
};

bool isPickupLikeKickStep(int stepInBar)
{
    return stepInBar >= 12;
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
            if (isPickupLikeKickStep(step))
                pickups += 1.0f;
            else if (step != 0)
                supports += 1.0f;
        }

        break;
    }

    if (contributingBars <= 0)
        return feel;

    const float invBars = 1.0f / static_cast<float>(contributingBars);
    for (auto& value : feel.presence)
        value *= invBars;
    feel.available = true;
    feel.density = std::clamp((totalNotes * invBars) / 4.0f, 0.0f, 1.0f);
    feel.supportRatio = totalNotes > 0.0f ? supports / totalNotes : 0.0f;
    feel.pickupRatio = totalNotes > 0.0f ? pickups / totalNotes : 0.0f;
    return feel;
}

const std::array<int, 7>& intervalsForScale(int scaleMode)
{
    static const std::array<int, 7> minor { 0, 2, 3, 5, 7, 8, 10 };
    static const std::array<int, 7> major { 0, 2, 4, 5, 7, 9, 11 };
    static const std::array<int, 7> harmonicMinor { 0, 2, 3, 5, 7, 8, 11 };

    if (scaleMode == 1)
        return major;
    if (scaleMode == 2)
        return harmonicMinor;
    return minor;
}

int pitchForDegree(int keyRoot, int scaleMode, int octaveBase, int degree)
{
    const auto& intervals = intervalsForScale(scaleMode);
    const int clampedDegree = std::clamp(degree, 0, 6);
    return octaveBase + keyRoot + intervals[static_cast<size_t>(clampedDegree)];
}

int choosePitch(int previousPitch,
                int keyRoot,
                int scaleMode,
                DrillLowEndIntent intent,
                DrillPhraseBarRole role,
                std::mt19937& rng)
{
    const int octaveBase = 24;
    const int root = pitchForDegree(keyRoot, scaleMode, octaveBase, 0);
    const int color = pitchForDegree(keyRoot, scaleMode, octaveBase, role == DrillPhraseBarRole::Release ? 6 : 2);
    const int fifth = pitchForDegree(keyRoot, scaleMode, octaveBase, 4);
    const int octave = root + 12;

    std::array<int, 4> candidates { root, octave, fifth, color };
    std::array<int, 4> weights { 52, 18, 18, 12 };

    if (intent == DrillLowEndIntent::Move)
        weights = { 22, 20, 32, 26 };
    else if (intent == DrillLowEndIntent::Hold)
        weights = { 62, 24, 10, 4 };
    else if (intent == DrillLowEndIntent::Release)
        weights = { 58, 18, 18, 6 };

    if (role == DrillPhraseBarRole::Lift)
    {
        weights[1] += 8;
        weights[2] += 6;
    }
    else if (role == DrillPhraseBarRole::Release)
    {
        weights[0] += 10;
        weights[3] -= 4;
    }

    if (previousPitch >= 0)
    {
        for (size_t index = 0; index < candidates.size(); ++index)
        {
            const int distance = std::abs(candidates[index] - previousPitch);
            if (intent == DrillLowEndIntent::Move)
                weights[index] += std::clamp(8 - std::abs(distance - 5), 0, 8);
            else
                weights[index] += std::clamp(6 - distance, 0, 6);
        }
    }

    std::discrete_distribution<int> pick(weights.begin(), weights.end());
    return candidates[static_cast<size_t>(pick(rng))];
}

std::vector<int> collectKickStarts(const TrackState& kickTrack, int barIndex)
{
    std::vector<int> starts;
    for (const auto& note : kickTrack.notes)
    {
        if ((note.step / 16) == barIndex)
            starts.push_back(note.step % 16);
    }

    std::sort(starts.begin(), starts.end());
    starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
    return starts;
}

std::optional<int> selectKickSupportStart(const std::vector<int>& kickStarts,
                                          const DrillPhraseBarPlan& bar,
                                          const std::vector<int>& existingStarts)
{
    struct Candidate
    {
        int step = 0;
        int score = 0;
    };

    std::vector<Candidate> candidates;
    candidates.reserve(kickStarts.size());
    for (const int step : kickStarts)
    {
        if (std::find(existingStarts.begin(), existingStarts.end(), step) != existingStarts.end())
            continue;

        if (std::any_of(existingStarts.begin(), existingStarts.end(), [step](int existing)
        {
            return std::abs(existing - step) < 4;
        }))
        {
            continue;
        }

        bool collidesWithSnare = false;
        for (const int snareStep : bar.anchorMap.snareAnchorSteps)
            if (snareStep >= 0 && step == snareStep)
                collidesWithSnare = true;
        if (collidesWithSnare)
            continue;

        int score = 0;
        if (bar.lowEnd == DrillLowEndIntent::Move)
        {
            if (step >= 6 && step <= 13)
                score += 4;
            if (step >= 8 && step <= 11)
                score += 2;
        }
        else if (bar.lowEnd == DrillLowEndIntent::Release)
        {
            if (step >= 12)
                score += 5;
            if (step == kickStarts.back())
                score += 2;
        }
        else
        {
            if (step >= 8)
                score += 1;
        }

        for (const int lowEndStep : bar.anchorMap.lowEndAnchorSteps)
            if (lowEndStep == step)
                score += 2;

        if (score > 0)
            candidates.push_back({ step, score });
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& lhs, const Candidate& rhs)
    {
        if (lhs.score != rhs.score)
            return lhs.score > rhs.score;
        return lhs.step < rhs.step;
    });

    if (candidates.empty())
        return std::nullopt;
    return candidates.front().step;
}

std::optional<int> selectReferenceStart(const ReferenceDrillKickFeel& referenceFeel,
                                        const DrillPhraseBarPlan& bar,
                                        const std::vector<int>& existingStarts)
{
    if (!referenceFeel.available || bar.lowEnd == DrillLowEndIntent::Hold)
        return std::nullopt;

    struct Candidate
    {
        int step = 0;
        float score = 0.0f;
    };

    std::vector<Candidate> candidates;
    candidates.reserve(8);

    for (int step = 0; step < 16; ++step)
    {
        if (referenceFeel.presence[static_cast<size_t>(step)] < 0.56f)
            continue;

        if (bar.lowEnd == DrillLowEndIntent::Move && (step < 6 || step > 13))
            continue;
        if (bar.lowEnd == DrillLowEndIntent::Release && step < 12)
            continue;
        if (bar.lowEnd == DrillLowEndIntent::Anchor && step < 8)
            continue;

        if (std::find(existingStarts.begin(), existingStarts.end(), step) != existingStarts.end())
            continue;

        if (std::any_of(existingStarts.begin(), existingStarts.end(), [step](int existing)
        {
            return std::abs(existing - step) < 4;
        }))
            continue;

        bool collidesWithSnare = false;
        for (const int snareStep : bar.anchorMap.snareAnchorSteps)
            if (snareStep >= 0 && step == snareStep)
                collidesWithSnare = true;
        if (collidesWithSnare)
            continue;

        float score = referenceFeel.presence[static_cast<size_t>(step)];
        if (bar.lowEnd == DrillLowEndIntent::Move && step > 0 && step < 15)
            score += 0.12f + referenceFeel.supportRatio * 0.08f;
        if (bar.lowEnd == DrillLowEndIntent::Release && isPickupLikeKickStep(step))
            score += 0.18f + referenceFeel.pickupRatio * 0.10f;
        if (bar.lowEnd == DrillLowEndIntent::Anchor && step >= 8)
            score += 0.06f;

        for (const int lowEndStep : bar.anchorMap.lowEndAnchorSteps)
            if (lowEndStep == step)
                score += 0.10f;

        candidates.push_back({ step, score });
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& lhs, const Candidate& rhs)
    {
        if (lhs.score != rhs.score)
            return lhs.score > rhs.score;
        return lhs.step < rhs.step;
    });

    if (candidates.empty())
        return std::nullopt;

    return candidates.front().score >= (bar.lowEnd == DrillLowEndIntent::Move ? 0.82f : 0.88f)
        ? std::optional<int>(candidates.front().step)
        : std::nullopt;
}

std::vector<int> selectStartsForBar(const std::vector<int>& kickStarts,
                                    const DrillPhraseBarPlan& bar,
                                    const ReferenceDrillKickFeel* referenceFeel)
{
    std::vector<int> starts;

    if (kickStarts.empty())
    {
        starts.push_back(bar.anchorMap.lowEndAnchorSteps[0] >= 0 ? bar.anchorMap.lowEndAnchorSteps[0] : 0);
        return starts;
    }

    starts.push_back(kickStarts.front());
    if (bar.lowEnd == DrillLowEndIntent::Hold)
        return starts;

    if (bar.lowEnd == DrillLowEndIntent::Anchor)
    {
        return starts;
    }

    if (bar.lowEnd == DrillLowEndIntent::Move)
    {
        if (const auto kickSupport = selectKickSupportStart(kickStarts, bar, starts); kickSupport.has_value())
            starts.push_back(*kickSupport);

        if (referenceFeel != nullptr && starts.size() < 2)
        {
            if (const auto referenceStep = selectReferenceStart(*referenceFeel, bar, starts); referenceStep.has_value())
                starts.push_back(*referenceStep);
        }
        std::sort(starts.begin(), starts.end());
        starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
        return starts;
    }

    if (bar.lowEnd == DrillLowEndIntent::Release)
    {
        if (const auto kickSupport = selectKickSupportStart(kickStarts, bar, starts); kickSupport.has_value())
            starts.push_back(*kickSupport);
        else if (bar.anchorMap.lowEndAnchorSteps[1] >= 0 && std::abs(bar.anchorMap.lowEndAnchorSteps[1] - starts.front()) >= 4)
            starts.push_back(bar.anchorMap.lowEndAnchorSteps[1]);

        if (referenceFeel != nullptr && starts.size() < 2)
        {
            if (const auto referenceStep = selectReferenceStart(*referenceFeel, bar, starts); referenceStep.has_value())
                starts.push_back(*referenceStep);
        }

        std::sort(starts.begin(), starts.end());
        starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
        return starts;
    }

    std::sort(starts.begin(), starts.end());
    starts.erase(std::unique(starts.begin(), starts.end()), starts.end());

    return starts;
}

int computeLengthForStart(int currentStepInBar,
                          int nextStepInBar,
                          int snareAnchor,
                          DrillLowEndIntent intent,
                          DrillPhraseBarRole role,
                          const ReferenceDrillKickFeel* referenceFeel)
{
    int barLimit = nextStepInBar >= 0 ? std::max(1, nextStepInBar - currentStepInBar) : std::max(1, 16 - currentStepInBar);
    if (snareAnchor > currentStepInBar && snareAnchor - currentStepInBar >= 2)
        barLimit = std::min(barLimit, snareAnchor - currentStepInBar);

    int minLength = 2;
    int maxLength = 6;
    if (intent == DrillLowEndIntent::Hold)
    {
        minLength = 4;
        maxLength = 10;
    }
    else if (intent == DrillLowEndIntent::Move)
    {
        minLength = 2;
        maxLength = 3;
    }
    else if (intent == DrillLowEndIntent::Release)
    {
        minLength = role == DrillPhraseBarRole::Release ? 5 : 4;
        maxLength = 7;
    }

    if (referenceFeel != nullptr && referenceFeel->available)
    {
        const float presence = referenceFeel->presence[static_cast<size_t>(std::clamp(currentStepInBar, 0, 15))];
        if (intent == DrillLowEndIntent::Move)
        {
            if (referenceFeel->density > 0.55f)
                maxLength = std::max(minLength, maxLength - 1);
            if (presence > 0.75f)
                minLength = std::max(1, minLength - 1);
        }
        else if (intent == DrillLowEndIntent::Release && isPickupLikeKickStep(currentStepInBar))
        {
            maxLength = std::min(10, maxLength + 1);
        }
    }

    return std::clamp(barLimit, minLength, maxLength);
}
} // namespace

void Drill808Generator::generate(TrackState& subTrack,
                                 const TrackState& kickTrack,
                                 const PatternProject& project,
                                 const DrillPhrasePlan& phrasePlan,
                                 const TrackState* snareTrack,
                                 std::mt19937& rng) const
{
    juce::ignoreUnused(snareTrack);

    subTrack.sub808Notes.clear();
    subTrack.notes.clear();
    subTrack.subProfile = "Main";
    subTrack.laneRole = "drill_sub";
    subTrack.sub808Settings.mono = true;
    subTrack.sub808Settings.cutItself = true;
    subTrack.sub808Settings.overlapMode = Sub808OverlapMode::Glide;
    subTrack.sub808Settings.scaleSnapPolicy = Sub808ScaleSnapPolicy::ForceToScale;
    subTrack.sub808Settings.glideTimeMs = 120;

    std::uniform_int_distribution<int> velocity(84, 110);
    int previousPitch = -1;

    for (const auto& bar : phrasePlan.bars)
    {
        const int barStart = bar.barIndex * 16;
        const int snareAnchor = bar.anchorMap.snareAnchorSteps[0];
        const auto referenceFeel = buildReferenceDrillKickFeel(project.styleInfluence, bar.barIndex, project.params.seed);
        auto starts = selectStartsForBar(collectKickStarts(kickTrack, bar.barIndex),
                                         bar,
                                         referenceFeel.available ? &referenceFeel : nullptr);
        std::sort(starts.begin(), starts.end());
        starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
        if (starts.empty())
            starts.push_back(0);

        for (size_t index = 0; index < starts.size(); ++index)
        {
            const int stepInBar = starts[index];
            const int nextStep = index + 1 < starts.size() ? starts[index + 1] : -1;
            Sub808NoteEvent note;
            note.step = barStart + stepInBar;
            note.length = computeLengthForStart(stepInBar,
                                                nextStep,
                                                snareAnchor,
                                                bar.lowEnd,
                                                bar.role,
                                                referenceFeel.available ? &referenceFeel : nullptr);
            note.velocity = velocity(rng);
            if (referenceFeel.available)
                note.velocity = std::clamp(note.velocity + static_cast<int>(std::round(referenceFeel.presence[static_cast<size_t>(stepInBar)] * 6.0f)), 80, 118);
            note.microOffset = 0;
            note.pitch = choosePitch(previousPitch, project.params.keyRoot, project.params.scaleMode, bar.lowEnd, bar.role, rng);
            note.semanticRole = bar.lowEnd == DrillLowEndIntent::Move ? "drill_sub_move"
                : (bar.lowEnd == DrillLowEndIntent::Hold ? "drill_sub_hold"
                : (bar.lowEnd == DrillLowEndIntent::Release ? "drill_sub_release" : "drill_sub_anchor"));

            subTrack.sub808Notes.push_back(note);
            previousPitch = note.pitch;
        }
    }

    int slideBudget = std::max(1, phrasePlan.phraseSpanBars / 4);
    for (size_t index = 0; index + 1 < subTrack.sub808Notes.size(); ++index)
    {
        auto& current = subTrack.sub808Notes[index];
        auto& next = subTrack.sub808Notes[index + 1];
        const int gap = next.step - current.step;
        const int interval = std::abs(next.pitch - current.pitch);
        const bool sameBar = (current.step / 16) == (next.step / 16);
        const bool phraseSlide = current.semanticRole == "drill_sub_release"
            || (current.semanticRole == "drill_sub_move" && current.length <= 3);
        if (slideBudget > 0 && sameBar && phraseSlide && gap >= 2 && gap <= 3 && interval >= 2 && interval <= 5)
        {
            current.glideToNext = true;
            current.isLegato = true;
            next.isSlide = true;
            current.length = std::max(current.length, gap + 1);
            --slideBudget;
        }
    }

    subTrack.notes = toLegacyNoteEvents(subTrack.sub808Notes);
}
} // namespace bbg