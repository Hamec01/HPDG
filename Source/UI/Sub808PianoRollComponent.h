#pragma once

#include <functional>
#include <optional>
#include <unordered_set>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "GridEditorComponent.h"

namespace bbg
{
class Sub808PianoRollComponent : public juce::Component
{
public:
    Sub808PianoRollComponent();

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

    void setBars(int barsCount);
    void setStepWidth(float width);
    void setNotes(const std::vector<NoteEvent>& notesIn);
    void setEditorTool(GridEditorComponent::EditorTool tool);
    void setInputBindings(const GridEditorComponent::InputBindings& bindings);
    void refreshTransientInputState();
    bool deleteSelectedNotes();
    bool copySelectionToClipboard();
    bool cutSelectionToClipboard();
    bool pasteClipboardAtAnchor();
    bool duplicateSelectionToRight();
    void clearNoteSelection();
    bool selectAllNotes();

    std::function<void(const std::vector<NoteEvent>&)> onNotesEdited;
    std::function<void()> onOpenHotkeys;
    std::function<void(GridEditorComponent::EditorTool)> onToolChanged;

private:
    struct DragSnapshot
    {
        int index = -1;
        int startTick = 0;
        int startPitch = 36;
        int startLength = 1;
        int startVelocity = 100;
    };

    struct ClipboardNote
    {
        NoteEvent note;
        int relativeTick = 0;
    };

    enum class ContextMenuTarget
    {
        Empty,
        SingleNote,
        Selection
    };

    enum class EditMode
    {
        None,
        Move,
        Stretch,
        Velocity,
        Marquee,
        BrushDraw,
        EraseDrag
    };

    enum class SnapMode
    {
        Free,
        OneSixteenth,
        OneEighth,
        OneQuarter,
        Triplet
    };

    static constexpr int kTopBarHeight = 30;
    static constexpr int kKeyboardWidth = 76;
    static constexpr int kVelocityLaneHeight = 92;
    static constexpr int kMinPitch = 24;
    static constexpr int kMaxPitch = 84;
    static constexpr int kPitchRows = (kMaxPitch - kMinPitch + 1);

    std::vector<NoteEvent> notes;
    int bars = 8;
    float stepWidth = 20.0f;
    int rowHeight = 14;
    SnapMode snapMode = SnapMode::OneSixteenth;
    GridEditorComponent::EditorTool editorTool = GridEditorComponent::EditorTool::Pencil;
    GridEditorComponent::InputBindings inputBindings;
    int focusedOctave = 3;
    std::vector<int> selectedIndices;
    int activeNoteIndex = -1;
    EditMode editMode = EditMode::None;
    int dragStartX = 0;
    int dragStartY = 0;
    std::vector<DragSnapshot> dragSnapshots;
    int velocityDragStartValue = 100;
    bool velocityWaveModeActive = false;
    int velocityOverlayPercent = 0;
    int velocityOverlayDelta = 0;
    juce::Point<int> marqueeStart;
    juce::Rectangle<int> marqueeRect;
    bool marqueeAdditive = false;
    std::vector<int> marqueeBaseSelection;
    juce::Point<int> lastDragPoint;
    int pasteAnchorTick = 0;
    std::unordered_set<int> drawVisitedKeys;
    std::unordered_set<int> eraseVisitedKeys;
    std::vector<ClipboardNote> clipboardNotes;
    int clipboardSourceStartTick = 0;
    int clipboardSpanTicks = 0;

    int totalSteps() const;
    int totalTicks() const;
    int contentWidth() const;
    int contentHeight() const;
    juce::Rectangle<int> topBarBounds() const;
    juce::Rectangle<int> noteGridBounds() const;
    juce::Rectangle<int> velocityLaneBounds() const;
    int noteStartTick(const NoteEvent& n) const;
    int noteEndTick(const NoteEvent& n) const;
    void setNoteStartTick(NoteEvent& n, int tick);
    int snapTickSize() const;
    int clampTickToRoll(int tick) const;
    int quantizeTick(int tick, bool bypassSnap = false) const;
    int tickAtX(int x) const;
    int snappedTickFromTick(int tick, bool bypassSnap = false) const;
    int snappedTickAtX(int x, bool bypassSnap = false) const;
    int pitchAtY(int y) const;
    int yForPitch(int pitch) const;
    juce::Rectangle<int> noteBounds(const NoteEvent& n) const;
    juce::Rectangle<int> velocityHandleBounds(const NoteEvent& n) const;
    std::optional<int> findNoteAt(juce::Point<int> p) const;
    std::optional<int> findVelocityHandleAt(juce::Point<int> p) const;
    bool isSelectedIndex(int index) const;
    void clearSelection();
    void setSingleSelection(int index);
    void setMarqueeSelection(const juce::Rectangle<int>& marquee, bool additive);
    void collectDragSnapshots();
    bool applySelectionVelocityDelta(int deltaVel);
    bool applySelectionVelocityWave(int deltaVel, int currentMouseX);
    bool copySelectionInternal(bool removeAfterCopy);
    ContextMenuTarget contextMenuTargetAt(juce::Point<int> p, std::optional<int>* hitOut = nullptr);
    void showContextMenu(juce::Point<int> p);
    bool applyContextAction(int actionId, juce::Point<int> p);
    bool canQuickRollSelection() const;
    std::vector<int> availableAdvancedRollDivisions() const;
    bool rollSelectionQuick();
    bool rollSelectionWithDivisions(int divisions);
    bool splitNoteAtTick(int noteIndex, int cutTick);
    bool eraseNoteAt(juce::Point<int> p);
    bool eraseNotesAlongSegment(juce::Point<int> from, juce::Point<int> to);
    bool placeDrawNoteAt(juce::Point<int> p);
    bool isVelocityEditKeyDown() const;
    bool isStretchEditKeyDown() const;
    juce::String snapModeLabel() const;
    void cycleSnapMode(bool forward);
    void shiftFocusedOctave(int delta);
    void updateMouseCursor();
    void sortNotes();
    void commit();
};
} // namespace bbg
