#include "PluginEditor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <numeric>

#include "../Core/PatternProjectSerialization.h"
#include "../Core/RuntimeLaneLifecycle.h"
#include "../Core/TrackRegistry.h"
#include "../Engine/StyleDefaults.h"
#include "../Services/MidiImportService.h"
#include "../UI/StyleLabComponent.h"
#include "../UI/StyleLabReferenceBrowserComponent.h"
#include "../Utils/TimingHelpers.h"

namespace bbg
{
namespace
{
constexpr int kRackMinWidth = 260;
constexpr int kRackCompactThreshold = 360;
constexpr int kRackFullThreshold = 820;
constexpr int kRackMaxWidth = 980;
constexpr int kGridMinWidth = 420;
constexpr int kSplitterVisualWidth = 2;
constexpr int kSplitterHitWidth = 10;
constexpr int kPaneGap = 4;
constexpr int kSoundModuleMinHeight = 210;
constexpr int kSoundModuleMaxHeight = 300;
constexpr int kEditorToolBarHeight = 34;
constexpr auto kHotkeysRootProp = "ui.hotkeys";
constexpr int kDesignWidth = 1460;
constexpr int kDesignHeight = 820;
constexpr float kMinUiScale = 0.56f;
constexpr auto kUiLeftWidthProp = "ui.leftPanelWidth";
constexpr auto kUiHeaderModeProp = "ui.headerControlsMode";

constexpr auto kActionDrawMode = "draw_mode";
constexpr auto kActionVelocityEdit = "velocity_edit";
constexpr auto kActionPanView = "pan_view";
constexpr auto kActionHorizontalZoomModifier = "horizontal_zoom_modifier";
constexpr auto kActionLaneHeightZoomModifier = "lane_height_zoom_modifier";
constexpr auto kActionPencilTool = "tool_pencil";
constexpr auto kActionBrushTool = "tool_brush";
constexpr auto kActionSelectTool = "tool_select";
constexpr auto kActionCutTool = "tool_cut";
constexpr auto kActionEraseTool = "tool_erase";
constexpr auto kActionMicrotimingEdit = "microtiming_edit";
constexpr auto kActionDeleteNote = "delete_note";
constexpr auto kActionCopySelection = "copy_selection";
constexpr auto kActionCutSelection = "cut_selection";
constexpr auto kActionPasteSelection = "paste_selection";
constexpr auto kActionDuplicateSelection = "duplicate_selection";
constexpr auto kActionDeselectAll = "deselect_all";
constexpr auto kActionSelectAllNotes = "select_all_notes";
constexpr auto kActionUndo = "undo";
constexpr auto kActionRedo = "redo";
constexpr auto kActionRoleAnchor = "semantic_role_anchor";
constexpr auto kActionRoleSupport = "semantic_role_support";
constexpr auto kActionRoleFill = "semantic_role_fill";
constexpr auto kActionRoleAccent = "semantic_role_accent";
constexpr auto kActionRoleClear = "semantic_role_clear";
constexpr auto kActionZoomToSelection = "zoom_to_selection";
constexpr auto kActionZoomToPattern = "zoom_to_pattern";

class SplitterHandleComponent final : public juce::Component
{
public:
    std::function<void(const juce::MouseEvent&)> onPress;
    std::function<void(const juce::MouseEvent&)> onDragMove;
    std::function<void(const juce::MouseEvent&)> onRelease;

    SplitterHandleComponent()
    {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        setInterceptsMouseClicks(true, false);
    }

    void paint(juce::Graphics&) override {}

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (onPress)
            onPress(event);
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (onDragMove)
            onDragMove(event);
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        if (onRelease)
            onRelease(event);
    }
};

class LaneNameDialogComponent final : public juce::Component
{
public:
    explicit LaneNameDialogComponent(const juce::String& initialName)
    {
        addAndMakeVisible(nameLabel);
        addAndMakeVisible(nameEditor);
        addAndMakeVisible(cancelButton);
        addAndMakeVisible(confirmButton);

        nameLabel.setText("Lane name", juce::dontSendNotification);
        nameLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(188, 198, 212));

        nameEditor.setText(initialName, juce::dontSendNotification);
        nameEditor.setSelectAllWhenFocused(true);
        nameEditor.onReturnKey = [this] { confirm(); };
        nameEditor.onEscapeKey = [this] { close(0); };

        cancelButton.onClick = [this] { close(0); };
        confirmButton.onClick = [this] { confirm(); };
        setSize(360, 120);
    }

    std::function<void(const juce::String&)> onSubmit;

    void resized() override
    {
        auto area = getLocalBounds().reduced(14);
        nameLabel.setBounds(area.removeFromTop(20));
        area.removeFromTop(6);
        nameEditor.setBounds(area.removeFromTop(28));
        area.removeFromTop(12);

        auto buttons = area.removeFromBottom(30);
        const int buttonWidth = 86;
        confirmButton.setBounds(buttons.removeFromRight(buttonWidth));
        buttons.removeFromRight(8);
        cancelButton.setBounds(buttons.removeFromRight(buttonWidth));
    }

private:
    void confirm()
    {
        if (onSubmit)
            onSubmit(nameEditor.getText());

        close(1);
    }

    void close(int result)
    {
        if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>())
            dialog->exitModalState(result);
    }

    juce::Label nameLabel;
    juce::TextEditor nameEditor;
    juce::TextButton cancelButton { "Cancel" };
    juce::TextButton confirmButton { "OK" };
};

juce::ModifierKeys::Flags defaultModifierForAction(const juce::String& actionId)
{
    if (actionId == kActionDrawMode)
        return juce::ModifierKeys::shiftModifier;
    if (actionId == kActionPanView)
        return juce::ModifierKeys::ctrlModifier;
    if (actionId == kActionHorizontalZoomModifier)
        return juce::ModifierKeys::ctrlModifier;
    if (actionId == kActionLaneHeightZoomModifier)
        return juce::ModifierKeys::shiftModifier;
    if (actionId == kActionMicrotimingEdit)
        return juce::ModifierKeys::altModifier;
    return juce::ModifierKeys::noModifiers;
}

juce::KeyPress defaultKeyForAction(const juce::String& actionId)
{
    if (actionId == kActionVelocityEdit)
        return juce::KeyPress('V');
    if (actionId == kActionPencilTool)
        return juce::KeyPress('1');
    if (actionId == kActionBrushTool)
        return juce::KeyPress('2');
    if (actionId == kActionSelectTool)
        return juce::KeyPress('3');
    if (actionId == kActionCutTool)
        return juce::KeyPress('4');
    if (actionId == kActionEraseTool)
        return juce::KeyPress('5');
    if (actionId == kActionDeleteNote)
        return juce::KeyPress(juce::KeyPress::deleteKey);
    if (actionId == kActionCopySelection)
        return juce::KeyPress('C', juce::ModifierKeys::ctrlModifier, 0);
    if (actionId == kActionCutSelection)
        return juce::KeyPress('X', juce::ModifierKeys::ctrlModifier, 0);
    if (actionId == kActionPasteSelection)
        return juce::KeyPress('V', juce::ModifierKeys::ctrlModifier, 0);
    if (actionId == kActionDuplicateSelection)
        return juce::KeyPress('D', juce::ModifierKeys::ctrlModifier, 0);
    if (actionId == kActionSelectAllNotes)
        return juce::KeyPress('A', juce::ModifierKeys::ctrlModifier, 0);
    if (actionId == kActionDeselectAll)
        return juce::KeyPress(juce::KeyPress::escapeKey);
    if (actionId == kActionUndo)
        return juce::KeyPress('U', juce::ModifierKeys::ctrlModifier, 0);
    if (actionId == kActionRedo)
        return juce::KeyPress('R', juce::ModifierKeys::ctrlModifier, 0);
    if (actionId == kActionRoleAnchor)
        return juce::KeyPress('1', juce::ModifierKeys::altModifier, 0);
    if (actionId == kActionRoleSupport)
        return juce::KeyPress('2', juce::ModifierKeys::altModifier, 0);
    if (actionId == kActionRoleFill)
        return juce::KeyPress('3', juce::ModifierKeys::altModifier, 0);
    if (actionId == kActionRoleAccent)
        return juce::KeyPress('4', juce::ModifierKeys::altModifier, 0);
    if (actionId == kActionRoleClear)
        return juce::KeyPress('0', juce::ModifierKeys::altModifier, 0);
    if (actionId == kActionZoomToSelection)
        return juce::KeyPress('F', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0);
    if (actionId == kActionZoomToPattern)
        return juce::KeyPress('P', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0);
    return juce::KeyPress();
}

std::vector<RuntimeLaneId> buildTrackListLaneOrder(const PatternProject& project,
                                                   const std::vector<RuntimeLaneId>& laneOrderPreference)
{
    std::vector<RuntimeLaneId> laneOrder;
    laneOrder.reserve(project.runtimeLaneProfile.lanes.size());

    for (const auto& laneId : laneOrderPreference)
    {
        const auto* lane = findRuntimeLaneById(project.runtimeLaneProfile, laneId);
        if (lane == nullptr)
            continue;

        if (std::find(laneOrder.begin(), laneOrder.end(), lane->laneId) == laneOrder.end())
            laneOrder.push_back(lane->laneId);
    }

    for (const auto& lane : project.runtimeLaneProfile.lanes)
    {
        if (std::find(laneOrder.begin(), laneOrder.end(), lane.laneId) == laneOrder.end())
            laneOrder.push_back(lane.laneId);
    }

    return laneOrder;
}

std::optional<TrackType> dedicatedPianoRollTrackType(const PatternProject& project)
{
    for (const auto& lane : project.runtimeLaneProfile.lanes)
    {
        if (lane.runtimeTrackType.has_value() && lane.editorCapabilities.alternateEditor == AlternateLaneEditor::PianoRoll)
            return lane.runtimeTrackType;
    }

    return std::nullopt;
}

AlternateLaneEditor alternateEditorForTrack(const PatternProject& project, TrackType type)
{
    if (const auto* lane = findRuntimeLaneForTrack(project.runtimeLaneProfile, type))
        return lane->editorCapabilities.alternateEditor;

    return makeLaneEditorCapabilities(type).alternateEditor;
}

bool isMouseModifierAction(const juce::String& actionId)
{
    return actionId == kActionDrawMode
        || actionId == kActionPanView
        || actionId == kActionHorizontalZoomModifier
        || actionId == kActionLaneHeightZoomModifier
        || actionId == kActionMicrotimingEdit;
}

juce::String hotkeyPropertyPath(const juce::String& actionId, const juce::String& suffix)
{
    return juce::String(kHotkeysRootProp) + "." + actionId + "." + suffix;
}

GridEditorComponent::GridResolution gridResolutionFromId(int id)
{
    switch (id)
    {
        case 1: return GridEditorComponent::GridResolution::Adaptive;
        case 2: return GridEditorComponent::GridResolution::Micro;
        case 10: return GridEditorComponent::GridResolution::OneQuarter;
        case 11: return GridEditorComponent::GridResolution::OneQuarterTriplet;
        case 12: return GridEditorComponent::GridResolution::OneEighth;
        case 13: return GridEditorComponent::GridResolution::OneEighthTriplet;
        case 15: return GridEditorComponent::GridResolution::OneSixteenthTriplet;
        case 16: return GridEditorComponent::GridResolution::OneThirtySecond;
        case 17: return GridEditorComponent::GridResolution::OneThirtySecondTriplet;
        case 18: return GridEditorComponent::GridResolution::OneSixtyFourth;
        case 19: return GridEditorComponent::GridResolution::OneSixtyFourthTriplet;
        case 14:
        default: return GridEditorComponent::GridResolution::OneSixteenth;
    }
}

PreviewPlaybackMode previewPlaybackModeFromId(int id)
{
    switch (id)
    {
        case 2: return PreviewPlaybackMode::LoopRange;
        case 1:
        default: return PreviewPlaybackMode::FromFlag;
    }
}

int previewPlaybackModeId(PreviewPlaybackMode mode)
{
    switch (mode)
    {
        case PreviewPlaybackMode::LoopRange: return 2;
        case PreviewPlaybackMode::FromFlag:
        default: return 1;
    }
}

#if JUCE_STANDALONE_APPLICATION
juce::Rectangle<int> gStandaloneLastRestoreBounds;
#endif

juce::File dragLogFile()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("DRUMENGINE")
        .getChildFile("Logs")
        .getChildFile("drag.log");
}

void logDrag(const juce::String& message)
{
    auto file = dragLogFile();
    file.getParentDirectory().createDirectory();
    const auto line = juce::Time::getCurrentTime().toString(true, true) + " | " + message + "\n";
    file.appendText(line, false, false, "\n");
}

// Shown as a modal dialog; captures the next key press and fires onKeyCaptured.
class KeyCaptureComponent final : public juce::Component
{
public:
    std::function<void(const juce::KeyPress&)> onKeyCaptured;

    explicit KeyCaptureComponent(const juce::String& actionDisplayName)
        : actionName(actionDisplayName)
    {
        setWantsKeyboardFocus(true);
    }

    void visibilityChanged() override
    {
        if (isVisible())
            juce::MessageManager::callAsync(
                [safe = juce::Component::SafePointer<KeyCaptureComponent>(this)]
                {
                    if (safe != nullptr)
                        safe->grabKeyboardFocus();
                });
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour::fromRGB(22, 25, 31));
        auto area = getLocalBounds().reduced(24, 16);

        g.setColour(juce::Colour::fromRGB(158, 169, 186));
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawText("Action:  " + actionName, area.removeFromTop(20), juce::Justification::centredLeft, true);
        area.removeFromTop(8);

        g.setColour(juce::Colour::fromRGB(100, 160, 255));
        g.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
        g.drawText("Press any key combination...", area.removeFromTop(26), juce::Justification::centred, true);
        area.removeFromTop(6);

        g.setColour(juce::Colour::fromRGB(100, 115, 135));
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText("Press Escape to cancel without changing", area, juce::Justification::centred, true);
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key.getKeyCode() == juce::KeyPress::escapeKey)
        {
            if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
                w->exitModalState(0);
            return true;
        }

        // Ignore bare tab / return used by dialog navigation
        if (key.getKeyCode() == juce::KeyPress::tabKey ||
            key.getKeyCode() == juce::KeyPress::returnKey)
            return false;

        if (onKeyCaptured)
            onKeyCaptured(key);

        if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
            w->exitModalState(1);
        return true;
    }

private:
    juce::String actionName;
};

class HotkeysManagerTableModel final : public juce::TableListBoxModel
{
public:
    using Binding = BoomBGeneratorAudioProcessorEditor::HotkeyBinding;

    std::function<std::vector<Binding>()> requestRows;
    std::function<juce::String(const Binding&)> formatShortcut;
    std::function<void(const juce::String&)> onRebind;
    std::function<void(const juce::String&)> onReset;

    void refresh()
    {
        rows = requestRows ? requestRows() : std::vector<Binding> {};
    }

    int getNumRows() override
    {
        return static_cast<int>(rows.size());
    }

    void paintRowBackground(juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected) override
    {
        juce::ignoreUnused(width, height);

        if (rowIsSelected)
        {
            g.fillAll(juce::Colour::fromRGB(58, 74, 95));
            return;
        }

        if ((rowNumber & 1) == 0)
            g.fillAll(juce::Colour::fromRGB(28, 31, 37));
        else
            g.fillAll(juce::Colour::fromRGB(24, 27, 33));
    }

    void paintCell(juce::Graphics& g,
                   int rowNumber,
                   int columnId,
                   int width,
                   int height,
                   bool rowIsSelected) override
    {
        juce::ignoreUnused(rowIsSelected);

        if (rowNumber < 0 || rowNumber >= static_cast<int>(rows.size()))
            return;

        const auto& row = rows[static_cast<size_t>(rowNumber)];
        juce::String text;
        if (columnId == 1)
            text = row.displayName;
        else if (columnId == 2)
            text = formatShortcut ? formatShortcut(row) : juce::String("(none)");
        else if (columnId == 3)
            text = "Rebind...";
        else if (columnId == 4)
            text = "Reset";

        g.setColour(columnId >= 3 ? juce::Colour::fromRGB(120, 186, 255) : juce::Colour::fromRGB(215, 223, 235));
        g.setFont(juce::Font(juce::FontOptions(12.0f, columnId >= 3 ? juce::Font::bold : juce::Font::plain)));
        g.drawText(text, 8, 0, width - 16, height, juce::Justification::centredLeft, true);

        g.setColour(juce::Colour::fromRGBA(255, 255, 255, 16));
        g.drawVerticalLine(width - 1, 0.0f, static_cast<float>(height));
    }

