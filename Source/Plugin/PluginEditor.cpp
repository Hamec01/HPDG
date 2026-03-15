#include "PluginEditor.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include "../Engine/StyleDefaults.h"

namespace bbg
{
namespace
{
constexpr int kRackMinWidth = 460;
constexpr int kPaneGap = 6;
constexpr int kDesignWidth = 1460;
constexpr int kDesignHeight = 820;
constexpr float kMinUiScale = 0.56f;

GridEditorComponent::GridResolution gridResolutionFromId(int id)
{
    switch (id)
    {
        case 1: return GridEditorComponent::GridResolution::OneEighth;
        case 3: return GridEditorComponent::GridResolution::OneThirtySecond;
        case 4: return GridEditorComponent::GridResolution::OneSixtyFourth;
        case 5: return GridEditorComponent::GridResolution::Triplet;
        case 2:
        default: return GridEditorComponent::GridResolution::OneSixteenth;
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
}

BoomBGeneratorAudioProcessorEditor::BoomBGeneratorAudioProcessorEditor(BoomBapGeneratorAudioProcessor& processor)
    : juce::AudioProcessorEditor(&processor)
    , audioProcessor(processor)
{
    addAndMakeVisible(header);
    addAndMakeVisible(trackList);
    addAndMakeVisible(gridViewport);
    gridViewport.setViewedComponent(&grid, false);
    gridViewport.setScrollBarsShown(true, false);
    gridViewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::all);

    setupAttachments();
    bindTrackCallbacks();

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
        if (shouldStart)
            audioProcessor.startPreview();
        else
            audioProcessor.stopPreview();
        refreshFromProcessor(false);
    };

    header.onExportFullPressed = [this]
    {
        exportFullPattern();
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
        updateGridGeometry();
    };

    header.onGridResolutionChanged = [this](int selectedId)
    {
        grid.setGridResolution(gridResolutionFromId(selectedId));
    };

    grid.onCellClicked = [this](TrackType track, int step)
    {
        audioProcessor.setPreviewStartStep(step);
        grid.setSelectedTrack(track);
    };

    grid.onTrackNotesEdited = [this](TrackType track, const std::vector<NoteEvent>& notes)
    {
        audioProcessor.setTrackNotes(track, notes);
        refreshFromProcessor(true);
    };

    grid.onHorizontalZoomGesture = [this](float delta)
    {
        horizontalZoom = juce::jlimit(0.6f, 2.6f, horizontalZoom + delta * 0.20f);
        header.zoomSlider.setValue(horizontalZoom, juce::dontSendNotification);
        grid.setStepWidth(20.0f * horizontalZoom);
        updateGridGeometry();
    };

    grid.onLaneHeightZoomGesture = [this](float delta)
    {
        laneHeight = juce::jlimit(22, 64, laneHeight + static_cast<int>(delta * 8.0f));
        header.laneHeightSlider.setValue(static_cast<double>(laneHeight), juce::dontSendNotification);
        trackList.setRowHeight(laneHeight);
        grid.setLaneHeight(laneHeight);
        updateGridGeometry();
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

void BoomBGeneratorAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(12, 14, 18));
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

    header.setBounds(area.removeFromTop(104));
    area.removeFromTop(6);

    const int rackWidth = juce::jlimit(kRackMinWidth, 920, static_cast<int>(area.getWidth() * 0.52f));
    trackList.setBounds(area.removeFromLeft(rackWidth));
    area.removeFromLeft(kPaneGap);
    gridViewport.setBounds(area);
    updateGridGeometry();

    const float scaledWidth = static_cast<float>(virtualWidth) * uiScale;
    const float scaledHeight = static_cast<float>(virtualHeight) * uiScale;
    const float tx = static_cast<float>(outer.getX()) + (static_cast<float>(outer.getWidth()) - scaledWidth) * 0.5f;
    const float ty = static_cast<float>(outer.getY()) + (static_cast<float>(outer.getHeight()) - scaledHeight) * 0.5f;

