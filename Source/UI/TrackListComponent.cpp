#include "TrackListComponent.h"

#include <algorithm>

#include "../Core/TrackRegistry.h"

namespace bbg
{
namespace
{
bool isSupportedAnalysisFile(const juce::String& path)
{
    const auto ext = juce::File(path).getFileExtension().toLowerCase();
    return ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".flac" || ext == ".mp3";
}

RuntimeLaneRowState buildLaneRowState(const RuntimeLaneDefinition& lane, const std::vector<TrackState>& tracks)
{
    RuntimeLaneRowState row;
    row.laneId = lane.laneId;
    row.laneName = lane.laneName;
    row.groupName = lane.groupName;
    row.dependencyName = lane.dependencyName;
    row.generationPriority = lane.generationPriority;
    row.isCore = lane.isCore;
    row.isVisibleInEditor = lane.isVisibleInEditor;
    row.enabled = lane.enabledByDefault;
    row.supportsDragExport = lane.supportsDragExport;
    row.isGhostTrack = lane.isGhostTrack;
    row.runtimeTrackType = lane.runtimeTrackType;

    const auto trackIt = std::find_if(tracks.begin(), tracks.end(), [&lane](const TrackState& track)
    {
        if (track.laneId.isNotEmpty() && track.laneId == lane.laneId)
            return true;
        return lane.runtimeTrackType.has_value() && track.type == *lane.runtimeTrackType;
    });

    if (trackIt != tracks.end())
    {
        row.enabled = trackIt->enabled;
        row.muted = trackIt->muted;
        row.solo = trackIt->solo;
        row.locked = trackIt->locked;
        row.laneVolume = trackIt->laneVolume;
        row.sound = trackIt->sound;
        row.selectedSampleName = trackIt->selectedSampleName;
        if (trackIt->runtimeTrackType.has_value())
            row.runtimeTrackType = trackIt->runtimeTrackType;
    }

    return row;
}

int displayRankForLane(const RuntimeLaneRowState& lane, const std::vector<RuntimeLaneId>& laneDisplayOrder)
{
    const auto it = std::find(laneDisplayOrder.begin(), laneDisplayOrder.end(), lane.laneId);
    return it != laneDisplayOrder.end() ? static_cast<int>(std::distance(laneDisplayOrder.begin(), it))
                                        : std::numeric_limits<int>::max();
}
}

TrackListComponent::TrackListComponent()
{
    addLaneButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(58, 84, 116));
    addLaneButton.onClick = [this]
    {
        if (onAddLaneRequested)
            onAddLaneRequested();
    };
    addAndMakeVisible(addLaneButton);
    setupAnalysisPanel();
}

void TrackListComponent::setLaneDisplayOrder(const std::vector<RuntimeLaneId>& order)
{
    laneDisplayOrder = order;
}

void TrackListComponent::setShowAnalysisPanel(bool shouldShow)
{
    if (showAnalysisPanel == shouldShow)
        return;

    showAnalysisPanel = shouldShow;
    resized();
    repaint();
}

void TrackListComponent::setDisplayMode(LaneRackDisplayMode mode)
{
    if (displayMode == mode)
        return;

    displayMode = mode;
    for (auto& row : rows)
        row->setDisplayMode(displayMode);

    repaint();
}

void TrackListComponent::setRowHeight(int newHeight)
{
    rowHeight = juce::jlimit(22, 80, newHeight);
    resized();
    repaint();
}

int TrackListComponent::getContentHeight() const
{
    return rulerHeight + rowHeight * static_cast<int>(rows.size()) + (showAnalysisPanel ? analysisPanelHeight : 0);
}

int TrackListComponent::getLaneSectionHeight() const
{
    return rulerHeight + rowHeight * static_cast<int>(rows.size());
}

bool TrackListComponent::isInterestedInFileDrag(const juce::StringArray& files)
{
    return std::any_of(files.begin(), files.end(), [](const juce::String& path)
    {
        return isSupportedAnalysisFile(path);
    });
}

void TrackListComponent::fileDragEnter(const juce::StringArray& files, int x, int y)
{
    fileDragMove(files, x, y);
}