    void cellClicked(int rowNumber, int columnId, const juce::MouseEvent&) override
    {
        if (rowNumber < 0 || rowNumber >= static_cast<int>(rows.size()))
            return;

        const auto actionId = rows[static_cast<size_t>(rowNumber)].actionId;
        if (columnId == 3)
        {
            if (onRebind)
                onRebind(actionId);
            return;
        }

        if (columnId == 4)
        {
            if (onReset)
                onReset(actionId);
        }
    }

private:
    std::vector<Binding> rows;
};

class HotkeysManagerComponent final : public juce::Component
{
public:
    using Binding = BoomBGeneratorAudioProcessorEditor::HotkeyBinding;

    std::function<std::vector<Binding>()> requestRows;
    std::function<juce::String(const Binding&)> formatShortcut;
    std::function<void(const juce::String&)> onRebind;
    std::function<void(const juce::String&)> onReset;
    std::function<void()> onResetAll;

    HotkeysManagerComponent()
    {
        titleLabel.setText("Hotkeys Manager", juce::dontSendNotification);
        titleLabel.setJustificationType(juce::Justification::centredLeft);
        titleLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(220, 228, 240));
        titleLabel.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
        addAndMakeVisible(titleLabel);

        hintLabel.setText("Click Rebind \xe2\x80\x94 then press any key or combo.  Click Reset to restore default.", juce::dontSendNotification);
        hintLabel.setJustificationType(juce::Justification::centredLeft);
        hintLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(158, 169, 186));
        hintLabel.setFont(juce::Font(juce::FontOptions(11.0f)));
        addAndMakeVisible(hintLabel);

        table.setColour(juce::ListBox::backgroundColourId, juce::Colour::fromRGB(20, 22, 28));
        table.setModel(&tableModel);
        table.getHeader().addColumn("Action", 1, 250, 120, 600, true);
        table.getHeader().addColumn("Current", 2, 170, 90, 380, true);
        table.getHeader().addColumn("Rebind", 3, 120, 90, 160, false);
        table.getHeader().addColumn("Reset", 4, 90, 70, 140, false);
        table.getHeader().setStretchToFitActive(true);
        addAndMakeVisible(table);

        restoreAllButton.setButtonText("Restore All Defaults");
        restoreAllButton.onClick = [this]
        {
            if (onResetAll)
                onResetAll();
            refresh();
        };
        addAndMakeVisible(restoreAllButton);

        tableModel.requestRows = [this]() { return requestRows ? requestRows() : std::vector<Binding> {}; };
        tableModel.formatShortcut = [this](const Binding& binding)
        {
            return formatShortcut ? formatShortcut(binding) : juce::String("(none)");
        };
        tableModel.onRebind = [this](const juce::String& actionId)
        {
            if (onRebind)
                onRebind(actionId);
            refresh();
        };
        tableModel.onReset = [this](const juce::String& actionId)
        {
            if (onReset)
                onReset(actionId);
            refresh();
        };
    }

    void refresh()
    {
        tableModel.refresh();
        table.updateContent();
        table.repaint();
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(10);
        auto header = area.removeFromTop(40);
        titleLabel.setBounds(header.removeFromTop(22));
        hintLabel.setBounds(header);

        auto footer = area.removeFromBottom(36);
        restoreAllButton.setBounds(footer.removeFromLeft(190));
        table.setBounds(area);
    }

private:
    juce::Label titleLabel;
    juce::Label hintLabel;
    juce::TableListBox table;
    juce::TextButton restoreAllButton;
    HotkeysManagerTableModel tableModel;
};
}

BoomBGeneratorAudioProcessorEditor::BoomBGeneratorAudioProcessorEditor(BoomBapGeneratorAudioProcessor& processor)
    : juce::AudioProcessorEditor(&processor)
    , audioProcessor(processor)
{
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(true);
    addKeyListener(this);

    addAndMakeVisible(header);
    addAndMakeVisible(laneSurface);
    laneSurface.addAndMakeVisible(trackList);
    laneSurface.addAndMakeVisible(gridViewport);
    addAndMakeVisible(analysisPanel);
    addAndMakeVisible(soundModule);
    addAndMakeVisible(editorToolBar);
    addAndMakeVisible(optionsToolButton);
    addAndMakeVisible(hotkeysToolButton);
    addAndMakeVisible(pianoRollFullscreenButton);
    addAndMakeVisible(gridEditorFullscreenButton);

    auto* splitter = new SplitterHandleComponent();
    splitter->onPress = [this](const juce::MouseEvent& event)
    {
        splitterDragging = true;
        splitterDragStartX = static_cast<int>(event.getEventRelativeTo(&laneSurface).position.x);
        splitterStartWidth = uiLayoutState.leftPanelWidth;
    };
    splitter->onDragMove = [this](const juce::MouseEvent& event)
    {
        if (!splitterDragging)
            return;

        const int currentX = static_cast<int>(event.getEventRelativeTo(&laneSurface).position.x);
        uiLayoutState.leftPanelWidth = splitterStartWidth + (currentX - splitterDragStartX);
        resized();
    };
    splitter->onRelease = [this](const juce::MouseEvent&)
    {
        if (!splitterDragging)
            return;

        splitterDragging = false;
        saveUiLayoutState();
    };
    splitterHandle.reset(splitter);
    laneSurface.addAndMakeVisible(*splitterHandle);
    splitterHandle->toFront(false);

    gridViewport.setViewedComponent(&grid, false);
    gridViewport.setScrollBarsShown(true, false);
    gridViewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never);

    sub808PianoRoll.onNotesEdited = [this](const std::vector<NoteEvent>& notes)
    {
        auto before = audioProcessor.getProjectSnapshot();
        auto after = before;
        auto dedicatedTrackType = alternateEditorSession.trackType;
        if (!dedicatedTrackType.has_value())
            dedicatedTrackType = dedicatedPianoRollTrackType(after);

        if (!dedicatedTrackType.has_value())
            return;

        for (auto& track : after.tracks)
        {
            if (track.type == *dedicatedTrackType)
            {
                track.notes = notes;
                break;
            }
        }

        audioProcessor.setTrackNotes(*dedicatedTrackType, notes);
        pushProjectHistoryState(before, after);
        refreshFromProcessor(true);
    };
    sub808PianoRoll.onOpenHotkeys = [this] { showHotkeysMenu(); };

    setupAttachments();
    bindTrackCallbacks();
    hatFxDragDensityUi = audioProcessor.getHatFxDragDensity();
    hatFxDragLockedUi = audioProcessor.isHatFxDragDensityLocked();
    trackList.setShowAnalysisPanel(false);
    trackList.setHatFxDragUiState(hatFxDragDensityUi, hatFxDragLockedUi);
    loadUiLayoutState();
    header.setHeaderControlsMode(uiLayoutState.headerControlsMode);
    trackList.setDisplayMode(laneRackModeForWidth(uiLayoutState.leftPanelWidth));

    editorToolBar.addAndMakeVisible(editorToolLabel);
    editorToolBar.addAndMakeVisible(pencilToolButton);
    editorToolBar.addAndMakeVisible(brushToolButton);
    editorToolBar.addAndMakeVisible(selectToolButton);
    editorToolBar.addAndMakeVisible(cutToolButton);
    editorToolBar.addAndMakeVisible(eraseToolButton);
    editorToolBar.setInterceptsMouseClicks(true, true);
    editorToolLabel.setText("Tools", juce::dontSendNotification);
    editorToolLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(178, 188, 202));
    editorToolLabel.setFont(juce::Font(juce::FontOptions(11.0f)));

    auto setupToolButton = [this](juce::TextButton& button, GridEditorComponent::EditorTool tool)
    {
        button.setClickingTogglesState(true);
        button.setRadioGroupId(22007);
        button.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGB(98, 152, 220));
        button.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(48, 56, 68));
        button.onClick = [this, tool]
        {
            setActiveEditorTool(tool);
        };
    };

    setupToolButton(pencilToolButton, GridEditorComponent::EditorTool::Pencil);
    setupToolButton(brushToolButton, GridEditorComponent::EditorTool::Brush);
    setupToolButton(selectToolButton, GridEditorComponent::EditorTool::Select);
    setupToolButton(cutToolButton, GridEditorComponent::EditorTool::Cut);
    setupToolButton(eraseToolButton, GridEditorComponent::EditorTool::Erase);

    sub808PianoRoll.onToolChanged = [this](GridEditorComponent::EditorTool tool)
    {
        setActiveEditorTool(tool);
    };
    optionsToolButton.onClick = [this]
    {
        showEditorOptionsMenu();
    };
    hotkeysToolButton.onClick = [this]
    {
        showHotkeysMenu();
    };
    pianoRollFullscreenButton.setClickingTogglesState(true);
    pianoRollFullscreenButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGB(98, 152, 220));
    pianoRollFullscreenButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(48, 56, 68));
    pianoRollFullscreenButton.onClick = [this]
    {
        toggleSub808PianoRollFullscreen();
    };
    
    gridEditorFullscreenButton.setClickingTogglesState(true);
    gridEditorFullscreenButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGB(98, 152, 220));
    gridEditorFullscreenButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(48, 56, 68));
    gridEditorFullscreenButton.onClick = [this]
    {
        toggleGridEditorFullscreen();
    };
    
    grid.setEditorTool(GridEditorComponent::EditorTool::Pencil);
    syncEditorToolButtons(GridEditorComponent::EditorTool::Pencil);
    setupHotkeys();

    header.onGeneratePressed = [this]
    {
        audioProcessor.generatePattern();
        refreshFromProcessor();
    };

    header.onMutatePressed = [this]
    {
        audioProcessor.mutatePattern();
        refreshFromProcessor();
    };

    header.onPlayToggled = [this](bool shouldStart)
    {
        setPreviewPlayback(shouldStart);
    };

    header.onStartPlayWithDawToggled = [this](bool enabled)
    {
        audioProcessor.setStartPlayWithDawEnabled(enabled);
        refreshFromProcessor(false);
    };

    header.onTransportToStart = [this]
    {
        seekPreviewToStep(0, true);
    };

    header.onTransportStepBack = [this]
    {
        stepPreviewStart(-1);
    };

    header.onTransportStepForward = [this]
    {
        stepPreviewStart(1);
    };

    header.onTransportToEnd = [this]
    {
        seekPreviewToStep(juce::jmax(0, getPatternStepCount() - 1), true);
    };

    header.onExportFullPressed = [this]
    {
        exportFullPattern();
    };

    header.onExportLoopWavPressed = [this]
    {
        exportLoopWav();
    };

    header.onDragFullPressed = [this]
    {
        dragFullPatternTempMidi();
    };

    header.onDragFullGesture = [this]
    {
        dragFullPatternExternal();
    };

    header.onToggleStandaloneWindow = [this]
    {
        toggleStandaloneWindowMaximize();
    };

    header.onZoomChanged = [this](float zoomX, float lane)
    {
        horizontalZoom = zoomX;
        laneHeight = static_cast<int>(lane);
        trackList.setRowHeight(laneHeight);
        grid.setLaneHeight(laneHeight);
        grid.setStepWidth(20.0f * horizontalZoom);
        sub808PianoRoll.setStepWidth(20.0f * horizontalZoom);
        updateGridGeometry();
    };

    header.onGridResolutionChanged = [this](int selectedId)
    {
        grid.setGridResolution(gridResolutionFromId(selectedId));
    };

    header.onPreviewPlaybackModeChanged = [this](int selectedId)
    {
        audioProcessor.setPreviewPlaybackMode(previewPlaybackModeFromId(selectedId));
        refreshFromProcessor(false);
    };

    grid.onGridModeDisplayChanged = [this](const juce::String& text)
    {
        header.setGridModeIndicatorText(text);
    };
        
        header.hatFxDensitySlider.onValueChange = [this]
        {
            const float density = static_cast<float>(header.hatFxDensitySlider.getValue());
            applyHatFxDragDensityFromUi(density, false);
        };
        
        header.hatFxDensityLockToggle.onClick = [this]
        {
            hatFxDragLockedUi = header.hatFxDensityLockToggle.getToggleState();
            audioProcessor.setHatFxDragDensity(hatFxDragDensityUi, hatFxDragLockedUi);
            refreshFromProcessor(true);
        };

    header.onHeaderControlsModeChanged = [this](MainHeaderComponent::HeaderControlsMode mode)
    {
        uiLayoutState.headerControlsMode = mode;
        saveUiLayoutState();
        resized();
    };

    grid.onTrackFocused = [this](const RuntimeLaneId& laneId)
    {
        audioProcessor.setSelectedTrack(laneId);
        grid.setSelectedTrack(laneId);
    };

    grid.onPlaybackFlagChanged = [this](int step)
    {
        audioProcessor.setPreviewStartStep(step);
        refreshFromProcessor(false);
    };

    grid.onTrackNotesEdited = [this](const RuntimeLaneId& laneId, const std::vector<NoteEvent>& notes)
    {
        auto before = audioProcessor.getProjectSnapshot();
        auto after = before;
        for (auto& lane : after.tracks)
        {
            if (lane.laneId == laneId)
            {
                lane.notes = notes;
                break;
            }
        }

        audioProcessor.setTrackNotes(laneId, notes);
        pushProjectHistoryState(before, after);
        refreshFromProcessor(true);
    };
    grid.onLoopRegionChanged = [this](const std::optional<juce::Range<int>>& tickRange)
    {
        audioProcessor.setPreviewLoopRegion(tickRange);
        refreshFromProcessor(false);
    };

    grid.onEditorRegionStateChanged = [this](const GridEditorComponent::EditorRegionState& state)
    {
        editorRegionState = state;
    };

    grid.onHorizontalZoomGesture = [this](float delta, juce::Point<int> anchor)
    {
        adjustGridHorizontalZoom(delta, anchor.x);
    };

    grid.onLaneHeightZoomGesture = [this](float delta, juce::Point<int> anchor)
    {
        adjustGridVerticalZoom(delta, anchor.y);
    };

    setSize(1280, 760);
    setResizable(true, true);
    setResizeLimits(760, 440, 2200, 1400);

#if JUCE_STANDALONE_APPLICATION
    header.setStandaloneWindowButtonVisible(true);

    juce::MessageManager::callAsync([safeEditor = juce::Component::SafePointer<BoomBGeneratorAudioProcessorEditor>(this)]
    {
        if (safeEditor == nullptr)
            return;

        if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        {
            const auto userArea = display->userArea;
            if (gStandaloneLastRestoreBounds.isEmpty())
                gStandaloneLastRestoreBounds = userArea.reduced(32);

            safeEditor->setSize(userArea.getWidth(), userArea.getHeight());

            if (auto* top = safeEditor->findParentComponentOfClass<juce::TopLevelWindow>())
                top->setBounds(userArea);
        }
    });
#else
    header.setStandaloneWindowButtonVisible(false);
#endif

    refreshFromProcessor();
    startTimerHz(30);
}

BoomBGeneratorAudioProcessorEditor::~BoomBGeneratorAudioProcessorEditor() = default;

