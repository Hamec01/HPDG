#include "PatternBlendEngine.h"

#include <algorithm>
#include <cmath>

#include "../Core/ProjectLaneAccess.h"
#include "../Core/ProjectStateController.h"

namespace bbg
{
namespace
{
constexpr std::array<TrackType, 11> kSampleApplyLanes {
    TrackType::Kick,
    TrackType::GhostKick,
    TrackType::Snare,
    TrackType::ClapGhostSnare,
    TrackType::HiHat,
    TrackType::OpenHat,
    TrackType::Perc,
    TrackType::Ride,
    TrackType::Cymbal,
    TrackType::HatFX,
    TrackType::Sub808
};

int stepInBar(int step)
{
    const int normalized = step % 16;
    return normalized < 0 ? normalized + 16 : normalized;
}

bool isCoreLane(TrackType lane)
{
    return lane == TrackType::Kick || lane == TrackType::Snare || lane == TrackType::Sub808;
}

bool isDecorativeLane(TrackType lane)
{
    switch (lane)
    {
        case TrackType::GhostKick:
        case TrackType::ClapGhostSnare:
        case TrackType::OpenHat:
        case TrackType::Ride:
        case TrackType::Cymbal:
        case TrackType::Perc:
        case TrackType::HatFX:
            return true;

        default:
            return false;
    }
}

bool isAnchorStep(TrackType lane, int step)
{
    const int localStep = stepInBar(step);
    switch (lane)
    {
        case TrackType::Kick:
        case TrackType::GhostKick:
        case TrackType::Sub808:
            return localStep == 0 || localStep == 8;

        case TrackType::Snare:
        case TrackType::ClapGhostSnare:
            return localStep == 4 || localStep == 12;

        case TrackType::HiHat:
        case TrackType::Ride:
            return (localStep % 2) == 0;

        default:
            return false;
    }
}

float notePriority(TrackType lane, const NoteEvent& note, bool sampleSource)
{
    float score = static_cast<float>(note.velocity) / 127.0f;

    if (sampleSource)
        score += 0.9f;
    if (isAnchorStep(lane, note.step))
        score += 2.2f;
    if (note.semanticRole.containsIgnoreCase("backbone") || note.semanticRole.containsIgnoreCase("anchor"))
        score += 1.7f;
    if (note.semanticRole.containsIgnoreCase("sample_copy"))
        score += 1.2f;
    if (isDecorativeLane(lane) && stepInBar(note.step) >= 12)
        score += 0.25f;
    if (note.isGhost)
        score -= 0.2f;

    return score;
}

bool slotsCollide(TrackType lane, const NoteEvent& left, const NoteEvent& right)
{
    if (left.step == right.step)
        return true;

    if (lane != TrackType::Sub808)
        return false;

    const int leftEnd = left.step + juce::jmax(1, left.length);
    const int rightEnd = right.step + juce::jmax(1, right.length);
    return left.step < rightEnd && right.step < leftEnd;
}

bool noteSequencesEqual(const std::vector<NoteEvent>& left, const std::vector<NoteEvent>& right)
{
    if (left.size() != right.size())
        return false;

    for (size_t index = 0; index < left.size(); ++index)
    {
        const auto& lhs = left[index];
        const auto& rhs = right[index];
        if (lhs.pitch != rhs.pitch
            || lhs.step != rhs.step
            || lhs.length != rhs.length
            || lhs.velocity != rhs.velocity
            || lhs.microOffset != rhs.microOffset
            || lhs.isGhost != rhs.isGhost
            || lhs.semanticRole != rhs.semanticRole
            || lhs.isSlide != rhs.isSlide
            || lhs.isLegato != rhs.isLegato
            || lhs.glideToNext != rhs.glideToNext)
        {
            return false;
        }
    }

    return true;
}

void dedupeAndSort(std::vector<NoteEvent>& notes)
{
    std::sort(notes.begin(), notes.end(), [](const NoteEvent& left, const NoteEvent& right)
    {
        if (left.step != right.step)
            return left.step < right.step;
        if (left.pitch != right.pitch)
            return left.pitch < right.pitch;
        return left.velocity > right.velocity;
    });

    notes.erase(std::unique(notes.begin(), notes.end(), [](const NoteEvent& left, const NoteEvent& right)
    {
        return left.step == right.step && left.pitch == right.pitch;
    }), notes.end());
}

std::vector<NoteEvent> prioritizedNotes(const std::vector<NoteEvent>& source, TrackType lane, bool sampleSource)
{
    auto result = source;
    std::stable_sort(result.begin(), result.end(), [lane, sampleSource](const NoteEvent& left, const NoteEvent& right)
    {
        const float leftScore = notePriority(lane, left, sampleSource);
        const float rightScore = notePriority(lane, right, sampleSource);
        if (std::abs(leftScore - rightScore) > 0.001f)
            return leftScore > rightScore;
        if (left.step != right.step)
            return left.step < right.step;
        return left.pitch < right.pitch;
    });
    return result;
}

bool containsCollision(const std::vector<NoteEvent>& notes, const NoteEvent& candidate, TrackType lane)
{
    return std::any_of(notes.begin(), notes.end(), [&](const NoteEvent& existing)
    {
        return slotsCollide(lane, existing, candidate);
    });
}

void overlayOrAdd(std::vector<NoteEvent>& notes, const NoteEvent& candidate, TrackType lane)
{
    auto it = std::find_if(notes.begin(), notes.end(), [&](const NoteEvent& existing)
    {
        return slotsCollide(lane, existing, candidate);
    });

    if (it == notes.end())
    {
        notes.push_back(candidate);
        return;
    }

    if (notePriority(lane, candidate, true) >= notePriority(lane, *it, false))
        *it = candidate;
}

void pruneToCount(std::vector<NoteEvent>& notes, TrackType lane, int maxCount)
{
    if (maxCount < 0 || static_cast<int>(notes.size()) <= maxCount)
        return;

    auto prioritized = prioritizedNotes(notes, lane, false);
    prioritized.resize(static_cast<size_t>(maxCount));
    dedupeAndSort(prioritized);
    notes = std::move(prioritized);
}

std::vector<NoteEvent> blendLaneNotes(const std::vector<NoteEvent>& generated,
                                      const std::vector<NoteEvent>& extracted,
                                      TrackType lane,
                                      const SampleApplyWeights& weights)
{
    if (weights.exactCopy)
        return extracted;

    const float sampleWeight = lane == TrackType::Sub808 ? weights.extractedBassWeight : weights.extractedDrumsWeight;
    const float genreWeight = lane == TrackType::Sub808 ? weights.generatedBassWeight : weights.generatedDrumsWeight;

    if (extracted.empty())
        return genreWeight > 0.0f ? generated : std::vector<NoteEvent> {};

    if (generated.empty())
        return sampleWeight > 0.0f ? extracted : std::vector<NoteEvent> {};

    std::vector<NoteEvent> result;

    if (sampleWeight > genreWeight + 0.15f)
    {
        result = extracted;
        const auto genreCandidates = prioritizedNotes(generated, lane, false);
        const int maxAdds = juce::jlimit(0,
                                         static_cast<int>(genreCandidates.size()),
                                         static_cast<int>(std::round(weights.genreFillAmount * 0.5f * static_cast<float>(genreCandidates.size()))));
        int added = 0;
        for (const auto& note : genreCandidates)
        {
            if (added >= maxAdds)
                break;
            if (containsCollision(result, note, lane))
                continue;

            result.push_back(note);
            ++added;
        }
    }
    else if (genreWeight > sampleWeight + 0.15f)
    {
        result = generated;
        for (const auto& note : prioritizedNotes(extracted, lane, true))
        {
            if (!isCoreLane(lane) && notePriority(lane, note, true) < 1.8f)
                continue;

            overlayOrAdd(result, note, lane);
        }
    }
    else
    {
        result = generated;
        for (const auto& note : prioritizedNotes(extracted, lane, true))
            overlayOrAdd(result, note, lane);

        if (isDecorativeLane(lane))
        {
            const int targetCount = juce::jmax(static_cast<int>(extracted.size()),
                                               static_cast<int>(std::round(0.5f * static_cast<float>(generated.size() + extracted.size()))));
            pruneToCount(result, lane, targetCount);
        }
    }

    dedupeAndSort(result);
    return result;
}

void clearTrackContent(TrackState& state)
{
    state.notes.clear();
    state.baseNotes.clear();
    state.sub808Notes.clear();
    state.baseSub808Notes.clear();
}

void assignLaneNotes(PatternProject& project, TrackState& state, TrackType lane, const std::vector<NoteEvent>& notes)
{
    if (notes.empty())
    {
        state.notes.clear();
        if (lane == TrackType::Sub808)
            state.sub808Notes.clear();
        return;
    }

    state.enabled = true;
    ProjectStateController::setTrackNotes(project, lane, notes);
}
}

juce::String describePatternBlendReport(const PatternBlendReport& report)
{
    return "Sample apply result: copied " + juce::String(report.copiedLaneCount)
        + " | blended " + juce::String(report.blendedLaneCount)
        + " | cleared " + juce::String(report.clearedLaneCount)
        + (report.exactCopy ? " | exact copy" : "");
}

PatternBlendReport PatternBlendEngine::apply(PatternProject& project,
                                             const ExtractedPatternData& extracted,
                                             const SampleApplyWeights& weights)
{
    PatternBlendReport report;
    report.exactCopy = weights.exactCopy;

    for (const auto lane : kSampleApplyLanes)
    {
        auto* state = ProjectLaneAccess::findTrackState(project, lane);
        if (state == nullptr || state->locked)
            continue;

        const auto& extractedNotes = extracted.laneNotes[static_cast<size_t>(trackTypeIndex(lane))];
        const auto existingNotes = state->notes;
        const auto blendedNotes = blendLaneNotes(existingNotes, extractedNotes, lane, weights);

        if (weights.exactCopy)
            clearTrackContent(*state);

        if (noteSequencesEqual(existingNotes, blendedNotes) && !(weights.exactCopy && existingNotes.empty() && !extractedNotes.empty()))
            continue;

        assignLaneNotes(project, *state, lane, blendedNotes);
        report.changedTracks.insert(lane);

        if (weights.exactCopy)
        {
            if (!blendedNotes.empty())
                ++report.copiedLaneCount;
            else if (!existingNotes.empty())
                ++report.clearedLaneCount;
            continue;
        }

        if (!extractedNotes.empty() && !existingNotes.empty())
            ++report.blendedLaneCount;
        else if (!extractedNotes.empty())
            ++report.copiedLaneCount;
        else if (blendedNotes.empty() && !existingNotes.empty())
            ++report.clearedLaneCount;
    }

    return report;
}
} // namespace bbg