void TrackListComponent::fileDragMove(const juce::StringArray& files, int x, int y)
{
    const bool shouldHighlight = chooseFileButton.getBounds().contains(x, y)
        && std::any_of(files.begin(), files.end(), [](const juce::String& path)
        {
            return isSupportedAnalysisFile(path);
        });

    if (analysisFileDropHighlight == shouldHighlight)
        return;

    analysisFileDropHighlight = shouldHighlight;
    repaint();
}

void TrackListComponent::fileDragExit(const juce::StringArray& files)
{
    juce::ignoreUnused(files);
    if (!analysisFileDropHighlight)
        return;

    analysisFileDropHighlight = false;
    repaint();
}

void TrackListComponent::filesDropped(const juce::StringArray& files, int x, int y)
{
    const bool droppedOnButton = chooseFileButton.getBounds().contains(x, y);
    analysisFileDropHighlight = false;
    repaint();

    if (!droppedOnButton)
        return;

    auto it = std::find_if(files.begin(), files.end(), [](const juce::String& path)
    {
        return isSupportedAnalysisFile(path);
    });

    if (it == files.end())
        return;

    if (onAnalysisFileDropped)
        onAnalysisFileDropped(juce::File(*it));
}

void TrackListComponent::setHatFxDragUiState(float density, bool locked)
{
    hatFxDragDensity = juce::jlimit(0.0f, 2.0f, density);
    hatFxDragLocked = locked;

    for (auto& row : rows)
    {
        if (row->isBackedBy(TrackType::HatFX))
            row->setHatFxDragState(hatFxDragDensity, hatFxDragLocked);
    }
}

void TrackListComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(17, 19, 23));

    g.setColour(juce::Colour::fromRGB(30, 34, 40));
    g.fillRect(0, 0, getWidth(), rulerHeight);
    g.setColour(juce::Colour::fromRGB(210, 216, 224));
    g.setFont(juce::Font(12.5f, juce::Font::bold));
    g.drawText("INSTRUMENT RACK", 10, 0, juce::jmax(120, getWidth() - 260), rulerHeight, juce::Justification::centredLeft);

    g.setColour(juce::Colour::fromRGB(154, 164, 178));
    g.setFont(juce::Font(9.5f));
    g.drawText("RG: regenerate lane | S: solo | M: mute | < / >: samples | Drag: export/drag | ...: lane actions",
               170,
               0,
               getWidth() - 178,
               rulerHeight,
               juce::Justification::centredLeft,
               false);

    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 20));
    g.drawLine(0.0f, static_cast<float>(rulerHeight), static_cast<float>(getWidth()), static_cast<float>(rulerHeight));

    const int rowsBottom = rulerHeight + rowHeight * static_cast<int>(rows.size());
    if (rowsBottom < getHeight())
    {
        g.setColour(juce::Colour::fromRGBA(255, 255, 255, 22));
        g.drawLine(0.0f,
                   static_cast<float>(rowsBottom),
                   static_cast<float>(getWidth()),
                   static_cast<float>(rowsBottom));
    }

    if (showAnalysisPanel && analysisFileDropHighlight)
    {
        g.setColour(juce::Colour::fromRGBA(130, 188, 255, 58));
        g.fillRoundedRectangle(chooseFileButton.getBounds().toFloat().reduced(-2.0f), 6.0f);
        g.setColour(juce::Colour::fromRGBA(178, 218, 255, 190));
        g.drawRoundedRectangle(chooseFileButton.getBounds().toFloat().reduced(-2.0f), 6.0f, 1.5f);
    }
}