bool BoomBGeneratorAudioProcessorEditor::keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent)
{
    juce::ignoreUnused(originatingComponent);

    if (key == juce::KeyPress('L', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::altModifier | juce::ModifierKeys::shiftModifier, 0))
    {
        showStyleLabWindow();
        return true;
    }

    // Handle ESC to exit fullscreen modes
    if (key == juce::KeyPress::escapeKey)
    {
        if (gridEditorFullscreenMode)
        {
            toggleGridEditorFullscreen();
            return true;
        }
        if (pianoRollFullscreenMode)
        {
            toggleSub808PianoRollFullscreen();
            return true;
        }
    }

    auto isKeyForAction = [this, &key](const juce::String& actionId)
    {
        auto it = std::find_if(hotkeyBindings.begin(), hotkeyBindings.end(), [&actionId](const HotkeyBinding& binding)
        {
            return binding.actionId == actionId;
        });

        if (it == hotkeyBindings.end() || it->usesMouseModifier || !it->keyPress.isValid())
            return false;

        return key.getKeyCode() == it->keyPress.getKeyCode()
            && key.getModifiers().getRawFlags() == it->keyPress.getModifiers().getRawFlags();
    };

    auto applyCurrentEditorTool = [this](GridEditorComponent::EditorTool tool)
    {
        setActiveEditorTool(tool);
        return true;
    };

    if (isKeyForAction(kActionPencilTool))
        return applyCurrentEditorTool(GridEditorComponent::EditorTool::Pencil);

    if (isKeyForAction(kActionBrushTool))
        return applyCurrentEditorTool(GridEditorComponent::EditorTool::Brush);

    if (isKeyForAction(kActionSelectTool))
        return applyCurrentEditorTool(GridEditorComponent::EditorTool::Select);

    if (isKeyForAction(kActionCutTool))
        return applyCurrentEditorTool(GridEditorComponent::EditorTool::Cut);

    if (isKeyForAction(kActionEraseTool))
        return applyCurrentEditorTool(GridEditorComponent::EditorTool::Erase);

    if (isKeyForAction(kActionUndo))
        return performUndo();

    if (isKeyForAction(kActionRedo))
        return performRedo();

    if (isPianoRollEditorVisible())
    {
        if (isKeyForAction(kActionDeleteNote) && sub808PianoRoll.deleteSelectedNotes())
            return true;

        if (isKeyForAction(kActionCopySelection) && sub808PianoRoll.copySelectionToClipboard())
            return true;

        if (isKeyForAction(kActionCutSelection) && sub808PianoRoll.cutSelectionToClipboard())
            return true;

        if (isKeyForAction(kActionPasteSelection) && sub808PianoRoll.pasteClipboardAtAnchor())
            return true;

        if (isKeyForAction(kActionDuplicateSelection) && sub808PianoRoll.duplicateSelectionToRight())
            return true;

        if (isKeyForAction(kActionSelectAllNotes) && sub808PianoRoll.selectAllNotes())
            return true;

        if (isKeyForAction(kActionDeselectAll))
        {
            sub808PianoRoll.clearNoteSelection();
            return true;
        }
    }

    if (isKeyForAction(kActionDeleteNote) && grid.deleteSelectedNotes())
        return true;

    if (isKeyForAction(kActionCopySelection) && grid.copySelectionToClipboard())
        return true;

    if (isKeyForAction(kActionCutSelection) && grid.cutSelectionToClipboard())
        return true;

    if (isKeyForAction(kActionPasteSelection) && grid.pasteClipboardAtPlayhead())
        return true;

    if (isKeyForAction(kActionDuplicateSelection) && grid.duplicateSelectionToRight())
        return true;

    if (isKeyForAction(kActionSelectAllNotes))
    {
        const auto project = audioProcessor.getProjectSnapshot();
        if (!project.tracks.empty())
        {
            const int idx = juce::jlimit(0, static_cast<int>(project.tracks.size()) - 1, project.selectedTrackIndex);
            if (grid.selectAllNotesInLane(project.tracks[static_cast<size_t>(idx)].laneId))
                return true;
        }
    }

    if (isKeyForAction(kActionDeselectAll))
    {
        grid.clearNoteSelection();
        return true;
    }

    if (isKeyForAction(kActionRoleAnchor) && grid.setSelectionSemanticRole("anchor"))
        return true;

    if (isKeyForAction(kActionRoleSupport) && grid.setSelectionSemanticRole("support"))
        return true;

    if (isKeyForAction(kActionRoleFill) && grid.setSelectionSemanticRole("fill"))
        return true;

    if (isKeyForAction(kActionRoleAccent) && grid.setSelectionSemanticRole("accent"))
        return true;

    if (isKeyForAction(kActionRoleClear) && grid.setSelectionSemanticRole({}))
        return true;

    if (isKeyForAction(kActionZoomToSelection) && zoomGridToSelection())
        return true;

    if (isKeyForAction(kActionZoomToPattern))
    {
        zoomGridToPattern();
        return true;
    }

    if (key.getKeyCode() == juce::KeyPress::leftKey)
    {
        if (key.getModifiers().isShiftDown())
            return grid.nudgeSelectionMicro(-1);
        return grid.nudgeSelectionBySnap(-1);
    }

    if (key.getKeyCode() == juce::KeyPress::rightKey)
    {
        if (key.getModifiers().isShiftDown())
            return grid.nudgeSelectionMicro(1);
        return grid.nudgeSelectionBySnap(1);
    }

    if (key == juce::KeyPress::spaceKey)
    {
        if (dynamic_cast<juce::TextEditor*>(juce::Component::getCurrentlyFocusedComponent()) != nullptr)
            return false;

        togglePreviewPlayback();
        return true;
    }

    return false;
}

bool BoomBGeneratorAudioProcessorEditor::keyStateChanged(bool isKeyDown, juce::Component* originatingComponent)
{
    juce::ignoreUnused(isKeyDown, originatingComponent);
    grid.refreshTransientInputState();
    if (isPianoRollEditorVisible())
        sub808PianoRoll.refreshTransientInputState();
    return false;
}

void BoomBGeneratorAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(12, 14, 18));

    if (!splitterBounds.isEmpty())
    {
        const auto x = currentUiOffsetX + static_cast<float>(splitterBounds.getCentreX()) * currentUiScale;
        const auto top = currentUiOffsetY + static_cast<float>(splitterBounds.getY()) * currentUiScale;
        const auto bottom = currentUiOffsetY + static_cast<float>(splitterBounds.getBottom()) * currentUiScale;

        g.setColour(juce::Colour::fromRGBA(255, 255, 255, 44));
        g.drawLine(x, top, x, bottom, static_cast<float>(kSplitterVisualWidth));
    }
}

void BoomBGeneratorAudioProcessorEditor::resized()
{
    auto outer = getLocalBounds().reduced(8);
    if (outer.isEmpty())
        return;

    const float sx = static_cast<float>(outer.getWidth()) / static_cast<float>(kDesignWidth);
    const float sy = static_cast<float>(outer.getHeight()) / static_cast<float>(kDesignHeight);
    const float uiScale = juce::jlimit(kMinUiScale, 1.0f, std::min(sx, sy));

    const int virtualWidth = juce::jmax(1, static_cast<int>(std::round(static_cast<float>(outer.getWidth()) / uiScale)));
    const int virtualHeight = juce::jmax(1, static_cast<int>(std::round(static_cast<float>(outer.getHeight()) / uiScale)));

    juce::Rectangle<int> area(0, 0, virtualWidth, virtualHeight);

#if JUCE_STANDALONE_APPLICATION
    if (auto* top = findParentComponentOfClass<juce::TopLevelWindow>())
        if (!isStandaloneWindowMaximized())
            gStandaloneLastRestoreBounds = top->getBounds();
#endif

    header.setVisible(!gridEditorFullscreenMode && !pianoRollFullscreenMode);

    if (!gridEditorFullscreenMode && !pianoRollFullscreenMode)
    {
        const int headerHeight = header.getPreferredHeight();
        header.setBounds(area.removeFromTop(headerHeight));
        area.removeFromTop(6);
    }
    else
    {
        header.setBounds({});
    }

    // ─────────────────────────────────────────────────────────────────────
    // GRID EDITOR FULLSCREEN MODE
    // ─────────────────────────────────────────────────────────────────────
    if (gridEditorFullscreenMode)
    {
        laneSurface.setVisible(true);
        analysisPanel.setVisible(false);
        soundModule.setVisible(false);
        soundModule.setBounds({});
        optionsToolButton.setVisible(false);
        hotkeysToolButton.setVisible(false);

        const int maxRackByAvailableSpace = juce::jmax(kRackMinWidth, area.getWidth() - kGridMinWidth - kSplitterHitWidth - kPaneGap);
        const int rackMax = juce::jmax(kRackMinWidth, juce::jmin(kRackMaxWidth, maxRackByAvailableSpace));
        uiLayoutState.leftPanelWidth = juce::jlimit(kRackMinWidth, rackMax, uiLayoutState.leftPanelWidth);

        const int visibleRows = juce::jmax(1, trackList.getVisibleRowCount());
        const int fullscreenLaneHeight = juce::jlimit(22,
                                                      96,
                                                      (juce::jmax(1, area.getHeight() - kEditorToolBarHeight - 6 - 24)
                                                       - 24) / visibleRows);
        trackList.setRowHeight(fullscreenLaneHeight);
        grid.setLaneHeight(fullscreenLaneHeight);

        trackList.setDisplayMode(laneRackModeForWidth(uiLayoutState.leftPanelWidth));
        const int laneSurfaceHeight = juce::jmax(1, trackList.getLaneSectionHeight());
        laneSurface.setBounds(area.removeFromTop(laneSurfaceHeight));

        auto laneArea = laneSurface.getLocalBounds();
        trackList.setBounds(laneArea.removeFromLeft(uiLayoutState.leftPanelWidth));
        laneArea.removeFromLeft(kPaneGap);
        auto splitterLocalArea = juce::Rectangle<int>(laneArea.getX(), laneArea.getY(), kSplitterHitWidth, laneSurface.getHeight());
        splitterBounds = splitterLocalArea.translated(laneSurface.getX(), laneSurface.getY());
        if (splitterHandle != nullptr)
            splitterHandle->setBounds(splitterLocalArea);
        laneArea.removeFromLeft(kSplitterHitWidth + kPaneGap);
        gridViewport.setBounds(laneArea);

        const int toolsHeight = kEditorToolBarHeight;
        const int gridToToolsGap = 2;
        if (area.getHeight() > 0)
            area.removeFromTop(juce::jmin(gridToToolsGap, area.getHeight()));

        if (area.getHeight() >= toolsHeight)
        {
            auto toolBarArea = area.removeFromTop(toolsHeight);
            editorToolBar.setBounds(toolBarArea);
            auto tb = editorToolBar.getLocalBounds().reduced(6, 4);
            editorToolLabel.setBounds(tb.removeFromLeft(42));
            pencilToolButton.setBounds(tb.removeFromLeft(66));
            tb.removeFromLeft(4);
            brushToolButton.setBounds(tb.removeFromLeft(62));
            tb.removeFromLeft(4);
            selectToolButton.setBounds(tb.removeFromLeft(62));
            tb.removeFromLeft(4);
            cutToolButton.setBounds(tb.removeFromLeft(52));
            tb.removeFromLeft(4);
            eraseToolButton.setBounds(tb.removeFromLeft(58));
        }
        else
        {
            editorToolBar.setBounds({});
        }
    }
    // ─────────────────────────────────────────────────────────────────────
    // FULLSCREEN MODE: piano roll occupies entire remaining space
    // ─────────────────────────────────────────────────────────────────────
    else if (pianoRollFullscreenMode && isAlternateEditorVisible())
    {
        laneSurface.setVisible(true);
        analysisPanel.setVisible(false);
        soundModule.setVisible(false);
        soundModule.setBounds({});
        splitterBounds = {};
        if (splitterHandle != nullptr)
            splitterHandle->setBounds({});
        optionsToolButton.setVisible(false);
        hotkeysToolButton.setVisible(false);

        const int maxRackByAvailableSpace = juce::jmax(kRackMinWidth, area.getWidth() - kGridMinWidth - kSplitterHitWidth - kPaneGap);
        const int rackMax = juce::jmax(kRackMinWidth, juce::jmin(kRackMaxWidth, maxRackByAvailableSpace));
        uiLayoutState.leftPanelWidth = juce::jlimit(kRackMinWidth, rackMax, uiLayoutState.leftPanelWidth);
        const int visibleRows = juce::jmax(1, trackList.getVisibleRowCount());
        const int fullscreenLaneHeight = juce::jlimit(22,
                                  96,
                                  (juce::jmax(1, area.getHeight() - kEditorToolBarHeight - 6 - 24)
                                   - 24) / visibleRows);
        trackList.setRowHeight(fullscreenLaneHeight);
        trackList.setDisplayMode(laneRackModeForWidth(uiLayoutState.leftPanelWidth));
        const int laneSurfaceHeight = juce::jmax(1, trackList.getLaneSectionHeight());
        laneSurface.setBounds(area.removeFromTop(laneSurfaceHeight));

        auto laneArea = laneSurface.getLocalBounds();
        trackList.setBounds(laneArea.removeFromLeft(uiLayoutState.leftPanelWidth));
        laneArea.removeFromLeft(kPaneGap);
        auto splitterLocalArea = juce::Rectangle<int>(laneArea.getX(), laneArea.getY(), kSplitterHitWidth, laneSurface.getHeight());
        splitterBounds = splitterLocalArea.translated(laneSurface.getX(), laneSurface.getY());
        if (splitterHandle != nullptr)
            splitterHandle->setBounds(splitterLocalArea);
        const int toolsHeight = kEditorToolBarHeight;
        const int gridToToolsGap = 2;
        laneArea.removeFromLeft(kSplitterHitWidth + kPaneGap);
        gridViewport.setBounds(laneArea);

        if (area.getHeight() > 0)
            area.removeFromTop(juce::jmin(gridToToolsGap, area.getHeight()));

        if (area.getHeight() >= toolsHeight)
        {
            auto toolBarArea = area.removeFromTop(toolsHeight);
            editorToolBar.setBounds(toolBarArea);
            auto tb = editorToolBar.getLocalBounds().reduced(6, 4);
            editorToolLabel.setBounds(tb.removeFromLeft(42));
            pencilToolButton.setBounds(tb.removeFromLeft(66));
            tb.removeFromLeft(4);
            brushToolButton.setBounds(tb.removeFromLeft(62));
            tb.removeFromLeft(4);
            selectToolButton.setBounds(tb.removeFromLeft(62));
            tb.removeFromLeft(4);
            cutToolButton.setBounds(tb.removeFromLeft(52));
            tb.removeFromLeft(4);
            eraseToolButton.setBounds(tb.removeFromLeft(58));
        }
        else
        {
            editorToolBar.setBounds({});
        }
    }
    // ─────────────────────────────────────────────────────────────────────
    // NORMAL MODE: standard layout with left panel + grid + sound module
    // ─────────────────────────────────────────────────────────────────────
    else
    {
        trackList.setRowHeight(laneHeight);
        grid.setLaneHeight(laneHeight);
        laneSurface.setVisible(true);
        const bool alternateEditorVisible = isAlternateEditorVisible();
        analysisPanel.setVisible(!alternateEditorVisible);
        soundModule.setVisible(!alternateEditorVisible);

        const int maxRackByAvailableSpace = juce::jmax(kRackMinWidth, area.getWidth() - kGridMinWidth - kSplitterHitWidth - kPaneGap);
        const int rackMax = juce::jmax(kRackMinWidth, juce::jmin(kRackMaxWidth, maxRackByAvailableSpace));
        uiLayoutState.leftPanelWidth = juce::jlimit(kRackMinWidth, rackMax, uiLayoutState.leftPanelWidth);

        trackList.setDisplayMode(laneRackModeForWidth(uiLayoutState.leftPanelWidth));
        const int toolsHeight = kEditorToolBarHeight;
        const int toolsToBottomGap = 10;
        const int gridToToolsGap = 2;
        int soundModuleHeight = juce::jlimit(kSoundModuleMinHeight,
                                             kSoundModuleMaxHeight,
                                             static_cast<int>(std::round(static_cast<float>(area.getHeight()) * 0.26f)));
        if (area.getHeight() < (kSoundModuleMinHeight + 220))
            soundModuleHeight = 0;

        if (alternateEditorVisible)
            soundModuleHeight = 0;

        const int laneSectionHeight = juce::jmax(1, trackList.getLaneSectionHeight());
        const int bottomPanelHeight = alternateEditorVisible ? juce::jmax(120, area.getHeight() - laneSectionHeight - toolsHeight - toolsToBottomGap)
                                                             : juce::jmax(soundModuleHeight, 180);
        const int maxLaneHeight = juce::jmax(1, area.getHeight() - toolsHeight - toolsToBottomGap - bottomPanelHeight);
        const int laneSurfaceHeight = juce::jlimit(1, maxLaneHeight, laneSectionHeight);
        laneSurface.setBounds(area.removeFromTop(laneSurfaceHeight));

        auto laneArea = laneSurface.getLocalBounds();
        trackList.setBounds(laneArea.removeFromLeft(uiLayoutState.leftPanelWidth));
        laneArea.removeFromLeft(kPaneGap);
        auto splitterLocalArea = juce::Rectangle<int>(laneArea.getX(), laneArea.getY(), kSplitterHitWidth, laneSurface.getHeight());
        splitterBounds = splitterLocalArea.translated(laneSurface.getX(), laneSurface.getY());
        if (splitterHandle != nullptr)
            splitterHandle->setBounds(splitterLocalArea);
        laneArea.removeFromLeft(kSplitterHitWidth + kPaneGap);
        gridViewport.setBounds(laneArea);

        if (area.getHeight() > 0)
            area.removeFromTop(juce::jmin(gridToToolsGap, area.getHeight()));

        if (area.getHeight() >= toolsHeight)
        {
            auto toolBarArea = area.removeFromTop(toolsHeight);
            editorToolBar.setBounds(toolBarArea);
            auto tb = editorToolBar.getLocalBounds().reduced(6, 4);
            editorToolLabel.setBounds(tb.removeFromLeft(42));
            pencilToolButton.setBounds(tb.removeFromLeft(66));
            tb.removeFromLeft(4);
            brushToolButton.setBounds(tb.removeFromLeft(62));
            tb.removeFromLeft(4);
            selectToolButton.setBounds(tb.removeFromLeft(62));
            tb.removeFromLeft(4);
            cutToolButton.setBounds(tb.removeFromLeft(52));
            tb.removeFromLeft(4);
            eraseToolButton.setBounds(tb.removeFromLeft(58));
        }
        else
        {
            editorToolBar.setBounds({});
        }

        if (area.getHeight() > 0)
            area.removeFromTop(juce::jmin(toolsToBottomGap, area.getHeight()));

        auto bottomArea = area;
        auto leftColumn = bottomArea.removeFromLeft(uiLayoutState.leftPanelWidth);
        if (!alternateEditorVisible)
        {
            bottomArea.removeFromLeft(kPaneGap + kSplitterHitWidth + kPaneGap);
            soundModule.setBounds(bottomArea);
            soundModule.setVisible(!bottomArea.isEmpty());
        }
        else
        {
            soundModule.setVisible(false);
            soundModule.setBounds({});
        }

        if (!alternateEditorVisible)
            analysisPanel.setBounds(leftColumn);
        else
            analysisPanel.setBounds({});
    }

    // Keep global Options/Hotkeys in top-left corner only in normal mode
    if (!pianoRollFullscreenMode)
    {
        optionsToolButton.setBounds(6, 6, 72, 24);
        hotkeysToolButton.setBounds(82, 6, 74, 24);
        hotkeysToolButton.setVisible(true);
    }
    else
    {
        optionsToolButton.setVisible(false);
        hotkeysToolButton.setVisible(false);
    }

    const int fullscreenButtonWidth = 60;
    const int fullscreenButtonHeight = 24;
    const auto viewportInEditor = gridViewport.getBounds().translated(laneSurface.getX(), laneSurface.getY());
    const int fullscreenButtonX = juce::jmax(0, viewportInEditor.getRight() - fullscreenButtonWidth - 8);
    const int fullscreenButtonY = juce::jmax(0, viewportInEditor.getY() + 6);

    gridEditorFullscreenButton.setBounds(fullscreenButtonX, fullscreenButtonY, fullscreenButtonWidth, fullscreenButtonHeight);
    pianoRollFullscreenButton.setBounds(fullscreenButtonX, fullscreenButtonY, fullscreenButtonWidth, fullscreenButtonHeight);
    gridEditorFullscreenButton.setVisible(!isAlternateEditorVisible());
    pianoRollFullscreenButton.setVisible(isPianoRollEditorVisible());

    updateGridGeometry();

    const float scaledWidth = static_cast<float>(virtualWidth) * uiScale;
    const float scaledHeight = static_cast<float>(virtualHeight) * uiScale;
    const float tx = static_cast<float>(outer.getX()) + (static_cast<float>(outer.getWidth()) - scaledWidth) * 0.5f;
    const float ty = static_cast<float>(outer.getY()) + (static_cast<float>(outer.getHeight()) - scaledHeight) * 0.5f;

    currentUiScale = uiScale;
    currentUiOffsetX = tx;
    currentUiOffsetY = ty;

    const auto transform = juce::AffineTransform::scale(uiScale).translated(tx, ty);
    header.setTransform(transform);
    laneSurface.setTransform(transform);
    analysisPanel.setTransform(transform);
    soundModule.setTransform(transform);
    editorToolBar.setTransform(transform);
    optionsToolButton.setTransform(transform);
    hotkeysToolButton.setTransform(transform);
    pianoRollFullscreenButton.setTransform(transform);
    gridEditorFullscreenButton.setTransform(transform);

    repaint();
}

