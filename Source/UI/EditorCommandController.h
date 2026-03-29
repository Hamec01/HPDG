#pragma once

#include <functional>
#include <memory>

#include <juce_gui_extra/juce_gui_extra.h>

#include "../Core/RuntimeLaneLifecycle.h"
#include "../Core/TrackRegistry.h"
#include "../Plugin/PluginProcessor.h"
#include "../Services/MidiImportService.h"

namespace bbg
{
class BoomBapGeneratorAudioProcessor;

class EditorCommandController
{
public:
    explicit EditorCommandController(BoomBapGeneratorAudioProcessor& processor)
        : audioProcessor(processor)
    {
    }

    void exportFullPattern(juce::Component* parent) const
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

        juce::ignoreUnused(parent);
    }

    void exportLoopWav(juce::Component* parent, const std::function<void(const juce::String&)>& logDrag) const
    {
        auto chooser = std::make_shared<juce::FileChooser>(
            "Export loop WAV",
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getNonexistentChildFile("HPDG_Loop", ".wav"),
            "*.wav");

        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                             [this, chooser, logDrag](const juce::FileChooser& fc)
                             {
                                 const auto result = fc.getResult();
                                 if (result == juce::File())
                                     return;

                                 const bool ok = audioProcessor.exportLoopWavToFile(result);
                                 if (!ok)
                                     logDrag("exportLoopWav failed path=" + result.getFullPathName());
                             });

