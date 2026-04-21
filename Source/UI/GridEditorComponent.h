#pragma once

#include <functional>
#include <optional>
#include <set>
#include <unordered_set>
#include <vector>

#include <juce_graphics/juce_graphics.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Core/PatternProject.h"
#include "GridEditActions.h"
#include "GridGeometry.h"

namespace bbg
{
class GridEditorComponent : public juce::Component
{
public:
    struct InputBindings
    {
        juce::ModifierKeys::Flags drawModeModifier = juce::ModifierKeys::shiftModifier;
        int velocityEditKeyCode = 'V';
        int stretchNoteKeyCode = 'R';
        juce::ModifierKeys::Flags stretchNoteModifiers = juce::ModifierKeys::shiftModifier;
        juce::ModifierKeys::Flags panViewModifier = juce::ModifierKeys::ctrlModifier;
        juce::ModifierKeys::Flags horizontalZoomModifier = juce::ModifierKeys::ctrlModifier;
        juce::ModifierKeys::Flags laneHeightZoomModifier = juce::ModifierKeys::shiftModifier;
        juce::ModifierKeys::Flags microtimingEditModifier = juce::ModifierKeys::altModifier;
    };

    enum class EditorTool
    {
        Pencil,
        Brush,
        Select,
        Cut,
        Erase
    };

    enum class GridResolution
    {
        Adaptive,
        Micro,
        OneQuarter,
        OneQuarterTriplet,
        OneEighth,
        OneEighthTriplet,
        OneSixteenth,
        OneSixteenthTriplet,
        OneThirtySecond,
        OneThirtySecondTriplet,
        OneSixtyFourth,
        OneSixtyFourthTriplet
    };

    enum class LaneHeightPreset
    {
        Compact,
        Normal,
        Large
    };

    struct EditorRegionState
    {
        std::optional<juce::Range<int>> selectedTickRange;
        std::optional<juce::Range<int>> loopTickRange;
        std::vector<RuntimeLaneId> activeLaneIds;
        RuntimeLaneId primaryLaneId;
        int previewStartTick = 0;
    };

    void setProject(const PatternProject& value);
    void setStepWidth(float width);
    void setLaneHeight(int height);
    void setGridResolution(GridResolution resolution);
    void setLaneDisplayOrder(const std::vector<RuntimeLaneId>& order);
    void setSelectedTrack(const RuntimeLaneId& laneId);
    void setPlayheadStep(float step);
    void setEditorTool(EditorTool tool);
    void setInputBindings(const InputBindings& bindings);
    void setLoopRegion(const std::optional<juce::Range<int>>& tickRange);
    std::optional<juce::Range<int>> getLoopRegion() const;
    void refreshTransientInputState();
    EditorTool getEditorTool() const { return editorTool; }
    bool deleteSelectedNote();
    bool deleteSelectedNotes();
    bool copySelectionToClipboard();
    bool cutSelectionToClipboard();
    bool pasteClipboardAtPlayhead();
    bool duplicateSelectionToRight();
    bool duplicateSelectionRepeated(int repeatCount);
    void clearNoteSelection();
    bool selectAllNotes();
    bool selectAllNotesInLane(const RuntimeLaneId& laneId);
    bool setSelectionSemanticRole(const juce::String& role);
    bool selectNotesBySemanticRole(const juce::String& role);
    bool nudgeSelectionBySnap(int direction);
    bool nudgeSelectionMicro(int direction);
    bool hasSelection() const { return !selectedNotes.empty(); }
    EditorRegionState getEditorRegionState() const { return editorRegionState; }

    int getGridWidth() const;
    int getGridHeight() const;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

    std::function<void(const RuntimeLaneId&)> onTrackFocused;
    std::function<void(int)> onPlaybackFlagChanged;
    std::function<void(const RuntimeLaneId&, const std::vector<NoteEvent>&)> onTrackNotesEdited;
    std::function<void(float, juce::Point<int>)> onHorizontalZoomGesture;
    std::function<void(float, juce::Point<int>)> onLaneHeightZoomGesture;
    std::function<void(const std::optional<juce::Range<int>>&)> onLoopRegionChanged;
    std::function<void(const juce::String&)> onGridModeDisplayChanged;
    std::function<void(const EditorRegionState&)> onEditorRegionStateChanged;