void BoomBGeneratorAudioProcessorEditor::timerCallback()
{
    refreshSubstyleBindingForGenre();
    audioProcessor.applySelectedStylePreset(false);
    refreshFromProcessor(false);
}

void BoomBGeneratorAudioProcessorEditor::refreshFromProcessor(bool refreshTrackRows)
{
    auto project = audioProcessor.getProjectSnapshot();
    syncLaneDisplayOrder(project);
    const auto transport = audioProcessor.getLastTransportSnapshot();
    const auto analysisRequest = audioProcessor.getSampleAnalysisRequest();
    const auto analysisResult = audioProcessor.getSampleAnalysisResult();
    const auto sampleContext = audioProcessor.getSampleAwareGenerationContext();
    const bool analysisReady = audioProcessor.isSampleAnalysisReady();
    const auto analysisMode = audioProcessor.getAnalysisMode();

    hatFxDragDensityUi = audioProcessor.getHatFxDragDensity();
    hatFxDragLockedUi = audioProcessor.isHatFxDragDensityLocked();
    trackList.setHatFxDragUiState(hatFxDragDensityUi, hatFxDragLockedUi);

    const auto* syncValue = audioProcessor.getApvts().getRawParameterValue(ParamIds::syncDawTempo);
    const auto* bpmLockValue = audioProcessor.getApvts().getRawParameterValue(ParamIds::bpmLock);
    const bool syncEnabled = syncValue != nullptr && syncValue->load() > 0.5f;
    const bool bpmLocked = bpmLockValue != nullptr && bpmLockValue->load() > 0.5f;
    header.setBpmLocked((syncEnabled && transport.hasHostTempo) || bpmLocked);

    if (refreshTrackRows)
    {
        trackList.setHatFxDragUiState(hatFxDragDensityUi, hatFxDragLockedUi);
        trackList.setLaneDisplayOrder(buildTrackListLaneOrder(project, laneDisplayOrder));
        trackList.setTracks(project.runtimeLaneProfile,
                            project.tracks,
                            audioProcessor.getBassKeyRootChoice(),
                            audioProcessor.getBassScaleModeChoice());
    }
    grid.setLaneDisplayOrder(laneDisplayOrder);

    const auto soundTarget = audioProcessor.getSoundModuleTrack();
    SoundLayerState panelSound = project.globalSound;
    if (soundTarget.has_value())
    {
        for (const auto& track : project.tracks)
        {
            if (track.type == *soundTarget)
            {
                panelSound = track.sound;
                break;
            }
        }
    }
    soundModule.setState(project.tracks, soundTarget, panelSound);

    header.setPreviewPlaying(audioProcessor.isPreviewPlaying());
    header.setStartPlayWithDawEnabled(audioProcessor.isStartPlayWithDawEnabled());
    header.setPreviewPlaybackModeId(previewPlaybackModeId(project.previewPlaybackMode));
    header.setStandaloneWindowMaximized(isStandaloneWindowMaximized());
    grid.setPlayheadStep(audioProcessor.getPreviewPlayheadStep());
    grid.setLoopRegion(audioProcessor.getPreviewLoopRegion());
    const auto selectedTrack = project.tracks.empty()
        ? RuntimeLaneId{}
        : project.tracks[static_cast<size_t>(juce::jlimit(0, static_cast<int>(project.tracks.size()) - 1, project.selectedTrackIndex))].laneId;
    grid.setSelectedTrack(selectedTrack);

    sub808PianoRoll.setBars(project.params.bars);
    sub808PianoRoll.setStepWidth(20.0f * horizontalZoom);
    for (const auto& track : project.tracks)
    {
        if (alternateEditorForTrack(project, track.type) == AlternateLaneEditor::PianoRoll)
        {
            sub808PianoRoll.setNotes(track.notes);
            break;
        }
    }

    const bool mustRefreshGridModel = refreshTrackRows || (project.generationCounter != lastGenerationCounter);
    if (mustRefreshGridModel)
    {
        grid.setProject(project);
        lastGenerationCounter = project.generationCounter;
    }

    editorRegionState = grid.getEditorRegionState();

    updateGridGeometry();
    header.setHatFxDensityState(hatFxDragDensityUi, hatFxDragLockedUi);

    juce::String status;
    juce::String details;
    if (analysisReady)
    {
        const auto avg = [](const std::vector<float>& values)
        {
            if (values.empty())
                return 0.0f;

            const float sum = std::accumulate(values.begin(), values.end(), 0.0f);
            return sum / static_cast<float>(values.size());
        };

        status = "BPM " + juce::String(analysisResult.detectedBpm, 1)
            + (analysisResult.bpmReliable ? " (reliable)" : " (estimate)")
            + " | bars " + juce::String(analysisResult.analyzedBars)
            + " | boundaries " + juce::String(static_cast<int>(analysisResult.phraseBoundaryBars.size()));

        details = "Energy " + juce::String(avg(analysisResult.energyPerStep), 2)
            + " | onset " + juce::String(avg(analysisResult.onsetStrengthPerStep), 2)
            + " | density bars " + juce::String(static_cast<int>(analysisResult.densityPerBar.size()))
            + "\nFlags: sparse=" + juce::String(analysisResult.sparse ? "yes" : "no")
            + " dense=" + juce::String(analysisResult.dense ? "yes" : "no")
            + " transientRich=" + juce::String(analysisResult.transientRich ? "yes" : "no")
            + " loopLike=" + juce::String(analysisResult.loopLike ? "yes" : "no");
    }

    juce::String generationDebug = audioProcessor.getGenerationDebugSummary();
    if (analysisReady && !audioProcessor.getAudioFeatureMap().steps.empty())
    {
        const auto map = audioProcessor.getAudioFeatureMap();
        int strongestStep = 0;
        float strongestOnset = map.steps.front().onset;
        for (int i = 1; i < static_cast<int>(map.steps.size()); ++i)
        {
            if (map.steps[static_cast<size_t>(i)].onset > strongestOnset)
            {
                strongestOnset = map.steps[static_cast<size_t>(i)].onset;
                strongestStep = i;
            }
        }

        generationDebug += "\nPeak onset step: " + juce::String(strongestStep + 1)
            + " / " + juce::String(static_cast<int>(map.steps.size()))
            + " (" + juce::String(strongestOnset, 2) + ")";
    }

    analysisPanel.setPanelState(analysisRequest.source,
                                analysisMode,
                                analysisRequest.barsToCapture,
                                analysisRequest.tempoHandling,
                                sampleContext.reactivity,
                                sampleContext.supportVsContrast,
                                analysisRequest.audioFile.existsAsFile()
                                    ? ("File: " + analysisRequest.audioFile.getFileName())
                                    : "File: (none)",
                                status,
                                details,
                                generationDebug,
                                analysisReady);
}

void BoomBGeneratorAudioProcessorEditor::setPreviewPlayback(bool shouldStart)
{
    if (shouldStart)
        audioProcessor.startPreview();
    else
        audioProcessor.stopPreview();

    refreshFromProcessor(false);
}

void BoomBGeneratorAudioProcessorEditor::togglePreviewPlayback()
{
    setPreviewPlayback(!audioProcessor.isPreviewPlaying());
}

void BoomBGeneratorAudioProcessorEditor::seekPreviewToStep(int step, bool resumeIfPlaying)
{
    const int maxStep = juce::jmax(0, getPatternStepCount() - 1);
    const int clampedStep = juce::jlimit(0, maxStep, step);
    const bool wasPlaying = audioProcessor.isPreviewPlaying();

    audioProcessor.setPreviewStartStep(clampedStep);

    if (wasPlaying)
    {
        audioProcessor.stopPreview();
        if (resumeIfPlaying)
            audioProcessor.startPreview();
    }

    refreshFromProcessor(false);
}

void BoomBGeneratorAudioProcessorEditor::stepPreviewStart(int stepDelta)
{
    const auto project = audioProcessor.getProjectSnapshot();
    seekPreviewToStep(project.previewStartStep + stepDelta, true);
}

int BoomBGeneratorAudioProcessorEditor::getPatternStepCount() const
{
    const auto project = audioProcessor.getProjectSnapshot();
    return juce::jmax(1, project.params.bars * 16);
}

void BoomBGeneratorAudioProcessorEditor::setupAttachments()
{
    auto& apvts = audioProcessor.getApvts();

    bpmAttachment = std::make_unique<SliderAttachment>(apvts, ParamIds::bpm, header.bpmSlider);
    bpmLockAttachment = std::make_unique<ButtonAttachment>(apvts, ParamIds::bpmLock, header.bpmLockToggle);
    syncAttachment = std::make_unique<ButtonAttachment>(apvts, ParamIds::syncDawTempo, header.syncTempoToggle);
    swingAttachment = std::make_unique<SliderAttachment>(apvts, ParamIds::swingPercent, header.swingSlider);
    velocityAttachment = std::make_unique<SliderAttachment>(apvts, ParamIds::velocityAmount, header.velocitySlider);
    timingAttachment = std::make_unique<SliderAttachment>(apvts, ParamIds::timingAmount, header.timingSlider);
    humanizeAttachment = std::make_unique<SliderAttachment>(apvts, ParamIds::humanizeAmount, header.humanizeSlider);
    densityAttachment = std::make_unique<SliderAttachment>(apvts, ParamIds::densityAmount, header.densitySlider);
    tempoInterpretationAttachment = std::make_unique<ComboAttachment>(apvts, ParamIds::tempoInterpretation, header.tempoInterpretationCombo);
    barsAttachment = std::make_unique<ComboAttachment>(apvts, ParamIds::bars, header.barsCombo);
    genreAttachment = std::make_unique<ComboAttachment>(apvts, ParamIds::genre, header.genreCombo);
    seedAttachment = std::make_unique<SliderAttachment>(apvts, ParamIds::seed, header.seedSlider);
    seedLockAttachment = std::make_unique<ButtonAttachment>(apvts, ParamIds::seedLock, header.seedLockToggle);
    masterVolumeAttachment = std::make_unique<SliderAttachment>(apvts, ParamIds::masterVolume, header.masterVolumeSlider);
    masterCompressorAttachment = std::make_unique<SliderAttachment>(apvts, ParamIds::masterCompressor, header.masterCompressorSlider);
    masterLofiAttachment = std::make_unique<SliderAttachment>(apvts, ParamIds::masterLofi, header.masterLofiSlider);

    refreshSubstyleBindingForGenre();
}

void BoomBGeneratorAudioProcessorEditor::refreshSubstyleBindingForGenre()
{
    auto& apvts = audioProcessor.getApvts();
    const auto* genreValue = apvts.getRawParameterValue(ParamIds::genre);
    const int genreChoice = genreValue != nullptr ? static_cast<int>(genreValue->load()) : 0;
    if (genreChoice == lastGenreChoice && substyleAttachment != nullptr)
        return;

    lastGenreChoice = genreChoice;

    juce::StringArray choices;
    const char* substyleParamId = ParamIds::boombapSubstyle;
    switch (genreChoice)
    {
        case 1:
            choices = getRapSubstyleNames();
            substyleParamId = ParamIds::rapSubstyle;
            break;
        case 2:
            choices = getTrapSubstyleNames();
            substyleParamId = ParamIds::trapSubstyle;
            break;
        case 3:
            choices = getDrillSubstyleNames();
            substyleParamId = ParamIds::drillSubstyle;
            break;
        case 0:
        default:
            choices = getBoomBapSubstyleNames();
            substyleParamId = ParamIds::boombapSubstyle;
            break;
    }

    substyleAttachment.reset();
    header.substyleCombo.clear(juce::dontSendNotification);
    for (int i = 0; i < choices.size(); ++i)
        header.substyleCombo.addItem(choices[i], i + 1);

    substyleAttachment = std::make_unique<ComboAttachment>(apvts,
                                                           substyleParamId,
                                                           header.substyleCombo);
}

