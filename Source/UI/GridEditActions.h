#pragma once

#include <optional>
#include <set>
#include <vector>

#include <juce_core/juce_core.h>

#include "../Core/PatternProject.h"

namespace bbg::GridEditActions
{
struct SelectedNoteRef
{
    RuntimeLaneId laneId;
    int index = -1;
};

struct DragSnapshot
{
    RuntimeLaneId laneId;
    int index = -1;
    NoteEvent sourceNote;
    int startTick = 0;
    int startLength = 1;
    int startEndTick = 0;
    int startVelocity = 100;
    int startMicroOffset = 0;
    int startPitch = 36;
};

struct ClipboardNote
{
    RuntimeLaneId sourceLaneId;
    NoteEvent note;
    int relativeTick = 0;
    int relativeLaneOffset = 0;
};

struct ModelContext
{
    PatternProject& project;
    int ticksPerStep = 1;
};

struct TimelineContext
{
    int& previewStartStep;
    int& previewStartTick;
    std::optional<juce::Range<int>>& loopRegionTicks;
    int ticksPerStep = 1;
    int totalSteps = 0;
};

TrackState* findMutableTrack(ModelContext context, const RuntimeLaneId& laneId);
const TrackState* findTrack(const PatternProject& project, const RuntimeLaneId& laneId);
int noteStartTick(const NoteEvent& note, int ticksPerStep);
int noteEndTick(const NoteEvent& note, int ticksPerStep);
bool setNoteStartTick(NoteEvent& note, int targetTick, int bars, int ticksPerStep);
bool hasNoteAtTick(const PatternProject& project, const RuntimeLaneId& laneId, int tick, int ticksPerStep);
bool addNote(ModelContext context, const RuntimeLaneId& laneId, const NoteEvent& note);
bool removeNotes(ModelContext context, const std::vector<SelectedNoteRef>& refs, std::set<RuntimeLaneId>* changedLaneIds = nullptr);
bool splitNote(ModelContext context, const RuntimeLaneId& laneId, int noteIndex, int cutTick);
bool changeVelocityDelta(ModelContext context, const std::vector<DragSnapshot>& snapshots, int deltaVel, int* velocitySum = nullptr, int* startVelocitySum = nullptr, int* velocityCount = nullptr);
bool changeVelocityAbsolute(ModelContext context, const std::vector<DragSnapshot>& snapshots, int targetVelocity, int* velocitySum = nullptr, int* startVelocitySum = nullptr, int* velocityCount = nullptr);
bool changeVelocityWave(ModelContext context, const std::vector<DragSnapshot>& snapshots, int deltaVel, int minMouseX, int maxMouseX, int currentMouseX, float stepWidth, int dragStartX, int* velocitySum = nullptr, int* startVelocitySum = nullptr, int* velocityCount = nullptr);
bool changeMicroOffset(ModelContext context, const std::vector<DragSnapshot>& snapshots, int deltaTicks);
bool moveSelection(ModelContext context,
                   const std::vector<SelectedNoteRef>& currentSelection,
                   const std::vector<DragSnapshot>& snapshots,
                   const std::vector<RuntimeLaneId>& laneOrder,
                   int deltaSteps,
                   int deltaLanes,
                   int deltaMicro,
                   std::vector<SelectedNoteRef>* movedSelection = nullptr,
                   std::set<RuntimeLaneId>* changedLaneIds = nullptr);
bool resizeSelection(ModelContext context,
                     const std::vector<DragSnapshot>& snapshots,
                     int deltaTicks,
                     bool resizeFromStart);
bool pasteNotes(ModelContext context,
                const std::vector<ClipboardNote>& clipboardNotes,
                int anchorTick,
                const std::vector<RuntimeLaneId>& laneOrder,
                std::optional<RuntimeLaneId> anchorLaneId = std::nullopt,
                std::vector<SelectedNoteRef>* insertedSelection = nullptr,
                std::set<RuntimeLaneId>* changedLaneIds = nullptr);
bool rollSelection(ModelContext context,
                   const std::vector<SelectedNoteRef>& selectedNotes,
                   int divisions,
                   std::vector<SelectedNoteRef>* insertedSelection = nullptr,
                   std::set<RuntimeLaneId>* changedLaneIds = nullptr);
void setPlaybackFlag(TimelineContext context, int tick);
void setLoopRange(TimelineContext context, const std::optional<juce::Range<int>>& tickRange);
} // namespace bbg::GridEditActions