void TrackListComponent::setAnalysisPanelState(SampleAnalysisRequest::SourceType source,
                                               AnalysisMode mode,
                                               int barsToCapture,
                                               SampleAnalysisRequest::TempoHandling tempoHandling,
                                               float reactivity,
                                               float supportVsContrast,
                                               const juce::String& fileLabelText,
                                               const juce::String& statusText,
                                               const juce::String& detailsText,
                                               const juce::String& debugText,
                                               bool analysisReady)
{
    const int sourceId = source == SampleAnalysisRequest::SourceType::AudioFile ? 3
        : (source == SampleAnalysisRequest::SourceType::LiveInput ? 2 : 1);
    sourceCombo.setSelectedId(sourceId, juce::dontSendNotification);

    const int modeId = mode == AnalysisMode::AnalyzeOnly ? 2
        : (mode == AnalysisMode::GenerateFromSample ? 3 : 1);
    modeCombo.setSelectedId(modeId, juce::dontSendNotification);

    const int barsId = barsToCapture <= 2 ? 1
        : (barsToCapture <= 4 ? 2 : (barsToCapture <= 8 ? 3 : 4));
    barsCombo.setSelectedId(barsId, juce::dontSendNotification);

    const int tempoId = tempoHandling == SampleAnalysisRequest::TempoHandling::PreferHalfTime ? 2
        : (tempoHandling == SampleAnalysisRequest::TempoHandling::PreferDoubleTime ? 3
            : (tempoHandling == SampleAnalysisRequest::TempoHandling::KeepDetected ? 4 : 1));
    tempoCombo.setSelectedId(tempoId, juce::dontSendNotification);

    reactivitySlider.setValue(juce::jlimit(0.0, 1.0, static_cast<double>(reactivity)), juce::dontSendNotification);
    supportSlider.setValue(juce::jlimit(0.0, 1.0, static_cast<double>(supportVsContrast)), juce::dontSendNotification);

    fileLabel.setText(fileLabelText.isNotEmpty() ? fileLabelText : "File: (none)", juce::dontSendNotification);

    const juce::String prefix = analysisReady ? "Result: ready" : "Result: none";
    statusLabel.setText(statusText.isNotEmpty() ? (prefix + " | " + statusText) : prefix,
                        juce::dontSendNotification);
    detailsLabel.setText(detailsText.isNotEmpty() ? detailsText : "Details: -",
                         juce::dontSendNotification);
    debugTextBox.setText(debugText.isNotEmpty() ? debugText : "No generator debug details yet.",
                         juce::dontSendNotification);
}

void TrackListComponent::setSoundPanelState(const std::vector<TrackState>& tracks,
                                            const std::optional<TrackType>& selectedTarget,
                                            const SoundLayerState& soundState)
{
    soundTargetTracks.clear();
    soundTargetCombo.clear(juce::dontSendNotification);
    soundTargetCombo.addItem("All Tracks (Global)", 1);

    int selectedId = 1;
    int itemId = 2;
    for (const auto& track : tracks)
    {
        const auto* info = TrackRegistry::find(track.type);
        if (info == nullptr || !info->visibleInUI)
            continue;

        soundTargetTracks.push_back(track.type);
        soundTargetCombo.addItem(info->displayName, itemId);
        if (selectedTarget.has_value() && selectedTarget.value() == track.type)
            selectedId = itemId;
        ++itemId;
    }

    currentSoundTarget = selectedTarget;
    soundTargetCombo.setSelectedId(selectedId, juce::dontSendNotification);

    panSlider.setValue(soundState.pan, juce::dontSendNotification);
    widthSlider.setValue(soundState.width, juce::dontSendNotification);
    eqSlider.setValue(soundState.eqTone, juce::dontSendNotification);
    compSlider.setValue(soundState.compression, juce::dontSendNotification);
    reverbSlider.setValue(soundState.reverb, juce::dontSendNotification);
    gateSlider.setValue(soundState.gate, juce::dontSendNotification);
    transientSlider.setValue(soundState.transient, juce::dontSendNotification);
    driveSlider.setValue(soundState.drive, juce::dontSendNotification);
}

