#pragma once

#include <functional>
#include <optional>
#include <vector>

#include <juce_gui_extra/juce_gui_extra.h>

#include "PluginProcessor.h"
#include "../Services/StyleLabReferenceService.h"
#include "../UI/EditorCommandController.h"
#include "../UI/EditorHistoryController.h"
#include "../UI/EditorLayoutController.h"
#include "../UI/GridEditorComponent.h"
#include "../UI/HotkeyController.h"
#include "../UI/MainHeaderComponent.h"
#include "../UI/SampleAnalysisPanelComponent.h"
#include "../UI/SoundModuleComponent.h"
#include "../UI/StyleLabDraftState.h"
#include "../UI/Sub808PianoRollComponent.h"
#include "../UI/TrackListComponent.h"

namespace bbg
{
class BoomBGeneratorAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                 private juce::KeyListener,
                                                 private juce::Timer
{
public:
    using HotkeyBinding = HotkeyController::Binding;
    using UiLayoutState = EditorLayoutController::State;

    enum class Sub808ViewMode
    {
        Hidden,
        Docked,
        Split,
        Detached
    };

    struct AlternateEditorSession
    {
        AlternateLaneEditor editorType = AlternateLaneEditor::None;
        std::optional<TrackType> trackType;
        RuntimeLaneId laneId;
        bool isVisible = false;
    };

    explicit BoomBGeneratorAudioProcessorEditor(BoomBapGeneratorAudioProcessor& processor);
    ~BoomBGeneratorAudioProcessorEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    bool keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent) override;
    bool keyStateChanged(bool isKeyDown, juce::Component* originatingComponent) override;
    void timerCallback() override;
    void refreshFromProcessor(bool refreshTrackRows = true);
    void setPreviewPlayback(bool shouldStart);
    void togglePreviewPlayback();
    void seekPreviewToStep(int step, bool resumeIfPlaying);
    void stepPreviewStart(int stepDelta);
    int getPatternStepCount() const;
    void setupAttachments();
    void refreshSubstyleBindingForGenre();
    void bindTrackCallbacks();
    void syncLaneDisplayOrder(const PatternProject& project);
    void moveLaneDisplayOrder(const RuntimeLaneId& laneId, int delta);
    void applyHatFxDragDensityFromUi(float density, bool forceWhenLocked);
    void exportFullPattern();
    void exportLoopWav();
    void dragFullPatternTempMidi();
    void dragFullPatternExternal();
    bool isHatFxLane(const RuntimeLaneId& laneId) const;
    void exportTrack(const RuntimeLaneId& laneId);
    void showSampleMenu(const RuntimeLaneId& laneId);
    void dragTrackTempMidi(const RuntimeLaneId& laneId);
    void dragTrackExternal(const RuntimeLaneId& laneId);
    void updateGridGeometry();
    void adjustGridHorizontalZoom(float delta, int anchorViewportX);
    void adjustGridVerticalZoom(float delta, int anchorViewportY);
    bool zoomGridToSelection();
    void zoomGridToPattern();
    void syncAlternateEditorForLane(const PatternProject& project, const RuntimeLaneId& laneId);
    void syncEditorToolButtons(GridEditorComponent::EditorTool tool);
    void setAlternateEditorVisible(bool shouldShow,
                                   AlternateLaneEditor editorType = AlternateLaneEditor::None,
                                   std::optional<TrackType> trackType = std::nullopt,
                                   RuntimeLaneId laneId = {});
    void setupHotkeys();
    void applyHotkeysToGrid();
    void loadHotkeys();
    void saveHotkeys();
    void restoreDefaultHotkeys();
    void pushProjectHistoryState(const PatternProject& before, const PatternProject& after);
    void commitPendingProjectHistoryState();
    bool performUndo();
    bool performRedo();
    bool projectsEquivalent(const PatternProject& lhs, const PatternProject& rhs) const;
    juce::String hotkeyToDisplayText(const HotkeyBinding& binding) const;
    bool confirmReplaceConflict(const juce::String& actionName,
                                const juce::String& conflictedActionName,
                                const juce::String& shortcutText);
    bool tryRebindKeyAction(const juce::String& actionId, const juce::KeyPress& keyPress);
    bool tryRebindMouseModifierAction(const juce::String& actionId, juce::ModifierKeys::Flags modifier);
    bool resetHotkeyActionToDefault(const juce::String& actionId);
    void showEditorOptionsMenu();
    StyleLabState buildDefaultStyleLabState() const;
    void showStyleLabReferenceBrowserWindow();
    void showStyleLabWindow();
    void showHotkeysMenu();
    void showHotkeyActionMenu(const juce::String& actionId, std::function<void()> onDone = {});
    void applyProjectSnapshotChange(const PatternProject& before, const PatternProject& after, bool refreshTrackRows = true);
    void applyProcessorProjectMutation(const std::function<void()>& mutation, bool refreshTrackRows = true);
    bool clearLaneNotes(const RuntimeLaneId& laneId);
    bool clearAllPatternNotes();
    void showImportMidiToLaneDialog(const RuntimeLaneId& laneId);
    void showRenameLaneDialog(const RuntimeLaneId& laneId);
    void requestDeleteLane(const RuntimeLaneId& laneId);
    void showAddLaneMenu();
    LaneRackDisplayMode laneRackModeForWidth(int width) const;
    void loadUiLayoutState();
    void saveUiLayoutState();
    juce::Point<float> toVirtualPoint(juce::Point<float> point) const;
    void setActiveEditorTool(GridEditorComponent::EditorTool tool);
    void toggleSub808PianoRollFullscreen();
    void toggleGridEditorFullscreen();
    void setSub808ViewMode(Sub808ViewMode mode);
    void updateSub808ViewVisibility();
    void updateSub808Layout();
    void setSub808DetachedWindowVisible(bool shouldShow);
    void showSub808ViewModeMenu();
    void updateCursorForMousePosition(juce::Point<float> point);
    bool isStandaloneWindowMaximized() const;
    void toggleStandaloneWindowMaximize();
    bool isAlternateEditorVisible() const;
    bool isPianoRollEditorVisible() const;
    bool canShowSub808Editor() const;
    juce::Component* activeAlternateEditorComponent();
    const juce::Component* activeAlternateEditorComponent() const;