        juce::ignoreUnused(parent);
    }

    void exportTrack(TrackType type, juce::Component* parent) const
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

        juce::ignoreUnused(parent);
    }

    void exportTrack(const RuntimeLaneId& laneId, juce::Component* parent) const
    {
        auto chooser = std::make_shared<juce::FileChooser>(
            "Export track MIDI",
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getNonexistentChildFile("HPDG_Track", ".mid"),
            "*.mid");

        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                             [this, chooser, laneId](const juce::FileChooser& fc)
                             {
                                 const auto result = fc.getResult();
                                 if (result == juce::File())
                                     return;

                                 audioProcessor.exportTrackToFile(laneId, result);
                             });

        juce::ignoreUnused(parent);
    }

    void dragFullPatternTempMidi(const std::function<void(const juce::String&)>& logDrag) const
    {
        logDrag("dragFullPatternTempMidi click");
        const auto file = audioProcessor.createTemporaryFullPatternMidiFile();
        if (!file.existsAsFile())
        {
            logDrag("dragFullPatternTempMidi no file");
            return;
        }

        logDrag("dragFullPatternTempMidi reveal " + file.getFullPathName());
        file.revealToUser();
    }

    void dragFullPatternExternal(juce::Component* parent, const std::function<void(const juce::String&)>& logDrag) const
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
                                                                                             parent,
                                                                                             [logDrag]
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

    void dragTrackTempMidi(TrackType type, const std::function<void(const juce::String&)>& logDrag) const
    {
        logDrag("dragTrackTempMidi click type=" + juce::String(static_cast<int>(type)));
        const auto file = audioProcessor.createTemporaryTrackMidiFile(type);
        if (!file.existsAsFile())
        {
            logDrag("dragTrackTempMidi no file type=" + juce::String(static_cast<int>(type)));
            return;
        }

        logDrag("dragTrackTempMidi reveal " + file.getFullPathName());
        file.revealToUser();
    }

    void dragTrackTempMidi(const RuntimeLaneId& laneId, const std::function<void(const juce::String&)>& logDrag) const
    {
        logDrag("dragTrackTempMidi click lane=" + laneId);
        const auto file = audioProcessor.createTemporaryTrackMidiFile(laneId);
        if (!file.existsAsFile())
        {
            logDrag("dragTrackTempMidi no file lane=" + laneId);
            return;
        }

        logDrag("dragTrackTempMidi reveal " + file.getFullPathName());
        file.revealToUser();
    }

    void dragTrackExternal(TrackType type, juce::Component* parent, const std::function<void(const juce::String&)>& logDrag) const
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
                                                                                             parent,
                                                                                             [logDrag]
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

    void dragTrackExternal(const RuntimeLaneId& laneId,
                           juce::Component* parent,
                           const std::function<void(const juce::String&)>& logDrag) const
    {
        logDrag("dragTrackExternal gesture start lane=" + laneId);
        const auto file = audioProcessor.createTemporaryTrackMidiFile(laneId);
        if (!file.existsAsFile())
        {
            logDrag("dragTrackExternal no file lane=" + laneId);
            return;
        }

        try
        {
            const bool started = juce::DragAndDropContainer::performExternalDragDropOfFiles({ file.getFullPathName() },
                                                                                             false,
                                                                                             parent,
                                                                                             [logDrag]
                                                                                             {
                                                                                                 logDrag("dragTrackExternal callback end");
                                                                                             });
            logDrag("dragTrackExternal performExternalDragDropOfFiles started=" + juce::String(started ? "1" : "0")
                    + " lane=" + laneId);
            if (!started)
                file.revealToUser();
        }
        catch (...)
        {
            logDrag("dragTrackExternal exception fallback reveal lane=" + laneId);
            file.revealToUser();
        }
    }

    template <typename PushHistoryFn, typename RefreshFn>
    void showSampleMenu(TrackType type,
                        juce::Component* parent,
                        PushHistoryFn&& pushHistory,
                        RefreshFn&& refreshFromProcessor,
                        const std::function<void(const juce::String&)>& logDrag) const
    {
        juce::PopupMenu menu;
        menu.addItem(1, "Load new sample (.wav)");
        menu.addItem(2, "Delete selected sample");
        menu.addSeparator();
        menu.addItem(3, "Open lane sample folder");

        const auto mousePos = juce::Desktop::getInstance().getMainMouseSource().getScreenPosition().roundToInt();
        const juce::Rectangle<int> targetArea(mousePos.x, mousePos.y, 1, 1);

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(targetArea).withParentComponent(parent),
                           [this,
                            parent,
                            type,
                            pushHistory = std::forward<PushHistoryFn>(pushHistory),
                            refreshFromProcessor = std::forward<RefreshFn>(refreshFromProcessor),
                            logDrag](int choice) mutable
                           {
                               if (choice == 1)
                               {
                                   auto chooser = std::make_shared<juce::FileChooser>(
                                       "Select WAV sample",
                                       juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                                       "*.wav");

                                   chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                                        [this, chooser, type, pushHistory = std::move(pushHistory), refreshFromProcessor = std::move(refreshFromProcessor), logDrag](const juce::FileChooser& fc) mutable
                                                        {
                                                            const auto result = fc.getResult();
                                                            if (result == juce::File())
                                                                return;

                                                            const auto before = audioProcessor.getProjectSnapshot();
                                                            juce::String error;
                                                            auto trackType = type;
                                                            const bool ok = audioProcessor.importLaneSample(trackType, result, &error);
                                                            const auto after = audioProcessor.getProjectSnapshot();
                                                            pushHistory(before, after);
                                                            refreshFromProcessor();
                                                            if (!ok && error.isNotEmpty())
                                                                logDrag("importLaneSample error: " + error);
                                                        });
                                   return;
                               }

                               if (choice == 2)
                               {
                                   const auto before = audioProcessor.getProjectSnapshot();
                                   juce::String error;
                                   auto trackType = type;
                                   const bool ok = audioProcessor.deleteSelectedLaneSample(trackType, &error);
                                   const auto after = audioProcessor.getProjectSnapshot();
                                   pushHistory(before, after);
                                   refreshFromProcessor();
                                   if (!ok && error.isNotEmpty())
                                       logDrag("deleteSelectedLaneSample error: " + error);
                                   return;
                               }

                               if (choice == 3)
                               {
                                   const auto folder = audioProcessor.getLaneSampleDirectory(type);
                                   folder.createDirectory();
                                   folder.revealToUser();
                               }

                               juce::ignoreUnused(parent);
                           });
    }

    template <typename PushHistoryFn, typename RefreshFn>
    void showSampleMenu(const RuntimeLaneId& laneId,
                        juce::Component* parent,
                        PushHistoryFn&& pushHistory,
                        RefreshFn&& refreshFromProcessor,
                        const std::function<void(const juce::String&)>& logDrag) const
    {
        juce::PopupMenu menu;
        menu.addItem(1, "Load new sample (.wav)");
        menu.addItem(2, "Delete selected sample");
        menu.addSeparator();
        menu.addItem(3, "Open lane sample folder");

        const auto mousePos = juce::Desktop::getInstance().getMainMouseSource().getScreenPosition().roundToInt();
        const juce::Rectangle<int> targetArea(mousePos.x, mousePos.y, 1, 1);

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(targetArea).withParentComponent(parent),
                           [this,
                            parent,
                            laneId,
                            pushHistory = std::forward<PushHistoryFn>(pushHistory),
                            refreshFromProcessor = std::forward<RefreshFn>(refreshFromProcessor),
                            logDrag](int choice) mutable
                           {
                               if (choice == 1)
                               {
                                   auto chooser = std::make_shared<juce::FileChooser>(
                                       "Select WAV sample",
                                       juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                                       "*.wav");

                                   chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                                        [this, chooser, laneId, pushHistory = std::move(pushHistory), refreshFromProcessor = std::move(refreshFromProcessor), logDrag](const juce::FileChooser& fc) mutable
                                                        {
                                                            const auto result = fc.getResult();
                                                            if (result == juce::File())
                                                                return;

                                                            const auto before = audioProcessor.getProjectSnapshot();
                                                            juce::String error;
                                                            const bool ok = audioProcessor.importLaneSample(laneId, result, &error);
                                                            const auto after = audioProcessor.getProjectSnapshot();
                                                            pushHistory(before, after);
                                                            refreshFromProcessor();
                                                            if (!ok && error.isNotEmpty())
                                                                logDrag("importLaneSample error: " + error);
                                                        });
                                   return;
                               }

                               if (choice == 2)
                               {
                                   const auto before = audioProcessor.getProjectSnapshot();
                                   juce::String error;
                                   const bool ok = audioProcessor.deleteSelectedLaneSample(laneId, &error);
                                   const auto after = audioProcessor.getProjectSnapshot();
                                   pushHistory(before, after);
                                   refreshFromProcessor();
                                   if (!ok && error.isNotEmpty())
                                       logDrag("deleteSelectedLaneSample error: " + error);
                                   return;
                               }

                               if (choice == 3)
                               {
                                   const auto folder = audioProcessor.getLaneSampleDirectory(laneId);
                                   folder.createDirectory();
                                   folder.revealToUser();
                               }

                               juce::ignoreUnused(parent);
                           });
    }

    template <typename ApplySnapshotChangeFn, typename ClearSelectionFn>
    bool clearLaneNotes(const RuntimeLaneId& laneId,
                        ApplySnapshotChangeFn&& applyProjectSnapshotChange,
                        ClearSelectionFn&& clearSelection) const
    {
        if (laneId.isEmpty())
            return false;

        const auto before = audioProcessor.getProjectSnapshot();
        auto after = before;
        bool changed = false;

        for (auto& track : after.tracks)
        {
            if (track.laneId != laneId)
                continue;

            changed = changed || !track.notes.empty() || !track.sub808Notes.empty();
            track.notes.clear();
            track.sub808Notes.clear();
            break;
        }

        if (!changed)
            return false;

        applyProjectSnapshotChange(before, after, true);
        clearSelection();
        return true;
    }

    template <typename ApplySnapshotChangeFn, typename ClearSelectionFn>
    bool clearAllPatternNotes(ApplySnapshotChangeFn&& applyProjectSnapshotChange,
                              ClearSelectionFn&& clearSelection) const
    {
        const auto before = audioProcessor.getProjectSnapshot();
        auto after = before;
        bool changed = false;

        for (auto& track : after.tracks)
        {
            changed = changed || !track.notes.empty() || !track.sub808Notes.empty();
            track.notes.clear();
            track.sub808Notes.clear();
        }

        if (!changed)
        {
            clearSelection();
            return false;
        }

        applyProjectSnapshotChange(before, after, true);
        clearSelection();
        return true;
    }

    template <typename ApplySnapshotChangeFn>
    void showImportMidiToLaneDialog(const RuntimeLaneId& laneId,
                                    juce::Component* parent,
                                    ApplySnapshotChangeFn&& applyProjectSnapshotChange) const
    {
        auto chooser = std::make_shared<juce::FileChooser>(
            "Import MIDI to lane",
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
            "*.mid;*.midi");

        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [this, parent, chooser, laneId, applyProjectSnapshotChange = std::forward<ApplySnapshotChangeFn>(applyProjectSnapshotChange)](const juce::FileChooser& fc) mutable
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
                                                                            parent);
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
                                                                            parent);
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
                                                                            parent);
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
                                                                            parent);
                                 }
                             });
    }

    template <typename ApplySnapshotChangeFn>
    void showRenameLaneDialog(const RuntimeLaneId& laneId,
                              juce::Component* parent,
                              ApplySnapshotChangeFn&& applyProjectSnapshotChange) const
    {
        const auto before = audioProcessor.getProjectSnapshot();
        const auto* lane = findRuntimeLaneById(before.runtimeLaneProfile, laneId);
        if (lane == nullptr)
            return;

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

        auto component = std::make_unique<LaneNameDialogComponent>(lane->laneName);
        component->onSubmit = [this, parent, laneId, applyProjectSnapshotChange = std::forward<ApplySnapshotChangeFn>(applyProjectSnapshotChange)](const juce::String& submittedName) mutable
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
                                                       parent);
                return;
            }

            applyProjectSnapshotChange(current, after, true);
        };

        juce::DialogWindow::LaunchOptions options;
        options.dialogTitle = "Rename Lane";
        options.content.setOwned(component.release());
        options.componentToCentreAround = parent;
        options.dialogBackgroundColour = juce::Colour::fromRGB(22, 25, 31);
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = false;

        if (auto* window = options.launchAsync())
            window->setSize(380, 150);
    }

    template <typename ApplySnapshotChangeFn>
    void requestDeleteLane(const RuntimeLaneId& laneId,
                           juce::Component* parent,
                           ApplySnapshotChangeFn&& applyProjectSnapshotChange) const
    {
        const auto before = audioProcessor.getProjectSnapshot();
        const auto* lane = findRuntimeLaneById(before.runtimeLaneProfile, laneId);
        if (lane == nullptr)
            return;

        const bool confirmed = juce::NativeMessageBox::showOkCancelBox(juce::MessageBoxIconType::WarningIcon,
                                                                       "Delete Lane",
                                                                       "Delete lane '" + lane->laneName + "'?",
                                                                       parent,
                                                                       nullptr);
        if (!confirmed)
            return;

        auto after = before;
        if (!RuntimeLaneLifecycle::deleteLane(after, laneId))
            return;

        applyProjectSnapshotChange(before, after, true);
    }

    template <typename ApplySnapshotChangeFn>
    void showAddLaneMenu(juce::Component* parent,
                         ApplySnapshotChangeFn&& applyProjectSnapshotChange) const
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
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(targetArea).withParentComponent(parent),
                           [this, applyProjectSnapshotChange = std::forward<ApplySnapshotChangeFn>(applyProjectSnapshotChange), menuTrackTypes](int choice) mutable
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

private:
    BoomBapGeneratorAudioProcessor& audioProcessor;
};
} // namespace bbg