void TrackListComponent::setTracks(const RuntimeLaneProfile& profile,
                   const std::vector<TrackState>& tracks,
                   int bassKeyRootChoice,
                   int bassScaleModeChoice)
{
    std::vector<RuntimeLaneRowState> visibleLanes;
    visibleLanes.reserve(profile.lanes.size());

    for (const auto& lane : profile.lanes)
    {
        if (!lane.isVisibleInEditor)
            continue;

        visibleLanes.push_back(buildLaneRowState(lane, tracks));
    }

    std::stable_sort(visibleLanes.begin(), visibleLanes.end(), [this](const RuntimeLaneRowState& lhs, const RuntimeLaneRowState& rhs)
    {
        return displayRankForLane(lhs, laneDisplayOrder) < displayRankForLane(rhs, laneDisplayOrder);
    });

    if (rows.size() == visibleLanes.size() && !rows.empty())
    {
        for (size_t i = 0; i < rows.size(); ++i)
        {
            rows[i]->syncFromState(visibleLanes[i]);
            rows[i]->setBassControls(bassKeyRootChoice, bassScaleModeChoice);
            if (rows[i]->isBackedBy(TrackType::HatFX))
                rows[i]->setHatFxDragState(hatFxDragDensity, hatFxDragLocked);
        }

        repaint();
        return;
    }

    rows.clear();

    for (const auto& lane : visibleLanes)
    {
        auto row = std::make_unique<LaneHeaderComponent>(lane);
        row->setDisplayMode(displayMode);
        row->syncFromState(lane);
        row->onRegenerate = [this](const RuntimeLaneId& laneId)
        {
            if (onRegenerateTrack)
                onRegenerateTrack(laneId);
        };
        row->onImportMidiToLane = [this](const RuntimeLaneId& laneId)
        {
            if (onImportMidiLaneTrack)
                onImportMidiLaneTrack(laneId);
        };
        row->onSoloChanged = [this](const RuntimeLaneId& laneId, bool value)
        {
            if (onSoloTrack)
                onSoloTrack(laneId, value);
        };
        row->onMuteChanged = [this](const RuntimeLaneId& laneId, bool value)
        {
            if (onMuteTrack)
                onMuteTrack(laneId, value);
        };
        row->onClear = [this](const RuntimeLaneId& laneId)
        {
            if (onClearTrack)
                onClearTrack(laneId);
        };
        row->onLockChanged = [this](const RuntimeLaneId& laneId, bool value)
        {
            if (onLockTrack)
                onLockTrack(laneId, value);
        };
        row->onEnableChanged = [this](const RuntimeLaneId& laneId, bool value)
        {
            if (onEnableTrack)
                onEnableTrack(laneId, value);
        };
        row->onDrag = [this](const RuntimeLaneId& laneId)
        {
            if (onDragTrack)
                onDragTrack(laneId);
        };
        row->onDragGesture = [this](const RuntimeLaneId& laneId)
        {
            if (onDragTrackGesture)
                onDragTrackGesture(laneId);
        };
        row->onDragDensityChanged = [this](const RuntimeLaneId& laneId, float density)
        {
            if (onDragDensityTrack)
                onDragDensityTrack(laneId, density);
        };
        row->onDragDensityLockChanged = [this](const RuntimeLaneId& laneId, bool locked)
        {
            if (onDragDensityLockTrack)
                onDragDensityLockTrack(laneId, locked);
        };
        row->onExport = [this](const RuntimeLaneId& laneId)
        {
            if (onExportTrack)
                onExportTrack(laneId);
        };
        row->onPrevSample = [this](const RuntimeLaneId& laneId)
        {
            if (onPrevSampleTrack)
                onPrevSampleTrack(laneId);
        };
        row->onNextSample = [this](const RuntimeLaneId& laneId)
        {
            if (onNextSampleTrack)
                onNextSampleTrack(laneId);
        };
        row->onSampleMenuRequested = [this](const RuntimeLaneId& laneId)
        {
            if (onSampleMenuTrack)
                onSampleMenuTrack(laneId);
        };
        row->onTrackNameClicked = [this](const RuntimeLaneId& laneId)
        {
            if (onTrackNameClicked)
                onTrackNameClicked(laneId);
        };
        row->onRenameLaneRequested = [this](const RuntimeLaneId& laneId)
        {
            if (onRenameLaneRequested)
                onRenameLaneRequested(laneId);
        };
        row->onDeleteLaneRequested = [this](const RuntimeLaneId& laneId)
        {
            if (onDeleteLaneRequested)
                onDeleteLaneRequested(laneId);
        };
        row->onLaneVolumeChanged = [this](const RuntimeLaneId& laneId, float volume)
        {
            if (onLaneVolumeTrack)
                onLaneVolumeTrack(laneId, volume);
        };
        row->onLaneSoundChanged = [this](const RuntimeLaneId& laneId, const SoundLayerState& state)
        {
            if (onLaneSoundTrack)
                onLaneSoundTrack(laneId, state);
        };
        row->onMoveLaneUp = [this](const RuntimeLaneId& laneId)
        {
            if (onLaneMoveUpTrack)
                onLaneMoveUpTrack(laneId);
        };
        row->onMoveLaneDown = [this](const RuntimeLaneId& laneId)
        {
            if (onLaneMoveDownTrack)
                onLaneMoveDownTrack(laneId);
        };
        row->onBassKeyChanged = [this](int choice)
        {
            if (onBassKeyChanged)
                onBassKeyChanged(choice);
        };
        row->onBassScaleChanged = [this](int choice)
        {
            if (onBassScaleChanged)
                onBassScaleChanged(choice);
        };
        row->setBassControls(bassKeyRootChoice, bassScaleModeChoice);
        if (row->isBackedBy(TrackType::HatFX))
            row->setHatFxDragState(hatFxDragDensity, hatFxDragLocked);

        addAndMakeVisible(*row);
        rows.push_back(std::move(row));
    }

    resized();
    repaint();
}