    const auto transform = juce::AffineTransform::scale(uiScale).translated(tx, ty);
    header.setTransform(transform);
    trackList.setTransform(transform);
    gridViewport.setTransform(transform);
}

void BoomBGeneratorAudioProcessorEditor::timerCallback()
{
    refreshSubstyleBindingForGenre();
    audioProcessor.applySelectedStylePreset(false);
    refreshFromProcessor(false);
}

void BoomBGeneratorAudioProcessorEditor::refreshFromProcessor(bool refreshTrackRows)
{
    const auto project = audioProcessor.getProjectSnapshot();
    const auto transport = audioProcessor.getLastTransportSnapshot();

    const bool syncEnabled = audioProcessor.getApvts().getRawParameterValue(ParamIds::syncDawTempo)->load() > 0.5f;
    header.setBpmLocked(syncEnabled && transport.hasHostTempo);

    if (refreshTrackRows)
        trackList.setTracks(project.tracks,
                            audioProcessor.getBassKeyRootChoice(),
                            audioProcessor.getBassScaleModeChoice());

    header.setPreviewPlaying(audioProcessor.isPreviewPlaying());
    header.setStandaloneWindowMaximized(isStandaloneWindowMaximized());
    grid.setPlayheadStep(audioProcessor.getPreviewPlayheadStep());
    const auto selectedTrack = project.tracks.empty()
        ? TrackType::Kick
        : project.tracks[static_cast<size_t>(juce::jlimit(0, static_cast<int>(project.tracks.size()) - 1, project.selectedTrackIndex))].type;
    grid.setSelectedTrack(selectedTrack);

    const bool mustRefreshGridModel = refreshTrackRows || (project.generationCounter != lastGenerationCounter);
    if (mustRefreshGridModel)
    {
        grid.setProject(project);
        lastGenerationCounter = project.generationCounter;
    }

    updateGridGeometry();
}

void BoomBGeneratorAudioProcessorEditor::setupAttachments()
{
    auto& apvts = audioProcessor.getApvts();

    bpmAttachment = std::make_unique<SliderAttachment>(apvts, ParamIds::bpm, header.bpmSlider);
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
    trackList.onRegenerateTrack = [this](TrackType type)
    {
        audioProcessor.regenerateTrack(type);
        refreshFromProcessor();
    };

    trackList.onSoloTrack = [this](TrackType type, bool value)
    {
        audioProcessor.setTrackSolo(type, value);
        refreshFromProcessor();
    };

    trackList.onMuteTrack = [this](TrackType type, bool value)
    {
        audioProcessor.setTrackMuted(type, value);
        refreshFromProcessor();
    };

    trackList.onClearTrack = [this](TrackType type)
    {
        audioProcessor.clearTrack(type);
        refreshFromProcessor();
    };

    trackList.onLockTrack = [this](TrackType type, bool value)
    {
        audioProcessor.setTrackLocked(type, value);
        refreshFromProcessor();
    };

    trackList.onEnableTrack = [this](TrackType type, bool value)
    {
        audioProcessor.setTrackEnabled(type, value);
        refreshFromProcessor();
    };

    trackList.onDragTrack = [this](TrackType type)
    {
        dragTrackTempMidi(type);
    };

    trackList.onDragTrackGesture = [this](TrackType type)
    {
        dragTrackExternal(type);
    };

    trackList.onExportTrack = [this](TrackType type)
    {
        exportTrack(type);
    };

    trackList.onPrevSampleTrack = [this](TrackType type)
    {
        audioProcessor.selectPreviousLaneSample(type);
        refreshFromProcessor();
    };

    trackList.onNextSampleTrack = [this](TrackType type)
    {
        audioProcessor.selectNextLaneSample(type);
        refreshFromProcessor();
    };

    trackList.onLaneVolumeTrack = [this](TrackType type, float volume)
    {
        audioProcessor.setTrackLaneVolume(type, volume);
        refreshFromProcessor(false);
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
    const int width = juce::jmax(gridViewport.getWidth(), grid.getGridWidth());
    const int height = juce::jmax(gridViewport.getHeight(), trackList.getContentHeight());
    grid.setSize(width, height);
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