    BoomBapGeneratorAudioProcessor& audioProcessor;

    MainHeaderComponent header;
    juce::Component laneSurface;
    TrackListComponent trackList;
    SampleAnalysisPanelComponent analysisPanel;
    SoundModuleComponent soundModule;
    juce::Component editorToolBar;
    juce::Label editorToolLabel;
    juce::TextButton pencilToolButton { "Pencil" };
    juce::TextButton brushToolButton { "Brush" };
    juce::TextButton selectToolButton { "Select" };
    juce::TextButton cutToolButton { "Cut" };
    juce::TextButton eraseToolButton { "Erase" };
    juce::TextButton optionsToolButton { "Options" };
    juce::TextButton hotkeysToolButton { "Hotkeys" };
    juce::TextButton sub808ViewModeButton { "808" };
    juce::TextButton pianoRollFullscreenButton { "Full" };
    juce::TextButton gridEditorFullscreenButton { "Full" };
    juce::Viewport gridViewport;
    juce::Viewport sub808Viewport;
    GridEditorComponent grid;
    Sub808PianoRollComponent sub808PianoRoll;
    float horizontalZoom = 1.0f;
    int laneHeight = 30;
    Sub808ViewMode sub808ViewMode = Sub808ViewMode::Hidden;
    Sub808ViewMode sub808LastInlineViewMode = Sub808ViewMode::Hidden;
    AlternateEditorSession alternateEditorSession;
    std::unique_ptr<juce::DocumentWindow> sub808DetachedWindow;
    juce::Rectangle<int> sub808DetachedWindowBounds;
    int lastGenerationCounter = -1;
    juce::Rectangle<int> standaloneRestoreBounds;
    EditorLayoutController layoutController;
    EditorCommandController commandController;
    EditorHistoryController historyController;
    HotkeyController hotkeyController;
    juce::Rectangle<int> splitterBounds;
    std::unique_ptr<juce::Component> splitterHandle;
    float currentUiScale = 1.0f;
    float currentUiOffsetX = 0.0f;
    float currentUiOffsetY = 0.0f;
    float hatFxDragDensityUi = 1.0f;
    bool hatFxDragLockedUi = false;
    std::optional<StyleLabState> styleLabState;
    std::optional<StyleLabDraftState> styleLabDraftState;
    std::vector<RuntimeLaneId> laneDisplayOrder;
    GridEditorComponent::EditorRegionState editorRegionState;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAttachment> bpmAttachment;
    std::unique_ptr<ButtonAttachment> bpmLockAttachment;
    std::unique_ptr<ButtonAttachment> syncAttachment;
    std::unique_ptr<SliderAttachment> swingAttachment;
    std::unique_ptr<SliderAttachment> velocityAttachment;
    std::unique_ptr<SliderAttachment> timingAttachment;
    std::unique_ptr<SliderAttachment> humanizeAttachment;
    std::unique_ptr<SliderAttachment> densityAttachment;
    std::unique_ptr<ComboAttachment> tempoInterpretationAttachment;
    std::unique_ptr<ComboAttachment> barsAttachment;
    std::unique_ptr<ComboAttachment> genreAttachment;
    std::unique_ptr<ComboAttachment> substyleAttachment;
    int lastGenreChoice = -1;
    std::unique_ptr<SliderAttachment> seedAttachment;
    std::unique_ptr<ButtonAttachment> seedLockAttachment;
    std::unique_ptr<SliderAttachment> masterVolumeAttachment;
    std::unique_ptr<SliderAttachment> masterCompressorAttachment;
    std::unique_ptr<SliderAttachment> masterLofiAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BoomBGeneratorAudioProcessorEditor)
};
} // namespace bbg