void TrackListComponent::resized()
{
    auto area = getLocalBounds();
    addLaneButton.setBounds(juce::jmax(10, getWidth() - 82), 2, 72, juce::jmax(18, rulerHeight - 4));
    area.removeFromTop(rulerHeight);

    for (auto& row : rows)
        row->setBounds(area.removeFromTop(rowHeight));

    if (!showAnalysisPanel)
    {
        analysisTitleLabel.setBounds({});
        sourceLabel.setBounds({});
        modeLabel.setBounds({});
        barsLabel.setBounds({});
        tempoLabel.setBounds({});
        reactivityLabel.setBounds({});
        supportLabel.setBounds({});
        fileLabel.setBounds({});
        statusLabel.setBounds({});
        detailsLabel.setBounds({});
        debugLabel.setBounds({});
        sourceCombo.setBounds({});
        modeCombo.setBounds({});
        barsCombo.setBounds({});
        tempoCombo.setBounds({});
        reactivitySlider.setBounds({});
        supportSlider.setBounds({});
        chooseFileButton.setBounds({});
        analyzeButton.setBounds({});
        debugTextBox.setBounds({});
        return;
    }

    auto panel = area.reduced(8, 8);
    if (panel.isEmpty())
        return;

    analysisTitleLabel.setBounds(panel.removeFromTop(22));
    panel.removeFromTop(2);

    auto line1 = panel.removeFromTop(26);
    sourceLabel.setBounds(line1.removeFromLeft(68));
    sourceCombo.setBounds(line1.removeFromLeft(130));
    line1.removeFromLeft(8);
    modeLabel.setBounds(line1.removeFromLeft(52));
    modeCombo.setBounds(line1);

    panel.removeFromTop(6);
    auto line2 = panel.removeFromTop(26);
    barsLabel.setBounds(line2.removeFromLeft(68));
    barsCombo.setBounds(line2.removeFromLeft(74));
    line2.removeFromLeft(6);
    tempoLabel.setBounds(line2.removeFromLeft(52));
    tempoCombo.setBounds(line2.removeFromLeft(130));
    line2.removeFromLeft(8);
    chooseFileButton.setBounds(line2.removeFromLeft(90));
    line2.removeFromLeft(6);
    analyzeButton.setBounds(line2.removeFromLeft(80));

    panel.removeFromTop(6);
    fileLabel.setBounds(panel.removeFromTop(20));

    panel.removeFromTop(4);
    auto line3 = panel.removeFromTop(24);
    reactivityLabel.setBounds(line3.removeFromLeft(78));
    reactivitySlider.setBounds(line3);

    panel.removeFromTop(4);
    auto line4 = panel.removeFromTop(24);
    supportLabel.setBounds(line4.removeFromLeft(78));
    supportSlider.setBounds(line4);

    panel.removeFromTop(6);
    statusLabel.setBounds(panel.removeFromTop(36));

    panel.removeFromTop(4);
    detailsLabel.setBounds(panel.removeFromTop(48));

    panel.removeFromTop(6);
    debugLabel.setBounds(panel.removeFromTop(18));

    panel.removeFromTop(2);
    debugTextBox.setBounds(panel.removeFromTop(104));

}

