#include "GridEditActions.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace bbg::GridEditActions
{
namespace
{
struct NoteSignature
{
    int pitch = 0;
    int step = 0;
    int length = 1;
    int velocity = 100;
    int microOffset = 0;
    bool isGhost = false;
    juce::String semanticRole;

    bool operator<(const NoteSignature& other) const
    {
        if (pitch != other.pitch)
            return pitch < other.pitch;
        if (step != other.step)
            return step < other.step;
        if (length != other.length)
            return length < other.length;
        if (velocity != other.velocity)
            return velocity < other.velocity;
        if (microOffset != other.microOffset)
            return microOffset < other.microOffset;
        if (isGhost != other.isGhost)
            return isGhost < other.isGhost;
        return semanticRole < other.semanticRole;
    }
};

struct PlannedMove
{
    RuntimeLaneId laneId;
    NoteEvent note;
};

NoteSignature makeSignature(const NoteEvent& note)
{
    return { note.pitch,
             note.step,
             note.length,
             note.velocity,
             note.microOffset,
             note.isGhost,
             note.semanticRole };
}

void sortTrackNotes(TrackState& track)
{
    std::stable_sort(track.notes.begin(), track.notes.end(), [](const NoteEvent& a, const NoteEvent& b)
    {
        if (a.step != b.step)
            return a.step < b.step;
        return a.microOffset < b.microOffset;
    });
}
}

TrackState* findMutableTrack(ModelContext context, const RuntimeLaneId& laneId)
{
    auto it = std::find_if(context.project.tracks.begin(), context.project.tracks.end(), [&laneId](const TrackState& track)
    {
        return track.laneId == laneId;
    });

    return (it != context.project.tracks.end()) ? &(*it) : nullptr;
}

const TrackState* findTrack(const PatternProject& project, const RuntimeLaneId& laneId)
{
    auto it = std::find_if(project.tracks.begin(), project.tracks.end(), [&laneId](const TrackState& track)
    {
        return track.laneId == laneId;
    });

    return (it != project.tracks.end()) ? &(*it) : nullptr;
}

int noteStartTick(const NoteEvent& note, int ticksPerStep)
{
    return note.step * ticksPerStep + note.microOffset;
}

int noteEndTick(const NoteEvent& note, int ticksPerStep)
{
    return noteStartTick(note, ticksPerStep) + std::max(1, note.length) * ticksPerStep;
}

bool setNoteStartTick(NoteEvent& note, int targetTick, int bars, int ticksPerStep)
{
    const int maxTicks = juce::jmax(1, bars * 16 * ticksPerStep);
    int clamped = juce::jlimit(0, maxTicks - 1, targetTick);
    int step = clamped / ticksPerStep;
    int micro = clamped - step * ticksPerStep;
    if (micro > ticksPerStep / 2)
    {
        micro -= ticksPerStep;
        ++step;
    }

    step = juce::jlimit(0, bars * 16 - 1, step);
    note.step = step;
    note.microOffset = juce::jlimit(-240, 240, micro);
    return true;
}

bool hasNoteAtTick(const PatternProject& project, const RuntimeLaneId& laneId, int tick, int ticksPerStep)
{
    if (const auto* track = findTrack(project, laneId))
    {
        for (const auto& note : track->notes)
        {
            if (noteStartTick(note, ticksPerStep) == tick)
                return true;
        }
    }

    return false;
}

bool addNote(ModelContext context, const RuntimeLaneId& laneId, const NoteEvent& note)
{
    auto* track = findMutableTrack(context, laneId);
    if (track == nullptr)
        return false;

    track->notes.push_back(note);
    sortTrackNotes(*track);
    return true;
}

bool removeNotes(ModelContext context, const std::vector<SelectedNoteRef>& refs, std::set<RuntimeLaneId>* changedLaneIds)
{
    auto sortedRefs = refs;
    std::sort(sortedRefs.begin(), sortedRefs.end(), [](const SelectedNoteRef& a, const SelectedNoteRef& b)
    {
        if (a.laneId != b.laneId)
            return a.laneId > b.laneId;
        return a.index > b.index;
    });

    bool changed = false;
    for (const auto& ref : sortedRefs)
    {
        auto* track = findMutableTrack(context, ref.laneId);
        if (track == nullptr || ref.index < 0 || ref.index >= static_cast<int>(track->notes.size()))
            continue;

        track->notes.erase(track->notes.begin() + ref.index);
        if (changedLaneIds != nullptr)
            changedLaneIds->insert(ref.laneId);
        changed = true;
    }

    return changed;
}

bool splitNote(ModelContext context, const RuntimeLaneId& laneId, int noteIndex, int cutTick)
{
    auto* track = findMutableTrack(context, laneId);
    if (track == nullptr || noteIndex < 0 || noteIndex >= static_cast<int>(track->notes.size()))
        return false;

    auto& note = track->notes[static_cast<size_t>(noteIndex)];
    const int startTick = noteStartTick(note, context.ticksPerStep);
    const int endTick = noteEndTick(note, context.ticksPerStep);
    const int minLenTicks = context.ticksPerStep;
    if (cutTick <= startTick + minLenTicks || cutTick >= endTick - minLenTicks)
        return false;

    NoteEvent second = note;
    setNoteStartTick(second, cutTick, juce::jmax(1, context.project.params.bars), context.ticksPerStep);
    note.length = juce::jmax(1, static_cast<int>(std::ceil(static_cast<double>(cutTick - startTick) / static_cast<double>(context.ticksPerStep))));
    second.length = juce::jmax(1, static_cast<int>(std::ceil(static_cast<double>(endTick - cutTick) / static_cast<double>(context.ticksPerStep))));
    track->notes.push_back(second);
    sortTrackNotes(*track);
    return true;
}

bool changeVelocityDelta(ModelContext context, const std::vector<DragSnapshot>& snapshots, int deltaVel, int* velocitySum, int* startVelocitySum, int* velocityCount)
{
    bool changed = false;
    int sum = 0;
    int startSum = 0;
    int count = 0;

    for (const auto& snapshot : snapshots)
    {
        auto* track = findMutableTrack(context, snapshot.laneId);
        if (track == nullptr || snapshot.index < 0 || snapshot.index >= static_cast<int>(track->notes.size()))
            continue;

        auto& note = track->notes[static_cast<size_t>(snapshot.index)];
        const int next = juce::jlimit(1, 127, snapshot.startVelocity + deltaVel);
        if (note.velocity != next)
        {
            note.velocity = next;
            changed = true;
        }

        sum += note.velocity;
        startSum += snapshot.startVelocity;
        ++count;
    }

    if (velocitySum != nullptr)
        *velocitySum = sum;
    if (startVelocitySum != nullptr)
        *startVelocitySum = startSum;
    if (velocityCount != nullptr)
        *velocityCount = count;
    return changed;
}

bool changeVelocityAbsolute(ModelContext context, const std::vector<DragSnapshot>& snapshots, int targetVelocity, int* velocitySum, int* startVelocitySum, int* velocityCount)
{
    const int clampedVelocity = juce::jlimit(1, 127, targetVelocity);
    bool changed = false;

    if (velocitySum != nullptr)
        *velocitySum = 0;
    if (startVelocitySum != nullptr)
        *startVelocitySum = 0;
    if (velocityCount != nullptr)
        *velocityCount = 0;

    for (const auto& snap : snapshots)
    {
        auto* track = findMutableTrack(context, snap.laneId);
        if (track == nullptr || snap.index < 0 || snap.index >= static_cast<int>(track->notes.size()))
            continue;

        auto& note = track->notes[static_cast<size_t>(snap.index)];
        if (note.velocity != clampedVelocity)
        {
            note.velocity = clampedVelocity;
            changed = true;
        }

        if (velocitySum != nullptr)
            *velocitySum += note.velocity;
        if (startVelocitySum != nullptr)
            *startVelocitySum += snap.startVelocity;
        if (velocityCount != nullptr)
            ++(*velocityCount);
    }

    return changed;
}

bool changeVelocityWave(ModelContext context,
                        const std::vector<DragSnapshot>& snapshots,
                        int deltaVel,
                        int minMouseX,
                        int maxMouseX,
                        int currentMouseX,
                        float stepWidth,
                        int dragStartX,
                        int* velocitySum,
                        int* startVelocitySum,
                        int* velocityCount)
{
    bool changed = false;
    int sum = 0;
    int startSum = 0;
    int count = 0;
    const int span = juce::jmax(1, maxMouseX - minMouseX);

    for (const auto& snapshot : snapshots)
    {
        auto* track = findMutableTrack(context, snapshot.laneId);
        if (track == nullptr || snapshot.index < 0 || snapshot.index >= static_cast<int>(track->notes.size()))
            continue;

        auto& note = track->notes[static_cast<size_t>(snapshot.index)];
        const int noteX = static_cast<int>(std::round((static_cast<float>(snapshot.startTick) / static_cast<float>(context.ticksPerStep)) * stepWidth));
        const float ratio = juce::jlimit(0.0f, 1.0f, static_cast<float>(noteX - minMouseX) / static_cast<float>(span));
        const float weighted = currentMouseX >= dragStartX ? ratio : (1.0f - ratio);
        const int next = juce::jlimit(1, 127, snapshot.startVelocity + static_cast<int>(std::round(static_cast<float>(deltaVel) * weighted)));
        if (note.velocity != next)
        {
            note.velocity = next;
            changed = true;
        }

        sum += note.velocity;
        startSum += snapshot.startVelocity;
        ++count;
    }

    if (velocitySum != nullptr)
        *velocitySum = sum;
    if (startVelocitySum != nullptr)
        *startVelocitySum = startSum;
    if (velocityCount != nullptr)
        *velocityCount = count;
    return changed;
}

bool changeMicroOffset(ModelContext context, const std::vector<DragSnapshot>& snapshots, int deltaTicks)
{
    bool changed = false;
    const int microLimit = juce::jmax(1, context.ticksPerStep / 2);
    for (const auto& snapshot : snapshots)
    {
        auto* track = findMutableTrack(context, snapshot.laneId);
        if (track == nullptr || snapshot.index < 0 || snapshot.index >= static_cast<int>(track->notes.size()))
            continue;

        auto& note = track->notes[static_cast<size_t>(snapshot.index)];
        const int nextMicro = juce::jlimit(-microLimit, microLimit, snapshot.startMicroOffset + deltaTicks);
        if (note.microOffset != nextMicro)
        {
            note.microOffset = nextMicro;
            changed = true;
        }
    }

    return changed;
}

bool moveSelection(ModelContext context,
                   const std::vector<SelectedNoteRef>& currentSelection,
                   const std::vector<DragSnapshot>& snapshots,
                   const std::vector<RuntimeLaneId>& laneOrder,
                   int deltaSteps,
                   int deltaLanes,
                   int deltaMicro,
                   std::vector<SelectedNoteRef>* movedSelection,
                   std::set<RuntimeLaneId>* changedLaneIds)
{
    if (snapshots.empty() || laneOrder.empty())
        return false;

    if (deltaSteps == 0 && deltaLanes == 0 && deltaMicro == 0)
        return false;

    const int maxTick = context.project.params.bars * 16 * context.ticksPerStep - 1;
    std::vector<PlannedMove> plannedMoves;
    plannedMoves.reserve(snapshots.size());

    bool changed = false;
    for (const auto& snapshot : snapshots)
    {
        auto laneIt = std::find(laneOrder.begin(), laneOrder.end(), snapshot.laneId);
        if (laneIt == laneOrder.end())
            continue;

        const int sourceLaneIndex = static_cast<int>(std::distance(laneOrder.begin(), laneIt));
        const int targetLaneIndex = juce::jlimit(0,
                                                 static_cast<int>(laneOrder.size()) - 1,
                                                 sourceLaneIndex + deltaLanes);
        const auto& targetLaneId = laneOrder[static_cast<size_t>(targetLaneIndex)];

        NoteEvent movedNote = snapshot.sourceNote;
        const int targetTick = juce::jlimit(0,
                                            juce::jmax(0, maxTick),
                                            snapshot.startTick + deltaSteps * context.ticksPerStep + deltaMicro);
        setNoteStartTick(movedNote, targetTick, context.project.params.bars, context.ticksPerStep);

        if (targetLaneId != snapshot.laneId
            || movedNote.step != snapshot.sourceNote.step
            || movedNote.microOffset != snapshot.sourceNote.microOffset)
        {
            changed = true;
        }

        plannedMoves.push_back({ targetLaneId, movedNote });
    }

    if (!changed || plannedMoves.empty())
        return false;

    std::map<RuntimeLaneId, std::map<NoteSignature, int>> insertedCounts;

    removeNotes(context, currentSelection, changedLaneIds);

    std::map<RuntimeLaneId, std::map<NoteSignature, int>> existingCounts;
    for (const auto& track : context.project.tracks)
        for (const auto& note : track.notes)
            ++existingCounts[track.laneId][makeSignature(note)];

    for (const auto& move : plannedMoves)
    {
        auto* track = findMutableTrack(context, move.laneId);
        if (track == nullptr)
            continue;

        track->notes.push_back(move.note);
        if (changedLaneIds != nullptr)
            changedLaneIds->insert(move.laneId);
        ++insertedCounts[move.laneId][makeSignature(move.note)];
    }

    for (auto& track : context.project.tracks)
        sortTrackNotes(track);

    if (movedSelection != nullptr)
    {
        movedSelection->clear();
        for (const auto& laneId : laneOrder)
        {
            auto* track = findMutableTrack(context, laneId);
            if (track == nullptr)
                continue;

            auto existingIt = existingCounts.find(laneId);
            auto insertedIt = insertedCounts.find(laneId);
            if (insertedIt == insertedCounts.end())
                continue;

            auto existingForLane = existingIt != existingCounts.end()
                ? existingIt->second
                : std::map<NoteSignature, int>{};
            auto& insertedForLane = insertedIt->second;

            for (int noteIndex = 0; noteIndex < static_cast<int>(track->notes.size()); ++noteIndex)
            {
                const auto signature = makeSignature(track->notes[static_cast<size_t>(noteIndex)]);
                auto existingCount = existingForLane.find(signature);
                if (existingCount != existingForLane.end() && existingCount->second > 0)
                {
                    --existingCount->second;
                    continue;
                }

                auto insertedCount = insertedForLane.find(signature);
                if (insertedCount == insertedForLane.end() || insertedCount->second <= 0)
                    continue;

                movedSelection->push_back({ laneId, noteIndex });
                --insertedCount->second;
            }
        }
    }

    return changed;
}

bool resizeSelection(ModelContext context, const std::vector<DragSnapshot>& snapshots, int deltaTicks)
{
    if (deltaTicks <= 0)
        return false;

    bool changed = false;
    const int maxTick = context.project.params.bars * 16 * context.ticksPerStep;
    for (const auto& snapshot : snapshots)
    {
        auto* track = findMutableTrack(context, snapshot.laneId);
        if (track == nullptr || snapshot.index < 0 || snapshot.index >= static_cast<int>(track->notes.size()))
            continue;

        auto& note = track->notes[static_cast<size_t>(snapshot.index)];
        const int clampedEndTick = juce::jlimit(snapshot.startTick + context.ticksPerStep, maxTick, snapshot.startEndTick + deltaTicks);
        const int nextLength = juce::jlimit(1,
                                            context.project.params.bars * 16,
                                            static_cast<int>(std::ceil(static_cast<double>(clampedEndTick - snapshot.startTick)
                                                                       / static_cast<double>(context.ticksPerStep))));
        if (note.length != nextLength)
        {
            note.length = nextLength;
            changed = true;
        }
    }

    return changed;
}

bool pasteNotes(ModelContext context,
                const std::vector<ClipboardNote>& clipboardNotes,
                int anchorTick,
                std::vector<SelectedNoteRef>* insertedSelection,
                std::set<RuntimeLaneId>* changedLaneIds)
{
    bool changed = false;
    std::vector<SelectedNoteRef> inserted;
    const int maxTicks = context.project.params.bars * 16 * context.ticksPerStep;

    for (const auto& clipboard : clipboardNotes)
    {
        auto* track = findMutableTrack(context, clipboard.laneId);
        if (track == nullptr)
            continue;

        NoteEvent note = clipboard.note;
        const int targetTick = juce::jlimit(0, juce::jmax(0, maxTicks - 1), anchorTick + clipboard.relativeTick);
        setNoteStartTick(note, targetTick, context.project.params.bars, context.ticksPerStep);
        track->notes.push_back(note);
        sortTrackNotes(*track);

        for (int index = static_cast<int>(track->notes.size()) - 1; index >= 0; --index)
        {
            const auto& current = track->notes[static_cast<size_t>(index)];
            if (noteStartTick(current, context.ticksPerStep) == noteStartTick(note, context.ticksPerStep)
                && current.length == note.length
                && current.velocity == note.velocity
                && current.pitch == note.pitch
                && current.microOffset == note.microOffset)
            {
                inserted.push_back({ clipboard.laneId, index });
                break;
            }
        }

        if (changedLaneIds != nullptr)
            changedLaneIds->insert(clipboard.laneId);
        changed = true;
    }

    if (insertedSelection != nullptr)
        *insertedSelection = std::move(inserted);
    return changed;
}

bool rollSelection(ModelContext context,
                   const std::vector<SelectedNoteRef>& selectedNotes,
                   int divisions,
                   std::vector<SelectedNoteRef>* insertedSelection,
                   std::set<RuntimeLaneId>* changedLaneIds)
{
    if (divisions < 2)
        return false;

    bool changed = false;
    std::vector<SelectedNoteRef> inserted;
    auto refs = selectedNotes;
    std::sort(refs.begin(), refs.end(), [](const SelectedNoteRef& a, const SelectedNoteRef& b)
    {
        if (a.laneId != b.laneId)
            return a.laneId > b.laneId;
        return a.index > b.index;
    });

    for (const auto& ref : refs)
    {
        auto* track = findMutableTrack(context, ref.laneId);
        if (track == nullptr || ref.index < 0 || ref.index >= static_cast<int>(track->notes.size()))
            continue;

        const auto source = track->notes[static_cast<size_t>(ref.index)];
        if (source.length < divisions || (source.length % divisions) != 0)
            continue;

        const int startTick = noteStartTick(source, context.ticksPerStep);
        const int segmentSteps = source.length / divisions;
        track->notes.erase(track->notes.begin() + ref.index);

        for (int division = 0; division < divisions; ++division)
        {
            NoteEvent rolled = source;
            rolled.length = segmentSteps;
            setNoteStartTick(rolled, startTick + division * segmentSteps * context.ticksPerStep, context.project.params.bars, context.ticksPerStep);
            track->notes.push_back(rolled);
        }

        sortTrackNotes(*track);
        for (int index = static_cast<int>(track->notes.size()) - 1; index >= 0; --index)
        {
            const auto& current = track->notes[static_cast<size_t>(index)];
            if (noteStartTick(current, context.ticksPerStep) >= startTick
                && noteStartTick(current, context.ticksPerStep) < startTick + source.length * context.ticksPerStep)
            {
                inserted.push_back({ ref.laneId, index });
            }
        }

        if (changedLaneIds != nullptr)
            changedLaneIds->insert(ref.laneId);
        changed = true;
    }

    if (insertedSelection != nullptr)
        *insertedSelection = std::move(inserted);
    return changed;
}

void setPlaybackFlag(TimelineContext context, int tick)
{
    context.previewStartTick = tick;
    context.previewStartStep = juce::jlimit(0,
                                            juce::jmax(0, context.totalSteps - 1),
                                            tick / juce::jmax(1, context.ticksPerStep));
}

void setLoopRange(TimelineContext context, const std::optional<juce::Range<int>>& tickRange)
{
    context.loopRegionTicks = tickRange;
}
} // namespace bbg::GridEditActions