    float getStepWidth() const { return stepWidth; }
    int getLaneHeightPx() const { return laneHeight; }
    int getRulerHeightPx() const { return rulerHeight; }

private:
    using SelectedNoteRef = GridEditActions::SelectedNoteRef;
    using DragSnapshot = GridEditActions::DragSnapshot;
    using ClipboardNote = GridEditActions::ClipboardNote;

    enum class ContextMenuTarget
    {
        Empty,
        SingleNote,
        Selection
    };

    void invalidateStaticCache();
    void ensureStaticCache();
    std::optional<SelectedNoteRef> findNoteAt(juce::Point<int> position) const;
    std::optional<SelectedNoteRef> findNoteAt(juce::Point<int> position, const RuntimeLaneId& laneId) const;
    std::optional<SelectedNoteRef> findNoteCoveringTick(const RuntimeLaneId& laneId, int tick) const;
    std::optional<SelectedNoteRef> findClosestNoteAt(juce::Point<int> position, const RuntimeLaneId& laneId, int maxDistancePx) const;
    juce::Rectangle<int> noteBounds(const NoteEvent& note, int row) const;
    int noteStartTick(const NoteEvent& note) const;
    int noteEndTick(const NoteEvent& note) const;
    int noteEditorEndTick(const NoteEvent& note) const;
    bool setNoteStartTick(NoteEvent& note, int targetTick, int bars);
    bool eraseNote(const SelectedNoteRef& ref);
    bool eraseNoteAtCell(const RuntimeLaneId& laneId, int tick);
    bool splitNoteAtTick(const RuntimeLaneId& laneId, int noteIndex, int cutTick);
    bool eraseNoteAt(juce::Point<int> position, std::optional<RuntimeLaneId> laneHint = std::nullopt);
    bool eraseNotesAlongSegment(juce::Point<int> from, juce::Point<int> to);
    bool applyDrawAtCell(const RuntimeLaneId& laneId, int tick);
    bool applyEraseAtCell(const RuntimeLaneId& laneId, int tick);
    bool applyStrokeAlongSegment(juce::Point<int> from, juce::Point<int> to, bool erase, std::optional<RuntimeLaneId> fixedLane = std::nullopt);
    void sortTrackNotes(TrackState& track);
    int resizeHandleWidthPx(const NoteEvent& note, int row) const;
    void updateHoverState(juce::Point<int> position);
    void applyCursorForHover();
    bool isSelected(const SelectedNoteRef& ref) const;
    void clearSelectionInternal();
    void addSelection(const SelectedNoteRef& ref);
    void setSingleSelection(const SelectedNoteRef& ref);
    void toggleSelection(const SelectedNoteRef& ref);
    void normalizeSelection();
    void collectDragSnapshots();
    void updateMarqueeSelection();
    bool applySelectionVelocityDeltaWithNotify(int deltaVel);
    bool applySelectionVelocityDelta(int deltaVel);
    bool applySelectionVelocityAbsolute(int targetVelocity);
    bool applySelectionVelocityWave(int deltaVel, int currentMouseX);
    bool applySelectionMicroOffsetDelta(int deltaTicks);
    bool applySelectionMoveDelta(int deltaSteps, int deltaLanes, int deltaMicro);
    bool applySelectionStretchTicks(int deltaTicks, int bars);
    bool copySelectionInternal(bool removeAfterCopy);
    bool pasteClipboardAtTick(int anchorTick,
                              std::optional<RuntimeLaneId> anchorLaneId = std::nullopt,
                              std::vector<SelectedNoteRef>* insertedSelectionOut = nullptr,
                              std::set<RuntimeLaneId>* changedTracksOut = nullptr,
                              bool clearSelectionBeforeInsert = true);
    ContextMenuTarget contextMenuTargetAt(juce::Point<int> p,
                                          std::optional<RuntimeLaneId> laneHint,
                                          std::optional<SelectedNoteRef>* hitOut = nullptr);
    void showNoteContextMenu(juce::Point<int> p, std::optional<RuntimeLaneId> laneHint);
    bool applyContextMenuAction(int actionId, std::optional<RuntimeLaneId> laneHint, juce::Point<int> p);
    bool canQuickRollSelection() const;
    std::vector<int> availableAdvancedRollDivisions() const;
    bool rollSelectionQuick();
    bool rollSelectionWithDivisions(int divisions);
    LaneEditorCapabilities laneEditorCapabilitiesForLane(const RuntimeLaneId& laneId) const;
    bool canEditLaneInGrid(const RuntimeLaneId& laneId) const;
    int clampTickToProject(int tick) const;
    int quantizeTick(int tick) const;
    TrackState* findMutableTrack(const RuntimeLaneId& laneId);
    const TrackState* findTrack(const RuntimeLaneId& laneId) const;
    bool hasNoteAtTick(const RuntimeLaneId& laneId, int tick) const;
    void emitTrackEdited(const RuntimeLaneId& laneId);
    void notifyTrackFocused(const RuntimeLaneId& laneId);
    void refreshEditorRegionState();
    bool beginVelocityEditGesture(const SelectedNoteRef& ref, juce::Point<int> position);
    int velocityFromY(int y, const RuntimeLaneId& laneId) const;
    int currentPasteAnchorTick() const;
    std::optional<RuntimeLaneId> currentPasteAnchorLane() const;
    int tickAtX(int x) const;
    int snappedTickFromTick(int tick) const;
    int quantizedTickAtX(int x, bool snapToGrid) const;
    int stepAtX(int x) const;
    bool placeDrawNoteAt(const RuntimeLaneId& laneId, int tick);
    bool fillLaneRange(const RuntimeLaneId& laneId, juce::Range<int> tickRange, int stepInterval);
    bool isModifierDown(const juce::ModifierKeys& mods, juce::ModifierKeys::Flags modifier) const;
    bool isVelocityEditKeyDown() const;
    bool isStretchEditKeyDown() const;
    bool isSnapEnabled() const;
    int defaultNoteLengthSteps() const;
    int displayedNoteLengthTicks(const NoteEvent& note) const;
    int defaultNoteLengthTicks() const;
    int effectiveSnapTicks() const;
    int adaptiveSnapTicks() const;
    int keyboardNudgeTicks() const;
    int keyboardMicroNudgeTicks() const;
    int visualSubdivisionTicks() const;
    int visualFineSubdivisionTicks() const;
    int snapThresholdTicks() const;
    juce::String effectiveSnapLabel() const;
    juce::String currentGridModeLabel() const;
    void applyLaneHeightPreset(LaneHeightPreset preset);
    bool toggleLaneCollapsed(const RuntimeLaneId& laneId, std::optional<bool> collapsed = std::nullopt);
    bool isLaneCollapsed(const RuntimeLaneId& laneId) const;
    int collapsedLaneHeightPx() const;
    int laneVisualHeight(const RuntimeLaneId& laneId) const;
    juce::Rectangle<int> laneRowBounds(int laneIndex) const;
    std::optional<int> visibleLaneIndexAtY(int y) const;
    std::optional<RuntimeLaneId> trackForYPosition(int y) const;
    bool isLanePinned(const RuntimeLaneId& laneId) const;
    bool toggleLanePinned(const RuntimeLaneId& laneId, std::optional<bool> pinned = std::nullopt);
    void clearPinnedLanes();
    bool isLaneEditorMuted(const RuntimeLaneId& laneId) const;
    bool toggleLaneEditorMuted(const RuntimeLaneId& laneId, std::optional<bool> muted = std::nullopt);
    void clearEditorMutedLanes();
    bool setFocusedLane(const std::optional<RuntimeLaneId>& laneId);
    bool isLaneDimmedByFocus(const RuntimeLaneId& laneId) const;
    void pruneLocalLaneState();
    juce::String laneDisplayLabel(const RuntimeLaneId& laneId) const;
    juce::String laneCompactLabel(const RuntimeLaneId& laneId) const;
    juce::String laneGroupLabel(const RuntimeLaneId& laneId) const;
    juce::Colour laneTintColour(const RuntimeLaneId& laneId) const;
    bool isHelperLane(const RuntimeLaneId& laneId) const;
    bool applyAutoCollapseHelperLanesNow();
    void notifyGridModeDisplayChanged();
    int snapTicksForCurrentResolution() const;
    int ticksForGridResolution() const;
    GridGeometry makeGeometry() const;
    GridEditActions::ModelContext makeModelContext();
    GridEditActions::TimelineContext makeTimelineContext();
    std::vector<RuntimeLaneId> baseVisibleLaneIds() const;
    std::vector<RuntimeLaneId> orderedVisibleLaneIds() const;
    RuntimeLaneId resolvedEditableLaneId(const RuntimeLaneId& laneId) const;