void TrackListComponent::setupAnalysisPanel()
{
    analysisTitleLabel.setText("SAMPLE ANALYSIS", juce::dontSendNotification);
    analysisTitleLabel.setFont(juce::Font(12.5f, juce::Font::bold));
    analysisTitleLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(222, 228, 236));
    analysisTitleLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(analysisTitleLabel);

    sourceLabel.setText("Source", juce::dontSendNotification);
    modeLabel.setText("Mode", juce::dontSendNotification);
    barsLabel.setText("Scan", juce::dontSendNotification);
    tempoLabel.setText("BPM", juce::dontSendNotification);
    reactivityLabel.setText("React", juce::dontSendNotification);
    supportLabel.setText("Intent", juce::dontSendNotification);
    fileLabel.setText("File: (none)", juce::dontSendNotification);
    statusLabel.setText("Result: none", juce::dontSendNotification);
    detailsLabel.setText("Details: -", juce::dontSendNotification);
    debugLabel.setText("Generator Debug", juce::dontSendNotification);

    auto styleLabel = [](juce::Label& label)
    {
        label.setJustificationType(juce::Justification::centredLeft);
        label.setColour(juce::Label::textColourId, juce::Colour::fromRGB(166, 176, 192));
        label.setFont(juce::Font(11.0f));
    };

    styleLabel(sourceLabel);
    styleLabel(modeLabel);
    styleLabel(barsLabel);
    styleLabel(tempoLabel);
    styleLabel(reactivityLabel);
    styleLabel(supportLabel);
    styleLabel(fileLabel);
    styleLabel(statusLabel);
    styleLabel(detailsLabel);
    styleLabel(debugLabel);
    statusLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(152, 200, 255));
    detailsLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(174, 188, 208));
    debugLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(192, 198, 210));

    debugTextBox.setMultiLine(true);
    debugTextBox.setReadOnly(true);
    debugTextBox.setCaretVisible(false);
    debugTextBox.setScrollbarsShown(true);
    debugTextBox.setPopupMenuEnabled(false);
    debugTextBox.setText("No generator debug details yet.", juce::dontSendNotification);
    debugTextBox.setColour(juce::TextEditor::backgroundColourId, juce::Colour::fromRGB(22, 25, 30));
    debugTextBox.setColour(juce::TextEditor::textColourId, juce::Colour::fromRGB(178, 194, 214));
    debugTextBox.setColour(juce::TextEditor::outlineColourId, juce::Colour::fromRGB(44, 52, 66));
    debugTextBox.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour::fromRGB(76, 120, 182));

    sourceCombo.addItem("None", 1);
    sourceCombo.addItem("Live Input", 2);
    sourceCombo.addItem("Audio File", 3);
    sourceCombo.setSelectedId(1);

    modeCombo.addItem("Off", 1);
    modeCombo.addItem("Analyze Only", 2);
    modeCombo.addItem("Generate From Sample", 3);
    modeCombo.setSelectedId(1);

    barsCombo.addItem("2 bars", 1);
    barsCombo.addItem("4 bars", 2);
    barsCombo.addItem("8 bars", 3);
    barsCombo.addItem("16 bars", 4);
    barsCombo.setSelectedId(3);

    tempoCombo.addItem("Auto", 1);
    tempoCombo.addItem("Half", 2);
    tempoCombo.addItem("Double", 3);
    tempoCombo.addItem("Orig", 4);
    tempoCombo.setSelectedId(1);

    reactivitySlider.setSliderStyle(juce::Slider::LinearHorizontal);
    reactivitySlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    reactivitySlider.setRange(0.0, 1.0, 0.01);
    reactivitySlider.setValue(0.7, juce::dontSendNotification);
    reactivitySlider.setColour(juce::Slider::trackColourId, juce::Colour::fromRGB(98, 170, 242));
    reactivitySlider.setColour(juce::Slider::thumbColourId, juce::Colour::fromRGB(192, 220, 255));

    supportSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    supportSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    supportSlider.setRange(0.0, 1.0, 0.01);
    supportSlider.setValue(0.5, juce::dontSendNotification);
    supportSlider.setColour(juce::Slider::trackColourId, juce::Colour::fromRGB(234, 150, 88));
    supportSlider.setColour(juce::Slider::thumbColourId, juce::Colour::fromRGB(248, 210, 168));

    chooseFileButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(74, 88, 118));
    analyzeButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(96, 132, 176));

    addAndMakeVisible(sourceLabel);
    addAndMakeVisible(modeLabel);
    addAndMakeVisible(barsLabel);
    addAndMakeVisible(tempoLabel);
    addAndMakeVisible(reactivityLabel);
    addAndMakeVisible(supportLabel);
    addAndMakeVisible(fileLabel);
    addAndMakeVisible(statusLabel);
    addAndMakeVisible(detailsLabel);
    addAndMakeVisible(debugLabel);

    addAndMakeVisible(sourceCombo);
    addAndMakeVisible(modeCombo);
    addAndMakeVisible(barsCombo);
    addAndMakeVisible(tempoCombo);
    addAndMakeVisible(reactivitySlider);
    addAndMakeVisible(supportSlider);
    addAndMakeVisible(chooseFileButton);
    addAndMakeVisible(analyzeButton);
    addAndMakeVisible(debugTextBox);

    sourceCombo.onChange = [this]
    {
        if (!onAnalysisSourceChanged)
            return;

        const auto id = sourceCombo.getSelectedId();
        const auto source = id == 3 ? SampleAnalysisRequest::SourceType::AudioFile
            : (id == 2 ? SampleAnalysisRequest::SourceType::LiveInput : SampleAnalysisRequest::SourceType::None);
        onAnalysisSourceChanged(source);
    };

    modeCombo.onChange = [this]
    {
        if (!onAnalysisModeChanged)
            return;

        const auto id = modeCombo.getSelectedId();
        const auto mode = id == 2 ? AnalysisMode::AnalyzeOnly
            : (id == 3 ? AnalysisMode::GenerateFromSample : AnalysisMode::Off);
        onAnalysisModeChanged(mode);
    };

    barsCombo.onChange = [this]
    {
        if (!onAnalysisBarsChanged)
            return;

        const auto id = barsCombo.getSelectedId();
        const int bars = id == 1 ? 2 : (id == 2 ? 4 : (id == 3 ? 8 : 16));
        onAnalysisBarsChanged(bars);
    };

    tempoCombo.onChange = [this]
    {
        if (!onAnalysisTempoHandlingChanged)
            return;

        const auto id = tempoCombo.getSelectedId();
        const auto mode = id == 2 ? SampleAnalysisRequest::TempoHandling::PreferHalfTime
            : (id == 3 ? SampleAnalysisRequest::TempoHandling::PreferDoubleTime
                : (id == 4 ? SampleAnalysisRequest::TempoHandling::KeepDetected
                    : SampleAnalysisRequest::TempoHandling::Auto));
        onAnalysisTempoHandlingChanged(mode);
    };

    reactivitySlider.onValueChange = [this]
    {
        if (onAnalysisReactivityChanged)
            onAnalysisReactivityChanged(static_cast<float>(reactivitySlider.getValue()));
    };

    supportSlider.onValueChange = [this]
    {
        if (onSupportVsContrastChanged)
            onSupportVsContrastChanged(static_cast<float>(supportSlider.getValue()));
    };

    chooseFileButton.onClick = [this]
    {
        if (onChooseAnalysisFile)
            onChooseAnalysisFile();
    };

    analyzeButton.onClick = [this]
    {
        if (onRunAnalysis)
            onRunAnalysis();
    };
}

