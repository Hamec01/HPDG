#pragma once

#include <functional>
#include <optional>
#include <unordered_set>
#include <vector>

#include <juce_graphics/juce_graphics.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Core/PatternProject.h"

namespace bbg
{
class GridEditorComponent : public juce::Component
{
public:
    struct InputBindings
    {
        juce::ModifierKeys::Flags drawModeModifier = juce::ModifierKeys::shiftModifier;
        int velocityEditKeyCode = 'V';
        int stretchNoteKeyCode = 'S';
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
        OneSixthStep,
        OneQuarterStep,
        OneThirdStep,
        OneHalfStep,
        Step,
        OneSixthBeat,
        OneQuarterBeat,
        OneThirdBeat,
        OneHalfBeat,
        Beat,
        Bar,
        OneEighth,
        OneSixteenth,
        OneThirtySecond,
        OneSixtyFourth,
        Triplet
    };

    void setProject(const PatternProject& value);
    void setStepWidth(float width);
    void setLaneHeight(int height);
    void setGridResolution(GridResolution resolution);
    void setLaneDisplayOrder(const std::vector<TrackType>& order);
    void setSelectedTrack(TrackType type);
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
    void clearNoteSelection();
    bool selectAllNotes();
    bool selectAllNotesInLane(TrackType lane);
    bool hasSelection() const { return !selectedNotes.empty(); }

    int getGridWidth() const;
    int getGridHeight() const;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

    std::function<void(TrackType, int)> onCellClicked;
    std::function<void(TrackType, const std::vector<NoteEvent>&)> onTrackNotesEdited;
    std::function<void(float)> onHorizontalZoomGesture;
    std::function<void(float)> onLaneHeightZoomGesture;
    std::function<void(const std::optional<juce::Range<int>>&)> onLoopRegionChanged;

private:
    struct SelectedNoteRef
    {
        TrackType track = TrackType::Kick;
        int index = -1;
    };

    struct DragSnapshot
    {
        TrackType track = TrackType::Kick;
        int index = -1;
        int startTick = 0;
        int startLength = 1;
        int startEndTick = 0;
        int startVelocity = 100;
        int startMicroOffset = 0;
        int startPitch = 36;
    };

    struct ClipboardNote
    {
        TrackType track = TrackType::Kick;
        NoteEvent note;
        int relativeTick = 0;
    };

    enum class ContextMenuTarget
    {
        Empty,
        SingleNote,
        Selection
    };

    void invalidateStaticCache();
    void ensureStaticCache();
    std::optional<SelectedNoteRef> findNoteAt(juce::Point<int> position) const;
    std::optional<SelectedNoteRef> findNoteAt(juce::Point<int> position, TrackType lane) const;
    std::optional<SelectedNoteRef> findClosestNoteAt(juce::Point<int> position, TrackType lane, int maxDistancePx) const;
    std::optional<SelectedNoteRef> findSub808NoteForPitchEdit(const juce::MouseEvent& event) const;
    juce::Rectangle<int> noteBounds(const NoteEvent& note, int row) const;
    int noteStartTick(const NoteEvent& note) const;
    int noteEndTick(const NoteEvent& note) const;
    bool setNoteStartTick(NoteEvent& note, int targetTick, int bars);
    bool splitNoteAtTick(TrackType trackType, int noteIndex, int cutTick);
    bool eraseNoteAt(juce::Point<int> position, std::optional<TrackType> laneHint = std::nullopt);
    bool eraseNotesAlongSegment(juce::Point<int> from, juce::Point<int> to);
    bool updatePitchForSub808Selection(int semitoneDelta);
    void sortTrackNotes(TrackState& track);
    int resizeHandleWidthPx(const NoteEvent& note, int row) const;
    void updateHoverState(juce::Point<int> position);
    void applyCursorForHover();
    bool isSelected(const SelectedNoteRef& ref) const;
    void clearSelectionInternal();
    void setSingleSelection(const SelectedNoteRef& ref);
    void toggleSelection(const SelectedNoteRef& ref);
    void normalizeSelection();
    void collectDragSnapshots();
    bool applySelectionVelocityDelta(int deltaVel);
    bool applySelectionVelocityWave(int deltaVel, int currentMouseX);
    bool applySelectionMicroOffsetDelta(int deltaTicks);
    bool applySelectionMoveTicks(int deltaTicks, int bars);
    bool applySelectionStretchTicks(int deltaTicks, int bars);
    bool copySelectionInternal(bool removeAfterCopy);
    ContextMenuTarget contextMenuTargetAt(juce::Point<int> p,
                                          std::optional<TrackType> laneHint,
                                          std::optional<SelectedNoteRef>* hitOut = nullptr);
    void showNoteContextMenu(juce::Point<int> p, std::optional<TrackType> laneHint);
    bool applyContextMenuAction(int actionId, std::optional<TrackType> laneHint, juce::Point<int> p);
    bool canQuickRollSelection() const;
    std::vector<int> availableAdvancedRollDivisions() const;
    bool rollSelectionQuick();
    bool rollSelectionWithDivisions(int divisions);
    int clampTickToProject(int tick) const;
    int quantizeTick(int tick) const;
    TrackState* findMutableTrack(TrackType type);
    void emitTrackEdited(TrackType type);
    int tickAtX(int x) const;
    int snappedTickFromTick(int tick) const;
    int quantizedTickAtX(int x, bool snapToGrid) const;
    int stepAtX(int x) const;
    bool placeDrawNoteAt(TrackType trackType, int tick);
    bool fillLaneRange(TrackType trackType, juce::Range<int> tickRange, int stepInterval);
    bool isModifierDown(const juce::ModifierKeys& mods, juce::ModifierKeys::Flags modifier) const;
    bool isVelocityEditKeyDown() const;
    bool isStretchEditKeyDown() const;
    int snapTicksForCurrentResolution() const;
    int ticksForGridResolution() const;

    bool isVisibleLane(TrackType type) const;
    int visibleLaneIndex(TrackType type) const;
    std::optional<TrackType> trackForVisibleLaneIndex(int laneIndex) const;

    PatternProject project;
    juce::Image staticGridCache;
    bool staticCacheDirty = true;

    float stepWidth = 20.0f;
    int laneHeight = 30;
    int rulerHeight = 24;
    GridResolution gridResolution = GridResolution::OneSixteenth;
    std::vector<TrackType> laneDisplayOrder;
    TrackType selectedTrack = TrackType::Kick;
    int previewStartStep = 0;
    int previewStartTick = 0;
    std::optional<juce::Range<int>> loopRegionTicks;
    float playheadStep = -1.0f;
    int lastPlayheadPixel = -1;

    std::vector<SelectedNoteRef> selectedNotes;
    std::optional<SelectedNoteRef> primarySelectedNote;

    enum class HoverZone
    {
        None,
        NoteBody
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
    int velocityOverlayPercent = 0;
    int velocityOverlayDelta = 0;
    bool velocityWaveModeActive = false;
    juce::Point<int> panStartViewPos;
    juce::Point<int> lastDragPoint;
    int rulerDragStartTick = 0;
    bool rulerDraggingLoop = false;
    bool rulerLoopDragMoved = false;
    TrackType drawTrack = TrackType::Kick;
    std::unordered_set<int> drawVisitedTicks;
    std::unordered_set<int> eraseVisitedKeys;

    std::optional<SelectedNoteRef> hoverNote;
    HoverZone hoverZone = HoverZone::None;
};
} // namespace bbg