    bool isVisibleLane(const RuntimeLaneId& laneId) const;
    int visibleLaneIndex(const RuntimeLaneId& laneId) const;
    std::optional<RuntimeLaneId> trackForVisibleLaneIndex(int laneIndex) const;

    PatternProject project;
    juce::Image staticGridCache;
    bool staticCacheDirty = true;

    float stepWidth = 20.0f;
    int laneHeight = 30;
    int rulerHeight = 24;
    LaneHeightPreset laneHeightPreset = LaneHeightPreset::Normal;
    bool laneGroupTintEnabled = true;
    bool autoCollapseHelperLanes = false;
    bool strongBarBeatAccents = false;
    std::set<RuntimeLaneId> pinnedLaneIds;
    std::set<RuntimeLaneId> editorMutedLaneIds;
    std::optional<RuntimeLaneId> focusedLaneId;
    std::set<RuntimeLaneId> collapsedLaneIds;
    GridResolution gridResolution = GridResolution::OneSixteenth;
    std::vector<RuntimeLaneId> laneDisplayOrder;
    RuntimeLaneId selectedTrack;
    int previewStartStep = 0;
    int previewStartTick = 0;
    std::optional<juce::Range<int>> loopRegionTicks;
    EditorRegionState editorRegionState;
    float playheadStep = -1.0f;
    int lastPlayheadPixel = -1;