void TrackListComponent::setupSoundPanel()
{
    soundTitleLabel.setText("SOUND MODULE", juce::dontSendNotification);
    soundTitleLabel.setFont(juce::Font(12.5f, juce::Font::bold));
    soundTitleLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(222, 228, 236));
    soundTitleLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(soundTitleLabel);

    soundTargetLabel.setText("Target", juce::dontSendNotification);
    soundTargetLabel.setJustificationType(juce::Justification::centredLeft);
    soundTargetLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(166, 176, 192));
    soundTargetLabel.setFont(juce::Font(11.0f));
    addAndMakeVisible(soundTargetLabel);
    addAndMakeVisible(soundTargetCombo);

    panLabel.setText("Pan", juce::dontSendNotification);
    widthLabel.setText("Width", juce::dontSendNotification);
    eqLabel.setText("EQ Tone", juce::dontSendNotification);
    compLabel.setText("Compression", juce::dontSendNotification);
    reverbLabel.setText("Reverb", juce::dontSendNotification);
    gateLabel.setText("Gate", juce::dontSendNotification);
    transientLabel.setText("Transient", juce::dontSendNotification);
    driveLabel.setText("LoFi / Drive", juce::dontSendNotification);

    auto styleLabel = [](juce::Label& label)
    {
        label.setJustificationType(juce::Justification::centredLeft);
        label.setColour(juce::Label::textColourId, juce::Colour::fromRGB(166, 176, 192));
        label.setFont(juce::Font(10.5f));
    };

    styleLabel(panLabel);
    styleLabel(widthLabel);
    styleLabel(eqLabel);
    styleLabel(compLabel);
    styleLabel(reverbLabel);
    styleLabel(gateLabel);
    styleLabel(transientLabel);
    styleLabel(driveLabel);

    auto setupSlider = [this](juce::Slider& slider,
                              double min,
                              double max,
                              double step,
                              juce::Colour trackColour,
                              juce::Colour thumbColour)
    {
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        slider.setRange(min, max, step);
        slider.setColour(juce::Slider::trackColourId, trackColour);
        slider.setColour(juce::Slider::thumbColourId, thumbColour);
        addAndMakeVisible(slider);
    };

    setupSlider(panSlider, -1.0, 1.0, 0.01, juce::Colour::fromRGB(120, 166, 232), juce::Colour::fromRGB(196, 222, 255));
    setupSlider(widthSlider, 0.0, 2.0, 0.01, juce::Colour::fromRGB(120, 166, 232), juce::Colour::fromRGB(196, 222, 255));
    setupSlider(eqSlider, -1.0, 1.0, 0.01, juce::Colour::fromRGB(112, 172, 142), juce::Colour::fromRGB(188, 230, 206));
    setupSlider(compSlider, 0.0, 1.0, 0.01, juce::Colour::fromRGB(112, 172, 142), juce::Colour::fromRGB(188, 230, 206));
    setupSlider(reverbSlider, 0.0, 1.0, 0.01, juce::Colour::fromRGB(214, 150, 96), juce::Colour::fromRGB(248, 206, 162));
    setupSlider(gateSlider, 0.0, 1.0, 0.01, juce::Colour::fromRGB(214, 150, 96), juce::Colour::fromRGB(248, 206, 162));
    setupSlider(transientSlider, 0.0, 1.0, 0.01, juce::Colour::fromRGB(154, 144, 214), juce::Colour::fromRGB(212, 206, 250));
    setupSlider(driveSlider, 0.0, 1.0, 0.01, juce::Colour::fromRGB(154, 144, 214), juce::Colour::fromRGB(212, 206, 250));

    addAndMakeVisible(panLabel);
    addAndMakeVisible(widthLabel);
    addAndMakeVisible(eqLabel);
    addAndMakeVisible(compLabel);
    addAndMakeVisible(reverbLabel);
    addAndMakeVisible(gateLabel);
    addAndMakeVisible(transientLabel);
    addAndMakeVisible(driveLabel);

    soundTargetCombo.onChange = [this]
    {
        const int id = soundTargetCombo.getSelectedId();
        std::optional<TrackType> target;
        if (id >= 2)
        {
            const int index = id - 2;
            if (index >= 0 && index < static_cast<int>(soundTargetTracks.size()))
                target = soundTargetTracks[static_cast<size_t>(index)];
        }

        currentSoundTarget = target;
        if (onSoundTargetChanged)
            onSoundTargetChanged(currentSoundTarget);
    };

    const auto emitSoundChange = [this]
    {
        if (!onSoundLayerChanged)
            return;

        SoundLayerState state;
        state.pan = static_cast<float>(panSlider.getValue());
        state.width = static_cast<float>(widthSlider.getValue());
        state.eqTone = static_cast<float>(eqSlider.getValue());
        state.compression = static_cast<float>(compSlider.getValue());
        state.reverb = static_cast<float>(reverbSlider.getValue());
        state.gate = static_cast<float>(gateSlider.getValue());
        state.transient = static_cast<float>(transientSlider.getValue());
        state.drive = static_cast<float>(driveSlider.getValue());
        onSoundLayerChanged(currentSoundTarget, state);
    };

    panSlider.onValueChange = emitSoundChange;
    widthSlider.onValueChange = emitSoundChange;
    eqSlider.onValueChange = emitSoundChange;
    compSlider.onValueChange = emitSoundChange;
    reverbSlider.onValueChange = emitSoundChange;
    gateSlider.onValueChange = emitSoundChange;
    transientSlider.onValueChange = emitSoundChange;
    driveSlider.onValueChange = emitSoundChange;
}
} // namespace bbg