void BoomBGeneratorAudioProcessorEditor::bindTrackCallbacks()
{
    const auto resolveTrackType = [this](const RuntimeLaneId& laneId) -> std::optional<TrackType>
    {
        const auto project = audioProcessor.getProjectSnapshot();
        const auto* lane = findRuntimeLaneById(project.runtimeLaneProfile, laneId);
        if (lane == nullptr || !lane->runtimeTrackType.has_value())
            return std::nullopt;

        return lane->runtimeTrackType;
    };

    trackList.onRegenerateTrack = [this, resolveTrackType](const RuntimeLaneId& laneId)
    {
        const auto type = resolveTrackType(laneId);
        if (!type.has_value())
            return;

        audioProcessor.regenerateTrack(*type);
        refreshFromProcessor();
    };

    trackList.onSoloTrack = [this, resolveTrackType](const RuntimeLaneId& laneId, bool value)
    {
        const auto type = resolveTrackType(laneId);
        if (!type.has_value())
            return;

        audioProcessor.setTrackSolo(*type, value);
        refreshFromProcessor();
    };

    trackList.onMuteTrack = [this, resolveTrackType](const RuntimeLaneId& laneId, bool value)
    {
        const auto type = resolveTrackType(laneId);
        if (!type.has_value())
            return;

        audioProcessor.setTrackMuted(*type, value);
        refreshFromProcessor();
    };

    trackList.onClearTrack = [this, resolveTrackType](const RuntimeLaneId& laneId)
    {
        const auto type = resolveTrackType(laneId);
        if (!type.has_value())
            return;

        audioProcessor.clearTrack(*type);
        refreshFromProcessor();
    };

    trackList.onLockTrack = [this, resolveTrackType](const RuntimeLaneId& laneId, bool value)
    {
        const auto type = resolveTrackType(laneId);
        if (!type.has_value())
            return;

        audioProcessor.setTrackLocked(*type, value);
        refreshFromProcessor();
    };

    trackList.onEnableTrack = [this, resolveTrackType](const RuntimeLaneId& laneId, bool value)
    {
        const auto type = resolveTrackType(laneId);
        if (!type.has_value())
            return;

        audioProcessor.setTrackEnabled(*type, value);
        refreshFromProcessor();
    };

    trackList.onDragTrack = [this, resolveTrackType](const RuntimeLaneId& laneId)
    {
        const auto type = resolveTrackType(laneId);
        if (!type.has_value())
            return;

        dragTrackTempMidi(*type);
    };

    trackList.onDragTrackGesture = [this, resolveTrackType](const RuntimeLaneId& laneId)
    {
        const auto type = resolveTrackType(laneId);
        if (!type.has_value())
            return;

        dragTrackExternal(*type);
    };

    trackList.onDragDensityTrack = [this, resolveTrackType](const RuntimeLaneId& laneId, float density)
    {
        const auto type = resolveTrackType(laneId);
        if (!type.has_value() || *type != TrackType::HatFX)
            return;

        applyHatFxDragDensityFromUi(density, false);
    };

    trackList.onDragDensityLockTrack = [this, resolveTrackType](const RuntimeLaneId& laneId, bool locked)
    {
        const auto type = resolveTrackType(laneId);
        if (!type.has_value() || *type != TrackType::HatFX)
            return;

        hatFxDragLockedUi = locked;
        audioProcessor.setHatFxDragDensity(hatFxDragDensityUi, hatFxDragLockedUi);
        refreshFromProcessor(true);
    };

    trackList.onExportTrack = [this, resolveTrackType](const RuntimeLaneId& laneId)
    {
        const auto type = resolveTrackType(laneId);
        if (!type.has_value())
            return;

        exportTrack(*type);
    };

    trackList.onPrevSampleTrack = [this, resolveTrackType](const RuntimeLaneId& laneId)
    {
        const auto type = resolveTrackType(laneId);
        if (!type.has_value())
            return;

        audioProcessor.selectPreviousLaneSample(*type);
        refreshFromProcessor();
    };

    trackList.onNextSampleTrack = [this, resolveTrackType](const RuntimeLaneId& laneId)
    {
        const auto type = resolveTrackType(laneId);
        if (!type.has_value())
            return;

        audioProcessor.selectNextLaneSample(*type);
        refreshFromProcessor();
    };

    trackList.onSampleMenuTrack = [this, resolveTrackType](const RuntimeLaneId& laneId)
    {
        const auto type = resolveTrackType(laneId);
        if (!type.has_value())
            return;

        showSampleMenu(*type);
    };

    trackList.onImportMidiLaneTrack = [this](const RuntimeLaneId& laneId)
    {
        showImportMidiToLaneDialog(laneId);
    };

    trackList.onTrackNameClicked = [this, resolveTrackType](const RuntimeLaneId& laneId)
    {
        const auto type = resolveTrackType(laneId);
        if (!type.has_value())
            return;

        const auto project = audioProcessor.getProjectSnapshot();
        const auto editorType = alternateEditorForTrack(project, *type);
        if (editorType != AlternateLaneEditor::None)
        {
            audioProcessor.setSelectedTrack(laneId);
            grid.setSelectedTrack(laneId);
            const bool isSameEditor = alternateEditorSession.isVisible
                && alternateEditorSession.editorType == editorType
                && alternateEditorSession.trackType == *type;
            setAlternateEditorVisible(!isSameEditor, editorType, *type);
            return;
        }

        setAlternateEditorVisible(false);
    };

    trackList.onLaneVolumeTrack = [this, resolveTrackType](const RuntimeLaneId& laneId, float volume)
    {
        const auto type = resolveTrackType(laneId);
        if (!type.has_value())
            return;

        audioProcessor.setTrackLaneVolume(*type, volume);
        refreshFromProcessor(false);
    };

    trackList.onLaneSoundTrack = [this, resolveTrackType](const RuntimeLaneId& laneId, const SoundLayerState& state)
    {
        const auto type = resolveTrackType(laneId);
        if (!type.has_value())
            return;

        auto project = audioProcessor.getProjectSnapshot();
        SoundLayerState nextState = state;

        for (const auto& track : project.tracks)
        {
            if (track.type != *type)
                continue;

            nextState.eqTone = track.sound.eqTone;
            nextState.compression = track.sound.compression;
            nextState.reverb = track.sound.reverb;
            nextState.gate = track.sound.gate;
            nextState.transient = track.sound.transient;
            nextState.drive = track.sound.drive;
            break;
        }

        audioProcessor.setTrackSoundLayer(*type, nextState);
        refreshFromProcessor(false);
    };

    trackList.onRenameLaneRequested = [this](const RuntimeLaneId& laneId)
    {
        showRenameLaneDialog(laneId);
    };

    trackList.onDeleteLaneRequested = [this](const RuntimeLaneId& laneId)
    {
        requestDeleteLane(laneId);
    };

    trackList.onLaneMoveUpTrack = [this, resolveTrackType](const RuntimeLaneId& laneId)
    {
        const auto type = resolveTrackType(laneId);
        if (!type.has_value())
            return;

        moveLaneDisplayOrder(laneId, -1);
    };

    trackList.onLaneMoveDownTrack = [this, resolveTrackType](const RuntimeLaneId& laneId)
    {
        const auto type = resolveTrackType(laneId);
        if (!type.has_value())
            return;

        moveLaneDisplayOrder(laneId, 1);
    };

    trackList.onBassKeyChanged = [this](int choice)
    {
        audioProcessor.setBassKeyRootChoice(choice);
        refreshFromProcessor(false);
    };

    trackList.onBassScaleChanged = [this](int choice)
    {
        audioProcessor.setBassScaleModeChoice(choice);
        refreshFromProcessor(false);
    };

    trackList.onAddLaneRequested = [this]
    {
        showAddLaneMenu();
    };

    soundModule.onSoundTargetChanged = [this](const std::optional<TrackType>& target)
    {
        audioProcessor.setSoundModuleTrack(target);
        refreshFromProcessor(false);
    };

    soundModule.onSoundLayerChanged = [this](const std::optional<TrackType>& target, const SoundLayerState& state)
    {
        if (target.has_value())
            audioProcessor.setTrackSoundLayer(*target, state);
        else
            audioProcessor.setGlobalSoundLayer(state);
        refreshFromProcessor(false);
    };

    analysisPanel.onAnalysisSourceChanged = [this](SampleAnalysisRequest::SourceType source)
    {
        auto request = audioProcessor.getSampleAnalysisRequest();
        request.source = source;
        audioProcessor.setSampleAnalysisRequest(request);
        refreshFromProcessor(false);
    };

    analysisPanel.onAnalysisModeChanged = [this](AnalysisMode mode)
    {
        audioProcessor.setAnalysisMode(mode);
        refreshFromProcessor(false);
    };

    analysisPanel.onAnalysisBarsChanged = [this](int bars)
    {
        auto request = audioProcessor.getSampleAnalysisRequest();
        request.barsToCapture = juce::jlimit(2, 16, bars);
        audioProcessor.setSampleAnalysisRequest(request);
        refreshFromProcessor(false);
    };

    analysisPanel.onAnalysisTempoHandlingChanged = [this](SampleAnalysisRequest::TempoHandling tempoHandling)
    {
        auto request = audioProcessor.getSampleAnalysisRequest();
        request.tempoHandling = tempoHandling;
        audioProcessor.setSampleAnalysisRequest(request);
        refreshFromProcessor(false);
    };

    analysisPanel.onAnalysisReactivityChanged = [this](float value)
    {
        audioProcessor.setSampleReactivity(value);
        refreshFromProcessor(false);
    };

    analysisPanel.onSupportVsContrastChanged = [this](float value)
    {
        audioProcessor.setSupportVsContrast(value);
        refreshFromProcessor(false);
    };

    analysisPanel.onChooseAnalysisFile = [this]
    {
        auto chooser = std::make_shared<juce::FileChooser>(
            "Select audio file for analysis",
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
            "*.wav;*.aif;*.aiff;*.flac;*.mp3");

        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [this, chooser](const juce::FileChooser& fc)
                             {
                                 const auto selected = fc.getResult();
                                 if (selected == juce::File())
                                     return;

                                 auto request = audioProcessor.getSampleAnalysisRequest();
                                 request.source = SampleAnalysisRequest::SourceType::AudioFile;
                                 request.audioFile = selected;
                                 audioProcessor.setSampleAnalysisRequest(request);
                                 refreshFromProcessor(false);
                             });
    };

    analysisPanel.onAnalysisFileDropped = [this](const juce::File& file)
    {
        if (!file.existsAsFile())
            return;

        auto request = audioProcessor.getSampleAnalysisRequest();
        request.source = SampleAnalysisRequest::SourceType::AudioFile;
        request.audioFile = file;
        audioProcessor.setSampleAnalysisRequest(request);
        refreshFromProcessor(false);
    };

    analysisPanel.onRunAnalysis = [this]
    {
        juce::String error;
        const bool ok = audioProcessor.analyzeCurrentSampleSource(&error);
        if (!ok && error.isNotEmpty())
            logDrag("analysis error: " + error);
        refreshFromProcessor(false);
    };
}

void BoomBGeneratorAudioProcessorEditor::syncLaneDisplayOrder(const PatternProject& project)
{
    std::vector<RuntimeLaneId> normalized;
    normalized.reserve(project.tracks.size());

    for (const auto& laneId : laneDisplayOrder)
    {
        auto it = std::find_if(project.tracks.begin(), project.tracks.end(), [&laneId](const TrackState& track)
        {
            return track.laneId == laneId;
        });

        if (it != project.tracks.end())
            normalized.push_back(laneId);
    }

    for (const auto& track : project.tracks)
    {
        if (std::find(normalized.begin(), normalized.end(), track.laneId) == normalized.end())
            normalized.push_back(track.laneId);
    }

    laneDisplayOrder.swap(normalized);
}

void BoomBGeneratorAudioProcessorEditor::moveLaneDisplayOrder(const RuntimeLaneId& laneId, int delta)
{
    if (delta == 0)
        return;

    auto it = std::find(laneDisplayOrder.begin(), laneDisplayOrder.end(), laneId);
    if (it == laneDisplayOrder.end())
        return;

    const int index = static_cast<int>(std::distance(laneDisplayOrder.begin(), it));
    const int target = juce::jlimit(0, static_cast<int>(laneDisplayOrder.size()) - 1, index + delta);
    if (target == index)
        return;

    std::swap(laneDisplayOrder[static_cast<size_t>(index)], laneDisplayOrder[static_cast<size_t>(target)]);
    trackList.setLaneDisplayOrder(buildTrackListLaneOrder(audioProcessor.getProjectSnapshot(), laneDisplayOrder));
    grid.setLaneDisplayOrder(laneDisplayOrder);
    refreshFromProcessor(true);
}

void BoomBGeneratorAudioProcessorEditor::applyHatFxDragDensityFromUi(float density, bool forceWhenLocked)
{
    const float clamped = juce::jlimit(0.0f, 2.0f, density);
    hatFxDragDensityUi = clamped;

    if (hatFxDragLockedUi && !forceWhenLocked)
    {
        trackList.setHatFxDragUiState(hatFxDragDensityUi, hatFxDragLockedUi);
        return;
    }

    audioProcessor.setHatFxDragDensity(hatFxDragDensityUi, hatFxDragLockedUi);
    refreshFromProcessor(true);
}

void BoomBGeneratorAudioProcessorEditor::mouseMove(const juce::MouseEvent& event)
{
    updateCursorForMousePosition(event.position);
}

void BoomBGeneratorAudioProcessorEditor::mouseDown(const juce::MouseEvent& event)
{
    const auto virtualPos = toVirtualPoint(event.position);
    if (splitterBounds.contains(static_cast<int>(virtualPos.x), static_cast<int>(virtualPos.y)))
    {
        splitterDragging = true;
        splitterDragStartX = static_cast<int>(virtualPos.x);
        splitterStartWidth = uiLayoutState.leftPanelWidth;
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    }
}

void BoomBGeneratorAudioProcessorEditor::mouseDrag(const juce::MouseEvent& event)
{
    if (!splitterDragging)
        return;

    const auto virtualPos = toVirtualPoint(event.position);
    const int delta = static_cast<int>(virtualPos.x) - splitterDragStartX;
    uiLayoutState.leftPanelWidth = splitterStartWidth + delta;
    resized();
}

void BoomBGeneratorAudioProcessorEditor::mouseUp(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);
    if (!splitterDragging)
        return;

    splitterDragging = false;
    saveUiLayoutState();
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void BoomBGeneratorAudioProcessorEditor::exportFullPattern()
{
    auto chooser = std::make_shared<juce::FileChooser>(
        "Export full pattern MIDI",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getNonexistentChildFile("HPDG_Full", ".mid"),
        "*.mid");

    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                         [this, chooser](const juce::FileChooser& fc)
                         {
                             const auto result = fc.getResult();
                             if (result == juce::File())
                                 return;

                             audioProcessor.exportFullPatternToFile(result);
                         });
}

void BoomBGeneratorAudioProcessorEditor::exportLoopWav()
{
    auto chooser = std::make_shared<juce::FileChooser>(
        "Export loop WAV",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getNonexistentChildFile("HPDG_Loop", ".wav"),
        "*.wav");

    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                         [this, chooser](const juce::FileChooser& fc)
                         {
                             const auto result = fc.getResult();
                             if (result == juce::File())
                                 return;

                             const bool ok = audioProcessor.exportLoopWavToFile(result);
                             if (!ok)
                                 logDrag("exportLoopWav failed path=" + result.getFullPathName());
                         });
}

void BoomBGeneratorAudioProcessorEditor::showSampleMenu(TrackType type)
{
    juce::PopupMenu menu;
    menu.addItem(1, "Load new sample (.wav)");
    menu.addItem(2, "Delete selected sample");
    menu.addSeparator();
    menu.addItem(3, "Open lane sample folder");

    const auto mousePos = juce::Desktop::getInstance().getMainMouseSource().getScreenPosition().roundToInt();
    const juce::Rectangle<int> targetArea(mousePos.x, mousePos.y, 1, 1);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(targetArea).withParentComponent(this),
                       [this, type](int choice)
                       {
                           if (choice == 1)
                           {
                               auto chooser = std::make_shared<juce::FileChooser>(
                                   "Select WAV sample",
                                   juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                                   "*.wav");

                               chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                                    [this, chooser, type](const juce::FileChooser& fc)
                                                    {
                                                        const auto result = fc.getResult();
                                                        if (result == juce::File())
                                                            return;

                                                        juce::String error;
                                                        const bool ok = audioProcessor.importLaneSample(type, result, &error);
                                                        if (!ok && error.isNotEmpty())
                                                            logDrag("importLaneSample error: " + error);
                                                        refreshFromProcessor();
                                                    });
                               return;
                           }

                           if (choice == 2)
                           {
                               juce::String error;
                               const bool ok = audioProcessor.deleteSelectedLaneSample(type, &error);
                               if (!ok && error.isNotEmpty())
                                   logDrag("deleteSelectedLaneSample error: " + error);
                               refreshFromProcessor();
                               return;
                           }

                           if (choice == 3)
                           {
                               const auto folder = audioProcessor.getLaneSampleDirectory(type);
                               folder.createDirectory();
                               folder.revealToUser();
                           }
                       });
}

void BoomBGeneratorAudioProcessorEditor::dragFullPatternTempMidi()
{
    logDrag("dragFullPatternTempMidi click");
    const auto file = audioProcessor.createTemporaryFullPatternMidiFile();
    if (!file.existsAsFile())
    {
        logDrag("dragFullPatternTempMidi no file");
        return;
    }

    // Click path stays safe across hosts: expose file in shell.
    logDrag("dragFullPatternTempMidi reveal " + file.getFullPathName());
    file.revealToUser();
}

void BoomBGeneratorAudioProcessorEditor::dragFullPatternExternal()
{
    logDrag("dragFullPatternExternal gesture start");
    const auto file = audioProcessor.createTemporaryFullPatternMidiFile();
    if (!file.existsAsFile())
    {
        logDrag("dragFullPatternExternal no file");
        return;
    }

    try
    {
        const bool started = juce::DragAndDropContainer::performExternalDragDropOfFiles({ file.getFullPathName() },
                                                                                         false,
                                                                                         this,
                                                                                         []
                                                                                         {
                                                                                             logDrag("dragFullPatternExternal callback end");
                                                                                         });
        logDrag("dragFullPatternExternal performExternalDragDropOfFiles started=" + juce::String(started ? "1" : "0"));
        if (!started)
            file.revealToUser();
    }
    catch (...)
    {
        logDrag("dragFullPatternExternal exception fallback reveal");
        file.revealToUser();
    }
}