    std::vector<SelectedNoteRef> selectedNotes;
    std::optional<SelectedNoteRef> primarySelectedNote;

    enum class HoverZone
    {
        None,
        NoteBody,
        ResizeLeft,
        ResizeRight
    };

    enum class EditMode
    {
        None,
        Pan,
        MoveNote,
        StretchNote,
        Velocity,
        Draw,
        BrushDraw,
        EraseDrag,
        Cut,
        MicroOffset,
        Marquee
    };

    enum class StrokeMode
    {
        None,
        Draw,
        Erase
    };

    EditorTool editorTool = EditorTool::Pencil;
    InputBindings inputBindings;
    EditMode editMode = EditMode::None;
    int dragStartX = 0;
    int dragStartY = 0;
    int dragOriginalTicks = 0;
    int dragOriginalLength = 1;
    int dragOriginalEndTick = 0;
    int dragOriginalVelocity = 100;
    int dragOriginalMicroOffset = 0;
    int dragOriginalPitch = 36;
    int dragResizeAnchorTick = 0;
    bool resizeFromStart = false;
    bool movedDuringDrag = false;
    juce::Rectangle<int> marqueeRect;
    juce::Point<int> marqueeStart;
    bool marqueeAdditive = false;
    std::vector<SelectedNoteRef> marqueeBaseSelection;
    std::vector<DragSnapshot> dragSnapshots;
    std::vector<ClipboardNote> clipboardNotes;
    int clipboardSourceStartTick = 0;
    int clipboardSpanTicks = 0;
    RuntimeLaneId clipboardSourceAnchorLane;
    int velocityOverlayPercent = 0;
    int velocityOverlayDelta = 0;
    bool velocityWaveModeActive = false;
    juce::Point<int> panStartViewPos;
    juce::Point<int> lastDragPoint;
    int rulerDragStartTick = 0;
    bool rulerDraggingLoop = false;
    bool rulerLoopDragMoved = false;
    RuntimeLaneId drawTrack;
    std::unordered_set<int> drawVisitedTicks;
    std::unordered_set<int> eraseVisitedKeys;
    StrokeMode strokeMode = StrokeMode::None;
    std::set<RuntimeLaneId> strokeEditedLanes;

    std::optional<SelectedNoteRef> hoverNote;
    std::optional<RuntimeLaneId> hoverDrawTrack;
    int hoverDrawTick = -1;
    HoverZone hoverZone = HoverZone::None;
};
} // namespace bbg
