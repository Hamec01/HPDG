#include "PluginEditor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <numeric>

#include "../Core/PatternProjectSerialization.h"
#include "../Core/RuntimeLaneLifecycle.h"
#include "../Core/Sub808TrackAccess.h"
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
class DetachedSub808Window final : public juce::DocumentWindow
{
public:
    DetachedSub808Window()
        : juce::DocumentWindow("Sub808 Piano Roll",
                               juce::Colour::fromRGB(18, 22, 28),
                               juce::DocumentWindow::allButtons,
                               true)
    {
        setUsingNativeTitleBar(true);
        setResizable(true, true);
    }

    std::function<void()> onCloseRequested;

    void closeButtonPressed() override
    {
        if (onCloseRequested)
            onCloseRequested();
    }
};

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
constexpr auto kActionStretchResize = "stretch_resize";
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
    if (actionId == kActionStretchResize)
        return juce::KeyPress('R', juce::ModifierKeys::shiftModifier, 0);
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
        return juce::KeyPress('Z', juce::ModifierKeys::ctrlModifier, 0);
    if (actionId == kActionRedo)
        return juce::KeyPress('Z', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0);
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
    , commandController(processor)
{
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(true);
    addKeyListener(this);

    addAndMakeVisible(header);
    addAndMakeVisible(laneSurface);
    laneSurface.addAndMakeVisible(trackList);
    laneSurface.addAndMakeVisible(gridViewport);
    addAndMakeVisible(sub808Viewport);
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
        layoutController.beginSplitterDrag(static_cast<int>(event.getEventRelativeTo(&laneSurface).position.x));
    };
    splitter->onDragMove = [this](const juce::MouseEvent& event)
    {
        if (!layoutController.isSplitterDragging())
            return;

        const int currentX = static_cast<int>(event.getEventRelativeTo(&laneSurface).position.x);
        layoutController.updateSplitterDrag(currentX);
        resized();
    };
    splitter->onRelease = [this](const juce::MouseEvent&)
    {
        if (!layoutController.isSplitterDragging())
            return;

        layoutController.endSplitterDrag();
        saveUiLayoutState();
    };
    splitterHandle.reset(splitter);
    laneSurface.addAndMakeVisible(*splitterHandle);
    splitterHandle->toFront(false);

    grid.setWantsKeyboardFocus(true);
    grid.setMouseClickGrabsKeyboardFocus(true);
    sub808PianoRoll.setWantsKeyboardFocus(true);
    sub808PianoRoll.setMouseClickGrabsKeyboardFocus(true);

    gridViewport.setViewedComponent(&grid, false);
    gridViewport.setScrollBarsShown(true, false);
    gridViewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never);
    sub808Viewport.setViewedComponent(&sub808PianoRoll, false);
    sub808Viewport.setScrollBarsShown(true, true);
    sub808Viewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never);
    sub808Viewport.setVisible(false);

    sub808PianoRoll.onNotesEdited = [this](const std::vector<Sub808NoteEvent>& notes)
    {
        auto before = audioProcessor.getProjectSnapshot();
        auto after = before;
        auto dedicatedTrackType = alternateEditorSession.trackType;
        const auto dedicatedLaneId = alternateEditorSession.laneId;
        if (!dedicatedTrackType.has_value())
            dedicatedTrackType = dedicatedPianoRollTrackType(after);

        if (!dedicatedTrackType.has_value())
            return;

        for (auto& track : after.tracks)
        {
            if ((!dedicatedLaneId.isEmpty() && track.laneId == dedicatedLaneId)
                || (dedicatedLaneId.isEmpty() && track.type == *dedicatedTrackType))
            {
                track.sub808Notes = notes;
                track.notes = toLegacyNoteEvents(notes);
                break;
            }
        }

        if (!dedicatedLaneId.isEmpty())
            audioProcessor.setSub808TrackNotes(dedicatedLaneId, notes);
        else
            audioProcessor.setSub808TrackNotes(*dedicatedTrackType, notes);
        pushProjectHistoryState(before, after);
        refreshFromProcessor(true);
    };
    sub808PianoRoll.onLaneSettingsEdited = [this](const Sub808LaneSettings& settings)
    {
        auto before = audioProcessor.getProjectSnapshot();
        auto after = before;
        auto dedicatedTrackType = alternateEditorSession.trackType;
        const auto dedicatedLaneId = alternateEditorSession.laneId;
        if (!dedicatedTrackType.has_value())
            dedicatedTrackType = dedicatedPianoRollTrackType(after);

        if (!dedicatedTrackType.has_value())
            return;

        for (auto& track : after.tracks)
        {
            if ((!dedicatedLaneId.isEmpty() && track.laneId == dedicatedLaneId)
                || (dedicatedLaneId.isEmpty() && track.type == *dedicatedTrackType))
            {
                track.sub808Settings = settings;
                break;
            }
        }

        if (!dedicatedLaneId.isEmpty())
            audioProcessor.setSub808LaneSettings(dedicatedLaneId, settings);
        else
            audioProcessor.setSub808LaneSettings(*dedicatedTrackType, settings);
        pushProjectHistoryState(before, after);
        refreshFromProcessor(true);
    };
    sub808PianoRoll.onPreviewNote = [this](int pitch, int velocity, int lengthTicks)
    {
        audioProcessor.auditionSub808Note(pitch, velocity, lengthTicks);
    };
    sub808PianoRoll.onOpenHotkeys = [this] { showHotkeysMenu(); };

    setupAttachments();
    bindTrackCallbacks();
    hatFxDragDensityUi = audioProcessor.getHatFxDragDensity();
    hatFxDragLockedUi = audioProcessor.isHatFxDragDensityLocked();
    trackList.setShowAnalysisPanel(false);
    trackList.setHatFxDragUiState(hatFxDragDensityUi, hatFxDragLockedUi);
    loadUiLayoutState();
    header.setHeaderControlsMode(layoutController.getState().headerControlsMode);
    trackList.setDisplayMode(laneRackModeForWidth(layoutController.getState().leftPanelWidth));

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
    pianoRollFullscreenButton.setButtonText("Window");
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
        applyProcessorProjectMutation([this]
        {
            audioProcessor.generatePattern();
        });
    };

    header.onMutatePressed = [this]
    {
        applyProcessorProjectMutation([this]
        {
            audioProcessor.mutatePattern();
        });
    };

    header.onPlayToggled = [this](bool shouldStart)
    {
        setPreviewPlayback(shouldStart);
    };

    header.onStartPlayWithDawToggled = [this](bool enabled)
    {
        applyProcessorProjectMutation([this, enabled]
        {
            audioProcessor.setStartPlayWithDawEnabled(enabled);
        }, false);
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

    header.onClearAllPressed = [this]
    {
        clearAllPatternNotes();
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
        applyProcessorProjectMutation([this, selectedId]
        {
            audioProcessor.setPreviewPlaybackMode(previewPlaybackModeFromId(selectedId));
        }, false);
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
            applyProcessorProjectMutation([this]
            {
                audioProcessor.setHatFxDragDensity(hatFxDragDensityUi, hatFxDragLockedUi);
            });
        };

    header.onHeaderControlsModeChanged = [this](MainHeaderComponent::HeaderControlsMode mode)
    {
        layoutController.getState().headerControlsMode = mode;
        saveUiLayoutState();
        resized();
    };

    grid.onTrackFocused = [this](const RuntimeLaneId& laneId)
    {
        const auto project = audioProcessor.getProjectSnapshot();
        audioProcessor.setSelectedTrack(laneId);
        grid.setSelectedTrack(laneId);
        syncAlternateEditorForLane(project, laneId);
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

BoomBGeneratorAudioProcessorEditor::~BoomBGeneratorAudioProcessorEditor()
{
    setSub808DetachedWindowVisible(false);
}

bool BoomBGeneratorAudioProcessorEditor::keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent)
{
    if (key == juce::KeyPress('L', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::altModifier | juce::ModifierKeys::shiftModifier, 0))
    {
        showStyleLabWindow();
        return true;
    }

    // Handle ESC to exit fullscreen modes
    if (key == juce::KeyPress::escapeKey)
    {
        if (layoutController.isGridEditorFullscreenMode())
        {
            toggleGridEditorFullscreen();
            return true;
        }
        if (layoutController.isPianoRollFullscreenMode() || (sub808DetachedWindow != nullptr && sub808DetachedWindow->isVisible()))
        {
            toggleSub808PianoRollFullscreen();
            return true;
        }
    }

    auto isKeyForAction = [this, &key](const juce::String& actionId)
    {
        return hotkeyController.matchesKeyAction(actionId, key);
    };

    if (isKeyForAction(HotkeyController::kActionUndo))
        return performUndo();

    const bool redoHotkeyPressed = isKeyForAction(HotkeyController::kActionRedo)
        || key == juce::KeyPress('Y', juce::ModifierKeys::ctrlModifier, 0);

    if (redoHotkeyPressed)
        return performRedo();

    auto focusIsWithin = [](juce::Component& component, juce::Component* focused, juce::Component* origin)
    {
        return focused == &component
            || (focused != nullptr && component.isParentOf(focused))
            || origin == &component
            || (origin != nullptr && component.isParentOf(origin));
    };

    auto* focusedComponent = juce::Component::getCurrentlyFocusedComponent();
    const bool routeToPianoRoll = isPianoRollEditorVisible()
        && (layoutController.isPianoRollFullscreenMode() || focusIsWithin(sub808PianoRoll, focusedComponent, originatingComponent));

    auto isPlainLetter = [&key](juce::juce_wchar upperCaseLetter)
    {
        if (key.getModifiers().isCtrlDown() || key.getModifiers().isAltDown() || key.getModifiers().isCommandDown())
            return false;

        const int keyCode = static_cast<int>(key.getKeyCode());
        const int upperCode = static_cast<int>(upperCaseLetter);
        const int lowerCode = std::tolower(upperCode);
        return keyCode == upperCode || keyCode == lowerCode;
    };

    auto applyGridEditorTool = [this](GridEditorComponent::EditorTool tool)
    {
        grid.setEditorTool(tool);
        syncEditorToolButtons(tool);
        return true;
    };

    auto applyPianoRollEditorTool = [this](GridEditorComponent::EditorTool tool)
    {
        sub808PianoRoll.setEditorTool(tool);
        syncEditorToolButtons(tool);
        return true;
    };

    if (routeToPianoRoll)
    {
        if (isPlainLetter('D') || isKeyForAction(kActionPencilTool))
            return applyPianoRollEditorTool(GridEditorComponent::EditorTool::Pencil);

        if (isPlainLetter('B') || isKeyForAction(kActionBrushTool))
            return applyPianoRollEditorTool(GridEditorComponent::EditorTool::Brush);

        if (isPlainLetter('S') || isKeyForAction(kActionSelectTool))
            return applyPianoRollEditorTool(GridEditorComponent::EditorTool::Select);

        if (isKeyForAction(kActionCutTool))
            return applyPianoRollEditorTool(GridEditorComponent::EditorTool::Cut);

        if (isKeyForAction(kActionEraseTool))
            return applyPianoRollEditorTool(GridEditorComponent::EditorTool::Erase);

        if ((isKeyForAction(kActionDeleteNote) || key.getKeyCode() == juce::KeyPress::backspaceKey) && sub808PianoRoll.deleteSelectedNotes())
            return true;

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

        if (key.getKeyCode() == juce::KeyPress::leftKey)
        {
            if (key.getModifiers().isAltDown())
                return sub808PianoRoll.nudgeSelectionByTicks(-1);
            if (key.getModifiers().isShiftDown())
                return sub808PianoRoll.nudgeSelectionByTicks(-4 * ticksPerStep());
            return sub808PianoRoll.nudgeSelectionByTicks(-ticksPerStep());
        }

        if (key.getKeyCode() == juce::KeyPress::rightKey)
        {
            if (key.getModifiers().isAltDown())
                return sub808PianoRoll.nudgeSelectionByTicks(1);
            if (key.getModifiers().isShiftDown())
                return sub808PianoRoll.nudgeSelectionByTicks(4 * ticksPerStep());
            return sub808PianoRoll.nudgeSelectionByTicks(ticksPerStep());
        }

        if (key.getKeyCode() == juce::KeyPress::upKey)
        {
            if (key.getModifiers().isShiftDown())
                return sub808PianoRoll.nudgeSelectionByPitch(12);
            return sub808PianoRoll.nudgeSelectionByPitch(1);
        }

        if (key.getKeyCode() == juce::KeyPress::downKey)
        {
            if (key.getModifiers().isShiftDown())
                return sub808PianoRoll.nudgeSelectionByPitch(-12);
            return sub808PianoRoll.nudgeSelectionByPitch(-1);
        }
    }

    if (isKeyForAction(kActionPencilTool))
        return applyGridEditorTool(GridEditorComponent::EditorTool::Pencil);

    if (isKeyForAction(kActionBrushTool))
        return applyGridEditorTool(GridEditorComponent::EditorTool::Brush);

    if (isKeyForAction(kActionSelectTool))
        return applyGridEditorTool(GridEditorComponent::EditorTool::Select);

    if (isKeyForAction(kActionCutTool))
        return applyGridEditorTool(GridEditorComponent::EditorTool::Cut);

    if (isKeyForAction(kActionEraseTool))
        return applyGridEditorTool(GridEditorComponent::EditorTool::Erase);

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
    auto& layoutState = layoutController.getState();
    const bool gridEditorFullscreenMode = layoutController.isGridEditorFullscreenMode();
    const bool pianoRollFullscreenMode = layoutController.isPianoRollFullscreenMode();

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

    const bool sub808DetachedVisible = sub808DetachedWindow != nullptr && sub808DetachedWindow->isVisible();

    // ─────────────────────────────────────────────────────────────────────
    // GRID EDITOR FULLSCREEN MODE
    // ─────────────────────────────────────────────────────────────────────
    if (gridEditorFullscreenMode)
    {
        laneSurface.setVisible(true);
        if (!sub808DetachedVisible)
        {
            sub808Viewport.setVisible(false);
            sub808Viewport.setBounds({});
        }
        analysisPanel.setVisible(false);
        soundModule.setVisible(false);
        soundModule.setBounds({});
        optionsToolButton.setVisible(false);
        hotkeysToolButton.setVisible(false);

        const int maxRackByAvailableSpace = juce::jmax(kRackMinWidth, area.getWidth() - kGridMinWidth - kSplitterHitWidth - kPaneGap);
        const int rackMax = juce::jmax(kRackMinWidth, juce::jmin(kRackMaxWidth, maxRackByAvailableSpace));
        layoutState.leftPanelWidth = juce::jlimit(kRackMinWidth, rackMax, layoutState.leftPanelWidth);

        const int visibleRows = juce::jmax(1, trackList.getVisibleRowCount());
        const int fullscreenLaneHeight = juce::jlimit(22,
                                                      96,
                                                      (juce::jmax(1, area.getHeight() - kEditorToolBarHeight - 6 - 24)
                                                       - 24) / visibleRows);
        trackList.setRowHeight(fullscreenLaneHeight);
        grid.setLaneHeight(fullscreenLaneHeight);

        trackList.setDisplayMode(laneRackModeForWidth(layoutState.leftPanelWidth));
        const int laneSurfaceHeight = juce::jmax(1, trackList.getLaneSectionHeight());
        laneSurface.setBounds(area.removeFromTop(laneSurfaceHeight));

        auto laneArea = laneSurface.getLocalBounds();
        trackList.setBounds(laneArea.removeFromLeft(layoutState.leftPanelWidth));
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
        laneSurface.setVisible(false);
        trackList.setBounds({});
        gridViewport.setBounds({});
        if (!sub808DetachedVisible)
            sub808Viewport.setVisible(true);
        analysisPanel.setVisible(false);
        analysisPanel.setBounds({});
        soundModule.setVisible(false);
        soundModule.setBounds({});
        splitterBounds = {};
        if (splitterHandle != nullptr)
            splitterHandle->setBounds({});
        optionsToolButton.setVisible(false);
        hotkeysToolButton.setVisible(false);
        const int gridToToolsGap = 2;

        editorToolBar.setBounds({});

        if (area.getHeight() > 0)
            area.removeFromTop(juce::jmin(gridToToolsGap, area.getHeight()));

        if (!sub808DetachedVisible)
            sub808Viewport.setBounds(area);
    }
    // ─────────────────────────────────────────────────────────────────────
    // NORMAL MODE: standard layout with left panel + grid + sound module
    // ─────────────────────────────────────────────────────────────────────
    else
    {
        trackList.setRowHeight(laneHeight);
        grid.setLaneHeight(laneHeight);
        laneSurface.setVisible(true);
        const bool alternateEditorVisible = isAlternateEditorVisible() && !sub808DetachedVisible;
        if (!sub808DetachedVisible)
            sub808Viewport.setVisible(alternateEditorVisible);
        analysisPanel.setVisible(true);
        soundModule.setVisible(!alternateEditorVisible);

        const int maxRackByAvailableSpace = juce::jmax(kRackMinWidth, area.getWidth() - kGridMinWidth - kSplitterHitWidth - kPaneGap);
        const int rackMax = juce::jmax(kRackMinWidth, juce::jmin(kRackMaxWidth, maxRackByAvailableSpace));
        layoutState.leftPanelWidth = juce::jlimit(kRackMinWidth, rackMax, layoutState.leftPanelWidth);

        trackList.setDisplayMode(laneRackModeForWidth(layoutState.leftPanelWidth));
        const bool showGlobalTools = !alternateEditorVisible;
        const int toolsHeight = showGlobalTools ? kEditorToolBarHeight : 0;
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
        trackList.setBounds(laneArea.removeFromLeft(layoutState.leftPanelWidth));
        laneArea.removeFromLeft(kPaneGap);
        auto splitterLocalArea = juce::Rectangle<int>(laneArea.getX(), laneArea.getY(), kSplitterHitWidth, laneSurface.getHeight());
        splitterBounds = splitterLocalArea.translated(laneSurface.getX(), laneSurface.getY());
        if (splitterHandle != nullptr)
            splitterHandle->setBounds(splitterLocalArea);
        laneArea.removeFromLeft(kSplitterHitWidth + kPaneGap);
        gridViewport.setBounds(laneArea);

        if (area.getHeight() > 0)
            area.removeFromTop(juce::jmin(gridToToolsGap, area.getHeight()));

        if (showGlobalTools && area.getHeight() >= toolsHeight)
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
        auto leftColumn = bottomArea.removeFromLeft(layoutState.leftPanelWidth);
        bottomArea.removeFromLeft(kPaneGap + kSplitterHitWidth + kPaneGap);
        analysisPanel.setBounds(leftColumn);

        if (alternateEditorVisible)
        {
            soundModule.setVisible(false);
            soundModule.setBounds({});
            sub808Viewport.setBounds(bottomArea);
        }
        else
        {
            if (!sub808DetachedVisible)
            {
                sub808Viewport.setVisible(false);
                sub808Viewport.setBounds({});
            }
            soundModule.setBounds(bottomArea);
            soundModule.setVisible(!bottomArea.isEmpty());
        }
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
    const auto gridViewportInEditor = gridViewport.getBounds().translated(laneSurface.getX(), laneSurface.getY());
    const auto pianoViewportInEditor = sub808DetachedVisible ? juce::Rectangle<int>{} : sub808Viewport.getBounds();
    const int gridFullscreenButtonX = juce::jmax(0, gridViewportInEditor.getRight() - fullscreenButtonWidth - 8);
    const int gridFullscreenButtonY = juce::jmax(0, gridViewportInEditor.getY() + 6);
    const int pianoFullscreenButtonX = juce::jmax(0, pianoViewportInEditor.getRight() - fullscreenButtonWidth - 8);
    const int pianoFullscreenButtonY = juce::jmax(0, pianoViewportInEditor.getY() + 6);

    gridEditorFullscreenButton.setBounds(gridFullscreenButtonX, gridFullscreenButtonY, fullscreenButtonWidth, fullscreenButtonHeight);
    pianoRollFullscreenButton.setBounds(pianoFullscreenButtonX, pianoFullscreenButtonY, fullscreenButtonWidth, fullscreenButtonHeight);
    gridEditorFullscreenButton.setVisible(laneSurface.isVisible() && !gridViewportInEditor.isEmpty() && !pianoRollFullscreenMode);
    pianoRollFullscreenButton.setVisible(isPianoRollEditorVisible() && (sub808DetachedVisible || !pianoViewportInEditor.isEmpty()));

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
    if (sub808DetachedVisible)
        sub808Viewport.setTransform(juce::AffineTransform());
    else
        sub808Viewport.setTransform(transform);
    editorToolBar.setTransform(transform);
    optionsToolButton.setTransform(transform);
    hotkeysToolButton.setTransform(transform);
    pianoRollFullscreenButton.setTransform(transform);
    gridEditorFullscreenButton.setTransform(transform);

    repaint();
}

void BoomBGeneratorAudioProcessorEditor::timerCallback()
{
    const auto before = historyController.getLastObservedProjectState().has_value()
        ? *historyController.getLastObservedProjectState()
        : audioProcessor.getProjectSnapshot();

    refreshSubstyleBindingForGenre();
    audioProcessor.applySelectedStylePreset(false);

    const auto after = audioProcessor.getProjectSnapshot();
    if (historyController.getLastObservedProjectState().has_value())
        pushProjectHistoryState(before, after);
    else
        historyController.observeProjectState(after);

    refreshFromProcessor(false);
}

void BoomBGeneratorAudioProcessorEditor::refreshFromProcessor(bool refreshTrackRows)
{
    auto project = audioProcessor.getProjectSnapshot();
    if (!historyController.getLastObservedProjectState().has_value())
        historyController.observeProjectState(project);

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

    const auto soundTarget = audioProcessor.getSoundModuleTarget();
    const auto panelSound = SoundTargetController::resolveSoundState(project, soundTarget);
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
    syncAlternateEditorForLane(project, selectedTrack);

    sub808PianoRoll.setBars(project.params.bars);
    sub808PianoRoll.setStepWidth(20.0f * horizontalZoom);
    sub808PianoRoll.setScaleContext(audioProcessor.getBassKeyRootChoice(),
                                    audioProcessor.getBassScaleModeChoice());

    std::vector<Sub808PianoRollComponent::DrumGhostNote> drumGhostNotes;
    for (const auto& track : project.tracks)
    {
        if (!track.enabled || track.type == TrackType::Sub808)
            continue;

        const auto* lane = findRuntimeLaneById(project.runtimeLaneProfile, track.laneId);
        const bool isGhostTrack = lane != nullptr ? lane->isGhostTrack
                                                  : (track.type == TrackType::GhostKick || track.type == TrackType::ClapGhostSnare);

        for (const auto& note : track.notes)
        {
            Sub808PianoRollComponent::DrumGhostNote ghostNote;
            ghostNote.trackType = track.type;
            ghostNote.startTick = note.step * ticksPerStep() + note.microOffset;
            ghostNote.lengthTicks = juce::jmax(1, note.length) * ticksPerStep();
            ghostNote.velocity = note.velocity;
            ghostNote.isGhostTrack = isGhostTrack;
            drumGhostNotes.push_back(ghostNote);
        }
    }
    sub808PianoRoll.setDrumGhostNotes(drumGhostNotes);

    bool sub808TrackFound = false;
    for (const auto& track : project.tracks)
    {
        const bool laneMatch = !alternateEditorSession.laneId.isEmpty() && track.laneId == alternateEditorSession.laneId;
        const bool typeMatch = alternateEditorSession.laneId.isEmpty()
            && alternateEditorSession.trackType.has_value()
            && track.type == *alternateEditorSession.trackType;
        const bool fallbackMatch = alternateEditorSession.laneId.isEmpty()
            && !alternateEditorSession.trackType.has_value()
            && track.type == TrackType::Sub808;

        if (laneMatch || typeMatch || fallbackMatch)
        {
            sub808PianoRoll.setLaneSettings(track.sub808Settings);
            sub808PianoRoll.setNotes(sub808NotesForRead(track));
            sub808TrackFound = true;
            break;
        }
    }

    if (!sub808TrackFound)
    {
        sub808PianoRoll.setLaneSettings(Sub808LaneSettings{});
        sub808PianoRoll.setNotes({});
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
    trackList.onRegenerateTrack = [this](const RuntimeLaneId& laneId)
    {
        applyProcessorProjectMutation([this, laneId]
        {
            audioProcessor.regenerateTrack(laneId);
        });
    };

    trackList.onSoloTrack = [this](const RuntimeLaneId& laneId, bool value)
    {
        applyProcessorProjectMutation([this, laneId, value]
        {
            audioProcessor.setTrackSolo(laneId, value);
        });
    };

    trackList.onMuteTrack = [this](const RuntimeLaneId& laneId, bool value)
    {
        applyProcessorProjectMutation([this, laneId, value]
        {
            audioProcessor.setTrackMuted(laneId, value);
        });
    };

    trackList.onClearTrack = [this](const RuntimeLaneId& laneId)
    {
        clearLaneNotes(laneId);
    };

    trackList.onLockTrack = [this](const RuntimeLaneId& laneId, bool value)
    {
        applyProcessorProjectMutation([this, laneId, value]
        {
            audioProcessor.setTrackLocked(laneId, value);
        });
    };

    trackList.onEnableTrack = [this](const RuntimeLaneId& laneId, bool value)
    {
        applyProcessorProjectMutation([this, laneId, value]
        {
            audioProcessor.setTrackEnabled(laneId, value);
        });
    };

    trackList.onDragTrack = [this](const RuntimeLaneId& laneId)
    {
        dragTrackTempMidi(laneId);
    };

    trackList.onDragTrackGesture = [this](const RuntimeLaneId& laneId)
    {
        dragTrackExternal(laneId);
    };

    trackList.onDragDensityTrack = [this](const RuntimeLaneId& laneId, float density)
    {
        if (!isHatFxLane(laneId))
            return;

        applyHatFxDragDensityFromUi(density, false);
    };

    trackList.onDragDensityLockTrack = [this](const RuntimeLaneId& laneId, bool locked)
    {
        if (!isHatFxLane(laneId))
            return;

        hatFxDragLockedUi = locked;
        applyProcessorProjectMutation([this]
        {
            audioProcessor.setHatFxDragDensity(hatFxDragDensityUi, hatFxDragLockedUi);
        });
    };

    trackList.onExportTrack = [this](const RuntimeLaneId& laneId)
    {
        exportTrack(laneId);
    };

    trackList.onPrevSampleTrack = [this](const RuntimeLaneId& laneId)
    {
        applyProcessorProjectMutation([this, laneId]
        {
            audioProcessor.selectPreviousLaneSample(laneId);
        });
    };

    trackList.onNextSampleTrack = [this](const RuntimeLaneId& laneId)
    {
        applyProcessorProjectMutation([this, laneId]
        {
            audioProcessor.selectNextLaneSample(laneId);
        });
    };

    trackList.onSampleMenuTrack = [this](const RuntimeLaneId& laneId)
    {
        showSampleMenu(laneId);
    };

    trackList.onImportMidiLaneTrack = [this](const RuntimeLaneId& laneId)
    {
        showImportMidiToLaneDialog(laneId);
    };

    trackList.onTrackNameClicked = [this](const RuntimeLaneId& laneId)
    {
        const auto project = audioProcessor.getProjectSnapshot();
        audioProcessor.setSelectedTrack(laneId);
        grid.setSelectedTrack(laneId);
        syncAlternateEditorForLane(project, laneId);
    };

    trackList.onLaneVolumeTrack = [this](const RuntimeLaneId& laneId, float volume)
    {
        applyProcessorProjectMutation([this, laneId, volume]
        {
            audioProcessor.setTrackLaneVolume(laneId, volume);
        }, false);
    };

    trackList.onLaneSoundTrack = [this](const RuntimeLaneId& laneId, const SoundLayerState& state)
    {
        auto project = audioProcessor.getProjectSnapshot();
        SoundLayerState nextState = state;

        for (const auto& track : project.tracks)
        {
            if (track.laneId != laneId)
                continue;

            nextState.eqTone = track.sound.eqTone;
            nextState.compression = track.sound.compression;
            nextState.reverb = track.sound.reverb;
            nextState.gate = track.sound.gate;
            nextState.transient = track.sound.transient;
            nextState.drive = track.sound.drive;
            break;
        }

        applyProcessorProjectMutation([this, laneId, nextState]
        {
            audioProcessor.setTrackSoundLayer(laneId, nextState);
        }, false);
    };

    trackList.onRenameLaneRequested = [this](const RuntimeLaneId& laneId)
    {
        showRenameLaneDialog(laneId);
    };

    trackList.onDeleteLaneRequested = [this](const RuntimeLaneId& laneId)
    {
        requestDeleteLane(laneId);
    };

    trackList.onLaneMoveUpTrack = [this](const RuntimeLaneId& laneId)
    {
        moveLaneDisplayOrder(laneId, -1);
    };

    trackList.onLaneMoveDownTrack = [this](const RuntimeLaneId& laneId)
    {
        moveLaneDisplayOrder(laneId, 1);
    };

    trackList.onBassKeyChanged = [this](int choice)
    {
        applyProcessorProjectMutation([this, choice]
        {
            audioProcessor.setBassKeyRootChoice(choice);
        }, false);
    };

    trackList.onBassScaleChanged = [this](int choice)
    {
        applyProcessorProjectMutation([this, choice]
        {
            audioProcessor.setBassScaleModeChoice(choice);
        }, false);
    };

    trackList.onSub808SettingsChanged = [this](const RuntimeLaneId& laneId, const Sub808LaneSettings& settings)
    {
        auto before = audioProcessor.getProjectSnapshot();
        auto after = before;
        for (auto& track : after.tracks)
        {
            if (track.laneId == laneId)
            {
                track.sub808Settings = settings;
                break;
            }
        }

        audioProcessor.setSub808LaneSettings(laneId, settings);
        pushProjectHistoryState(before, after);
        refreshFromProcessor(true);
    };

    trackList.onAddLaneRequested = [this]
    {
        showAddLaneMenu();
    };

    soundModule.onSoundTargetChanged = [this](const SoundTargetDescriptor& target)
    {
        audioProcessor.setSoundModuleTarget(target);
        refreshFromProcessor(false);
    };

    soundModule.onSoundLayerChanged = [this](const SoundTargetDescriptor& target, const SoundLayerState& state)
    {
        applyProcessorProjectMutation([this, target, state]
        {
            audioProcessor.setSoundLayerForTarget(target, state);
        }, false);
    };

    analysisPanel.onAnalysisSourceChanged = [this](SampleAnalysisRequest::SourceType source)
    {
        applyProcessorProjectMutation([this, source]
        {
            auto request = audioProcessor.getSampleAnalysisRequest();
            request.source = source;
            audioProcessor.setSampleAnalysisRequest(request);
        }, false);
    };

    analysisPanel.onAnalysisModeChanged = [this](AnalysisMode mode)
    {
        applyProcessorProjectMutation([this, mode]
        {
            audioProcessor.setAnalysisMode(mode);
        }, false);
    };

    analysisPanel.onAnalysisBarsChanged = [this](int bars)
    {
        applyProcessorProjectMutation([this, bars]
        {
            auto request = audioProcessor.getSampleAnalysisRequest();
            request.barsToCapture = juce::jlimit(2, 16, bars);
            audioProcessor.setSampleAnalysisRequest(request);
        }, false);
    };

    analysisPanel.onAnalysisTempoHandlingChanged = [this](SampleAnalysisRequest::TempoHandling tempoHandling)
    {
        applyProcessorProjectMutation([this, tempoHandling]
        {
            auto request = audioProcessor.getSampleAnalysisRequest();
            request.tempoHandling = tempoHandling;
            audioProcessor.setSampleAnalysisRequest(request);
        }, false);
    };

    analysisPanel.onAnalysisReactivityChanged = [this](float value)
    {
        applyProcessorProjectMutation([this, value]
        {
            audioProcessor.setSampleReactivity(value);
        }, false);
    };

    analysisPanel.onSupportVsContrastChanged = [this](float value)
    {
        applyProcessorProjectMutation([this, value]
        {
            audioProcessor.setSupportVsContrast(value);
        }, false);
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

                                 applyProcessorProjectMutation([this, selected]
                                 {
                                     auto request = audioProcessor.getSampleAnalysisRequest();
                                     request.source = SampleAnalysisRequest::SourceType::AudioFile;
                                     request.audioFile = selected;
                                     audioProcessor.setSampleAnalysisRequest(request);
                                 }, false);
                             });
    };

    analysisPanel.onAnalysisFileDropped = [this](const juce::File& file)
    {
        if (!file.existsAsFile())
            return;

        applyProcessorProjectMutation([this, file]
        {
            auto request = audioProcessor.getSampleAnalysisRequest();
            request.source = SampleAnalysisRequest::SourceType::AudioFile;
            request.audioFile = file;
            audioProcessor.setSampleAnalysisRequest(request);
        }, false);
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

    applyProcessorProjectMutation([this]
    {
        audioProcessor.setHatFxDragDensity(hatFxDragDensityUi, hatFxDragLockedUi);
    });
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
        layoutController.beginSplitterDrag(static_cast<int>(virtualPos.x));
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    }
}

void BoomBGeneratorAudioProcessorEditor::mouseDrag(const juce::MouseEvent& event)
{
    if (!layoutController.isSplitterDragging())
        return;

    const auto virtualPos = toVirtualPoint(event.position);
    layoutController.updateSplitterDrag(static_cast<int>(virtualPos.x));
    resized();
}

void BoomBGeneratorAudioProcessorEditor::mouseUp(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);
    if (!layoutController.isSplitterDragging())
        return;

    layoutController.endSplitterDrag();
    saveUiLayoutState();
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void BoomBGeneratorAudioProcessorEditor::exportFullPattern()
{
    commandController.exportFullPattern(this);
}

bool BoomBGeneratorAudioProcessorEditor::isHatFxLane(const RuntimeLaneId& laneId) const
{
    const auto project = audioProcessor.getProjectSnapshot();
    const auto* lane = findRuntimeLaneById(project.runtimeLaneProfile, laneId);
    return lane != nullptr && lane->runtimeTrackType == TrackType::HatFX;
}

void BoomBGeneratorAudioProcessorEditor::exportLoopWav()
{
    commandController.exportLoopWav(this, logDrag);
}

void BoomBGeneratorAudioProcessorEditor::dragFullPatternTempMidi()
{
    commandController.dragFullPatternTempMidi(logDrag);
}

void BoomBGeneratorAudioProcessorEditor::dragFullPatternExternal()
{
    commandController.dragFullPatternExternal(this, logDrag);
}

void BoomBGeneratorAudioProcessorEditor::exportTrack(const RuntimeLaneId& laneId)
{
    commandController.exportTrack(laneId, this);
}

void BoomBGeneratorAudioProcessorEditor::showSampleMenu(const RuntimeLaneId& laneId)
{
    commandController.showSampleMenu(laneId,
                                     this,
                                     [this](const PatternProject& before, const PatternProject& after)
                                     {
                                         pushProjectHistoryState(before, after);
                                     },
                                     [this]()
                                     {
                                         refreshFromProcessor();
                                     },
                                     logDrag);
}

void BoomBGeneratorAudioProcessorEditor::dragTrackTempMidi(const RuntimeLaneId& laneId)
{
    commandController.dragTrackTempMidi(laneId, logDrag);
}

void BoomBGeneratorAudioProcessorEditor::dragTrackExternal(const RuntimeLaneId& laneId)
{
    commandController.dragTrackExternal(laneId, this, logDrag);
}

void BoomBGeneratorAudioProcessorEditor::updateGridGeometry()
{
    const int width = juce::jmax(1, grid.getGridWidth());
    const int targetTrackHeight = (layoutController.isGridEditorFullscreenMode() || layoutController.isPianoRollFullscreenMode())
        ? trackList.getLaneSectionHeight()
        : trackList.getContentHeight();
    const int height = juce::jmax(gridViewport.getHeight(), targetTrackHeight);
    grid.setSize(width, height);

    if (auto* component = activeAlternateEditorComponent())
    {
        const int alternateWidth = juce::jmax(sub808Viewport.getWidth(), sub808PianoRoll.getPreferredContentWidth());
        const int alternateHeight = juce::jmax(sub808Viewport.getHeight(), sub808PianoRoll.getPreferredContentHeight());
        component->setSize(alternateWidth, alternateHeight);
    }
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

void BoomBGeneratorAudioProcessorEditor::syncAlternateEditorForLane(const PatternProject& project,
                                                                    const RuntimeLaneId& laneId)
{
    if (laneId.isEmpty())
    {
        setAlternateEditorVisible(false);
        return;
    }

    const auto* lane = findRuntimeLaneById(project.runtimeLaneProfile, laneId);
    if (lane == nullptr || !lane->runtimeTrackType.has_value() || !lane->editorCapabilities.usesAlternateEditor())
    {
        setAlternateEditorVisible(false);
        return;
    }

    setAlternateEditorVisible(true, lane->editorCapabilities.alternateEditor, *lane->runtimeTrackType, laneId);
}

void BoomBGeneratorAudioProcessorEditor::setAlternateEditorVisible(bool shouldShow,
                                                                   AlternateLaneEditor editorType,
                                                                   std::optional<TrackType> trackType,
                                                                   RuntimeLaneId laneId)
{
    const auto nextType = shouldShow ? editorType : AlternateLaneEditor::None;
    const auto nextTrack = shouldShow ? trackType : std::nullopt;
    const auto nextLaneId = shouldShow ? laneId : RuntimeLaneId{};
    if (alternateEditorSession.isVisible == shouldShow
        && alternateEditorSession.editorType == nextType
        && alternateEditorSession.trackType == nextTrack
        && alternateEditorSession.laneId == nextLaneId)
    {
        return;
    }

    alternateEditorSession.isVisible = shouldShow;
    alternateEditorSession.editorType = nextType;
    alternateEditorSession.trackType = nextTrack;
    alternateEditorSession.laneId = nextLaneId;
    layoutController.setGridEditorFullscreenMode(false);
    if (!shouldShow)
        layoutController.setPianoRollFullscreenMode(false);
    gridEditorFullscreenButton.setToggleState(false, juce::dontSendNotification);

    if (!isPianoRollEditorVisible())
        setSub808DetachedWindowVisible(false);

    sub808Viewport.setVisible(isPianoRollEditorVisible());

    if (auto* component = activeAlternateEditorComponent())
    {
        component->setWantsKeyboardFocus(true);
        component->grabKeyboardFocus();
    }

    editorToolBar.setVisible(!isPianoRollEditorVisible());
    optionsToolButton.setVisible(!isPianoRollEditorVisible());
    hotkeysToolButton.setVisible(!isPianoRollEditorVisible());
    pianoRollFullscreenButton.setVisible(isPianoRollEditorVisible());
    resized();
}

void BoomBGeneratorAudioProcessorEditor::setupHotkeys()
{
    hotkeyController.setupDefaults();
    loadHotkeys();
    applyHotkeysToGrid();
}

void BoomBGeneratorAudioProcessorEditor::applyHotkeysToGrid()
{
    const auto bindings = hotkeyController.makeGridInputBindings();
    grid.setInputBindings(bindings);
    sub808PianoRoll.setInputBindings(bindings);
}

void BoomBGeneratorAudioProcessorEditor::loadHotkeys()
{
    hotkeyController.load(audioProcessor.getApvts().state);
}

void BoomBGeneratorAudioProcessorEditor::saveHotkeys()
{
    hotkeyController.save(audioProcessor.getApvts().state);
}

void BoomBGeneratorAudioProcessorEditor::restoreDefaultHotkeys()
{
    hotkeyController.restoreDefaults();
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

void BoomBGeneratorAudioProcessorEditor::applyProcessorProjectMutation(const std::function<void()>& mutation,
                                                                       bool refreshTrackRows)
{
    const auto before = audioProcessor.getProjectSnapshot();
    mutation();
    const auto after = audioProcessor.getProjectSnapshot();

    pushProjectHistoryState(before, after);
    refreshFromProcessor(refreshTrackRows);
}

bool BoomBGeneratorAudioProcessorEditor::clearLaneNotes(const RuntimeLaneId& laneId)
{
    return commandController.clearLaneNotes(laneId,
                                            [this](const PatternProject& before, const PatternProject& after, bool refreshTrackRows)
                                            {
                                                applyProjectSnapshotChange(before, after, refreshTrackRows);
                                            },
                                            [this]()
                                            {
                                                grid.clearNoteSelection();
                                                sub808PianoRoll.clearNoteSelection();
                                            });
}

bool BoomBGeneratorAudioProcessorEditor::clearAllPatternNotes()
{
    return commandController.clearAllPatternNotes([this](const PatternProject& before, const PatternProject& after, bool refreshTrackRows)
                                                  {
                                                      applyProjectSnapshotChange(before, after, refreshTrackRows);
                                                  },
                                                  [this]()
                                                  {
                                                      grid.clearNoteSelection();
                                                      sub808PianoRoll.clearNoteSelection();
                                                  });
}

void BoomBGeneratorAudioProcessorEditor::showImportMidiToLaneDialog(const RuntimeLaneId& laneId)
{
    commandController.showImportMidiToLaneDialog(laneId,
                                                 this,
                                                 [this](const PatternProject& before, const PatternProject& after, bool refreshTrackRows)
                                                 {
                                                     applyProjectSnapshotChange(before, after, refreshTrackRows);
                                                 });
}

void BoomBGeneratorAudioProcessorEditor::showRenameLaneDialog(const RuntimeLaneId& laneId)
{
    commandController.showRenameLaneDialog(laneId,
                                           this,
                                           [this](const PatternProject& before, const PatternProject& after, bool refreshTrackRows)
                                           {
                                               applyProjectSnapshotChange(before, after, refreshTrackRows);
                                           });
}

void BoomBGeneratorAudioProcessorEditor::requestDeleteLane(const RuntimeLaneId& laneId)
{
    commandController.requestDeleteLane(laneId,
                                        this,
                                        [this](const PatternProject& before, const PatternProject& after, bool refreshTrackRows)
                                        {
                                            applyProjectSnapshotChange(before, after, refreshTrackRows);
                                        });
}

void BoomBGeneratorAudioProcessorEditor::showAddLaneMenu()
{
    commandController.showAddLaneMenu(this,
                                      [this](const PatternProject& before, const PatternProject& after, bool refreshTrackRows)
                                      {
                                          applyProjectSnapshotChange(before, after, refreshTrackRows);
                                      });
}

void BoomBGeneratorAudioProcessorEditor::pushProjectHistoryState(const PatternProject& before, const PatternProject& after)
{
    auto safeEditor = juce::Component::SafePointer<BoomBGeneratorAudioProcessorEditor>(this);
    historyController.pushProjectHistoryState(before, after, [safeEditor]
    {
        juce::MessageManager::callAsync([safeEditor]
        {
            if (safeEditor != nullptr)
                safeEditor->commitPendingProjectHistoryState();
        });
    });
}

void BoomBGeneratorAudioProcessorEditor::commitPendingProjectHistoryState()
{
    historyController.commitPendingProjectHistoryState();
}

bool BoomBGeneratorAudioProcessorEditor::performUndo()
{
    commitPendingProjectHistoryState();
    PatternProject snapshot;
    if (!historyController.performUndo(audioProcessor.getProjectSnapshot(), snapshot))
        return false;

    audioProcessor.restoreEditorProjectSnapshot(snapshot);
    historyController.endSuppressedHistoryAction();
    refreshFromProcessor(true);
    return true;
}

bool BoomBGeneratorAudioProcessorEditor::performRedo()
{
    commitPendingProjectHistoryState();
    PatternProject snapshot;
    if (!historyController.performRedo(audioProcessor.getProjectSnapshot(), snapshot))
        return false;

    audioProcessor.restoreEditorProjectSnapshot(snapshot);
    historyController.endSuppressedHistoryAction();
    refreshFromProcessor(true);
    return true;
}

bool BoomBGeneratorAudioProcessorEditor::projectsEquivalent(const PatternProject& lhs, const PatternProject& rhs) const
{
    return historyController.projectsEquivalent(lhs, rhs);
}

juce::String BoomBGeneratorAudioProcessorEditor::hotkeyToDisplayText(const HotkeyBinding& binding) const
{
    return hotkeyController.hotkeyToDisplayText(binding);
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
    if (!hotkeyController.tryRebindKeyAction(actionId, keyPress, [this](const juce::String& actionName, const juce::String& conflictedActionName, const juce::String& shortcutText)
        {
            return confirmReplaceConflict(actionName, conflictedActionName, shortcutText);
        }))
        return false;

    saveHotkeys();
    applyHotkeysToGrid();
    return true;
}

bool BoomBGeneratorAudioProcessorEditor::tryRebindMouseModifierAction(const juce::String& actionId, juce::ModifierKeys::Flags modifier)
{
    if (!hotkeyController.tryRebindMouseModifierAction(actionId, modifier, [this](const juce::String& actionName, const juce::String& conflictedActionName, const juce::String& shortcutText)
        {
            return confirmReplaceConflict(actionName, conflictedActionName, shortcutText);
        }))
        return false;

    saveHotkeys();
    applyHotkeysToGrid();
    return true;
}

bool BoomBGeneratorAudioProcessorEditor::resetHotkeyActionToDefault(const juce::String& actionId)
{
    if (!hotkeyController.resetActionToDefault(actionId))
        return false;

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
        return hotkeyController.getBindings();
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
    const auto& bindings = hotkeyController.getBindings();
    auto it = std::find_if(bindings.begin(), bindings.end(), [&actionId](const HotkeyBinding& binding)
    {
        return binding.actionId == actionId;
    });

    if (it == bindings.end())
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
    return layoutController.laneRackModeForWidth(width);
}

void BoomBGeneratorAudioProcessorEditor::loadUiLayoutState()
{
    layoutController.load(audioProcessor.getApvts().state);
}

void BoomBGeneratorAudioProcessorEditor::saveUiLayoutState()
{
    layoutController.save(audioProcessor.getApvts().state);
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
    auto* focusedComponent = juce::Component::getCurrentlyFocusedComponent();
    const bool pianoRollFocused = isPianoRollEditorVisible()
        && (layoutController.isPianoRollFullscreenMode()
            || focusedComponent == &sub808PianoRoll
            || (focusedComponent != nullptr && sub808PianoRoll.isParentOf(focusedComponent)));

    if (pianoRollFocused)
        sub808PianoRoll.setEditorTool(tool);
    else
        grid.setEditorTool(tool);

    syncEditorToolButtons(tool);
}

void BoomBGeneratorAudioProcessorEditor::toggleSub808PianoRollFullscreen()
{
    if (!isPianoRollEditorVisible())
        return;

    const bool shouldShow = !(sub808DetachedWindow != nullptr && sub808DetachedWindow->isVisible());
    setSub808DetachedWindowVisible(shouldShow);
}

void BoomBGeneratorAudioProcessorEditor::toggleGridEditorFullscreen()
{
    if (isAlternateEditorVisible())
        return;

    layoutController.toggleGridEditorFullscreenMode();
    gridEditorFullscreenButton.setToggleState(layoutController.isGridEditorFullscreenMode(), juce::dontSendNotification);
    resized();
}

void BoomBGeneratorAudioProcessorEditor::setSub808DetachedWindowVisible(bool shouldShow)
{
    const bool isVisible = sub808DetachedWindow != nullptr && sub808DetachedWindow->isVisible();
    if (shouldShow == isVisible)
        return;

    if (!shouldShow)
    {
        layoutController.setPianoRollFullscreenMode(false);
        pianoRollFullscreenButton.setToggleState(false, juce::dontSendNotification);

        if (sub808DetachedWindow != nullptr)
        {
            sub808DetachedWindow->clearContentComponent();
            sub808DetachedWindow->setVisible(false);
        }

        if (sub808Viewport.getParentComponent() != this)
            addAndMakeVisible(sub808Viewport);

        sub808Viewport.setVisible(isPianoRollEditorVisible());
        resized();
        return;
    }

    if (!isPianoRollEditorVisible())
        return;

    if (sub808DetachedWindow == nullptr)
    {
        auto window = std::make_unique<DetachedSub808Window>();
        window->onCloseRequested = [this]
        {
            setSub808DetachedWindowVisible(false);
        };
        sub808DetachedWindow = std::move(window);
    }

    layoutController.setPianoRollFullscreenMode(true);
    pianoRollFullscreenButton.setToggleState(true, juce::dontSendNotification);

    if (sub808Viewport.getParentComponent() == this)
        removeChildComponent(&sub808Viewport);
    else if (auto* parent = sub808Viewport.getParentComponent(); parent != nullptr)
        parent->removeChildComponent(&sub808Viewport);

    sub808DetachedWindow->setContentNonOwned(&sub808Viewport, false);
    sub808DetachedWindow->centreAroundComponent(this,
                                                juce::jmax(720, getWidth() - 80),
                                                juce::jmax(520, getHeight() - 60));
    sub808DetachedWindow->setVisible(true);
    sub808DetachedWindow->toFront(true);
    sub808Viewport.setVisible(true);
    sub808PianoRoll.grabKeyboardFocus();
    resized();
}

void BoomBGeneratorAudioProcessorEditor::updateCursorForMousePosition(juce::Point<float> point)
{
    if (layoutController.isSplitterDragging())
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