void BoomBGeneratorAudioProcessorEditor::exportTrack(TrackType type)
{
    auto chooser = std::make_shared<juce::FileChooser>(
        "Export track MIDI",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getNonexistentChildFile("HPDG_Track", ".mid"),
        "*.mid");

    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                         [this, chooser, type](const juce::FileChooser& fc)
                         {
                             const auto result = fc.getResult();
                             if (result == juce::File())
                                 return;

                             audioProcessor.exportTrackToFile(type, result);
                         });
}

void BoomBGeneratorAudioProcessorEditor::dragTrackTempMidi(TrackType type)
{
    logDrag("dragTrackTempMidi click type=" + juce::String(static_cast<int>(type)));
    const auto file = audioProcessor.createTemporaryTrackMidiFile(type);
    if (!file.existsAsFile())
    {
        logDrag("dragTrackTempMidi no file type=" + juce::String(static_cast<int>(type)));
        return;
    }

    // Click path stays safe across hosts: expose file in shell.
    logDrag("dragTrackTempMidi reveal " + file.getFullPathName());
    file.revealToUser();
}

void BoomBGeneratorAudioProcessorEditor::dragTrackExternal(TrackType type)
{
    logDrag("dragTrackExternal gesture start type=" + juce::String(static_cast<int>(type)));
    const auto file = audioProcessor.createTemporaryTrackMidiFile(type);
    if (!file.existsAsFile())
    {
        logDrag("dragTrackExternal no file type=" + juce::String(static_cast<int>(type)));
        return;
    }

    try
    {
        const bool started = juce::DragAndDropContainer::performExternalDragDropOfFiles({ file.getFullPathName() },
                                                                                         false,
                                                                                         this,
                                                                                         []
                                                                                         {
                                                                                             logDrag("dragTrackExternal callback end");
                                                                                         });
        logDrag("dragTrackExternal performExternalDragDropOfFiles started=" + juce::String(started ? "1" : "0")
                + " type=" + juce::String(static_cast<int>(type)));
        if (!started)
            file.revealToUser();
    }
    catch (...)
    {
        logDrag("dragTrackExternal exception fallback reveal type=" + juce::String(static_cast<int>(type)));
        file.revealToUser();
    }
}

void BoomBGeneratorAudioProcessorEditor::updateGridGeometry()
{
    if (isAlternateEditorVisible())
    {
        const auto project = audioProcessor.getProjectSnapshot();
        const int width = juce::jmax(gridViewport.getWidth(),
                                     64 + static_cast<int>(std::round(static_cast<float>(project.params.bars * 16) * (20.0f * horizontalZoom))));
        const int height = juce::jmax(gridViewport.getHeight(), 30 + (61 * 14) + 92);
        if (auto* component = activeAlternateEditorComponent())
            component->setSize(width, height);
        return;
    }

    const int width = juce::jmax(1, grid.getGridWidth());
    const int targetTrackHeight = (gridEditorFullscreenMode || pianoRollFullscreenMode)
        ? trackList.getLaneSectionHeight()
        : trackList.getContentHeight();
    const int height = juce::jmax(gridViewport.getHeight(), targetTrackHeight);
    grid.setSize(width, height);
}

void BoomBGeneratorAudioProcessorEditor::adjustGridHorizontalZoom(float delta, int anchorViewportX)
{
    if (gridViewport.getWidth() <= 0)
        return;

    const float oldStepWidth = grid.getStepWidth();
    const int anchorX = juce::jlimit(0, juce::jmax(0, gridViewport.getWidth() - 1), anchorViewportX);
    const float anchorStep = (static_cast<float>(gridViewport.getViewPositionX()) + static_cast<float>(anchorX))
        / juce::jmax(1.0f, oldStepWidth);

    horizontalZoom = juce::jlimit(0.25f, 8.0f, horizontalZoom + delta * 0.12f);
    header.zoomSlider.setValue(horizontalZoom, juce::dontSendNotification);
    const float newStepWidth = 20.0f * horizontalZoom;
    grid.setStepWidth(newStepWidth);
    sub808PianoRoll.setStepWidth(newStepWidth);
    updateGridGeometry();

    const int maxViewX = juce::jmax(0, grid.getWidth() - gridViewport.getWidth());
    const int newViewX = juce::jlimit(0,
                                      maxViewX,
                                      static_cast<int>(std::round(anchorStep * newStepWidth - static_cast<float>(anchorX))));
    gridViewport.setViewPosition(newViewX, gridViewport.getViewPositionY());
}

void BoomBGeneratorAudioProcessorEditor::adjustGridVerticalZoom(float delta, int anchorViewportY)
{
    if (gridViewport.getHeight() <= 0)
        return;

    const int oldLaneHeight = grid.getLaneHeightPx();
    const int rulerHeight = grid.getRulerHeightPx();
    const int anchorY = juce::jlimit(0, juce::jmax(0, gridViewport.getHeight() - 1), anchorViewportY);
    const float anchorLane = anchorY <= rulerHeight
        ? -1.0f
        : (static_cast<float>(gridViewport.getViewPositionY() + anchorY - rulerHeight)
           / static_cast<float>(juce::jmax(1, oldLaneHeight)));

    laneHeight = juce::jlimit(22, 96, laneHeight + static_cast<int>(delta * 8.0f));
    header.laneHeightSlider.setValue(static_cast<double>(laneHeight), juce::dontSendNotification);
    trackList.setRowHeight(laneHeight);
    grid.setLaneHeight(laneHeight);
    updateGridGeometry();

    int newViewY = gridViewport.getViewPositionY();
    if (anchorLane >= 0.0f)
    {
        newViewY = static_cast<int>(std::round(static_cast<float>(rulerHeight)
                                               + anchorLane * static_cast<float>(laneHeight)
                                               - static_cast<float>(anchorY)));
    }

    const int maxViewY = juce::jmax(0, grid.getHeight() - gridViewport.getHeight());
    gridViewport.setViewPosition(gridViewport.getViewPositionX(), juce::jlimit(0, maxViewY, newViewY));
}

bool BoomBGeneratorAudioProcessorEditor::zoomGridToSelection()
{
    if (!editorRegionState.selectedTickRange.has_value() || editorRegionState.activeLaneIds.empty())
        return false;

    const int viewportWidth = juce::jmax(1, gridViewport.getWidth());
    const int viewportHeight = juce::jmax(1, gridViewport.getHeight());
    const auto tickRange = *editorRegionState.selectedTickRange;
    const int ticksPerQuarterStep = ticksPerStep();
    const float selectionSteps = static_cast<float>(juce::jmax(ticksPerQuarterStep, tickRange.getLength()))
        / static_cast<float>(ticksPerQuarterStep);
    const float paddedSteps = juce::jmax(4.0f, selectionSteps + juce::jmax(2.0f, selectionSteps * 0.35f));

    horizontalZoom = juce::jlimit(0.25f, 8.0f, (static_cast<float>(viewportWidth) / paddedSteps) / 20.0f);
    header.zoomSlider.setValue(horizontalZoom, juce::dontSendNotification);
    grid.setStepWidth(20.0f * horizontalZoom);
    sub808PianoRoll.setStepWidth(20.0f * horizontalZoom);

    int minLaneIndex = std::numeric_limits<int>::max();
    int maxLaneIndex = std::numeric_limits<int>::min();
    for (const auto& laneId : editorRegionState.activeLaneIds)
    {
        const auto it = std::find(laneDisplayOrder.begin(), laneDisplayOrder.end(), laneId);
        if (it == laneDisplayOrder.end())
            continue;

        const int laneIndex = static_cast<int>(std::distance(laneDisplayOrder.begin(), it));
        minLaneIndex = juce::jmin(minLaneIndex, laneIndex);
        maxLaneIndex = juce::jmax(maxLaneIndex, laneIndex);
    }

    if (minLaneIndex == std::numeric_limits<int>::max())
        return false;

    const int laneSpan = juce::jmax(1, maxLaneIndex - minLaneIndex + 1);
    laneHeight = juce::jlimit(22,
                              96,
                              static_cast<int>(std::floor(static_cast<float>(viewportHeight - grid.getRulerHeightPx() - 12)
                                                          / static_cast<float>(laneSpan))));
    header.laneHeightSlider.setValue(static_cast<double>(laneHeight), juce::dontSendNotification);
    trackList.setRowHeight(laneHeight);
    grid.setLaneHeight(laneHeight);
    updateGridGeometry();

    const float startStep = static_cast<float>(tickRange.getStart()) / static_cast<float>(ticksPerQuarterStep);
    const float marginSteps = juce::jmax(1.0f, selectionSteps * 0.175f);
    const int targetX = static_cast<int>(std::floor((startStep - marginSteps) * grid.getStepWidth()));
    const int targetY = grid.getRulerHeightPx() + minLaneIndex * grid.getLaneHeightPx() - 6;

    const int maxViewX = juce::jmax(0, grid.getWidth() - gridViewport.getWidth());
    const int maxViewY = juce::jmax(0, grid.getHeight() - gridViewport.getHeight());
    gridViewport.setViewPosition(juce::jlimit(0, maxViewX, targetX), juce::jlimit(0, maxViewY, targetY));
    return true;
}

void BoomBGeneratorAudioProcessorEditor::zoomGridToPattern()
{
    const auto project = audioProcessor.getProjectSnapshot();
    const int viewportWidth = juce::jmax(1, gridViewport.getWidth());
    const int viewportHeight = juce::jmax(1, gridViewport.getHeight());
    const int totalSteps = juce::jmax(16, project.params.bars * 16);
    const int visibleRows = juce::jmax(1, trackList.getVisibleRowCount());

    horizontalZoom = juce::jlimit(0.25f, 8.0f, (static_cast<float>(viewportWidth) / static_cast<float>(totalSteps)) / 20.0f);
    laneHeight = juce::jlimit(22,
                              96,
                              static_cast<int>(std::floor(static_cast<float>(viewportHeight - grid.getRulerHeightPx() - 12)
                                                          / static_cast<float>(visibleRows))));

    header.zoomSlider.setValue(horizontalZoom, juce::dontSendNotification);
    header.laneHeightSlider.setValue(static_cast<double>(laneHeight), juce::dontSendNotification);
    trackList.setRowHeight(laneHeight);
    grid.setLaneHeight(laneHeight);
    grid.setStepWidth(20.0f * horizontalZoom);
    sub808PianoRoll.setStepWidth(20.0f * horizontalZoom);
    updateGridGeometry();
    gridViewport.setViewPosition(0, 0);
}

void BoomBGeneratorAudioProcessorEditor::syncEditorToolButtons(GridEditorComponent::EditorTool tool)
{
    pencilToolButton.setToggleState(tool == GridEditorComponent::EditorTool::Pencil, juce::dontSendNotification);
    brushToolButton.setToggleState(tool == GridEditorComponent::EditorTool::Brush, juce::dontSendNotification);
    selectToolButton.setToggleState(tool == GridEditorComponent::EditorTool::Select, juce::dontSendNotification);
    cutToolButton.setToggleState(tool == GridEditorComponent::EditorTool::Cut, juce::dontSendNotification);
    eraseToolButton.setToggleState(tool == GridEditorComponent::EditorTool::Erase, juce::dontSendNotification);
}

bool BoomBGeneratorAudioProcessorEditor::isAlternateEditorVisible() const
{
    return alternateEditorSession.isVisible && alternateEditorSession.editorType != AlternateLaneEditor::None;
}

bool BoomBGeneratorAudioProcessorEditor::isPianoRollEditorVisible() const
{
    return alternateEditorSession.isVisible && alternateEditorSession.editorType == AlternateLaneEditor::PianoRoll;
}

juce::Component* BoomBGeneratorAudioProcessorEditor::activeAlternateEditorComponent()
{
    if (!isAlternateEditorVisible())
        return nullptr;

    switch (alternateEditorSession.editorType)
    {
        case AlternateLaneEditor::PianoRoll:
            return &sub808PianoRoll;
        case AlternateLaneEditor::None:
        default:
            return nullptr;
    }
}

const juce::Component* BoomBGeneratorAudioProcessorEditor::activeAlternateEditorComponent() const
{
    return const_cast<BoomBGeneratorAudioProcessorEditor*>(this)->activeAlternateEditorComponent();
}

void BoomBGeneratorAudioProcessorEditor::setAlternateEditorVisible(bool shouldShow,
                                                                   AlternateLaneEditor editorType,
                                                                   std::optional<TrackType> trackType)
{
    const auto nextType = shouldShow ? editorType : AlternateLaneEditor::None;
    const auto nextTrack = shouldShow ? trackType : std::nullopt;
    if (alternateEditorSession.isVisible == shouldShow
        && alternateEditorSession.editorType == nextType
        && alternateEditorSession.trackType == nextTrack)
    {
        return;
    }

    alternateEditorSession.isVisible = shouldShow;
    alternateEditorSession.editorType = nextType;
    alternateEditorSession.trackType = nextTrack;
    gridEditorFullscreenMode = false;
    gridEditorFullscreenButton.setToggleState(false, juce::dontSendNotification);
    pianoRollFullscreenMode = false;
    pianoRollFullscreenButton.setToggleState(false, juce::dontSendNotification);

    if (auto* component = activeAlternateEditorComponent())
    {
        gridViewport.setViewedComponent(component, false);
        component->setWantsKeyboardFocus(true);
        component->grabKeyboardFocus();
    }
    else
    {
        gridViewport.setViewedComponent(&grid, false);
    }

    editorToolBar.setVisible(true);
    optionsToolButton.setVisible(true);
    hotkeysToolButton.setVisible(true);
    pianoRollFullscreenButton.setVisible(isPianoRollEditorVisible());
    resized();
}

