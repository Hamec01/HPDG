#pragma once

#include <functional>
#include <optional>
#include <unordered_set>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Core/Sub808Types.h"
#include "../Core/TrackType.h"
#include "GridEditorComponent.h"
#include "Sub808EditModel.h"
#include "Sub808Geometry.h"

namespace bbg
{
class Sub808PianoRollComponent : public juce::Component,
                                 public juce::SettableTooltipClient
{
public:
    struct DrumGhostNote
    {
        TrackType trackType = TrackType::Kick;
        int startTick = 0;
        int lengthTicks = 960;
        int velocity = 100;
        bool isGhostTrack = false;
    };

    Sub808PianoRollComponent();

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;

    void setBars(int barsCount);
    void setStepWidth(float width);
    void setNotes(const std::vector<Sub808NoteEvent>& notesIn);
    void setDrumGhostNotes(const std::vector<DrumGhostNote>& ghostNotesIn);
    void setLaneSettings(const Sub808LaneSettings& settingsIn);
    void setScaleContext(int keyRootChoice, int scaleModeChoice);
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
    bool nudgeSelectionByTicks(int deltaTicks);
    bool nudgeSelectionByPitch(int deltaPitch);
    GridEditorComponent::EditorTool getEditorTool() const;
    int getPreferredContentWidth() const;
    int getPreferredContentHeight() const;

    std::function<void(const std::vector<Sub808NoteEvent>&)> onNotesEdited;
    std::function<void(const Sub808LaneSettings&)> onLaneSettingsEdited;
    std::function<void(int pitch, int velocity, int lengthTicks)> onPreviewNote;
    std::function<void()> onOpenHotkeys;
    std::function<void(GridEditorComponent::EditorTool)> onToolChanged;

private:
    using PitchedNoteEvent = Sub808EditModel::PitchedNoteEvent;
    using DragSnapshot = Sub808EditModel::DragSnapshot;
    using ClipboardNote = Sub808EditModel::ClipboardNote;

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

    enum class HoverZone
    {
        None,
        NoteBody,
        ResizeLeft,
        ResizeRight
    };

    enum class SnapMode
    {
        Free,
        OneSixteenth,
        OneEighth,
        OneQuarter,
        Triplet
    };

    enum class LocalScaleType
    {
        Minor = 0,
        Major,
        HarmonicMinor,
        Phrygian
    };

    struct HeaderLayout
    {
        juce::Rectangle<int> octaveDownButton;
        juce::Rectangle<int> octaveUpButton;
        juce::Rectangle<int> octaveInfo;
        juce::Rectangle<int> pencilBtn;
        juce::Rectangle<int> brushBtn;
        juce::Rectangle<int> selectBtn;
        juce::Rectangle<int> cutBtn;
        juce::Rectangle<int> eraseBtn;
        juce::Rectangle<int> inspectorBounds;
        juce::Rectangle<int> keyButton;
        juce::Rectangle<int> scaleButton;
        juce::Rectangle<int> scaleLockButton;
        juce::Rectangle<int> kickGuideButton;
        juce::Rectangle<int> hotkeysBtn;
        juce::Rectangle<int> snapButton;
    };

    static constexpr int kTopBarHeight = 30;
    static constexpr int kKeyboardWidth = 76;
    static constexpr int kVelocityLaneHeight = 92;
    static constexpr int kMinPitch = 24;
    static constexpr int kMaxPitch = 84;
    static constexpr int kPitchRows = (kMaxPitch - kMinPitch + 1);

    std::vector<PitchedNoteEvent> notes;
    std::vector<DrumGhostNote> drumGhostNotes;
    int bars = 8;
    float stepWidth = 20.0f;
    int rowHeight = 14;
    SnapMode snapMode = SnapMode::OneSixteenth;
    GridEditorComponent::EditorTool editorTool = GridEditorComponent::EditorTool::Pencil;
    GridEditorComponent::InputBindings inputBindings;
    int bassKeyRootChoice = 0;
    int bassScaleModeChoice = 0;
    int localKeyRootChoice = 0;
    LocalScaleType localScaleType = LocalScaleType::Minor;
    bool scaleLockEnabled = false;
    bool showKickGuideEnabled = true;
    bool localScaleContextOverridden = false;
    bool localScaleLockOverridden = false;
    Sub808LaneSettings laneSettings;
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
    std::optional<int> hoverNoteIndex;
    HoverZone hoverZone = HoverZone::None;
    int dragResizeAnchorTick = 0;
    bool resizeFromStart = false;
    std::unordered_set<int> drawVisitedKeys;
    std::unordered_set<int> eraseVisitedKeys;
    std::vector<ClipboardNote> clipboardNotes;
    int clipboardSourceStartTick = 0;
    int clipboardSpanTicks = 0;

    int totalSteps() const;
    int totalTicks() const;
    Sub808Geometry makeGeometry() const;
    HeaderLayout makeHeaderLayout() const;
    int contentWidth() const;
    int contentHeight() const;
    juce::Rectangle<int> topBarBounds() const;
    juce::Rectangle<int> noteGridBounds() const;
    juce::Rectangle<int> velocityLaneBounds() const;
    static PitchedNoteEvent toPitchedNoteEvent(const Sub808NoteEvent& note);
    static Sub808NoteEvent toSub808NoteEvent(const PitchedNoteEvent& note);
    int noteStartTick(const PitchedNoteEvent& n) const;
    int noteEndTick(const PitchedNoteEvent& n) const;
    void setNoteStartTick(PitchedNoteEvent& n, int tick);
    int snapTickSize() const;
    int clampTickToRoll(int tick) const;
    int quantizeTick(int tick, bool bypassSnap = false) const;
    int tickAtX(int x) const;
    int snappedTickFromTick(int tick, bool bypassSnap = false) const;
    int snappedTickAtX(int x, bool bypassSnap = false) const;
    int pitchAtY(int y) const;
    int yForPitch(int pitch) const;
    juce::Rectangle<int> noteBounds(const PitchedNoteEvent& n) const;
    int resizeHandleWidthPx(const PitchedNoteEvent& n) const;
    juce::Rectangle<int> velocityHandleBounds(const PitchedNoteEvent& n) const;
    std::optional<int> findNoteAt(juce::Point<int> p) const;
    std::optional<int> findVelocityHandleAt(juce::Point<int> p) const;
    void updateHoverState(juce::Point<int> p);
    bool isSelectedIndex(int index) const;
    void clearSelection();
    void setSingleSelection(int index);
    void setMarqueeSelection(const juce::Rectangle<int>& marquee, bool additive);
    void collectDragSnapshots();
    bool applySelectionVelocityDelta(int deltaVel);
    bool applySelectionVelocityWave(int deltaVel, int currentMouseX);
    bool copySelectionInternal(bool removeAfterCopy);
    int nextLinkedNoteIndex(int noteIndex) const;
    bool toggleSelectionSlideState();
    bool toggleSelectionLegatoState();
    bool toggleSelectionGlideState();
    bool setSelectionSlideState(bool enabled);
    bool setSelectionLegatoState(bool enabled);
    bool setSelectionGlideState(bool enabled);
    bool clearSelectionLinkFlags();
    bool makeSelectionLegatoToNext();
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
    int snappedPitchForInput(int rawPitch, bool bypassScaleSnap) const;
    int activeScaleModeChoice() const;
    juce::String activeScaleName() const;
    juce::String activeRootName() const;
    juce::String tooltipForPoint(juce::Point<int> p) const;
    bool isPitchInActiveScale(int pitch) const;
    bool isRootPitch(int pitch) const;
    bool shouldForceScaleSnap() const;
    std::vector<int> kickGuideTicks() const;
    void previewNote(const PitchedNoteEvent& note);
    juce::String snapModeLabel() const;
    void cycleSnapMode(bool forward);
    void shiftFocusedOctave(int delta);
    void updateMouseCursor();
    void sortNotes();
    void commit();
};
} // namespace bbg