void BoomBGeneratorAudioProcessorEditor::setupHotkeys()
{
    hotkeyBindings = {
        { kActionDrawMode, "Draw Mode", {}, true, defaultModifierForAction(kActionDrawMode), {}, defaultModifierForAction(kActionDrawMode) },
        { kActionVelocityEdit, "Velocity Edit", defaultKeyForAction(kActionVelocityEdit), false, juce::ModifierKeys::noModifiers, defaultKeyForAction(kActionVelocityEdit), juce::ModifierKeys::noModifiers },
        { kActionPanView, "Pan View", {}, true, defaultModifierForAction(kActionPanView), {}, defaultModifierForAction(kActionPanView) },
        { kActionHorizontalZoomModifier, "Horizontal Zoom modifier", {}, true, defaultModifierForAction(kActionHorizontalZoomModifier), {}, defaultModifierForAction(kActionHorizontalZoomModifier) },
        { kActionLaneHeightZoomModifier, "Lane Height Zoom modifier", {}, true, defaultModifierForAction(kActionLaneHeightZoomModifier), {}, defaultModifierForAction(kActionLaneHeightZoomModifier) },
        { kActionPencilTool, "Pencil Tool", defaultKeyForAction(kActionPencilTool), false, juce::ModifierKeys::noModifiers, defaultKeyForAction(kActionPencilTool), juce::ModifierKeys::noModifiers },
        { kActionBrushTool, "Brush Tool", defaultKeyForAction(kActionBrushTool), false, juce::ModifierKeys::noModifiers, defaultKeyForAction(kActionBrushTool), juce::ModifierKeys::noModifiers },
        { kActionSelectTool, "Select Tool", defaultKeyForAction(kActionSelectTool), false, juce::ModifierKeys::noModifiers, defaultKeyForAction(kActionSelectTool), juce::ModifierKeys::noModifiers },
        { kActionCutTool, "Cut Tool", defaultKeyForAction(kActionCutTool), false, juce::ModifierKeys::noModifiers, defaultKeyForAction(kActionCutTool), juce::ModifierKeys::noModifiers },
        { kActionEraseTool, "Erase Tool", defaultKeyForAction(kActionEraseTool), false, juce::ModifierKeys::noModifiers, defaultKeyForAction(kActionEraseTool), juce::ModifierKeys::noModifiers },
        { kActionMicrotimingEdit, "Microtiming Edit modifier", {}, true, defaultModifierForAction(kActionMicrotimingEdit), {}, defaultModifierForAction(kActionMicrotimingEdit) },
        { kActionDeleteNote, "Delete Note", defaultKeyForAction(kActionDeleteNote), false, juce::ModifierKeys::noModifiers, defaultKeyForAction(kActionDeleteNote), juce::ModifierKeys::noModifiers },
        { kActionCopySelection, "Copy", defaultKeyForAction(kActionCopySelection), false, juce::ModifierKeys::noModifiers, defaultKeyForAction(kActionCopySelection), juce::ModifierKeys::noModifiers },
        { kActionCutSelection, "Cut selection", defaultKeyForAction(kActionCutSelection), false, juce::ModifierKeys::noModifiers, defaultKeyForAction(kActionCutSelection), juce::ModifierKeys::noModifiers },
        { kActionPasteSelection, "Paste", defaultKeyForAction(kActionPasteSelection), false, juce::ModifierKeys::noModifiers, defaultKeyForAction(kActionPasteSelection), juce::ModifierKeys::noModifiers },
        { kActionDuplicateSelection, "Duplicate", defaultKeyForAction(kActionDuplicateSelection), false, juce::ModifierKeys::noModifiers, defaultKeyForAction(kActionDuplicateSelection), juce::ModifierKeys::noModifiers },
        { kActionSelectAllNotes, "Select all notes", defaultKeyForAction(kActionSelectAllNotes), false, juce::ModifierKeys::noModifiers, defaultKeyForAction(kActionSelectAllNotes), juce::ModifierKeys::noModifiers },
        { kActionDeselectAll, "Deselect all", defaultKeyForAction(kActionDeselectAll), false, juce::ModifierKeys::noModifiers, defaultKeyForAction(kActionDeselectAll), juce::ModifierKeys::noModifiers },
        { kActionUndo, "Undo", defaultKeyForAction(kActionUndo), false, juce::ModifierKeys::noModifiers, defaultKeyForAction(kActionUndo), juce::ModifierKeys::noModifiers },
        { kActionRedo, "Redo", defaultKeyForAction(kActionRedo), false, juce::ModifierKeys::noModifiers, defaultKeyForAction(kActionRedo), juce::ModifierKeys::noModifiers },
        { kActionRoleAnchor, "Semantic role: Anchor", defaultKeyForAction(kActionRoleAnchor), false, juce::ModifierKeys::noModifiers, defaultKeyForAction(kActionRoleAnchor), juce::ModifierKeys::noModifiers },
        { kActionRoleSupport, "Semantic role: Support", defaultKeyForAction(kActionRoleSupport), false, juce::ModifierKeys::noModifiers, defaultKeyForAction(kActionRoleSupport), juce::ModifierKeys::noModifiers },
        { kActionRoleFill, "Semantic role: Fill", defaultKeyForAction(kActionRoleFill), false, juce::ModifierKeys::noModifiers, defaultKeyForAction(kActionRoleFill), juce::ModifierKeys::noModifiers },
        { kActionRoleAccent, "Semantic role: Accent", defaultKeyForAction(kActionRoleAccent), false, juce::ModifierKeys::noModifiers, defaultKeyForAction(kActionRoleAccent), juce::ModifierKeys::noModifiers },
        { kActionRoleClear, "Semantic role: Clear", defaultKeyForAction(kActionRoleClear), false, juce::ModifierKeys::noModifiers, defaultKeyForAction(kActionRoleClear), juce::ModifierKeys::noModifiers },
        { kActionZoomToSelection, "Zoom to selection", defaultKeyForAction(kActionZoomToSelection), false, juce::ModifierKeys::noModifiers, defaultKeyForAction(kActionZoomToSelection), juce::ModifierKeys::noModifiers },
        { kActionZoomToPattern, "Zoom to pattern", defaultKeyForAction(kActionZoomToPattern), false, juce::ModifierKeys::noModifiers, defaultKeyForAction(kActionZoomToPattern), juce::ModifierKeys::noModifiers }
    };

    loadHotkeys();
    applyHotkeysToGrid();
}

void BoomBGeneratorAudioProcessorEditor::applyHotkeysToGrid()
{
    GridEditorComponent::InputBindings bindings;

    for (const auto& binding : hotkeyBindings)
    {
        if (binding.actionId == kActionDrawMode && binding.usesMouseModifier)
            bindings.drawModeModifier = binding.mouseModifier;
        else if (binding.actionId == kActionPanView && binding.usesMouseModifier)
            bindings.panViewModifier = binding.mouseModifier;
        else if (binding.actionId == kActionHorizontalZoomModifier && binding.usesMouseModifier)
            bindings.horizontalZoomModifier = binding.mouseModifier;
        else if (binding.actionId == kActionLaneHeightZoomModifier && binding.usesMouseModifier)
            bindings.laneHeightZoomModifier = binding.mouseModifier;
        else if (binding.actionId == kActionMicrotimingEdit && binding.usesMouseModifier)
            bindings.microtimingEditModifier = binding.mouseModifier;
        else if (binding.actionId == kActionVelocityEdit && !binding.usesMouseModifier && binding.keyPress.isValid())
            bindings.velocityEditKeyCode = binding.keyPress.getKeyCode();
    }

    grid.setInputBindings(bindings);
    sub808PianoRoll.setInputBindings(bindings);
}

void BoomBGeneratorAudioProcessorEditor::loadHotkeys()
{
    auto& state = audioProcessor.getApvts().state;
    for (auto& binding : hotkeyBindings)
    {
        if (binding.usesMouseModifier)
        {
            const auto path = hotkeyPropertyPath(binding.actionId, "modifier");
            if (state.hasProperty(path))
                binding.mouseModifier = static_cast<juce::ModifierKeys::Flags>(static_cast<int>(state.getProperty(path)));
            else
                binding.mouseModifier = binding.defaultMouseModifier;
            continue;
        }

        const auto keyPath = hotkeyPropertyPath(binding.actionId, "keyCode");
        const auto modsPath = hotkeyPropertyPath(binding.actionId, "modifiers");
        if (state.hasProperty(keyPath))
        {
            const int keyCode = static_cast<int>(state.getProperty(keyPath));
            const int mods = state.hasProperty(modsPath) ? static_cast<int>(state.getProperty(modsPath)) : 0;
            binding.keyPress = keyCode > 0 ? juce::KeyPress(keyCode, juce::ModifierKeys(mods), 0) : juce::KeyPress();
        }
        else
        {
            binding.keyPress = binding.defaultKeyPress;
        }
    }
}

void BoomBGeneratorAudioProcessorEditor::saveHotkeys()
{
    auto& state = audioProcessor.getApvts().state;
    for (const auto& binding : hotkeyBindings)
    {
        if (binding.usesMouseModifier)
        {
            state.setProperty(hotkeyPropertyPath(binding.actionId, "modifier"), static_cast<int>(binding.mouseModifier), nullptr);
            continue;
        }

        state.setProperty(hotkeyPropertyPath(binding.actionId, "keyCode"), binding.keyPress.getKeyCode(), nullptr);
        state.setProperty(hotkeyPropertyPath(binding.actionId, "modifiers"), binding.keyPress.getModifiers().getRawFlags(), nullptr);
    }
}

void BoomBGeneratorAudioProcessorEditor::restoreDefaultHotkeys()
{
    for (auto& binding : hotkeyBindings)
    {
        binding.keyPress = binding.defaultKeyPress;
        binding.mouseModifier = binding.defaultMouseModifier;
    }

    saveHotkeys();
    applyHotkeysToGrid();
}

void BoomBGeneratorAudioProcessorEditor::applyProjectSnapshotChange(const PatternProject& before,
                                                                    const PatternProject& after,
                                                                    bool refreshTrackRows)
{
    if (projectsEquivalent(before, after))
        return;

    audioProcessor.restoreEditorProjectSnapshot(after);
    pushProjectHistoryState(before, after);
    refreshFromProcessor(refreshTrackRows);
}

void BoomBGeneratorAudioProcessorEditor::showImportMidiToLaneDialog(const RuntimeLaneId& laneId)
{
    auto chooser = std::make_shared<juce::FileChooser>(
        "Import MIDI to lane",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.mid;*.midi");

    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this, chooser, laneId](const juce::FileChooser& fc)
                         {
                             const auto result = fc.getResult();
                             if (result == juce::File())
                                 return;

                             const auto before = audioProcessor.getProjectSnapshot();
                             const auto* lane = findRuntimeLaneById(before.runtimeLaneProfile, laneId);
                             if (lane == nullptr || !lane->runtimeTrackType.has_value())
                             {
                                 juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                                        "Lane Import",
                                                                        "This lane is not backed by an instrument track, so MIDI import is unavailable.",
                                                                        "OK",
                                                                        this);
                                 return;
                             }

                             const auto importResult = MidiImportService::importLaneFromFile(result,
                                                                                             *lane->runtimeTrackType,
                                                                                             before.params.bars);
                             if (!importResult.success)
                             {
                                 juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                                        "Lane Import",
                                                                        importResult.errorMessage.isNotEmpty() ? importResult.errorMessage : "Failed to import MIDI into this lane.",
                                                                        "OK",
                                                                        this);
                                 return;
                             }

                             auto after = before;
                             bool updatedLane = false;
                             for (auto& track : after.tracks)
                             {
                                 if (track.laneId == laneId)
                                 {
                                     track.notes = importResult.notes;
                                     updatedLane = true;
                                     break;
                                 }
                             }

                             if (!updatedLane)
                             {
                                 for (auto& track : after.tracks)
                                 {
                                     if (track.type == *lane->runtimeTrackType)
                                     {
                                         track.notes = importResult.notes;
                                         updatedLane = true;
                                         break;
                                     }
                                 }
                             }

                             if (!updatedLane)
                             {
                                 juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                                        "Lane Import",
                                                                        "Lane track state could not be resolved for MIDI import.",
                                                                        "OK",
                                                                        this);
                                 return;
                             }

                             applyProjectSnapshotChange(before, after, true);

                             if (importResult.requiredBars > before.params.bars)
                             {
                                 juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
                                                                        "Lane Import",
                                                                        importResult.summary + "\n\nImported MIDI is longer than the current pattern. Increase Bars to at least "
                                                                            + juce::String(importResult.requiredBars) + " to view the full lane.",
                                                                        "OK",
                                                                        this);
                             }
                         });
}

void BoomBGeneratorAudioProcessorEditor::showRenameLaneDialog(const RuntimeLaneId& laneId)
{
    const auto before = audioProcessor.getProjectSnapshot();
    const auto* lane = findRuntimeLaneById(before.runtimeLaneProfile, laneId);
    if (lane == nullptr)
        return;

    auto component = std::make_unique<LaneNameDialogComponent>(lane->laneName);
    component->onSubmit = [this, laneId](const juce::String& submittedName)
    {
        const auto current = audioProcessor.getProjectSnapshot();
        auto after = current;
        const auto newLaneName = submittedName.trim();
        if (!RuntimeLaneLifecycle::renameLane(after, laneId, newLaneName))
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                   "Rename Lane",
                                                   newLaneName.isEmpty() ? "Lane name cannot be empty." : "Lane rename failed.",
                                                   "OK",
                                                   this);
            return;
        }

        applyProjectSnapshotChange(current, after, true);
    };

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Rename Lane";
    options.content.setOwned(component.release());
    options.componentToCentreAround = this;
    options.dialogBackgroundColour = juce::Colour::fromRGB(22, 25, 31);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;

    if (auto* window = options.launchAsync())
        window->setSize(380, 150);
}

void BoomBGeneratorAudioProcessorEditor::requestDeleteLane(const RuntimeLaneId& laneId)
{
    const auto before = audioProcessor.getProjectSnapshot();
    const auto* lane = findRuntimeLaneById(before.runtimeLaneProfile, laneId);
    if (lane == nullptr)
        return;

    const bool confirmed = juce::NativeMessageBox::showOkCancelBox(juce::MessageBoxIconType::WarningIcon,
                                                                   "Delete Lane",
                                                                   "Delete lane '" + lane->laneName + "'?",
                                                                   this,
                                                                   nullptr);
    if (!confirmed)
        return;

    auto after = before;
    if (!RuntimeLaneLifecycle::deleteLane(after, laneId))
        return;

    applyProjectSnapshotChange(before, after, true);
}

void BoomBGeneratorAudioProcessorEditor::showAddLaneMenu()
{
    const auto project = audioProcessor.getProjectSnapshot();
    const auto availableRegistryTypes = RuntimeLaneLifecycle::listAvailableRegistryLaneTypes(project);

    juce::PopupMenu menu;
    menu.addItem(1, "Add Custom Lane");

    std::vector<TrackType> menuTrackTypes;
    if (!availableRegistryTypes.empty())
    {
        menu.addSeparator();
        int itemId = 100;
        for (const auto type : availableRegistryTypes)
        {
            const auto* info = TrackRegistry::find(type);
            menu.addItem(itemId, info != nullptr ? "Add " + info->displayName : "Add Lane");
            menuTrackTypes.push_back(type);
            ++itemId;
        }
    }

    const auto mousePos = juce::Desktop::getInstance().getMainMouseSource().getScreenPosition().roundToInt();
    const juce::Rectangle<int> targetArea(mousePos.x, mousePos.y, 1, 1);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(targetArea).withParentComponent(this),
                       [this, menuTrackTypes](int choice)
                       {
                           if (choice <= 0)
                               return;

                           const auto before = audioProcessor.getProjectSnapshot();
                           auto after = before;
                           bool changed = false;

                           if (choice == 1)
                           {
                               auto lane = RuntimeLaneLifecycle::createCustomLaneDefinition(after);
                               changed = RuntimeLaneLifecycle::addLane(after, std::move(lane));
                           }
                           else if (choice >= 100)
                           {
                               const auto index = static_cast<size_t>(choice - 100);
                               if (index < menuTrackTypes.size())
                                   changed = RuntimeLaneLifecycle::addRegistryLane(after, menuTrackTypes[index]);
                           }

                           if (!changed)
                               return;

                           applyProjectSnapshotChange(before, after, true);
                       });
}

void BoomBGeneratorAudioProcessorEditor::pushProjectHistoryState(const PatternProject& before, const PatternProject& after)
{
    if (suppressHistory || projectsEquivalent(before, after))
        return;

    if (!pendingHistoryBefore.has_value())
        pendingHistoryBefore = before;
    pendingHistoryAfter = after;

    if (pendingHistoryCommitScheduled)
        return;

    pendingHistoryCommitScheduled = true;
    juce::MessageManager::callAsync([safeEditor = juce::Component::SafePointer<BoomBGeneratorAudioProcessorEditor>(this)]
    {
        if (safeEditor != nullptr)
            safeEditor->commitPendingProjectHistoryState();
    });
}

void BoomBGeneratorAudioProcessorEditor::commitPendingProjectHistoryState()
{
    pendingHistoryCommitScheduled = false;
    if (!pendingHistoryBefore.has_value() || !pendingHistoryAfter.has_value())
        return;

    const auto before = *pendingHistoryBefore;
    const auto after = *pendingHistoryAfter;
    pendingHistoryBefore.reset();
    pendingHistoryAfter.reset();

    if (suppressHistory || projectsEquivalent(before, after))
        return;

    undoHistory.push_back(before);
    redoHistory.clear();

    constexpr size_t kMaxHistoryEntries = 128;
    if (undoHistory.size() > kMaxHistoryEntries)
        undoHistory.erase(undoHistory.begin(), undoHistory.begin() + static_cast<std::ptrdiff_t>(undoHistory.size() - kMaxHistoryEntries));
}

bool BoomBGeneratorAudioProcessorEditor::performUndo()
{
    commitPendingProjectHistoryState();
    if (undoHistory.empty())
        return false;

    const auto current = audioProcessor.getProjectSnapshot();
    auto snapshot = undoHistory.back();
    undoHistory.pop_back();
    redoHistory.push_back(current);

    suppressHistory = true;
    audioProcessor.restoreEditorProjectSnapshot(snapshot);
    suppressHistory = false;
    refreshFromProcessor(true);
    return true;
}

bool BoomBGeneratorAudioProcessorEditor::performRedo()
{
    commitPendingProjectHistoryState();
    if (redoHistory.empty())
        return false;

    const auto current = audioProcessor.getProjectSnapshot();
    auto snapshot = redoHistory.back();
    redoHistory.pop_back();
    undoHistory.push_back(current);

    suppressHistory = true;
    audioProcessor.restoreEditorProjectSnapshot(snapshot);
    suppressHistory = false;
    refreshFromProcessor(true);
    return true;
}

bool BoomBGeneratorAudioProcessorEditor::projectsEquivalent(const PatternProject& lhs, const PatternProject& rhs) const
{
    auto left = lhs;
    auto right = rhs;
    left.generationCounter = 0;
    right.generationCounter = 0;
    return PatternProjectSerialization::serialize(left).isEquivalentTo(PatternProjectSerialization::serialize(right));
}

juce::String BoomBGeneratorAudioProcessorEditor::hotkeyToDisplayText(const HotkeyBinding& binding) const
{
    if (binding.usesMouseModifier)
    {
        if (binding.mouseModifier == juce::ModifierKeys::shiftModifier)
            return "Shift + mouse";
        if (binding.mouseModifier == juce::ModifierKeys::ctrlModifier)
            return "Ctrl + mouse";
        if (binding.mouseModifier == juce::ModifierKeys::altModifier)
            return "Alt + mouse";
        if (binding.mouseModifier == juce::ModifierKeys::commandModifier)
            return "Cmd + mouse";
        return "(none)";
    }

    if (!binding.keyPress.isValid())
        return "(none)";

    return binding.keyPress.getTextDescription();
}

bool BoomBGeneratorAudioProcessorEditor::confirmReplaceConflict(const juce::String& actionName,
                                                                const juce::String& conflictedActionName,
                                                                const juce::String& shortcutText)
{
    return juce::NativeMessageBox::showOkCancelBox(juce::MessageBoxIconType::WarningIcon,
                                                    "Hotkey conflict",
                                                    "Shortcut '" + shortcutText + "' is already used by '" + conflictedActionName
                                                        + "'.\n\nReplace it with '" + actionName + "'?",
                                                    this,
                                                    nullptr);
}

bool BoomBGeneratorAudioProcessorEditor::tryRebindKeyAction(const juce::String& actionId, const juce::KeyPress& keyPress)
{
    auto targetIt = std::find_if(hotkeyBindings.begin(), hotkeyBindings.end(), [&actionId](const HotkeyBinding& binding)
    {
        return binding.actionId == actionId;
    });

    if (targetIt == hotkeyBindings.end() || targetIt->usesMouseModifier)
        return false;

    auto conflictIt = std::find_if(hotkeyBindings.begin(), hotkeyBindings.end(), [&actionId, &keyPress](const HotkeyBinding& binding)
    {
        return binding.actionId != actionId
            && !binding.usesMouseModifier
            && binding.keyPress.isValid()
            && binding.keyPress.getKeyCode() == keyPress.getKeyCode()
            && binding.keyPress.getModifiers().getRawFlags() == keyPress.getModifiers().getRawFlags();
    });

    if (conflictIt != hotkeyBindings.end())
    {
        if (!confirmReplaceConflict(targetIt->displayName, conflictIt->displayName, keyPress.getTextDescription()))
            return false;

        conflictIt->keyPress = conflictIt->defaultKeyPress;
    }

    targetIt->keyPress = keyPress;
    saveHotkeys();
    applyHotkeysToGrid();
    return true;
}

bool BoomBGeneratorAudioProcessorEditor::tryRebindMouseModifierAction(const juce::String& actionId, juce::ModifierKeys::Flags modifier)
{
    auto targetIt = std::find_if(hotkeyBindings.begin(), hotkeyBindings.end(), [&actionId](const HotkeyBinding& binding)
    {
        return binding.actionId == actionId;
    });

    if (targetIt == hotkeyBindings.end() || !targetIt->usesMouseModifier)
        return false;

    auto conflictIt = std::find_if(hotkeyBindings.begin(), hotkeyBindings.end(), [&actionId, modifier](const HotkeyBinding& binding)
    {
        return binding.actionId != actionId
            && binding.usesMouseModifier
            && binding.mouseModifier == modifier;
    });

    if (conflictIt != hotkeyBindings.end())
    {
        const auto shortcut = hotkeyToDisplayText(*targetIt).replace("(none)", "mouse modifier");
        if (!confirmReplaceConflict(targetIt->displayName, conflictIt->displayName, shortcut))
            return false;

        conflictIt->mouseModifier = conflictIt->defaultMouseModifier;
    }

    targetIt->mouseModifier = modifier;
    saveHotkeys();
    applyHotkeysToGrid();
    return true;
}

bool BoomBGeneratorAudioProcessorEditor::resetHotkeyActionToDefault(const juce::String& actionId)
{
    auto it = std::find_if(hotkeyBindings.begin(), hotkeyBindings.end(), [&actionId](const HotkeyBinding& binding)
    {
        return binding.actionId == actionId;
    });

    if (it == hotkeyBindings.end())
        return false;

    if (it->usesMouseModifier)
        it->mouseModifier = it->defaultMouseModifier;
    else
        it->keyPress = it->defaultKeyPress;

    saveHotkeys();
    applyHotkeysToGrid();
    return true;
}

void BoomBGeneratorAudioProcessorEditor::showEditorOptionsMenu()
{
    juce::PopupMenu menu;
    
    // Tool selection
    menu.addItem(10, "Pencil");
    menu.addItem(11, "Brush");
    menu.addItem(12, "Select");
    menu.addItem(13, "Cut");
    menu.addItem(14, "Erase");
    menu.addSeparator();
    menu.addItem(3, "Open References...");
    menu.addItem(1, "Reset Hotkeys to default");
    menu.addItem(2, "Show Hotkeys");

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&optionsToolButton).withParentComponent(this),
                       [this](int choice)
                       {
                           if (choice >= 10 && choice <= 14)
                           {
                               grid.setEditorTool(static_cast<GridEditorComponent::EditorTool>(choice - 10));
                               syncEditorToolButtons(static_cast<GridEditorComponent::EditorTool>(choice - 10));
                               return;
                           }

                           if (choice == 1)
                           {
                               restoreDefaultHotkeys();
                               return;
                           }

                           if (choice == 3)
                           {
                               showStyleLabReferenceBrowserWindow();
                               return;
                           }

                           if (choice == 2)
                               showHotkeysMenu();
                       });
}

StyleLabState BoomBGeneratorAudioProcessorEditor::buildDefaultStyleLabState() const
{
    const auto project = audioProcessor.getProjectSnapshot();
    const auto genre = header.genreCombo.getText();
    const auto substyle = header.substyleCombo.getText();
    const auto bars = juce::jmax(1, header.barsCombo.getText().getIntValue());
    const auto bpm = static_cast<int>(std::lround(header.bpmSlider.getValue()));
    return StyleLabReferenceService::createDefaultState(project, genre, substyle, bars, bpm);
}

void BoomBGeneratorAudioProcessorEditor::showStyleLabReferenceBrowserWindow()
{
    auto component = std::make_unique<StyleLabReferenceBrowserComponent>();
    component->onApplyReference = [this](const StyleLabReferenceRecord& record, juce::String& statusMessage)
    {
        const auto before = audioProcessor.getProjectSnapshot();
        auto after = before;

        if (!StyleLabReferenceBrowserService::applyReferenceToProject(record, after, &statusMessage))
            return false;

        applyProjectSnapshotChange(before, after, true);
        styleLabState = buildDefaultStyleLabState();

        if (statusMessage.isEmpty())
            statusMessage = "Reference applied to current editor project.";

        return true;
    };

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Style Lab Reference Browser";
    options.content.setOwned(component.release());
    options.componentToCentreAround = this;
    options.dialogBackgroundColour = juce::Colour::fromRGB(16, 18, 23);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;

    if (auto* window = options.launchAsync())
    {
        window->setResizeLimits(900, 560, 1660, 1080);
        window->setSize(1280, 760);
    }
}

void BoomBGeneratorAudioProcessorEditor::showStyleLabWindow()
{
    if (!styleLabState.has_value())
        styleLabState = buildDefaultStyleLabState();

    auto component = std::make_unique<StyleLabComponent>(*styleLabState);
    component->onStateChanged = [this](const StyleLabState& state)
    {
        styleLabState = state;
    };
    component->onSaveReferencePattern = [this, safeEditor = juce::Component::SafePointer<BoomBGeneratorAudioProcessorEditor>(this)](const StyleLabState& state)
    {
        styleLabState = state;

        const auto result = StyleLabReferenceService::saveReferencePattern(audioProcessor.getProjectSnapshot(), state);
        if (safeEditor == nullptr)
            return;

        if (!result.success)
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                   "Style Lab",
                                                   result.errorMessage.isNotEmpty() ? result.errorMessage : "Failed to save Style Lab reference pattern.",
                                                   "OK",
                                                   safeEditor.getComponent());
            return;
        }

        juce::String message = "Reference saved to:\n" + result.directory.getFullPathName();
        if (result.conflictMessage.isNotEmpty())
            message << "\n\n" << result.conflictMessage;

        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
                                               "Style Lab",
                                               message,
                                               "OK",
                                               safeEditor.getComponent());
    };

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Style Lab [dev]";
    options.content.setOwned(component.release());
    options.componentToCentreAround = this;
    options.dialogBackgroundColour = juce::Colour::fromRGB(16, 18, 23);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;

    if (auto* window = options.launchAsync())
    {
        window->setResizeLimits(860, 520, 1540, 1080);
        window->setSize(1180, 760);
    }
}

void BoomBGeneratorAudioProcessorEditor::showHotkeysMenu()
{
    auto manager = std::make_unique<HotkeysManagerComponent>();
    manager->requestRows = [this]()
    {
        return hotkeyBindings;
    };
    manager->formatShortcut = [this](const HotkeyBinding& binding)
    {
        return hotkeyToDisplayText(binding);
    };

    // Use SafePointer so the onDone callback can safely refresh the table
    // even if the user closes the manager window before key capture completes.
    juce::Component::SafePointer<HotkeysManagerComponent> safeManager(manager.get());

    manager->onRebind = [this, safeManager](const juce::String& actionId)
    {
        showHotkeyActionMenu(actionId, [safeManager]
        {
            if (safeManager != nullptr)
                safeManager->refresh();
        });
    };
    manager->onReset = [this](const juce::String& actionId)
    {
        resetHotkeyActionToDefault(actionId);
    };
    manager->onResetAll = [this]
    {
        restoreDefaultHotkeys();
    };
    manager->refresh();

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Hotkeys Manager";
    options.content.setOwned(manager.release());
    options.componentToCentreAround = this;
    options.dialogBackgroundColour = juce::Colour::fromRGB(22, 25, 31);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;

    if (auto* window = options.launchAsync())
    {
        window->setResizeLimits(520, 320, 960, 760);
        window->setSize(680, 430);
    }
}

void BoomBGeneratorAudioProcessorEditor::showHotkeyActionMenu(const juce::String& actionId, std::function<void()> onDone)
{
    auto it = std::find_if(hotkeyBindings.begin(), hotkeyBindings.end(), [&actionId](const HotkeyBinding& binding)
    {
        return binding.actionId == actionId;
    });

    if (it == hotkeyBindings.end())
        return;

    // ── Mouse-modifier actions: popup with Shift / Ctrl / Alt options ──────────
    if (it->usesMouseModifier)
    {
        juce::PopupMenu menu;
        menu.addItem(101, "Rebind: Shift + mouse");
        menu.addItem(102, "Rebind: Ctrl + mouse");
        menu.addItem(103, "Rebind: Alt + mouse");
        menu.addSeparator();
        menu.addItem(199, "Reset to default");

        menu.showMenuAsync(juce::PopupMenu::Options{}.withParentComponent(this),
                           [this, actionId, onDone](int choice)
                           {
                               if (choice <= 0)
                                   return;

                               if (choice == 199)
                               {
                                   resetHotkeyActionToDefault(actionId);
                                   if (onDone) onDone();
                                   return;
                               }

                               if (choice == 101)
                                   tryRebindMouseModifierAction(actionId, juce::ModifierKeys::shiftModifier);
                               else if (choice == 102)
                                   tryRebindMouseModifierAction(actionId, juce::ModifierKeys::ctrlModifier);
                               else if (choice == 103)
                                   tryRebindMouseModifierAction(actionId, juce::ModifierKeys::altModifier);

                               if (onDone) onDone();
                           });
        return;
    }

    // ── Key actions: open a key-capture dialog ─────────────────────────────────
    auto capture = std::make_unique<KeyCaptureComponent>(it->displayName);
    capture->onKeyCaptured = [this, actionId, onDone](const juce::KeyPress& key)
    {
        tryRebindKeyAction(actionId, key);
        if (onDone) onDone();
    };

    juce::DialogWindow::LaunchOptions opts;
    opts.dialogTitle     = "Rebind: " + it->displayName;
    opts.content.setOwned(capture.release());
    opts.componentToCentreAround     = this;
    opts.dialogBackgroundColour      = juce::Colour::fromRGB(22, 25, 31);
    opts.escapeKeyTriggersCloseButton = false;   // component handles Escape itself
    opts.useNativeTitleBar            = true;
    opts.resizable                    = false;

    if (auto* w = opts.launchAsync())
        w->setSize(370, 140);
}

LaneRackDisplayMode BoomBGeneratorAudioProcessorEditor::laneRackModeForWidth(int width) const
{
    if (width <= kRackCompactThreshold)
        return LaneRackDisplayMode::Minimal;
    if (width <= kRackFullThreshold)
        return LaneRackDisplayMode::Compact;
    return LaneRackDisplayMode::Full;
}

void BoomBGeneratorAudioProcessorEditor::loadUiLayoutState()
{
    auto& state = audioProcessor.getApvts().state;
    if (state.hasProperty(kUiLeftWidthProp))
        uiLayoutState.leftPanelWidth = static_cast<int>(state.getProperty(kUiLeftWidthProp));

    if (state.hasProperty(kUiHeaderModeProp))
    {
        const int stored = static_cast<int>(state.getProperty(kUiHeaderModeProp));
        if (stored == 1)
            uiLayoutState.headerControlsMode = MainHeaderComponent::HeaderControlsMode::Compact;
        else if (stored == 2)
            uiLayoutState.headerControlsMode = MainHeaderComponent::HeaderControlsMode::Hidden;
        else
            uiLayoutState.headerControlsMode = MainHeaderComponent::HeaderControlsMode::Expanded;
    }
}

void BoomBGeneratorAudioProcessorEditor::saveUiLayoutState()
{
    auto& state = audioProcessor.getApvts().state;
    state.setProperty(kUiLeftWidthProp, uiLayoutState.leftPanelWidth, nullptr);

    int modeValue = 0;
    if (uiLayoutState.headerControlsMode == MainHeaderComponent::HeaderControlsMode::Compact)
        modeValue = 1;
    else if (uiLayoutState.headerControlsMode == MainHeaderComponent::HeaderControlsMode::Hidden)
        modeValue = 2;

    state.setProperty(kUiHeaderModeProp, modeValue, nullptr);
}

juce::Point<float> BoomBGeneratorAudioProcessorEditor::toVirtualPoint(juce::Point<float> point) const
{
    if (currentUiScale <= 0.0f)
        return point;

    return { (point.x - currentUiOffsetX) / currentUiScale,
             (point.y - currentUiOffsetY) / currentUiScale };
}

void BoomBGeneratorAudioProcessorEditor::setActiveEditorTool(GridEditorComponent::EditorTool tool)
{
    grid.setEditorTool(tool);
    if (isPianoRollEditorVisible())
        sub808PianoRoll.setEditorTool(tool);
    syncEditorToolButtons(tool);
}

void BoomBGeneratorAudioProcessorEditor::toggleSub808PianoRollFullscreen()
{
    if (!isPianoRollEditorVisible())
        return;

    pianoRollFullscreenMode = !pianoRollFullscreenMode;
    pianoRollFullscreenButton.setToggleState(pianoRollFullscreenMode, juce::dontSendNotification);
    resized();
}

void BoomBGeneratorAudioProcessorEditor::toggleGridEditorFullscreen()
{
    if (isAlternateEditorVisible())
        return;

    gridEditorFullscreenMode = !gridEditorFullscreenMode;
    gridEditorFullscreenButton.setToggleState(gridEditorFullscreenMode, juce::dontSendNotification);
    resized();
}

void BoomBGeneratorAudioProcessorEditor::updateCursorForMousePosition(juce::Point<float> point)
{
    if (splitterDragging)
        return;

    const auto virtualPos = toVirtualPoint(point);
    const bool onSplitter = splitterBounds.contains(static_cast<int>(virtualPos.x), static_cast<int>(virtualPos.y));
    setMouseCursor(onSplitter ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

bool BoomBGeneratorAudioProcessorEditor::isStandaloneWindowMaximized() const
{
#if JUCE_STANDALONE_APPLICATION
    if (const auto* top = findParentComponentOfClass<juce::TopLevelWindow>())
    {
        if (const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(top->getBounds()))
            return top->getBounds().contains(display->userArea) && top->getBounds().getWidth() >= display->userArea.getWidth() - 2;
    }
#endif
    return false;
}

void BoomBGeneratorAudioProcessorEditor::toggleStandaloneWindowMaximize()
{
#if JUCE_STANDALONE_APPLICATION
    auto* top = findParentComponentOfClass<juce::TopLevelWindow>();
    if (top == nullptr)
        return;

    const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(top->getBounds());
    if (display == nullptr)
        return;

    const auto userArea = display->userArea;
    if (isStandaloneWindowMaximized())
    {
        if (!gStandaloneLastRestoreBounds.isEmpty())
            top->setBounds(gStandaloneLastRestoreBounds);
    }
    else
    {
        gStandaloneLastRestoreBounds = top->getBounds();
        top->setBounds(userArea);
    }

    header.setStandaloneWindowMaximized(isStandaloneWindowMaximized());
#endif
}
} // namespace bbg
