#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <thread>

#include "../Core/ProjectLaneAccess.h"
#include "../Core/Sub808TrackAccess.h"

#include "PluginEditor.h"
#include "../Core/PatternProjectSerialization.h"
#include "../Core/ProjectStateController.h"
#include "../Core/TrackRegistry.h"
#include "../Engine/ExtractPatternBuilder.h"
#include "../Engine/MidiExportEngine.h"
#include "../Engine/PatternPerformanceTransformEngine.h"
#include "../Engine/PatternBlendEngine.h"
#include "../Engine/StyleDefaults.h"
#include "../Engine/SubstyleRuleEnforcer.h"
#include "../Services/TemporaryMidiExportService.h"
#include "../UI/GridEditorComponent.h"
#include "../UI/MainHeaderComponent.h"
#include "../UI/EditorCommandController.h"
#include "../UI/SampleAnalysisPanelComponent.h"
#include "../UI/SoundModuleController.h"
#include "../UI/TrackListComponent.h"
#include "../UI/Vst3GridLiteComponent.h"
#include "../Utils/TimingHelpers.h"

namespace bbg
{
namespace
{
constexpr auto kStateType = "BoomBapState";
constexpr auto kRootSchemaVersion = 4;
constexpr double kEqAnalyzerMinFrequencyHz = 20.0;
constexpr double kEqAnalyzerMaxFrequencyHz = 20000.0;

class ProcessBlockActivityGuard
{
public:
    explicit ProcessBlockActivityGuard(std::atomic<int>& activeProcessBlockCountIn) noexcept
        : activeProcessBlockCount(activeProcessBlockCountIn)
    {
        activeProcessBlockCount.fetch_add(1, std::memory_order_acq_rel);
    }

    ~ProcessBlockActivityGuard()
    {
        activeProcessBlockCount.fetch_sub(1, std::memory_order_acq_rel);
    }

private:
    std::atomic<int>& activeProcessBlockCount;
};

class HeaderOnlyTestEditor final : public juce::AudioProcessorEditor
{
public:
    explicit HeaderOnlyTestEditor(BoomBapGeneratorAudioProcessor& processor)
        : juce::AudioProcessorEditor(&processor)
    {
        addAndMakeVisible(header);
        setSize(1280, 220);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour::fromRGB(15, 17, 21));
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(12);
        header.setBounds(area.removeFromTop(170));
    }

private:
    MainHeaderComponent header;
};

class SplitterHandleComponent final : public juce::Component
{
public:
    std::function<void(const juce::MouseEvent&)> onPress;
    std::function<void(const juce::MouseEvent&)> onDragMove;
    std::function<void(const juce::MouseEvent&)> onRelease;

    explicit SplitterHandleComponent(juce::MouseCursor::StandardCursorType cursorType)
    {
        setMouseCursor(juce::MouseCursor(cursorType));
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

// Standalone keeps the full authoring editor. VST3 intentionally uses a
// different grid surface so DAW workflow stays stable while both formats share
// the same core pattern/generation architecture.
class Vst3SafeHeaderEditor final : public juce::AudioProcessorEditor,
                                   private juce::Timer
{
public:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    explicit Vst3SafeHeaderEditor(BoomBapGeneratorAudioProcessor& processor)
        : juce::AudioProcessorEditor(&processor)
        , audioProcessor(processor)
        , commandController(processor)
        , soundModuleController(processor, soundModule)
    {
        addAndMakeVisible(header);
        addAndMakeVisible(trackListViewport);
        addAndMakeVisible(analysisPanel);
        addAndMakeVisible(soundModule);

        laneGridWorkspace.addAndMakeVisible(trackList);
        laneGridWorkspace.addAndMakeVisible(gridLite);
        trackListViewport.setViewedComponent(&laneGridWorkspace, false);
        trackListViewport.setScrollBarsShown(true, false);

        auto* verticalSplitter = new SplitterHandleComponent(juce::MouseCursor::LeftRightResizeCursor);
        verticalSplitter->onDragMove = [this](const juce::MouseEvent& event)
        {
            if (!event.mouseWasDraggedSinceMouseDown())
                return;

            const auto x = static_cast<int>(event.getEventRelativeTo(this).position.x);
            updateColumnWidthFromScreenX(x);
        };
        verticalSplitterHandle.reset(verticalSplitter);
        addAndMakeVisible(*verticalSplitterHandle);

        auto* horizontalSplitter = new SplitterHandleComponent(juce::MouseCursor::UpDownResizeCursor);
        horizontalSplitter->onDragMove = [this](const juce::MouseEvent& event)
        {
            if (!event.mouseWasDraggedSinceMouseDown())
                return;

            const auto y = static_cast<int>(event.getEventRelativeTo(this).position.y);
            updateTopSectionHeightFromScreenY(y);
        };
        horizontalSplitterHandle.reset(horizontalSplitter);
        addAndMakeVisible(*horizontalSplitterHandle);

        setSize(1460, 860);
        setResizable(true, true);
        setResizeLimits(1180, 720, 2200, 1500);

        header.setStandaloneWindowButtonVisible(false);
        header.setVst3GeneratorChromeEnabled(true);
        header.setHeaderControlsMode(MainHeaderComponent::HeaderControlsMode::Compact);
        header.setGridModeIndicatorText("VST3 generator mode");
        header.zoomSlider.setEnabled(false);
        header.laneHeightSlider.setEnabled(false);
        header.gridResolutionCombo.setEnabled(false);

        trackList.setShowAnalysisPanel(false);
        trackList.setShowSoundPanel(false);
        trackList.setDisplayMode(LaneRackDisplayMode::Full);
        trackList.setVisualStyle(RackVisualStyle::HpdgSoundVst3);
        trackList.setRowHeight(32);
        gridLite.setRowHeight(trackList.getRowHeight());

        soundModuleController.setHostCallbacks([this](const std::function<void()>& mutation, bool refreshTrackRows)
                                               {
                                                   juce::ignoreUnused(refreshTrackRows);
                                                   mutation();
                                                   refreshFromProcessor();
                                               },
                                               [this](bool refreshTrackRows)
                                               {
                                                   juce::ignoreUnused(refreshTrackRows);
                                                   refreshFromProcessor();
                                               });

        setupAttachments();
        wireCallbacks();
        refreshFromProcessor();
        startTimerHz(10);
    }

    ~Vst3SafeHeaderEditor() override
    {
        stopTimer();
    }

    void paint(juce::Graphics& g) override
    {
        juce::ColourGradient bg(juce::Colour::fromRGB(18, 17, 17), 0.0f, 0.0f,
                                juce::Colour::fromRGB(9, 10, 12), 0.0f, static_cast<float>(getHeight()), false);
        bg.addColour(0.18, juce::Colour::fromRGB(34, 24, 19));
        bg.addColour(0.44, juce::Colour::fromRGB(20, 19, 21));
        bg.addColour(0.82, juce::Colour::fromRGB(13, 14, 17));
        g.setGradientFill(bg);
        g.fillAll();
        drawEmberTexture(g, getLocalBounds().toFloat().reduced(10.0f), 0.72f);

        drawPanelShell(g, trackListViewport.getBounds(), "PATTERN RACK", true);
        drawPanelShell(g, analysisPanel.getBounds(), "ANALYSIS", false);
        drawPanelShell(g, soundModule.getBounds(), juce::String(), false);

        g.setColour(juce::Colour::fromRGBA(214, 171, 98, 58));
        g.fillRect(verticalSplitterVisualBounds);
        g.fillRect(horizontalSplitterVisualBounds);
    }

    void resized() override
    {
        auto area = getWorkspaceBounds();
        header.setBounds(area.removeFromTop(header.getPreferredHeight()));
        area.removeFromTop(10);

        const int splitterHitWidth = 10;
        const int splitterVisualWidth = 2;
        const int splitterHitHeight = 10;
        const int splitterVisualHeight = 2;
        const int paneGap = 8;
        const int minLeftWidth = 520;
        const int minRightWidth = 360;
        const int minTopHeight = 250;
        const int minBottomHeight = 230;

        const int maxLeftWidth = juce::jmax(minLeftWidth, area.getWidth() - splitterHitWidth - paneGap - minRightWidth);
        leftColumnWidth = juce::jlimit(minLeftWidth,
                                       maxLeftWidth,
                                       leftColumnWidth > 0
                                           ? leftColumnWidth
                                           : static_cast<int>(std::round(static_cast<double>(area.getWidth()) * 0.52)));

        const int maxTopHeight = juce::jmax(minTopHeight, area.getHeight() - splitterHitHeight - paneGap - minBottomHeight);
        topSectionHeight = juce::jlimit(minTopHeight,
                                        maxTopHeight,
                                        topSectionHeight > 0
                                            ? topSectionHeight
                                            : static_cast<int>(std::round(static_cast<double>(area.getHeight()) * 0.46)));

        auto topArea = area.removeFromTop(topSectionHeight);
        auto horizontalHit = area.removeFromTop(splitterHitHeight);
        area.removeFromTop(paneGap);
        auto bottomArea = area;

        auto topLeft = topArea.removeFromLeft(leftColumnWidth);
        auto verticalHitTop = topArea.removeFromLeft(splitterHitWidth);
        topArea.removeFromLeft(paneGap);
        auto topRight = topArea;

        auto bottomLeft = bottomArea.removeFromLeft(leftColumnWidth);
        auto verticalHitBottom = bottomArea.removeFromLeft(splitterHitWidth);
        bottomArea.removeFromLeft(paneGap);
        auto bottomRight = bottomArea;

        const int verticalHitX = verticalHitTop.getX();
        verticalSplitterHandle->setBounds(verticalHitX,
                                          topLeft.getY(),
                                          splitterHitWidth,
                                          bottomRight.getBottom() - topLeft.getY());
        verticalSplitterVisualBounds = juce::Rectangle<int>(verticalHitX + (splitterHitWidth - splitterVisualWidth) / 2,
                                                            topLeft.getY(),
                                                            splitterVisualWidth,
                                                            bottomRight.getBottom() - topLeft.getY());

        horizontalSplitterHandle->setBounds(topLeft.getX(),
                                            horizontalHit.getY(),
                                            topLeft.getWidth() + splitterHitWidth + paneGap + topRight.getWidth(),
                                            splitterHitHeight);
        horizontalSplitterVisualBounds = juce::Rectangle<int>(topLeft.getX(),
                                                              horizontalHit.getY() + (splitterHitHeight - splitterVisualHeight) / 2,
                                                              topLeft.getWidth() + splitterHitWidth + paneGap + topRight.getWidth(),
                                                              splitterVisualHeight);

        trackListViewport.setBounds(juce::Rectangle<int>(topLeft.getX(),
                                                         topLeft.getY(),
                                                         topLeft.getWidth() + splitterHitWidth + paneGap + topRight.getWidth(),
                                                         topLeft.getHeight()));
        syncRackViewportContentSize();
        analysisPanel.setBounds(bottomLeft);
        soundModule.setBounds(bottomRight);
    }

private:
    static float averageOrZero(const std::vector<float>& values)
    {
        if (values.empty())
            return 0.0f;

        const float sum = std::accumulate(values.begin(), values.end(), 0.0f);
        return sum / static_cast<float>(values.size());
    }

    static PreviewPlaybackMode previewModeFromChoiceId(int id)
    {
        switch (id)
        {
            case 2: return PreviewPlaybackMode::LoopRange;
            case 1:
            default: return PreviewPlaybackMode::FromFlag;
        }
    }

    static int previewModeToChoiceId(PreviewPlaybackMode mode)
    {
        switch (mode)
        {
            case PreviewPlaybackMode::LoopRange: return 2;
            case PreviewPlaybackMode::FromFlag:
            default: return 1;
        }
    }

    void timerCallback() override
    {
        refreshSubstyleBindingForGenre();
        // Keep VST3 behaviour in lockstep with the full Standalone editor:
        // genre/substyle changes must reapply the shared style preset before
        // generation, sample-bank refresh and UI sync.
        audioProcessor.applySelectedStylePreset(false);
        refreshFromProcessor();
    }

    void setupAttachments()
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

        refreshSubstyleBindingForGenre();
    }

    void refreshSubstyleBindingForGenre()
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

        substyleAttachment = std::make_unique<ComboAttachment>(apvts, substyleParamId, header.substyleCombo);
    }

    void wireCallbacks()
    {
        header.onGeneratePressed = [this]
        {
            audioProcessor.applySelectedStylePreset(false);
            audioProcessor.generatePattern();
            refreshFromProcessor();
        };

        header.onMutatePressed = [this]
        {
            audioProcessor.applySelectedStylePreset(false);
            audioProcessor.mutatePattern();
            refreshFromProcessor();
        };

        header.onPlayToggled = [this](bool shouldStart)
        {
            if (shouldStart)
                audioProcessor.startPreview();
            else
                audioProcessor.stopPreview();

            refreshFromProcessor();
        };

        header.barsCombo.onChange = [this]
        {
            const int selectedBars = juce::jmax(1, header.barsCombo.getText().getIntValue());
            if (audioProcessor.getProjectSnapshot().params.bars == selectedBars)
                return;

            audioProcessor.syncBarsFromState();
            refreshFromProcessor();
        };

        header.onStartPlayWithDawToggled = [this](bool enabled)
        {
            audioProcessor.setStartPlayWithDawEnabled(enabled);
            refreshFromProcessor();
        };

        header.onTransportToStart = [this]
        {
            audioProcessor.setPreviewStartStep(0);
            refreshFromProcessor();
        };

        header.onTransportStepBack = [this]
        {
            const auto project = audioProcessor.getProjectSnapshot();
            audioProcessor.setPreviewStartStep(juce::jmax(0, project.previewStartStep - 1));
            refreshFromProcessor();
        };

        header.onTransportStepForward = [this]
        {
            const auto project = audioProcessor.getProjectSnapshot();
            const int maxStep = juce::jmax(0, project.params.bars * 16 - 1);
            audioProcessor.setPreviewStartStep(juce::jmin(maxStep, project.previewStartStep + 1));
            refreshFromProcessor();
        };

        header.onTransportToEnd = [this]
        {
            const auto project = audioProcessor.getProjectSnapshot();
            audioProcessor.setPreviewStartStep(juce::jmax(0, project.params.bars * 16 - 1));
            refreshFromProcessor();
        };

        header.onClearAllPressed = [this]
        {
            const auto project = audioProcessor.getProjectSnapshot();
            for (const auto& lane : project.runtimeLaneProfile.lanes)
                audioProcessor.clearTrack(lane.laneId);

            refreshFromProcessor();
        };

        header.onExportFullPressed = [this]
        {
            commandController.exportFullPattern(this);
        };

        header.onExportLoopWavPressed = [this]
        {
            commandController.exportLoopWav(this, logUiAction);
        };

        header.onDragFullPressed = [this]
        {
            commandController.dragFullPatternTempMidi(logUiAction);
        };

        header.onDragFullGesture = [this]
        {
            commandController.dragFullPatternExternal(this, logUiAction);
        };

        header.onToggleStandaloneWindow = [] {};
        header.onZoomChanged = [](float, float) {};
        header.onGridResolutionChanged = [](int) {};
        header.onHeaderControlsModeChanged = [](MainHeaderComponent::HeaderControlsMode) {};

        header.onPreviewPlaybackModeChanged = [this](int selectedId)
        {
            audioProcessor.setPreviewPlaybackMode(previewModeFromChoiceId(selectedId));
            refreshFromProcessor();
        };

        header.hatFxDensitySlider.onValueChange = [this]
        {
            const float density = static_cast<float>(header.hatFxDensitySlider.getValue());
            const bool locked = header.hatFxDensityLockToggle.getToggleState();
            audioProcessor.setHatFxDragDensity(density, locked);
            refreshFromProcessor();
        };

        header.hatFxDensityLockToggle.onClick = [this]
        {
            const float density = static_cast<float>(header.hatFxDensitySlider.getValue());
            const bool locked = header.hatFxDensityLockToggle.getToggleState();
            audioProcessor.setHatFxDragDensity(density, locked);
            refreshFromProcessor();
        };

        gridLite.onLaneClicked = [this](const RuntimeLaneId& laneId)
        {
            audioProcessor.setSelectedTrack(laneId);
            refreshFromProcessor();
        };

        gridLite.onStepClicked = [this](int step)
        {
            audioProcessor.setPreviewStartStep(step);
            refreshFromProcessor();
        };

        trackList.onRegenerateTrack = [this](const RuntimeLaneId& laneId)
        {
            audioProcessor.applySelectedStylePreset(false);
            audioProcessor.regenerateTrack(laneId);
            refreshFromProcessor();
        };

        trackList.onMutateTrack = [this](const RuntimeLaneId& laneId)
        {
            audioProcessor.applySelectedStylePreset(false);
            audioProcessor.mutateTrack(laneId);
            refreshFromProcessor();
        };

        trackList.onSoloTrack = [this](const RuntimeLaneId& laneId, bool value)
        {
            audioProcessor.setTrackSolo(laneId, value);
            refreshFromProcessor();
        };

        trackList.onMuteTrack = [this](const RuntimeLaneId& laneId, bool value)
        {
            audioProcessor.setTrackMuted(laneId, value);
            refreshFromProcessor();
        };

        trackList.onClearTrack = [this](const RuntimeLaneId& laneId)
        {
            audioProcessor.clearTrack(laneId);
            refreshFromProcessor();
        };

        trackList.onLockTrack = [this](const RuntimeLaneId& laneId, bool value)
        {
            audioProcessor.setTrackLocked(laneId, value);
            refreshFromProcessor();
        };

        trackList.onEnableTrack = [this](const RuntimeLaneId& laneId, bool value)
        {
            audioProcessor.setTrackEnabled(laneId, value);
            refreshFromProcessor();
        };

        trackList.onPrevSampleTrack = [this](const RuntimeLaneId& laneId)
        {
            audioProcessor.selectPreviousLaneSample(laneId);
            refreshFromProcessor();
        };

        trackList.onNextSampleTrack = [this](const RuntimeLaneId& laneId)
        {
            audioProcessor.selectNextLaneSample(laneId);
            refreshFromProcessor();
        };

        trackList.onSampleMenuTrack = [this](const RuntimeLaneId& laneId)
        {
            commandController.showSampleMenu(laneId,
                                             this,
                                             [](const PatternProject&, const PatternProject&)
                                             {
                                             },
                                             [this]()
                                             {
                                                 refreshFromProcessor();
                                             },
                                             logUiAction);
        };

        trackList.onImportMidiLaneTrack = [this](const RuntimeLaneId& laneId)
        {
            commandController.showImportMidiToLaneDialog(laneId,
                                                         this,
                                                         [this](const PatternProject& before, const PatternProject& after, bool)
                                                         {
                                                             juce::ignoreUnused(before);
                                                             audioProcessor.restoreEditorProjectSnapshot(after);
                                                             refreshFromProcessor();
                                                         });
        };

        trackList.onTrackNameClicked = [this](const RuntimeLaneId& laneId)
        {
            audioProcessor.setSelectedTrack(laneId);
            refreshFromProcessor();
        };

        trackList.onDragTrack = [this](const RuntimeLaneId& laneId)
        {
            commandController.dragTrackTempMidi(laneId, logUiAction);
        };

        trackList.onDragTrackGesture = [this](const RuntimeLaneId& laneId)
        {
            commandController.dragTrackExternal(laneId, this, logUiAction);
        };

        trackList.onExportTrack = [this](const RuntimeLaneId& laneId)
        {
            commandController.exportTrack(laneId, this);
        };

        trackList.onLaneVolumeTrack = [this](const RuntimeLaneId& laneId, float volume)
        {
            audioProcessor.setTrackLaneVolume(laneId, volume);
            refreshFromProcessor();
        };

        trackList.onLaneSoundTrack = [this](const RuntimeLaneId& laneId, const SoundLayerState& state)
        {
            SoundLayerState nextState = state;
            const auto project = audioProcessor.getProjectSnapshot();
            for (const auto& track : project.tracks)
            {
                if (track.laneId != laneId)
                    continue;

                nextState.eq = track.sound.eq;
                nextState.compression = track.sound.compression;
                nextState.reverb = track.sound.reverb;
                nextState.monstaFx = track.sound.monstaFx;
                nextState.drumTransient = track.sound.drumTransient;
                nextState.gate = track.sound.gate;
                nextState.transient = track.sound.transient;
                nextState.drive = track.sound.drive;
                break;
            }

            audioProcessor.setTrackSoundLayer(laneId, nextState);
            refreshFromProcessor();
        };

        trackList.onBassKeyChanged = [this](int choice)
        {
            audioProcessor.setBassKeyRootChoice(choice);
            refreshFromProcessor();
        };

        trackList.onBassScaleChanged = [this](int choice)
        {
            audioProcessor.setBassScaleModeChoice(choice);
            refreshFromProcessor();
        };

        analysisPanel.onAnalysisSourceChanged = [this](SampleAnalysisRequest::SourceType source)
        {
            auto request = audioProcessor.getSampleAnalysisRequest();
            request.source = source;
            audioProcessor.setSampleAnalysisRequest(request);
            refreshFromProcessor();
        };

        analysisPanel.onAnalysisModeChanged = [this](AnalysisMode mode)
        {
            audioProcessor.setAnalysisMode(mode);
            refreshFromProcessor();
        };

        analysisPanel.onSampleApplyModeChanged = [this](SampleApplyMode mode)
        {
            audioProcessor.setSampleApplyMode(mode);
            refreshFromProcessor();
        };

        analysisPanel.onAnalysisBarsChanged = [this](int bars)
        {
            auto request = audioProcessor.getSampleAnalysisRequest();
            request.barsToCapture = juce::jlimit(2, 16, bars);
            audioProcessor.setSampleAnalysisRequest(request);
            refreshFromProcessor();
        };

        analysisPanel.onAnalysisTempoHandlingChanged = [this](SampleAnalysisRequest::TempoHandling tempoHandling)
        {
            auto request = audioProcessor.getSampleAnalysisRequest();
            request.tempoHandling = tempoHandling;
            audioProcessor.setSampleAnalysisRequest(request);
            refreshFromProcessor();
        };

        analysisPanel.onAnalysisReactivityChanged = [this](float value)
        {
            audioProcessor.setSampleReactivity(value);
            refreshFromProcessor();
        };

        analysisPanel.onSupportVsContrastChanged = [this](float value)
        {
            audioProcessor.setSupportVsContrast(value);
            refreshFromProcessor();
        };

        analysisPanel.onChooseAnalysisFile = [this]
        {
            auto safeEditor = juce::Component::SafePointer<Vst3SafeHeaderEditor>(this);
            auto chooser = std::make_shared<juce::FileChooser>("Select audio file for analysis",
                                                               juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                                                               "*.wav;*.aif;*.aiff;*.flac;*.mp3");

            chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                 [safeEditor, chooser](const juce::FileChooser& fc)
                                 {
                                     const auto selected = fc.getResult();
                                     if (safeEditor == nullptr || selected == juce::File())
                                         return;

                                     auto request = safeEditor->audioProcessor.getSampleAnalysisRequest();
                                     request.source = SampleAnalysisRequest::SourceType::AudioFile;
                                     request.audioFile = selected;
                                     safeEditor->audioProcessor.setSampleAnalysisRequest(request);
                                     safeEditor->refreshFromProcessor();
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
            refreshFromProcessor();
        };

        analysisPanel.onRunAnalysis = [this]
        {
            juce::String error;
            const bool ok = audioProcessor.analyzeCurrentSampleSource(&error);
            if (!ok && error.isNotEmpty())
                juce::Logger::writeToLog("[HPDG_VST3_UI] analysis error: " + error);
            refreshFromProcessor();
        };
    }

    void refreshFromProcessor()
    {
        const auto project = audioProcessor.getProjectSnapshot();
        const auto analysisRequest = audioProcessor.getSampleAnalysisRequest();
        const auto analysisResult = audioProcessor.getSampleAnalysisResult();
        const auto sampleContext = audioProcessor.getSampleAwareGenerationContext();
        const bool analysisReady = audioProcessor.isSampleAnalysisReady();
        const auto analysisMode = audioProcessor.getAnalysisMode();
        const auto selectedTrack = project.tracks.empty()
            ? RuntimeLaneId{}
            : project.tracks[static_cast<size_t>(juce::jlimit(0,
                                                              static_cast<int>(project.tracks.size()) - 1,
                                                              project.selectedTrackIndex))].laneId;

        header.setPreviewPlaying(audioProcessor.isPreviewPlaying());
        header.setStartPlayWithDawEnabled(audioProcessor.isStartPlayWithDawEnabled());
        header.setPreviewPlaybackModeId(previewModeToChoiceId(audioProcessor.getPreviewPlaybackMode()));
        header.setHatFxDensityState(audioProcessor.getHatFxDragDensity(), audioProcessor.isHatFxDragDensityLocked());

        gridLite.setProject(project);
        gridLite.setLaneDisplayOrder(project.runtimeLaneOrder);
        gridLite.setSelectedTrack(selectedTrack);
        gridLite.setPlayheadStep(audioProcessor.getPreviewPlayheadStep());
        gridLite.setPreviewStartStep(project.previewStartStep);
        gridLite.setLoopRegion(audioProcessor.getPreviewLoopRegion());
        gridLite.setRowHeight(trackList.getRowHeight());

        trackList.setTracks(project.runtimeLaneProfile,
                            project.tracks,
                            audioProcessor.getBassKeyRootChoice(),
                            audioProcessor.getBassScaleModeChoice());
        trackList.setLaneDisplayOrder(project.runtimeLaneOrder);
        trackList.setHatFxDragUiState(audioProcessor.getHatFxDragDensity(),
                                      audioProcessor.isHatFxDragDensityLocked());

        juce::String status;
        juce::String details;
        if (analysisReady)
        {
            status = "BPM " + juce::String(analysisResult.detectedBpm, 1)
                + (analysisResult.bpmReliable ? " (reliable)" : " (estimate)")
                + " | bars " + juce::String(analysisResult.analyzedBars)
                + " | boundaries " + juce::String(static_cast<int>(analysisResult.phraseBoundaryBars.size()));

            details = "Energy " + juce::String(averageOrZero(analysisResult.energyPerStep), 2)
                + " | onset " + juce::String(averageOrZero(analysisResult.onsetStrengthPerStep), 2)
                + " | density bars " + juce::String(static_cast<int>(analysisResult.densityPerBar.size()))
                + "\nFlags: sparse=" + juce::String(analysisResult.sparse ? "yes" : "no")
                + " dense=" + juce::String(analysisResult.dense ? "yes" : "no")
                + " transientRich=" + juce::String(analysisResult.transientRich ? "yes" : "no")
                + " loopLike=" + juce::String(analysisResult.loopLike ? "yes" : "no");
        }

        juce::String generationDebug = audioProcessor.getGenerationDebugSummary();
        const auto featureMap = audioProcessor.getAudioFeatureMap();
        if (analysisReady && !featureMap.steps.empty())
        {
            int strongestStep = 0;
            float strongestOnset = featureMap.steps.front().onset;
            for (int i = 1; i < static_cast<int>(featureMap.steps.size()); ++i)
            {
                if (featureMap.steps[static_cast<size_t>(i)].onset > strongestOnset)
                {
                    strongestOnset = featureMap.steps[static_cast<size_t>(i)].onset;
                    strongestStep = i;
                }
            }

            generationDebug += "\nPeak onset step: " + juce::String(strongestStep + 1)
                + " / " + juce::String(static_cast<int>(featureMap.steps.size()))
                + " (" + juce::String(strongestOnset, 2) + ")";
        }

        analysisPanel.setPanelState(analysisRequest.source,
                                    analysisMode,
                                    sampleContext.applyMode,
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

        soundModuleController.sync(project);
        soundModule.setEqDisplayAnalyzerState(audioProcessor.getEqDisplayAnalyzerState());
        syncRackViewportContentSize();
    }

    void syncRackViewportContentSize()
    {
        if (trackListViewport.getWidth() <= 0 || trackListViewport.getHeight() <= 0)
            return;

        const int contentWidth = juce::jmax(1, trackListViewport.getMaximumVisibleWidth());
        const int contentHeight = juce::jmax(trackList.getLaneSectionHeight(),
                                             gridLite.getPreferredContentHeight());
        const int gridGap = 8;
        const int gridWidth = juce::jmax(300, contentWidth - leftColumnWidth - gridGap);

        trackList.setBounds(0, 0, leftColumnWidth, contentHeight);
        gridLite.setBounds(leftColumnWidth + gridGap, 0, gridWidth, contentHeight);
        laneGridWorkspace.setSize(contentWidth, contentHeight);
    }

    void drawPanelShell(juce::Graphics& g, juce::Rectangle<int> childBounds, const juce::String& title, bool emphasise) const
    {
        if (childBounds.isEmpty())
            return;

        auto shell = childBounds.expanded(6);
        const auto shellF = shell.toFloat();
        juce::ColourGradient fill(juce::Colour::fromRGB(34, 25, 19), shellF.getTopLeft(),
                                  juce::Colour::fromRGB(14, 15, 17), shellF.getBottomLeft(), false);
        fill.addColour(0.18, juce::Colour::fromRGB(52, 34, 22));
        fill.addColour(0.52, juce::Colour::fromRGB(24, 22, 22));
        fill.addColour(1.0, juce::Colour::fromRGB(13, 13, 15));
        g.setGradientFill(fill);
        g.fillRoundedRectangle(shellF, 12.0f);
        drawEmberTexture(g, shellF.reduced(5.0f), emphasise ? 0.62f : 0.38f);

        g.setColour(juce::Colour::fromRGBA(255, 255, 255, 10));
        g.drawRoundedRectangle(shellF, 12.0f, 1.0f);

        g.setColour(juce::Colour::fromRGBA(214, 171, 98, emphasise ? 150 : 86));
        g.drawRoundedRectangle(shellF.reduced(0.5f), 12.0f, 1.0f);

        if (title.isEmpty())
            return;

        auto titleBounds = shell.removeFromTop(22).reduced(12, 2);
        g.setColour(juce::Colour::fromRGB(239, 202, 132));
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.drawText(title, titleBounds, juce::Justification::centredLeft, true);

        g.setColour(juce::Colour::fromRGBA(255, 255, 255, 12));
        g.drawHorizontalLine(titleBounds.getBottom() + 2,
                             static_cast<float>(titleBounds.getX()),
                             static_cast<float>(shell.getRight() - 12));
    }

    void drawEmberTexture(juce::Graphics& g, juce::Rectangle<float> area, float alphaScale) const
    {
        static const juce::File kTextureFile("C:/Users/ham/Documents/DRUMENGINE/Assets/hpdg_ember_texture.png");
        if (kTextureFile.existsAsFile())
        {
            if (auto texture = juce::ImageCache::getFromFile(kTextureFile); texture.isValid())
            {
                g.saveState();
                g.reduceClipRegion(area.getSmallestIntegerContainer());
                g.setOpacity(0.20f * alphaScale);
                g.drawImage(texture,
                            area.getX(),
                            area.getY(),
                            area.getWidth(),
                            area.getHeight(),
                            0.0f,
                            0.0f,
                            static_cast<float>(texture.getWidth()),
                            static_cast<float>(texture.getHeight()));
                g.restoreState();
            }
        }

        juce::Random rng(0x48504447);

        for (int i = 0; i < 40; ++i)
        {
            const float x = area.getX() + rng.nextFloat() * area.getWidth();
            const float y = area.getY() + rng.nextFloat() * area.getHeight();
            const float w = 58.0f + rng.nextFloat() * 240.0f;
            const float h = 10.0f + rng.nextFloat() * 46.0f;
            const auto alpha = static_cast<juce::uint8>((0.014f + rng.nextFloat() * 0.032f) * alphaScale * 255.0f);
            g.setColour(juce::Colour::fromRGBA(255, 167, 72, alpha));
            g.fillEllipse(x, y, w, h);
        }

        for (int i = 0; i < 24; ++i)
        {
            juce::Path smoke;
            smoke.startNewSubPath(area.getX() + rng.nextFloat() * area.getWidth(),
                                  area.getY() + rng.nextFloat() * area.getHeight());
            for (int segment = 0; segment < 4; ++segment)
            {
                smoke.quadraticTo(area.getX() + rng.nextFloat() * area.getWidth(),
                                  area.getY() + rng.nextFloat() * area.getHeight(),
                                  area.getX() + rng.nextFloat() * area.getWidth(),
                                  area.getY() + rng.nextFloat() * area.getHeight());
            }

            const auto alpha = static_cast<juce::uint8>((0.010f + rng.nextFloat() * 0.016f) * alphaScale * 255.0f);
            g.setColour(juce::Colour::fromRGBA(255, 223, 176, alpha));
            g.strokePath(smoke, juce::PathStrokeType(1.6f + rng.nextFloat() * 2.4f,
                                                     juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
        }
    }

    juce::Rectangle<int> getWorkspaceBounds() const
    {
        return getLocalBounds().reduced(12);
    }

    void updateColumnWidthFromScreenX(int x)
    {
        auto body = getWorkspaceBounds();
        body.removeFromTop(header.getPreferredHeight() + 10);
        const int maxLeftWidth = juce::jmax(520, body.getWidth() - 10 - 8 - 360);
        leftColumnWidth = juce::jlimit(520, maxLeftWidth, x - body.getX());
        resized();
        repaint();
    }

    void updateTopSectionHeightFromScreenY(int y)
    {
        auto body = getWorkspaceBounds();
        body.removeFromTop(header.getPreferredHeight() + 10);
        const int maxTopHeight = juce::jmax(250, body.getHeight() - 10 - 8 - 230);
        topSectionHeight = juce::jlimit(250, maxTopHeight, y - body.getY());
        resized();
        repaint();
    }

    BoomBapGeneratorAudioProcessor& audioProcessor;
    EditorCommandController commandController;
    std::function<void(const juce::String&)> logUiAction = [](const juce::String& message)
    {
        juce::Logger::writeToLog("[HPDG_VST3_UI] " + message);
    };

    MainHeaderComponent header;
    juce::Viewport trackListViewport;
    juce::Component laneGridWorkspace;
    TrackListComponent trackList;
    Vst3GridLiteComponent gridLite;
    SampleAnalysisPanelComponent analysisPanel;
    SoundModuleComponent soundModule;
    SoundModuleController soundModuleController;
    std::unique_ptr<SplitterHandleComponent> verticalSplitterHandle;
    std::unique_ptr<SplitterHandleComponent> horizontalSplitterHandle;
    juce::Rectangle<int> verticalSplitterVisualBounds;
    juce::Rectangle<int> horizontalSplitterVisualBounds;
    int leftColumnWidth = 760;
    int topSectionHeight = 320;

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
    std::unique_ptr<SliderAttachment> seedAttachment;
    std::unique_ptr<ButtonAttachment> seedLockAttachment;
    std::unique_ptr<SliderAttachment> masterVolumeAttachment;
    int lastGenreChoice = -1;
};

class GridOnlyTestEditor final : public juce::AudioProcessorEditor
{
public:
    explicit GridOnlyTestEditor(BoomBapGeneratorAudioProcessor& processor)
        : juce::AudioProcessorEditor(&processor)
    {
        addAndMakeVisible(grid);
        grid.setLaneHeight(30);
        grid.setStepWidth(20.0f);

        setSize(860, 760);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour::fromRGB(15, 17, 21));
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(12);
        grid.setBounds(area);
    }

private:
    GridEditorComponent grid;
};

class PassiveGridTestComponent final : public GridEditorComponent
{
public:
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour::fromRGB(24, 27, 33));
    }

    void mouseMove(const juce::MouseEvent&) override {}
    void mouseExit(const juce::MouseEvent&) override {}
};

class PassiveGridOnlyTestEditor final : public juce::AudioProcessorEditor
{
public:
    explicit PassiveGridOnlyTestEditor(BoomBapGeneratorAudioProcessor& processor)
        : juce::AudioProcessorEditor(&processor)
    {
        addAndMakeVisible(grid);
        setSize(860, 760);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour::fromRGB(15, 17, 21));
    }

    void resized() override
    {
        grid.setBounds(getLocalBounds().reduced(12));
    }

private:
    PassiveGridTestComponent grid;
};

inline int floorDiv(int a, int b)
{
    const int q = a / b;
    const int r = a % b;
    return (r != 0 && ((r > 0) != (b > 0))) ? (q - 1) : q;
}

int barsFromChoiceIndex(int choice)
{
    switch (choice)
    {
        case 0: return 1;
        case 1: return 2;
        case 2: return 4;
        case 3: return 8;
        case 4: return 16;
        default: return 2;
    }
}

int choiceIndexFromBars(int bars)
{
    switch (bars)
    {
        case 1: return 0;
        case 2: return 1;
        case 4: return 2;
        case 8: return 3;
        case 16: return 4;
        default:
            if (bars <= 1)
                return 0;
            if (bars <= 2)
                return 1;
            if (bars <= 4)
                return 2;
            if (bars <= 8)
                return 3;
            return 4;
    }
}

SampleApplyMode sampleApplyModeFromState(const juce::AudioProcessorValueTreeState& apvts)
{
    const auto* value = apvts.getRawParameterValue(ParamIds::sampleApplyMode);
    const int choice = value != nullptr ? static_cast<int>(value->load()) : choiceIndexFromSampleApplyMode(SampleApplyMode::Blend);
    return sampleApplyModeFromChoiceIndex(choice);
}

juce::StringArray makeValidChoiceParameterOptions(juce::StringArray options)
{
    if (options.isEmpty())
        options.add("Main");

    // JUCE AudioParameterChoice requires more than one item even when the
    // product currently exposes only a single real style.
    if (options.size() == 1)
        options.add(options[0]);

    return options;
}

int clampDrillSubstyleChoice(int choice)
{
    const int lastValidIndex = juce::jmax(0, getDrillSubstyleNames().size() - 1);
    return juce::jlimit(0, lastValidIndex, choice);
}

bool normalizedValuesEqual(float lhs, float rhs) noexcept
{
    return std::abs(lhs - rhs) <= 1.0e-6f;
}

GenreType genreFromChoice(int choice)
{
    switch (choice)
    {
        case 1: return GenreType::Rap;
        case 2: return GenreType::Trap;
        case 3: return GenreType::Drill;
        default: return GenreType::BoomBap;
    }
}

float playbackRateForTrackPitch(TrackType track, int pitch)
{
    const auto* info = TrackRegistry::find(track);
    const int basePitch = info != nullptr ? info->defaultMidiNote : 36;
    const int clampedPitch = juce::jlimit(0, 127, pitch);
    return std::pow(2.0f, static_cast<float>(clampedPitch - basePitch) / 12.0f);
}

bool isBpmLocked(const juce::AudioProcessorValueTreeState& apvts)
{
    const auto* bpmLockValue = apvts.getRawParameterValue(ParamIds::bpmLock);
    return bpmLockValue != nullptr && bpmLockValue->load() > 0.5f;
}

bool shouldIncludeTrackForPlayback(const PatternProject& project, const TrackState& track)
{
    if (!track.enabled || track.muted)
        return false;

    const bool hasSolo = std::any_of(project.tracks.begin(), project.tracks.end(), [](const TrackState& t)
    {
        return t.solo;
    });

    if (hasSolo && !track.solo)
        return false;

    return true;
}

std::optional<juce::Range<int>> activePreviewLoopTicks(const PatternProject& project)
{
    if (project.previewPlaybackMode != PreviewPlaybackMode::LoopRange || !project.previewLoopTicks.has_value())
        return std::nullopt;

    if (project.previewLoopTicks->getLength() <= 0)
        return std::nullopt;

    return project.previewLoopTicks;
}

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
    const auto line = juce::Time::getCurrentTime().toString(true, true) + " | PROCESSOR | " + message + "\n";
    file.appendText(line, false, false, "\n");
}

bool soundLayerHasActiveEq(const SoundLayerState& state)
{
    return std::any_of(state.eq.bands.begin(), state.eq.bands.end(), [](const EqBandState& band)
    {
        return band.enabled;
    });
}

bool soundLayerHasActiveReverb(const SoundLayerState& state)
{
    return state.reverb > 0.001f || isDrumReverbAudiblyActive(state.drumReverb);
}

bool soundLayerHasActiveStereoField(const SoundLayerState& state)
{
    return isStereoFieldAudiblyActive(state);
}

bool soundLayerHasActiveTransient(const SoundLayerState& state)
{
    return isDrumTransientAudiblyActive(state.drumTransient);
}

bool soundLayerHasActiveMonstaFx(const SoundLayerState& state)
{
    return isMonstaFxAudiblyActive(state.monstaFx);
}

bool soundLayerNeedsSeparatedRender(const SoundLayerState& state)
{
    if (soundLayerHasActiveEq(state))
        return true;

    if (state.compression > 0.001f || isCompressorAudiblyActive(state.compressor))
        return true;

    if (soundLayerHasActiveReverb(state))
        return true;

    if (soundLayerHasActiveTransient(state))
        return true;

    if (soundLayerHasActiveMonstaFx(state))
        return true;

    return soundLayerHasActiveStereoField(state);
}

float envelopeTimeCoefficient(double sampleRate, float timeMs)
{
    const float clampedMs = juce::jmax(0.1f, timeMs);
    const float sr = static_cast<float>(sampleRate > 1000.0 ? sampleRate : 44100.0);
    return std::exp(-1.0f / (0.001f * clampedMs * sr));
}

float followEnvelope(float input, float current, float attackCoeff, float releaseCoeff)
{
    const float coeff = input > current ? attackCoeff : releaseCoeff;
    return input + coeff * (current - input);
}

float softLimitSample(float sample, float ceiling, float drive)
{
    const float safeCeiling = juce::jmax(0.1f, ceiling);
    const float shapedDrive = juce::jmax(1.0f, drive);
    const float normalized = sample / safeCeiling;
    const float normalizer = juce::jmax(0.001f, std::tanh(shapedDrive));
    return std::tanh(normalized * shapedDrive) / normalizer * safeCeiling;
}

double eqAnalyzerFrequencyFromNormalized(double normalized)
{
    const double minLog = std::log10(kEqAnalyzerMinFrequencyHz);
    const double maxLog = std::log10(kEqAnalyzerMaxFrequencyHz);
    return std::pow(10.0, minLog + juce::jlimit(0.0, 1.0, normalized) * (maxLog - minLog));
}

juce::String genreDisplayName(GenreType genre)
{
    switch (genre)
    {
        case GenreType::Rap: return "Rap";
        case GenreType::Trap: return "Trap";
        case GenreType::Drill: return "Drill";
        case GenreType::BoomBap:
        default: return "Boom Bap";
    }
}

juce::String analysisModeDisplayName(AnalysisMode mode)
{
    switch (mode)
    {
        case AnalysisMode::AnalyzeOnly: return "Analyze Only";
        case AnalysisMode::GenerateFromSample: return "Generate From Sample";
        case AnalysisMode::ExtractFromSample: return "Extract / Copy";
        case AnalysisMode::Off:
        default: return "Off";
    }
}

juce::String sampleApplySummaryLine(const SampleAwareGenerationContext& context)
{
    return "Sample apply: " + sampleApplyModeDisplayName(context.applyMode)
        + " | " + describeSampleApplyWeights(context.applyWeights);
}

bool matchesExtractLane(TrackType targetLane, TrackType eventLane)
{
    return targetLane == eventLane;
}

std::vector<NoteEvent> noteEventsForLane(const std::vector<TranscribedEvent>& events, TrackType lane)
{
    std::vector<NoteEvent> notes;
    for (const auto& event : events)
    {
        if (!matchesExtractLane(lane, event.lane))
            continue;

        NoteEvent note;
        note.pitch = event.pitch;
        note.step = event.step;
        note.length = juce::jmax(1, event.lengthSteps);
        note.velocity = juce::jlimit(1, 127, event.velocity);
        note.isGhost = event.ghost;
        note.semanticRole = "sample_copy";
        notes.push_back(note);
    }

    std::sort(notes.begin(), notes.end(), [](const NoteEvent& left, const NoteEvent& right)
    {
        if (left.step != right.step)
            return left.step < right.step;
        if (left.pitch != right.pitch)
            return left.pitch < right.pitch;
        return left.velocity > right.velocity;
    });

    notes.erase(std::unique(notes.begin(), notes.end(), [](const NoteEvent& left, const NoteEvent& right)
    {
        return left.step == right.step && left.pitch == right.pitch;
    }), notes.end());
    return notes;
}

std::vector<Sub808NoteEvent> subNotesForEvents(const std::vector<TranscribedEvent>& events)
{
    std::vector<Sub808NoteEvent> notes;
    for (const auto& event : events)
    {
        if (event.lane != TrackType::Sub808)
            continue;

        Sub808NoteEvent note;
        note.pitch = event.pitch;
        note.step = event.step;
        note.length = juce::jmax(1, event.lengthSteps);
        note.velocity = juce::jlimit(1, 127, event.velocity);
        note.semanticRole = "sample_copy";
        notes.push_back(note);
    }

    std::sort(notes.begin(), notes.end(), [](const Sub808NoteEvent& left, const Sub808NoteEvent& right)
    {
        if (left.step != right.step)
            return left.step < right.step;
        if (left.pitch != right.pitch)
            return left.pitch < right.pitch;
        return left.velocity > right.velocity;
    });

    notes.erase(std::unique(notes.begin(), notes.end(), [](const Sub808NoteEvent& left, const Sub808NoteEvent& right)
    {
        return left.step == right.step && left.pitch == right.pitch;
    }), notes.end());
    return notes;
}

juce::String referenceZeroReasonDisplayName(StyleLabReferenceZeroReason reason)
{
    switch (reason)
    {
        case StyleLabReferenceZeroReason::RefsDisabledByStyleSwitch: return "refs disabled by style switch";
        case StyleLabReferenceZeroReason::LaneMappingEmpty: return "lane mapping empty";
        case StyleLabReferenceZeroReason::IncompatibleReferenceSpan: return "incompatible reference span";
        case StyleLabReferenceZeroReason::FilteredByDensity: return "filtered by density";
        case StyleLabReferenceZeroReason::ParsingFailed: return "parsing failed";
        case StyleLabReferenceZeroReason::NoRefsSelected: return "no refs selected";
        case StyleLabReferenceZeroReason::None:
        default: return {};
    }
}

juce::String selectedSubstyleDisplayName(const GeneratorParams& params)
{
    return getGenreStyleDefaults(params.genre, getSelectedSubstyleIndex(params)).substyleName;
}

juce::String formatUnitPercent(float value)
{
    return juce::String(static_cast<int>(std::round(juce::jlimit(0.0f, 1.0f, value) * 100.0f))) + "%";
}

juce::String formatDeltaText(int delta)
{
    if (delta > 0)
        return "+" + juce::String(delta);

    return juce::String(delta);
}

juce::String phraseSummaryForDisplay(const juce::String& summary)
{
    if (summary.trim().isEmpty())
        return "none";

    juce::StringArray roles = juce::StringArray::fromTokens(summary, "|", "");
    for (auto& role : roles)
        role = role.trim();
    roles.removeEmptyStrings();

    return roles.isEmpty() ? summary : roles.joinIntoString(" -> ");
}

juce::String joinLimited(const juce::StringArray& items, int maxItems)
{
    if (items.isEmpty())
        return {};

    juce::StringArray limited;
    for (int index = 0; index < items.size() && index < maxItems; ++index)
        limited.add(items[index]);

    auto text = limited.joinIntoString(", ");
    if (items.size() > maxItems)
        text += " +" + juce::String(items.size() - maxItems) + " more";
    return text;
}

const TrackState* findTrackInProject(const PatternProject& project, TrackType type)
{
    auto it = std::find_if(project.tracks.begin(), project.tracks.end(), [type](const TrackState& track)
    {
        return track.type == type;
    });

    return it != project.tracks.end() ? &(*it) : nullptr;
}

std::vector<NoteEvent> effectiveNotesForTrack(const TrackState& track)
{
    if (track.type == TrackType::Sub808)
        return toLegacyNoteEvents(sub808NotesForRead(track));

    return track.notes;
}

bool noteEventsEqual(const NoteEvent& left, const NoteEvent& right)
{
    return left.pitch == right.pitch
        && left.step == right.step
        && left.length == right.length
        && left.velocity == right.velocity
        && left.microOffset == right.microOffset
        && left.isGhost == right.isGhost
        && left.semanticRole == right.semanticRole
        && left.isSlide == right.isSlide
        && left.isLegato == right.isLegato
        && left.glideToNext == right.glideToNext;
}

bool tracksHaveSameMusicalContent(const TrackState* beforeTrack, const TrackState* afterTrack)
{
    if (beforeTrack == nullptr || afterTrack == nullptr)
        return beforeTrack == afterTrack;

    const auto beforeNotes = effectiveNotesForTrack(*beforeTrack);
    const auto afterNotes = effectiveNotesForTrack(*afterTrack);
    if (beforeNotes.size() != afterNotes.size())
        return false;

    for (size_t index = 0; index < beforeNotes.size(); ++index)
    {
        if (!noteEventsEqual(beforeNotes[index], afterNotes[index]))
            return false;
    }

    return true;
}

struct ReferenceCorpusMetrics
{
    bool available = false;
    int sourceCount = 0;
    int variantCount = 0;
    int totalBars = 0;
    float avgHitsPerBar = 0.0f;
    float avgVelocity = 0.0f;
    float anchorRatio = 0.0f;
    float supportRatio = 0.0f;
    float lateRatio = 0.0f;
    juce::StringArray sourceIds;
};

ReferenceCorpusMetrics summarizeHatCorpus(const StyleInfluenceState& styleInfluence)
{
    ReferenceCorpusMetrics metrics;
    const auto& corpus = styleInfluence.referenceHatCorpus;
    metrics.available = corpus.available && !corpus.variants.empty();
    metrics.variantCount = static_cast<int>(corpus.variants.size());

    float totalNotes = 0.0f;
    float totalVelocity = 0.0f;
    float anchorNotes = 0.0f;
    float supportNotes = 0.0f;
    float lateNotes = 0.0f;

    for (const auto& variant : corpus.variants)
    {
        if (!variant.available || variant.barMaps.empty())
            continue;

        if (variant.sourceId.isNotEmpty())
            metrics.sourceIds.addIfNotAlreadyThere(variant.sourceId);

        for (const auto& barMap : variant.barMaps)
        {
            ++metrics.totalBars;
            for (const auto& note : barMap.notes)
            {
                const int step16 = std::clamp(note.tickInBar / 120, 0, 15);
                totalNotes += 1.0f;
                totalVelocity += static_cast<float>(note.velocity);
                if ((step16 % 2) == 0)
                    anchorNotes += 1.0f;
                else
                    supportNotes += 1.0f;
                if (step16 >= 12)
                    lateNotes += 1.0f;
            }
        }
    }

    if (metrics.sourceIds.isEmpty() && styleInfluence.referenceHatSkeleton.available && styleInfluence.referenceHatSkeleton.sourceId.isNotEmpty())
        metrics.sourceIds.add(styleInfluence.referenceHatSkeleton.sourceId);

    metrics.sourceCount = juce::jmax(corpus.sourceReferenceCount, metrics.sourceIds.size(), metrics.variantCount);

    if (totalNotes > 0.0f)
    {
        metrics.avgVelocity = totalVelocity / totalNotes;
        metrics.anchorRatio = anchorNotes / totalNotes;
        metrics.supportRatio = supportNotes / totalNotes;
        metrics.lateRatio = lateNotes / totalNotes;
    }

    if (metrics.totalBars > 0)
        metrics.avgHitsPerBar = totalNotes / static_cast<float>(metrics.totalBars);

    return metrics;
}

ReferenceCorpusMetrics summarizeKickCorpus(const StyleInfluenceState& styleInfluence)
{
    ReferenceCorpusMetrics metrics;
    const auto& corpus = styleInfluence.referenceKickCorpus;
    metrics.available = corpus.available && !corpus.variants.empty();
    metrics.variantCount = static_cast<int>(corpus.variants.size());

    float totalNotes = 0.0f;
    float totalVelocity = 0.0f;
    float anchorNotes = 0.0f;
    float supportNotes = 0.0f;
    float lateNotes = 0.0f;

    for (const auto& variant : corpus.variants)
    {
        if (!variant.available || variant.barPatterns.empty())
            continue;

        for (const auto& barPattern : variant.barPatterns)
        {
            ++metrics.totalBars;
            for (const auto& note : barPattern.notes)
            {
                totalNotes += 1.0f;
                totalVelocity += static_cast<float>(note.velocity);
                if (note.step16 == 0 || note.step16 == 8)
                    anchorNotes += 1.0f;
                else
                    supportNotes += 1.0f;
                if (note.step16 >= 11)
                    lateNotes += 1.0f;
            }
        }
    }

    metrics.sourceCount = juce::jmax(corpus.sourceReferenceCount, metrics.variantCount);

    if (totalNotes > 0.0f)
    {
        metrics.avgVelocity = totalVelocity / totalNotes;
        metrics.anchorRatio = anchorNotes / totalNotes;
        metrics.supportRatio = supportNotes / totalNotes;
        metrics.lateRatio = lateNotes / totalNotes;
    }

    if (metrics.totalBars > 0)
        metrics.avgHitsPerBar = totalNotes / static_cast<float>(metrics.totalBars);

    return metrics;
}

struct LaneMetrics
{
    int noteCount = 0;
    int anchorCount = 0;
    int supportCount = 0;
    int lateCount = 0;
    int ghostCount = 0;
    int slideCount = 0;
    float avgVelocity = 0.0f;
};

LaneMetrics analyzeLaneMetrics(const TrackState& track)
{
    LaneMetrics metrics;
    const auto notes = effectiveNotesForTrack(track);
    metrics.noteCount = static_cast<int>(notes.size());
    if (notes.empty())
        return metrics;

    int velocitySum = 0;
    for (const auto& note : notes)
    {
        velocitySum += note.velocity;
        const int stepInBar = ((note.step % 16) + 16) % 16;

        switch (track.type)
        {
            case TrackType::Kick:
            case TrackType::GhostKick:
            case TrackType::Sub808:
                if (stepInBar == 0 || stepInBar == 8)
                    ++metrics.anchorCount;
                else
                    ++metrics.supportCount;
                break;

            case TrackType::Snare:
            case TrackType::ClapGhostSnare:
                if (stepInBar == 4 || stepInBar == 12)
                    ++metrics.anchorCount;
                else
                    ++metrics.supportCount;
                break;

            default:
                if ((stepInBar % 2) == 0)
                    ++metrics.anchorCount;
                else
                    ++metrics.supportCount;
                break;
        }

        if (stepInBar >= 12)
            ++metrics.lateCount;
        if (note.isGhost || track.type == TrackType::GhostKick)
            ++metrics.ghostCount;
        if (note.isSlide || note.isLegato || note.glideToNext)
            ++metrics.slideCount;
    }

    metrics.avgVelocity = static_cast<float>(velocitySum) / static_cast<float>(metrics.noteCount);
    return metrics;
}

juce::String laneReasonForTrack(const PatternProject& project,
                               const TrackState& track,
                               const LaneMetrics& metrics,
                               bool hatRefsAvailable,
                               bool kickRefsAvailable)
{
    switch (track.type)
    {
        case TrackType::Kick:
            return "anchors " + juce::String(metrics.anchorCount)
                + ", support " + juce::String(metrics.supportCount)
                + (kickRefsAvailable
                       ? "; kick refs biased anchor/pickup balance and velocity."
                       : "; driven by style kick density and pocket rules.");

        case TrackType::GhostKick:
            return "support punches " + juce::String(metrics.noteCount)
                + ", late-bar moves " + juce::String(metrics.lateCount)
                + (kickRefsAvailable
                       ? "; ghost lane shadowed kick-reference punctuation."
                       : "; support lane came from style defaults.");

        case TrackType::Snare:
            return "backbeats " + juce::String(metrics.anchorCount)
                + ", extra hits " + juce::String(metrics.supportCount)
                + "; kept the readable backbeat intact.";

        case TrackType::ClapGhostSnare:
            return "support/ghost hits " + juce::String(metrics.noteCount)
                + ", ghosts " + juce::String(metrics.ghostCount)
                + "; layered around the snare pocket.";

        case TrackType::HiHat:
            if (project.params.genre == GenreType::Trap)
            {
                return "grid anchors " + juce::String(metrics.anchorCount)
                    + ", fast support " + juce::String(metrics.supportCount)
                    + (hatRefsAvailable
                           ? "; hat refs biased subdivision shape and velocity."
                           : "; subdivision came from trap hat density rules.");
            }

            if (project.params.genre == GenreType::Drill)
            {
                return "motif anchors " + juce::String(metrics.anchorCount)
                    + ", motion hits " + juce::String(metrics.supportCount)
                    + (hatRefsAvailable
                           ? "; hat refs biased motion, gaps and accents."
                           : "; motion came from drill hat weights.");
            }

            return "carrier anchors " + juce::String(metrics.anchorCount)
                + ", support hits " + juce::String(metrics.supportCount)
                + (hatRefsAvailable
                       ? "; hat refs biased density, gaps and velocity feel."
                       : "; carrier feel came from style defaults.");

        case TrackType::OpenHat:
            return "accent hits " + juce::String(metrics.noteCount)
                + ", ending accents " + juce::String(metrics.lateCount)
                + (hatRefsAvailable
                       ? "; openings followed hat-reference space."
                       : "; accent lane followed style probability gates.");

        case TrackType::Ride:
            return "alternate carrier hits " + juce::String(metrics.noteCount)
                + (hatRefsAvailable
                       ? "; ride density reacted to hat-reference spacing."
                       : "; ride usage followed style carrier decisions.");

        case TrackType::Cymbal:
            return "transition markers " + juce::String(metrics.noteCount)
                + ", ending accents " + juce::String(metrics.lateCount)
                + "; kept as section punctuation.";

        case TrackType::Perc:
            return "texture hits " + juce::String(metrics.noteCount)
                + ", late-bar accents " + juce::String(metrics.lateCount)
                + "; filled available support space without crowding anchors.";

        case TrackType::HatFX:
            return "FX hits " + juce::String(metrics.noteCount)
                + ", support motion " + juce::String(metrics.supportCount)
                + "; used as accent motion rather than backbone.";

        case TrackType::Sub808:
            return "low-end anchors " + juce::String(metrics.anchorCount)
                + ", support notes " + juce::String(metrics.supportCount)
                + ", slides " + juce::String(metrics.slideCount)
                + (kickRefsAvailable
                       ? "; low-end followed kick-reference coupling."
                       : "; low-end followed the style coupling rules.");

        default:
            return "notes " + juce::String(metrics.noteCount) + ".";
    }
}

juce::String buildDecisionBasisLine(const PatternProject& project)
{
    switch (project.params.genre)
    {
        case GenreType::Rap:
            return "Decision basis: kick weight " + juce::String(laneBiasFor(project.styleInfluence, TrackType::Kick).activityWeight, 2)
                + " | hat weight " + juce::String(laneBiasFor(project.styleInfluence, TrackType::HiHat).activityWeight, 2)
                + " | clap balance " + juce::String(laneBiasFor(project.styleInfluence, TrackType::ClapGhostSnare).balanceWeight, 2);

        case GenreType::Trap:
            return "Decision basis: hat weight " + juce::String(laneBiasFor(project.styleInfluence, TrackType::HiHat).activityWeight, 2)
                + " | bounce " + juce::String(project.styleInfluence.bounceWeight, 2)
                + " | low-end coupling " + juce::String(project.styleInfluence.lowEndCouplingWeight, 2)
                + " | bass balance " + juce::String(laneBiasFor(project.styleInfluence, TrackType::Sub808).balanceWeight, 2);

        case GenreType::Drill:
            return "Decision basis: hat motion " + juce::String(project.styleInfluence.hatMotionWeight, 2)
                + " | gap intent " + juce::String(project.styleInfluence.drillHatGapIntentWeight, 2)
                + " | accent alternation " + juce::String(project.styleInfluence.drillHatAccentPatternWeight, 2)
                + " | low-end coupling " + juce::String(project.styleInfluence.lowEndCouplingWeight, 2);

        case GenreType::BoomBap:
        default:
            return "Decision basis: kick weight " + juce::String(laneBiasFor(project.styleInfluence, TrackType::Kick).activityWeight, 2)
                + " | hat weight " + juce::String(laneBiasFor(project.styleInfluence, TrackType::HiHat).activityWeight, 2)
                + " | support accent " + juce::String(project.styleInfluence.supportAccentWeight, 2);
    }
}

juce::String buildOutcomeOverview(const PatternProject& project)
{
    const auto* kick = findTrackInProject(project, TrackType::Kick);
    const auto* snare = findTrackInProject(project, TrackType::Snare);
    const auto* hat = findTrackInProject(project, TrackType::HiHat);
    const auto* ride = findTrackInProject(project, TrackType::Ride);
    const auto* hatFx = findTrackInProject(project, TrackType::HatFX);
    const auto* sub = findTrackInProject(project, TrackType::Sub808);

    const auto kickMetrics = kick != nullptr ? analyzeLaneMetrics(*kick) : LaneMetrics {};
    const auto snareMetrics = snare != nullptr ? analyzeLaneMetrics(*snare) : LaneMetrics {};
    const auto hatMetrics = hat != nullptr ? analyzeLaneMetrics(*hat) : LaneMetrics {};
    const auto rideMetrics = ride != nullptr ? analyzeLaneMetrics(*ride) : LaneMetrics {};
    const auto hatFxMetrics = hatFx != nullptr ? analyzeLaneMetrics(*hatFx) : LaneMetrics {};
    const auto subMetrics = sub != nullptr ? analyzeLaneMetrics(*sub) : LaneMetrics {};

    switch (project.params.genre)
    {
        case GenreType::Trap:
            return "Structure: kick " + juce::String(kickMetrics.noteCount)
                + " | 808 " + juce::String(subMetrics.noteCount)
                + " | hats " + juce::String(hatMetrics.noteCount)
                + " | hat FX " + juce::String(hatFxMetrics.noteCount);

        case GenreType::Drill:
            return "Structure: kick " + juce::String(kickMetrics.noteCount)
                + " | 808 " + juce::String(subMetrics.noteCount)
                + " | hats " + juce::String(hatMetrics.noteCount)
                + " | phrase " + phraseSummaryForDisplay(project.phraseRoleSummary);

        case GenreType::Rap:
            return "Pocket: kick anchors " + juce::String(kickMetrics.anchorCount)
                + " | snare backbeats " + juce::String(snareMetrics.anchorCount)
                + " | hats " + juce::String(hatMetrics.noteCount);

        case GenreType::BoomBap:
        default:
        {
            const juce::String carrier = rideMetrics.noteCount > 0
                ? (hatMetrics.noteCount > 0 ? "hybrid" : "ride")
                : "hihat";
            return "Pocket: kick anchors " + juce::String(kickMetrics.anchorCount)
                + " | snare backbeats " + juce::String(snareMetrics.anchorCount)
                + " | carrier " + carrier;
        }
    }
}

juce::String buildReferenceLaneDebugLine(const juce::String& label,
                                        const StyleLabReferenceLaneDiagnostics& diagnostics,
                                        const ReferenceCorpusMetrics& metrics,
                                        bool includeIds)
{
    juce::String line = "- " + label + " refs: requested " + juce::String(diagnostics.requestedCount)
        + " | resolved " + juce::String(diagnostics.resolvedCount);

    if (metrics.available)
    {
        line += " | sources " + juce::String(metrics.sourceCount)
             + " | variants " + juce::String(metrics.variantCount)
             + " | avg hits/bar " + juce::String(metrics.avgHitsPerBar, 1)
             + " | avg vel " + juce::String(metrics.avgVelocity, 1)
             + " | anchor share " + formatUnitPercent(metrics.anchorRatio);
        if (!includeIds)
            line += " | late-hit share " + formatUnitPercent(metrics.lateRatio);
        if (includeIds && !metrics.sourceIds.isEmpty())
            line += " | ids " + joinLimited(metrics.sourceIds, 2);
        return line;
    }

    const auto zeroReason = referenceZeroReasonDisplayName(diagnostics.zeroReason);
    if (zeroReason.isNotEmpty())
        line += " | reason " + zeroReason;
    if (diagnostics.detail.isNotEmpty())
        line += " | detail " + diagnostics.detail;
    return line;
}

juce::String buildLaneChangeLine(const PatternProject& afterProject,
                                 const TrackState& afterTrack,
                                 const TrackState* beforeTrack,
                                 bool hatRefsAvailable,
                                 bool kickRefsAvailable)
{
    const auto afterMetrics = analyzeLaneMetrics(afterTrack);
    const int beforeCount = beforeTrack != nullptr ? static_cast<int>(effectiveNotesForTrack(*beforeTrack).size()) : 0;
    const int delta = afterMetrics.noteCount - beforeCount;

    juce::String changeText;
    if (beforeCount == 0 && afterMetrics.noteCount > 0)
        changeText = "new";
    else if (beforeCount > 0 && afterMetrics.noteCount == 0)
        changeText = "cleared";
    else if (delta == 0)
        changeText = "reshaped";
    else
        changeText = formatDeltaText(delta);

    juce::String line = "- " + juce::String(toString(afterTrack.type))
        + ": " + juce::String(afterMetrics.noteCount) + " notes"
        + " (" + changeText + ")";

    if (afterMetrics.noteCount > 0)
        line += ", avg vel " + juce::String(afterMetrics.avgVelocity, 1);

    line += ". " + laneReasonForTrack(afterProject, afterTrack, afterMetrics, hatRefsAvailable, kickRefsAvailable);
    return line;
}

juce::String buildGenerationDebugReport(const juce::String& actionLabel,
                                        const PatternProject& beforeProject,
                                        const PatternProject& afterProject,
                                        const std::optional<TrackType>& focusTrack,
                                        bool sampleAwareModeEnabled,
                                        bool analysisReady)
{
    const auto hatCorpus = summarizeHatCorpus(afterProject.styleInfluence);
    const auto kickCorpus = summarizeKickCorpus(afterProject.styleInfluence);
    const auto& referenceDebug = afterProject.styleInfluence.referenceDebugDiagnostics;

    juce::StringArray lines;
    juce::String actionText = actionLabel;
    if (focusTrack.has_value())
        actionText += " [" + juce::String(toString(*focusTrack)) + "]";

    lines.add("Last action: " + actionText);
    lines.add("Style: " + genreDisplayName(afterProject.params.genre)
              + " / " + selectedSubstyleDisplayName(afterProject.params)
              + " | " + juce::String(juce::jmax(1, afterProject.params.bars)) + " bars"
              + " | " + juce::String(afterProject.params.bpm, 1) + " BPM"
              + " | seed " + juce::String(afterProject.params.seed));

    if (afterProject.phraseLengthBars > 0 || afterProject.phraseRoleSummary.isNotEmpty())
    {
        lines.add("Phrase plan: " + phraseSummaryForDisplay(afterProject.phraseRoleSummary)
                  + " | span " + juce::String(juce::jmax(1, afterProject.phraseLengthBars)) + " bars");
    }

    lines.add(buildDecisionBasisLine(afterProject));
    lines.add(buildOutcomeOverview(afterProject));

    if (!sampleAwareModeEnabled)
    {
        lines.add("Audio-reactive layer: off.");
    }
    else if (!analysisReady)
    {
        lines.add("Audio-reactive layer: armed, but no analysis was ready for this pass.");
    }
    else if (!afterProject.sampleContext.enabled || afterProject.sampleContext.featureMap.steps.empty())
    {
        lines.add("Audio-reactive layer: analysis existed, but no feature map reached the generation pass.");
    }
    else
    {
        lines.add("Audio-reactive layer: on | reactivity " + juce::String(afterProject.sampleContext.reactivity, 2)
                  + " | support vs contrast " + juce::String(afterProject.sampleContext.supportVsContrast, 2));
    }

    lines.add("Reference influence:");
    lines.add("- Ref scan: candidates " + juce::String(referenceDebug.candidateDirectoryCount)
              + " | matched " + juce::String(referenceDebug.matchingRecordCount)
              + " | selected " + juce::String(referenceDebug.selectedRecordCount)
              + " | parse failures " + juce::String(referenceDebug.parseFailureCount));
    lines.add(buildReferenceLaneDebugLine("Hat", referenceDebug.hat, hatCorpus, true));
    lines.add(buildReferenceLaneDebugLine("Kick", referenceDebug.kick, kickCorpus, false));

    if (hatCorpus.available || kickCorpus.available)
    {
        lines.add("- Use mode: influence only. The generator borrows shape, density, timing space and velocity feel instead of copying notes verbatim.");
    }
    else
    {
        lines.add("- With no loaded refs, this pass relied on style defaults, lane biases and front-panel knobs.");
    }

    if (afterProject.styleInfluence.brooklynHatDiagnostics.available)
    {
        const auto& diagnostics = afterProject.styleInfluence.brooklynHatDiagnostics;
        juce::String line = "- Brooklyn match: " + diagnostics.primaryReferenceId;
        if (diagnostics.secondaryReferenceId.isNotEmpty())
            line += " + " + diagnostics.secondaryReferenceId;
        line += " | blend " + juce::String(diagnostics.usedBlend ? "yes" : "no")
             + " | similarity " + juce::String(diagnostics.similarityScore, 2)
             + " | pool " + juce::String(diagnostics.candidatePoolSize);
        lines.add(line);
    }

    lines.add("Lane changes:");
    bool hasLaneChanges = false;
    for (const auto& afterTrack : afterProject.tracks)
    {
        const auto* beforeTrack = findTrackInProject(beforeProject, afterTrack.type);
        if (tracksHaveSameMusicalContent(beforeTrack, &afterTrack))
            continue;

        hasLaneChanges = true;
        lines.add(buildLaneChangeLine(afterProject,
                                      afterTrack,
                                      beforeTrack,
                                      hatCorpus.available,
                                      kickCorpus.available));
    }

    if (!hasLaneChanges)
    {
        if (focusTrack.has_value())
            lines.add("- " + juce::String(toString(*focusTrack)) + ": unchanged under the current locks/constraints.");
        else
            lines.add("- No lane note content changed in this pass.");
    }

    return lines.joinIntoString("\n");
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout BoomBapGeneratorAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(ParamIds::bpm, "BPM", juce::NormalisableRange<float>(60.0f, 180.0f, 0.1f), 90.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(ParamIds::bpmLock, "BPM Lock", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>(ParamIds::syncDawTempo, "Sync DAW Tempo", false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(ParamIds::swingPercent, "Swing %", juce::NormalisableRange<float>(50.0f, 75.0f, 0.1f), 56.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(ParamIds::velocityAmount, "Velocity Amount", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(ParamIds::timingAmount, "Timing Amount", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.4f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(ParamIds::humanizeAmount, "Humanize Amount", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.3f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(ParamIds::densityAmount, "Density Amount", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(ParamIds::tempoInterpretation,
                                                                   "Tempo Interpretation",
                                                                   juce::StringArray { "Auto", "Original", "Half-time Aware" },
                                                                   0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(ParamIds::bars,
                                                                   "Bars",
                                                                   juce::StringArray { "1", "2", "4", "8", "16" },
                                                                   1));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(ParamIds::genre,
                                                                   "Genre",
                                                                   juce::StringArray { "Boom Bap", "Rap", "Trap", "Drill" },
                                                                   0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(ParamIds::sampleApplyMode,
                                                                   "Sample Apply Mode",
                                                                   getSampleApplyModeNames(),
                                                                   1));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(ParamIds::keyRoot,
                                                                   "Key Root",
                                                                   juce::StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" },
                                                                   0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(ParamIds::scaleMode,
                                                                   "Scale",
                                                                   juce::StringArray { "Minor", "Major", "Harmonic Minor" },
                                                                   0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(ParamIds::boombapSubstyle, "BoomBap Substyle", getBoomBapSubstyleNames(), 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(ParamIds::rapSubstyle,
                                                                   "Rap Substyle",
                                                                   getRapSubstyleNames(),
                                                                   0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(ParamIds::trapSubstyle,
                                                                   "Trap Substyle",
                                                                   getTrapSubstyleNames(),
                                                                   0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(ParamIds::drillSubstyle,
                                                                   "Drill Substyle",
                                                                   makeValidChoiceParameterOptions(getDrillSubstyleNames()),
                                                                   0));
    params.push_back(std::make_unique<juce::AudioParameterInt>(ParamIds::seed, "Seed", 1, 999999, 1));
    params.push_back(std::make_unique<juce::AudioParameterBool>(ParamIds::seedLock, "Seed Lock", false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(ParamIds::masterVolume, "Master Volume", juce::NormalisableRange<float>(0.0f, 1.5f, 0.01f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(ParamIds::masterCompressor, "Master Compressor", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.2f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(ParamIds::masterLofi, "Master LoFi", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f));

    return { params.begin(), params.end() };
}

BoomBapGeneratorAudioProcessor::BoomBapGeneratorAudioProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
    , project(createDefaultProject())
    , apvts(*this, nullptr, "PARAMS", createParameterLayout())
{
    sampleLibraryManager.setGenre(project.params.genre);
    sampleLibraryManager.scan();
    laneSampleBank.applyLibrary(sampleLibraryManager);

    {
        std::scoped_lock lock(projectMutex);
        for (auto& track : project.tracks)
        {
            track.selectedSampleName = laneSampleBank.getSelectedName(track.type);
            track.selectedSampleIndex = laneSampleBank.getSelectedIndex(track.type);
        }
    }

    applySelectedStylePreset(true);
    resetEqDisplayAnalyzer();

    generatePattern();
}

BoomBapGeneratorAudioProcessor::~BoomBapGeneratorAudioProcessor()
{
    beginShutdown();
    waitForActiveProcessBlocks();
}

void BoomBapGeneratorAudioProcessor::prepareToPlay(double sampleRate, int)
{
    audioRenderSuspended.store(false, std::memory_order_release);
    currentSampleRate = sampleRate;
    transportSamplePosition = 0;
    previewSamplePosition = 0;
    previewEngine.prepare(sampleRate);

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(getBlockSize() > 0 ? getBlockSize() : 1024);
    spec.numChannels = 2;

    previewCompressor.reset();
    previewCompressor.prepare(spec);
    previewCompressor.setAttack(7.0f);
    previewCompressor.setRelease(120.0f);

    previewLofiFilter.reset();
    previewLofiFilter.prepare(spec);
    previewLofiFilter.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
    previewLofiFilter.setCutoffFrequency(18000.0f);

    lofiDownsampleCounter = 0;
    lofiHeldSample = { 0.0f, 0.0f };
    resetEqDisplayAnalyzer();

    for (auto& runtime : laneFxRuntimeStates)
        prepareSoundFxRuntimeState(runtime);
    prepareSoundFxRuntimeState(globalFxRuntimeState);
}

void BoomBapGeneratorAudioProcessor::releaseResources()
{
    suspendAudioRenderAndWait();

    {
        std::scoped_lock lock(projectMutex);
        previewPlaying = false;
        previewSamplePosition = 0;
        transportSamplePosition = 0;
        pendingPreviewNotes.clear();
        lastObservedHostPlaying = false;
    }

    previewEngine.reset();
    for (auto& runtime : laneFxRuntimeStates)
        resetSoundFxRuntimeState(runtime);
    resetSoundFxRuntimeState(globalFxRuntimeState);
    resetEqDisplayAnalyzer();
}

void BoomBapGeneratorAudioProcessor::reset()
{
    runtimeResetPending.store(true, std::memory_order_release);
}

void BoomBapGeneratorAudioProcessor::resetEqDisplayAnalyzer()
{
    eqDisplayAnalyzerFifoIndex = 0;
    eqDisplayAnalyzerFifo.fill(0.0f);
    eqDisplayAnalyzerFftData.fill(0.0f);
    for (auto& magnitude : eqDisplayAnalyzerMagnitudes)
        magnitude.store(0.0f, std::memory_order_relaxed);
    eqDisplayAnalyzerRms.store(0.0f, std::memory_order_relaxed);
    eqDisplayAnalyzerActive.store(false, std::memory_order_relaxed);
}

void BoomBapGeneratorAudioProcessor::captureEqDisplayAnalyzer(const juce::AudioBuffer<float>& buffer)
{
    if (buffer.getNumSamples() <= 0)
        return;

    const int channelCount = juce::jmax(1, buffer.getNumChannels());
    float sumSquares = 0.0f;
    bool producedFrame = false;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        float mono = 0.0f;
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            mono += buffer.getSample(channel, sample);

        mono /= static_cast<float>(channelCount);
        sumSquares += mono * mono;
        eqDisplayAnalyzerFifo[static_cast<size_t>(eqDisplayAnalyzerFifoIndex++)] = mono;

        if (eqDisplayAnalyzerFifoIndex >= kEqDisplayAnalyzerFftSize)
        {
            runEqDisplayAnalyzerFrame();
            eqDisplayAnalyzerFifoIndex = 0;
            producedFrame = true;
        }
    }

    const float blockRms = std::sqrt(sumSquares / static_cast<float>(buffer.getNumSamples()));
    const float previousRms = eqDisplayAnalyzerRms.load(std::memory_order_relaxed);
    const float smoothedRms = juce::jmax(blockRms, previousRms * 0.84f);
    eqDisplayAnalyzerRms.store(smoothedRms, std::memory_order_relaxed);

    if (!producedFrame)
    {
        float peakMagnitude = 0.0f;
        for (auto& magnitude : eqDisplayAnalyzerMagnitudes)
        {
            const float decayed = magnitude.load(std::memory_order_relaxed) * 0.965f;
            magnitude.store(decayed, std::memory_order_relaxed);
            peakMagnitude = juce::jmax(peakMagnitude, decayed);
        }

        eqDisplayAnalyzerActive.store(peakMagnitude > 0.012f || smoothedRms > 0.004f, std::memory_order_relaxed);
    }
}

void BoomBapGeneratorAudioProcessor::runEqDisplayAnalyzerFrame()
{
    if (currentSampleRate <= 0.0)
        return;

    std::fill(eqDisplayAnalyzerFftData.begin(), eqDisplayAnalyzerFftData.end(), 0.0f);
    std::copy(eqDisplayAnalyzerFifo.begin(), eqDisplayAnalyzerFifo.end(), eqDisplayAnalyzerFftData.begin());
    eqDisplayAnalyzerWindow.multiplyWithWindowingTable(eqDisplayAnalyzerFftData.data(), kEqDisplayAnalyzerFftSize);
    eqDisplayAnalyzerFft.performFrequencyOnlyForwardTransform(eqDisplayAnalyzerFftData.data());

    float peakMagnitude = 0.0f;
    for (int index = 0; index < kEqDisplayAnalyzerBinCount; ++index)
    {
        const double normalized = kEqDisplayAnalyzerBinCount > 1
            ? static_cast<double>(index) / static_cast<double>(kEqDisplayAnalyzerBinCount - 1)
            : 0.0;
        const double frequencyHz = eqAnalyzerFrequencyFromNormalized(normalized);
        const int fftIndex = juce::jlimit(1,
                                          kEqDisplayAnalyzerFftSize / 2 - 2,
                                          static_cast<int>(std::round(frequencyHz * static_cast<double>(kEqDisplayAnalyzerFftSize)
                                                                      / currentSampleRate)));

        float magnitude = 0.0f;
        for (int bin = fftIndex - 1; bin <= fftIndex + 1; ++bin)
            magnitude = juce::jmax(magnitude, eqDisplayAnalyzerFftData[static_cast<size_t>(bin)]);

        const float scaledMagnitude = magnitude / static_cast<float>(kEqDisplayAnalyzerFftSize);
        const float magnitudeDb = juce::Decibels::gainToDecibels(scaledMagnitude, -96.0f);
        const float normalizedMagnitude = juce::jmap(juce::jlimit(-90.0f, -18.0f, magnitudeDb), -90.0f, -18.0f, 0.0f, 1.0f);
        const float previousMagnitude = eqDisplayAnalyzerMagnitudes[static_cast<size_t>(index)].load(std::memory_order_relaxed);
        const float smoothedMagnitude = juce::jmax(normalizedMagnitude, previousMagnitude * 0.78f);
        eqDisplayAnalyzerMagnitudes[static_cast<size_t>(index)].store(smoothedMagnitude, std::memory_order_relaxed);
        peakMagnitude = juce::jmax(peakMagnitude, smoothedMagnitude);
    }

    eqDisplayAnalyzerActive.store(peakMagnitude > 0.012f || eqDisplayAnalyzerRms.load(std::memory_order_relaxed) > 0.004f,
                                  std::memory_order_relaxed);
}

bool BoomBapGeneratorAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void BoomBapGeneratorAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    ProcessBlockActivityGuard processBlockActivity(activeProcessBlockCount);
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    midiMessages.clear();

    if (audioRenderSuspended.load(std::memory_order_acquire))
        return;

    const bool runtimeResetRequested = runtimeResetPending.exchange(false, std::memory_order_acq_rel);
    const auto snapshot = TransportSnapshot::fromPlayHead(getPlayHead());
    const auto currentParams = buildParamsFromState(snapshot);
    LiveRenderUpdate liveRenderUpdate;
    bool shouldAutoStartPreview = false;
    bool shouldAutoStopPreview = false;
    PatternProject projectSnapshot;
    juce::MidiMessageSequence midiCacheSnapshot;
    std::vector<PreviewEvent> previewEventsSnapshot;
    std::vector<PendingPreviewNote> pendingPreviewNotesSnapshot;
    bool previewPlayingSnapshot = false;
    bool previewPlayingAtSnapshot = false;
    int previewSamplePositionSnapshot = 0;
    int transportSamplePositionSnapshot = 0;
    std::uint64_t midiCacheRevisionSnapshot = 0;

    {
        std::scoped_lock lock(projectMutex);
        if (runtimeResetRequested)
        {
            previewPlaying = false;
            previewSamplePosition = 0;
            transportSamplePosition = 0;
            pendingPreviewNotes.clear();
        }

        shouldAutoStartPreview = !runtimeResetRequested && startPlayWithDawEnabled && snapshot.isPlaying && !lastObservedHostPlaying;
        shouldAutoStopPreview = !runtimeResetRequested && startPlayWithDawEnabled && !snapshot.isPlaying && lastObservedHostPlaying;
        lastObservedHostPlaying = snapshot.isPlaying;
        lastTransport = snapshot;
        if (!runtimeResetRequested)
            liveRenderUpdate = syncLivePerformanceStateLocked(currentParams);

        if (shouldAutoStartPreview)
        {
            if (!laneSampleBank.hasSamples(TrackType::Kick)
                && !laneSampleBank.hasSamples(TrackType::Snare)
                && !laneSampleBank.hasSamples(TrackType::HiHat))
                rescanLaneSamplesLocked();

            previewPlaying = true;
            if (snapshot.hasPpq && currentParams.bpm > 0.0f)
            {
                const double samplesPerQuarter = (60.0 / static_cast<double>(currentParams.bpm)) * currentSampleRate;
                previewSamplePosition = static_cast<int>(std::lround(snapshot.ppqPosition * samplesPerQuarter));
            }
            else
            {
                startPreviewFromCurrentStartStepLocked();
            }
        }
        else if (shouldAutoStopPreview)
        {
            previewPlaying = false;
        }

        projectSnapshot = project;
        midiCacheRevisionSnapshot = midiCacheRevision;
        if (!liveRenderUpdate.requiresCacheRebuild())
        {
            midiCacheSnapshot = midiCache;
            previewEventsSnapshot = previewEvents;
        }
        pendingPreviewNotesSnapshot = pendingPreviewNotes;
        pendingPreviewNotes.clear();

        previewPlayingSnapshot = previewPlaying;
        previewPlayingAtSnapshot = previewPlaying;
        previewSamplePositionSnapshot = previewSamplePosition;
        transportSamplePositionSnapshot = transportSamplePosition;
    }

    if (runtimeResetRequested)
    {
        // `reset()` may come from an unsynchronised host lifecycle callback, so
        // the actual runtime reset is deferred to the audio thread here.
        previewEngine.reset();
        for (auto& runtime : laneFxRuntimeStates)
            resetSoundFxRuntimeState(runtime);
        resetSoundFxRuntimeState(globalFxRuntimeState);
        resetEqDisplayAnalyzer();
    }

    if (shouldAutoStartPreview || shouldAutoStopPreview)
        previewEngine.reset();

    if (liveRenderUpdate.performanceChanged)
        PatternPerformanceTransformEngine::applyPerformanceFromBase(projectSnapshot, currentParams);

    if (liveRenderUpdate.requiresCacheRebuild())
    {
        midiCacheSnapshot = buildMidiCacheForProject(projectSnapshot, currentSampleRate);
        previewEventsSnapshot = buildPreviewEventsForProject(projectSnapshot, currentSampleRate);
    }

    int startSample = transportSamplePositionSnapshot;
    if (snapshot.hasPpq)
        startSample = stepToSamples(static_cast<int>(snapshot.ppqPosition * 4.0), currentSampleRate, currentParams.bpm);

    const double safeBpm = currentParams.bpm > 0.0f ? static_cast<double>(currentParams.bpm) : 120.0;
    const double samplesPerQuarter = (60.0 / safeBpm) * currentSampleRate;
    MonstaFxTimelineContext monstaTimeline;
    monstaTimeline.bpm = safeBpm;
    monstaTimeline.blockStartSample = static_cast<std::int64_t>(startSample);
    monstaTimeline.blockStartQuarter = snapshot.hasPpq && snapshot.ppqPosition >= 0.0
        ? snapshot.ppqPosition
        : (samplesPerQuarter > 0.0 ? static_cast<double>(startSample) / samplesPerQuarter : 0.0);

    const int numSamples = buffer.getNumSamples();
    const int patternLength = getPatternLengthSamples(projectSnapshot, currentSampleRate);
    if (patternLength <= 0)
        return;

    bool requiresSeparatedSoundLayerPath = soundLayerNeedsSeparatedRender(projectSnapshot.globalSound);
    if (!requiresSeparatedSoundLayerPath)
    {
        for (const auto& track : projectSnapshot.tracks)
        {
            if (!shouldIncludeTrackForPlayback(projectSnapshot, track))
                continue;

            if (soundLayerNeedsSeparatedRender(track.sound))
            {
                requiresSeparatedSoundLayerPath = true;
                break;
            }
        }
    }

    for (const auto& audition : pendingPreviewNotesSnapshot)
    {
        PreviewEngine::TriggerOptions options;
        options.playbackRate = playbackRateForTrackPitch(audition.track, audition.pitch);
        if (audition.track == TrackType::Sub808)
        {
            if (const auto* subTrack = ProjectLaneAccess::findTrackState(projectSnapshot, TrackType::Sub808); subTrack != nullptr)
            {
                options.mono = subTrack->sub808Settings.mono;
                options.cutItself = subTrack->sub808Settings.cutItself;
                options.legato = subTrack->sub808Settings.overlapMode == Sub808OverlapMode::Legato;
                options.glide = subTrack->sub808Settings.overlapMode == Sub808OverlapMode::Glide;
                options.glideDurationSamples = static_cast<int>((static_cast<double>(subTrack->sub808Settings.glideTimeMs) / 1000.0) * currentSampleRate);
            }
        }
        if (audition.lengthTicks > 0)
            options.maxDurationSamples = juce::jmax(1, ticksToSamples(audition.lengthTicks, currentSampleRate, projectSnapshot.params.bpm));

        previewEngine.noteOn(audition.track,
                             juce::jlimit(0.0f, 1.0f, static_cast<float>(audition.velocity) / 127.0f),
                             laneSampleBank,
                             options);
    }

    bool shouldRenderPreviewVoices = false;

    for (int i = 0; i < midiCacheSnapshot.getNumEvents(); ++i)
    {
        const auto* holder = midiCacheSnapshot.getEventPointer(i);
        if (holder == nullptr)
            continue;

        const auto eventSample = static_cast<int>(holder->message.getTimeStamp());
        const int blockStartInPattern = ((startSample % patternLength) + patternLength) % patternLength;
        int rel = eventSample - blockStartInPattern;
        if (rel < 0)
            rel += patternLength;

        if (rel >= 0 && rel < numSamples)
            midiMessages.addEvent(holder->message, rel);
    }

    if (previewPlayingSnapshot)
    {
        const auto schedulePreviewSegment = [this, &previewEventsSnapshot](int segmentStart,
                                                                            int segmentLength,
                                                                            int bufferOffset,
                                                                            const std::optional<juce::Range<int>>& allowedSampleRange)
        {
            for (const auto& event : previewEventsSnapshot)
            {
                const int eventSample = event.sample;
                if (allowedSampleRange.has_value() && !allowedSampleRange->contains(eventSample))
                    continue;

                const int rel = eventSample - segmentStart;
                if (rel >= 0 && rel < segmentLength)
                {
                    PreviewEngine::TriggerOptions options;
                    if (event.track == TrackType::Sub808)
                    {
                        options.playbackRate = playbackRateForTrackPitch(event.track, event.pitch);
                        options.legato = event.legato;
                        options.glide = event.glide;
                        options.mono = event.mono;
                        options.cutItself = event.cutItself;
                        options.glideDurationSamples = juce::jmax(0, event.glideDurationSamples);
                        if (event.endSample > event.sample)
                            options.maxDurationSamples = juce::jmax(1, event.endSample - event.sample);
                    }
                    previewEngine.noteOnAtSample(event.track, event.gain, bufferOffset + rel, laneSampleBank, options);
                }
            }
        };

        if (const auto loopTicks = activePreviewLoopTicks(projectSnapshot); loopTicks.has_value())
        {
            const int loopStartSample = ticksToSamples(loopTicks->getStart(), currentSampleRate, projectSnapshot.params.bpm);
            const int loopEndSample = ticksToSamples(loopTicks->getEnd(), currentSampleRate, projectSnapshot.params.bpm);

            if (loopEndSample > loopStartSample)
            {
                int localPreviewPosition = previewSamplePositionSnapshot;
                if (localPreviewPosition < loopStartSample || localPreviewPosition >= loopEndSample)
                    localPreviewPosition = loopStartSample;

                int remainingSamples = numSamples;
                int bufferOffset = 0;
                const auto loopSampleRange = juce::Range<int>(loopStartSample, loopEndSample);

                while (remainingSamples > 0)
                {
                    const int segmentStart = juce::jlimit(loopStartSample, loopEndSample - 1, localPreviewPosition);
                    const int segmentLength = juce::jmin(remainingSamples, loopEndSample - segmentStart);
                    schedulePreviewSegment(segmentStart, segmentLength, bufferOffset, loopSampleRange);

                    remainingSamples -= segmentLength;
                    bufferOffset += segmentLength;
                    localPreviewPosition = segmentStart + segmentLength;
                    if (localPreviewPosition >= loopEndSample)
                        localPreviewPosition = loopStartSample;
                }

                previewSamplePositionSnapshot = localPreviewPosition;
            }
        }
        else
        {
            const int previewStart = previewSamplePositionSnapshot;
            const int previewBlockStartInPattern = ((previewStart % patternLength) + patternLength) % patternLength;

            for (const auto& event : previewEventsSnapshot)
            {
                const int eventSample = event.sample;
                int rel = eventSample - previewBlockStartInPattern;
                if (rel < 0)
                    rel += patternLength;

                if (rel >= 0 && rel < numSamples)
                {
                    PreviewEngine::TriggerOptions options;
                    if (event.track == TrackType::Sub808)
                    {
                        options.playbackRate = playbackRateForTrackPitch(event.track, event.pitch);
                        options.legato = event.legato;
                        options.glide = event.glide;
                        options.mono = event.mono;
                        options.cutItself = event.cutItself;
                        options.glideDurationSamples = juce::jmax(0, event.glideDurationSamples);
                        if (event.endSample > event.sample)
                            options.maxDurationSamples = juce::jmax(1, event.endSample - event.sample);
                    }
                    previewEngine.noteOnAtSample(event.track, event.gain, rel, laneSampleBank, options);
                }
            }

            previewSamplePositionSnapshot = previewStart + numSamples;
        }

        shouldRenderPreviewVoices = true;
    }

    shouldRenderPreviewVoices = shouldRenderPreviewVoices || previewEngine.hasActiveVoices();
    if (shouldRenderPreviewVoices)
    {
        if (!requiresSeparatedSoundLayerPath)
        {
            previewEngine.render(buffer, 0, numSamples);
            applyMasterFx(buffer);
        }
        else
        {
            const int outputChannels = juce::jmax(1, buffer.getNumChannels());
            for (auto& laneBuffer : previewLaneBuffers)
            {
                laneBuffer.setSize(outputChannels, numSamples, false, false, true);
                laneBuffer.clear();
            }

            previewEngine.renderSeparated(previewLaneBuffers, 0, numSamples);

            for (int trackIndex = 0; trackIndex < kTrackTypeCount; ++trackIndex)
            {
                auto& laneBuffer = previewLaneBuffers[static_cast<size_t>(trackIndex)];
                const auto trackType = static_cast<TrackType>(trackIndex);
                if (const auto* trackState = ProjectLaneAccess::findTrackState(projectSnapshot, trackType); trackState != nullptr)
                    applySoundLayerFx(laneBuffer,
                                      trackState->sound,
                                      laneFxRuntimeStates[static_cast<size_t>(trackIndex)],
                                      monstaTimeline);

                for (int channel = 0; channel < outputChannels; ++channel)
                    buffer.addFrom(channel, 0, laneBuffer, channel, 0, numSamples);
            }

            applySoundLayerFx(buffer, projectSnapshot.globalSound, globalFxRuntimeState, monstaTimeline);

            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            {
                auto* channelData = buffer.getWritePointer(channel);
                for (int sample = 0; sample < numSamples; ++sample)
                    channelData[sample] = std::tanh(channelData[sample] * 0.85f);
            }

            applyMasterFx(buffer);
        }
    }

    {
        std::scoped_lock lock(projectMutex);
        if (liveRenderUpdate.requiresCacheRebuild() && midiCacheRevision == midiCacheRevisionSnapshot)
        {
            midiCache = std::move(midiCacheSnapshot);
            previewEvents = std::move(previewEventsSnapshot);
            ++midiCacheRevision;
        }

        transportSamplePosition = startSample + numSamples;
        if (previewPlaying == previewPlayingAtSnapshot)
        {
            previewPlaying = previewPlayingSnapshot;
            previewSamplePosition = previewSamplePositionSnapshot;
        }
    }

    captureEqDisplayAnalyzer(buffer);
}

juce::AudioProcessorEditor* BoomBapGeneratorAudioProcessor::createEditor()
{
    if (wrapperType == juce::AudioProcessor::wrapperType_VST3)
        // VST3 intentionally uses a DAW-safe editor surface. Standalone keeps
        // the full authoring editor and remains the canonical grid/style lab.
        return new Vst3SafeHeaderEditor(*this);

    return new BoomBGeneratorAudioProcessorEditor(*this);
}

bool BoomBapGeneratorAudioProcessor::hasEditor() const { return true; }
const juce::String BoomBapGeneratorAudioProcessor::getName() const { return JucePlugin_Name; }
bool BoomBapGeneratorAudioProcessor::acceptsMidi() const { return true; }
bool BoomBapGeneratorAudioProcessor::producesMidi() const { return true; }
bool BoomBapGeneratorAudioProcessor::isMidiEffect() const
{
#if JUCE_STANDALONE_APPLICATION
    return false;
#else
    return true;
#endif
}
double BoomBapGeneratorAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int BoomBapGeneratorAudioProcessor::getNumPrograms() { return 1; }
int BoomBapGeneratorAudioProcessor::getCurrentProgram() { return 0; }
void BoomBapGeneratorAudioProcessor::setCurrentProgram(int) {}
const juce::String BoomBapGeneratorAudioProcessor::getProgramName(int) { return {}; }
void BoomBapGeneratorAudioProcessor::changeProgramName(int, const juce::String&) {}

void BoomBapGeneratorAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    suspendAudioRenderAndWait();

    juce::ValueTree state(kStateType);
    state.copyPropertiesAndChildrenFrom(apvts.copyState(), nullptr);
    state.setProperty("root_schema_version", kRootSchemaVersion, nullptr);

    PatternProject projectSnapshot;
    TransportSnapshot transportSnapshot;
    {
        std::scoped_lock lock(projectMutex);
        projectSnapshot = project;
        transportSnapshot = lastTransport;
    }

    PatternPerformanceTransformEngine::backfillMissingPerformanceBaseParams(projectSnapshot, buildParamsFromState(transportSnapshot));
    state.removeChild(state.getChildWithName("PATTERN_PROJECT"), nullptr);
    state.addChild(PatternProjectSerialization::serialize(projectSnapshot), -1, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);

    resumeAudioRender();
}

void BoomBapGeneratorAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr)
        return;

    const auto loaded = juce::ValueTree::fromXml(*xml);
    if (!loaded.isValid())
        return;

    suspendAudioRenderAndWait();

    apvts.replaceState(loaded);

    const auto* genreValue = apvts.getRawParameterValue(ParamIds::genre);
    const auto* boombapSubstyleValue = apvts.getRawParameterValue(ParamIds::boombapSubstyle);
    const auto* rapSubstyleValue = apvts.getRawParameterValue(ParamIds::rapSubstyle);
    const auto* trapSubstyleValue = apvts.getRawParameterValue(ParamIds::trapSubstyle);
    const auto* drillSubstyleValue = apvts.getRawParameterValue(ParamIds::drillSubstyle);
    lastAppliedGenreChoice = genreValue != nullptr ? static_cast<int>(genreValue->load()) : 0;
    lastAppliedBoomBapSubstyleChoice = boombapSubstyleValue != nullptr ? static_cast<int>(boombapSubstyleValue->load()) : 0;
    lastAppliedRapSubstyleChoice = rapSubstyleValue != nullptr ? static_cast<int>(rapSubstyleValue->load()) : 0;
    lastAppliedTrapSubstyleChoice = trapSubstyleValue != nullptr ? static_cast<int>(trapSubstyleValue->load()) : 0;
    lastAppliedDrillSubstyleChoice = drillSubstyleValue != nullptr ? clampDrillSubstyleChoice(static_cast<int>(drillSubstyleValue->load())) : 0;

    {
        std::scoped_lock lock(projectMutex);
        restorePatternProjectFromState(loaded);

        rescanLaneSamplesLocked();

        project.params = buildParamsFromState(lastTransport);
        rebuildMidiCache();
    }

    resumeAudioRender();
}

void BoomBapGeneratorAudioProcessor::generatePattern()
{
    std::scoped_lock lock(projectMutex);
    const auto beforeProject = project;
    advanceSeedForGeneration(std::nullopt);
    auto generationParams = buildParamsFromState(lastTransport);
    const auto bpmSelection = resolveGenerationBpm(generationParams,
                                                   generationParams.bpm,
                                                   isBpmLocked(apvts),
                                                   lastTransport.hasHostTempo && lastTransport.bpm > 0.0
                                                       ? std::optional<double>(lastTransport.bpm)
                                                       : std::nullopt);
    if (std::abs(bpmSelection.bpm - generationParams.bpm) > 0.05f)
        setFloatParameterValue(ParamIds::bpm, bpmSelection.bpm);

    generationParams.bpm = bpmSelection.bpm;
    project.params = generationParams;
    project.sampleContext = currentSampleContext;
    switch (project.params.genre)
    {
        case GenreType::Drill: drillEngine.generate(project); break;
        case GenreType::Rap: rapEngine.generate(project); break;
        case GenreType::Trap: trapEngine.generate(project); break;
        case GenreType::BoomBap:
        default: boomBapEngine.generate(project); break;
    }
    applySampleAwarePostProcessLocked();
    project.generationDebugReport = buildGenerationDebugReport("Generate Pattern",
                                                               beforeProject,
                                                               project,
                                                               std::nullopt,
                                                               sampleAwareModeEnabled,
                                                               analysisReady);
    if (lastSampleApplyDebug.isNotEmpty())
        project.generationDebugReport += "\n" + lastSampleApplyDebug;
    ++project.generationCounter;
    rebuildMidiCache();
}

void BoomBapGeneratorAudioProcessor::generateTrackNew(TrackType track)
{
    std::scoped_lock lock(projectMutex);
    const auto beforeProject = project;
    advanceSeedForGeneration(track);
    project.params = buildParamsFromState(lastTransport);
    project.sampleContext = currentSampleContext;
    switch (project.params.genre)
    {
        case GenreType::Drill: drillEngine.generateTrackNew(project, track); break;
        case GenreType::Rap: rapEngine.generateTrackNew(project, track); break;
        case GenreType::Trap: trapEngine.generateTrackNew(project, track); break;
        case GenreType::BoomBap:
        default: boomBapEngine.generateTrackNew(project, track); break;
    }
    applySampleAwarePostProcessLocked();
    project.generationDebugReport = buildGenerationDebugReport("Generate Track",
                                                               beforeProject,
                                                               project,
                                                               track,
                                                               sampleAwareModeEnabled,
                                                               analysisReady);
    if (lastSampleApplyDebug.isNotEmpty())
        project.generationDebugReport += "\n" + lastSampleApplyDebug;
    ++project.generationCounter;
    rebuildMidiCache();
}

void BoomBapGeneratorAudioProcessor::regenerateTrack(TrackType track)
{
    std::scoped_lock lock(projectMutex);
    const auto beforeProject = project;
    advanceSeedForGeneration(track);
    project.params = buildParamsFromState(lastTransport);
    project.sampleContext = currentSampleContext;
    switch (project.params.genre)
    {
        case GenreType::Drill: drillEngine.regenerateTrackVariation(project, track); break;
        case GenreType::Rap: rapEngine.regenerateTrackVariation(project, track); break;
        case GenreType::Trap: trapEngine.regenerateTrackVariation(project, track); break;
        case GenreType::BoomBap:
        default: boomBapEngine.regenerateTrackVariation(project, track); break;
    }
    applySampleAwarePostProcessLocked();
    project.generationDebugReport = buildGenerationDebugReport("Regenerate Variation",
                                                               beforeProject,
                                                               project,
                                                               track,
                                                               sampleAwareModeEnabled,
                                                               analysisReady);
    if (lastSampleApplyDebug.isNotEmpty())
        project.generationDebugReport += "\n" + lastSampleApplyDebug;
    ++project.generationCounter;
    rebuildMidiCache();
}

void BoomBapGeneratorAudioProcessor::regenerateTrack(const RuntimeLaneId& laneId)
{
    const auto type = resolveTrackTypeFromLane(laneId);
    if (!type.has_value())
        return;

    regenerateTrack(*type);
}

void BoomBapGeneratorAudioProcessor::mutatePattern()
{
    std::scoped_lock lock(projectMutex);
    const auto beforeProject = project;
    advanceSeedForGeneration(std::nullopt);
    project.params = buildParamsFromState(lastTransport);
    project.sampleContext = currentSampleContext;
    switch (project.params.genre)
    {
        case GenreType::Drill: drillEngine.mutatePattern(project); break;
        case GenreType::Rap: rapEngine.mutatePattern(project); break;
        case GenreType::Trap: trapEngine.mutatePattern(project); break;
        case GenreType::BoomBap:
        default: boomBapEngine.mutatePattern(project); break;
    }
    applySampleAwarePostProcessLocked();
    project.generationDebugReport = buildGenerationDebugReport("Mutate Pattern",
                                                               beforeProject,
                                                               project,
                                                               std::nullopt,
                                                               sampleAwareModeEnabled,
                                                               analysisReady);
    if (lastSampleApplyDebug.isNotEmpty())
        project.generationDebugReport += "\n" + lastSampleApplyDebug;
    ++project.generationCounter;
    rebuildMidiCache();
}

void BoomBapGeneratorAudioProcessor::mutateTrack(TrackType track)
{
    std::scoped_lock lock(projectMutex);
    const auto beforeProject = project;
    advanceSeedForGeneration(track);
    project.params = buildParamsFromState(lastTransport);
    project.sampleContext = currentSampleContext;
    switch (project.params.genre)
    {
        case GenreType::Drill: drillEngine.mutateTrack(project, track); break;
        case GenreType::Rap: rapEngine.mutateTrack(project, track); break;
        case GenreType::Trap: trapEngine.mutateTrack(project, track); break;
        case GenreType::BoomBap:
        default: boomBapEngine.mutateTrack(project, track); break;
    }
    applySampleAwarePostProcessLocked();
    project.generationDebugReport = buildGenerationDebugReport("Mutate Track",
                                                               beforeProject,
                                                               project,
                                                               track,
                                                               sampleAwareModeEnabled,
                                                               analysisReady);
    if (lastSampleApplyDebug.isNotEmpty())
        project.generationDebugReport += "\n" + lastSampleApplyDebug;
    ++project.generationCounter;
    rebuildMidiCache();
}

void BoomBapGeneratorAudioProcessor::mutateTrack(const RuntimeLaneId& laneId)
{
    const auto type = resolveTrackTypeFromLane(laneId);
    if (!type.has_value())
        return;

    mutateTrack(*type);
}

void BoomBapGeneratorAudioProcessor::startPreview()
{
    std::scoped_lock lock(projectMutex);
    if (!laneSampleBank.hasSamples(TrackType::Kick)
        && !laneSampleBank.hasSamples(TrackType::Snare)
        && !laneSampleBank.hasSamples(TrackType::HiHat))
        rescanLaneSamplesLocked();
    rebuildMidiCache();
    previewPlaying = true;
    startPreviewFromCurrentStartStepLocked();
    previewEngine.reset();
}

void BoomBapGeneratorAudioProcessor::stopPreview()
{
    std::scoped_lock lock(projectMutex);
    previewPlaying = false;
    previewEngine.reset();
}

bool BoomBapGeneratorAudioProcessor::isPreviewPlaying() const
{
    std::scoped_lock lock(projectMutex);
    return previewPlaying;
}

float BoomBapGeneratorAudioProcessor::getPreviewPlayheadStep() const
{
    std::scoped_lock lock(projectMutex);
    if (!previewPlaying)
        return -1.0f;

    const int totalSteps = juce::jmax(1, project.params.bars * 16);
    const int patternLength = getPatternLengthSamples();
    if (patternLength <= 0)
        return -1.0f;

    const int sampleInPattern = ((previewSamplePosition % patternLength) + patternLength) % patternLength;
    return static_cast<float>(sampleInPattern) * static_cast<float>(totalSteps) / static_cast<float>(patternLength);
}

void BoomBapGeneratorAudioProcessor::setPreviewStartStep(int step)
{
    std::scoped_lock lock(projectMutex);
    ProjectStateController::setPreviewStartStep(project, step);
}

void BoomBapGeneratorAudioProcessor::syncBarsFromState()
{
    std::scoped_lock lock(projectMutex);
    const auto liveParams = buildParamsFromState(lastTransport);
    ProjectStateController::setBars(project, liveParams.bars);
    rebuildMidiCache();
}

void BoomBapGeneratorAudioProcessor::setPreviewPlaybackMode(PreviewPlaybackMode mode)
{
    std::scoped_lock lock(projectMutex);
    ProjectStateController::setPreviewPlaybackMode(project, mode);
    if (previewPlaying)
    {
        startPreviewFromCurrentStartStepLocked();
        previewEngine.reset();
    }
}

PreviewPlaybackMode BoomBapGeneratorAudioProcessor::getPreviewPlaybackMode() const
{
    std::scoped_lock lock(projectMutex);
    return project.previewPlaybackMode;
}

void BoomBapGeneratorAudioProcessor::restoreEditorProjectSnapshot(const PatternProject& snapshot)
{
    std::scoped_lock lock(projectMutex);
    ProjectStateController::restoreEditorProjectSnapshot(project, snapshot, buildParamsFromState(lastTransport));
    PatternPerformanceTransformEngine::backfillMissingPerformanceBaseParams(project, project.params);
    rebuildMidiCache();
}

void BoomBapGeneratorAudioProcessor::setPreviewLoopRegion(const std::optional<juce::Range<int>>& tickRange)
{
    std::scoped_lock lock(projectMutex);
    ProjectStateController::setPreviewLoopRegion(project, tickRange);

    if (previewPlaying && project.previewPlaybackMode == PreviewPlaybackMode::LoopRange)
    {
        startPreviewFromCurrentStartStepLocked();
        previewEngine.reset();
    }
}

std::optional<juce::Range<int>> BoomBapGeneratorAudioProcessor::getPreviewLoopRegion() const
{
    std::scoped_lock lock(projectMutex);
    if (!project.previewLoopTicks.has_value() || project.previewLoopTicks->getLength() <= 0)
        return std::nullopt;

    return project.previewLoopTicks;
}

void BoomBapGeneratorAudioProcessor::startPreviewFromCurrentStartStepLocked()
{
    if (const auto loopTicks = activePreviewLoopTicks(project); loopTicks.has_value())
    {
        previewSamplePosition = ticksToSamples(loopTicks->getStart(), currentSampleRate, project.params.bpm);
        return;
    }

    const int maxStep = juce::jmax(0, project.params.bars * 16 - 1);
    const int startStep = juce::jlimit(0, maxStep, project.previewStartStep);
    previewSamplePosition = stepToSamples(startStep, currentSampleRate, project.params.bpm);
}

void BoomBapGeneratorAudioProcessor::setStartPlayWithDawEnabled(bool enabled)
{
    std::scoped_lock lock(projectMutex);
    startPlayWithDawEnabled = enabled;
}

bool BoomBapGeneratorAudioProcessor::isStartPlayWithDawEnabled() const
{
    std::scoped_lock lock(projectMutex);
    return startPlayWithDawEnabled;
}

void BoomBapGeneratorAudioProcessor::rescanLaneSamples()
{
    std::scoped_lock lock(projectMutex);
    rescanLaneSamplesLocked();
}

void BoomBapGeneratorAudioProcessor::rescanLaneSamplesLocked()
{
    sampleLibraryManager.setGenre(project.params.genre);
    sampleLibraryManager.scan();
    laneSampleBank.applyLibrary(sampleLibraryManager);

    for (auto& track : project.tracks)
    {
        const auto desired = track.selectedSampleIndex;
        laneSampleBank.selectIndex(track.type, desired);
        track.selectedSampleIndex = laneSampleBank.getSelectedIndex(track.type);
        track.selectedSampleName = laneSampleBank.getSelectedName(track.type);
    }
}

bool BoomBapGeneratorAudioProcessor::selectNextLaneSample(TrackType track)
{
    std::scoped_lock lock(projectMutex);
    if (!laneSampleBank.hasSamples(track))
        rescanLaneSamplesLocked();
    const bool ok = laneSampleBank.selectNext(track);
    if (auto* state = findTrackState(track))
    {
        state->selectedSampleIndex = laneSampleBank.getSelectedIndex(track);
        state->selectedSampleName = laneSampleBank.getSelectedName(track);
    }

    return ok;
}

bool BoomBapGeneratorAudioProcessor::selectPreviousLaneSample(TrackType track)
{
    std::scoped_lock lock(projectMutex);
    if (!laneSampleBank.hasSamples(track))
        rescanLaneSamplesLocked();
    const bool ok = laneSampleBank.selectPrevious(track);
    if (auto* state = findTrackState(track))
    {
        state->selectedSampleIndex = laneSampleBank.getSelectedIndex(track);
        state->selectedSampleName = laneSampleBank.getSelectedName(track);
    }

    return ok;
}

juce::String BoomBapGeneratorAudioProcessor::getSelectedLaneSampleName(TrackType track) const
{
    std::scoped_lock lock(projectMutex);
    return laneSampleBank.getSelectedName(track);
}

bool BoomBapGeneratorAudioProcessor::selectNextLaneSample(const RuntimeLaneId& laneId)
{
    const auto type = resolveTrackTypeFromLane(laneId);
    return type.has_value() ? selectNextLaneSample(*type) : false;
}

bool BoomBapGeneratorAudioProcessor::selectPreviousLaneSample(const RuntimeLaneId& laneId)
{
    const auto type = resolveTrackTypeFromLane(laneId);
    return type.has_value() ? selectPreviousLaneSample(*type) : false;
}

int BoomBapGeneratorAudioProcessor::getBassKeyRootChoice() const
{
    const auto* value = apvts.getRawParameterValue(ParamIds::keyRoot);
    return value != nullptr ? static_cast<int>(value->load()) : 0;
}

int BoomBapGeneratorAudioProcessor::getBassScaleModeChoice() const
{
    const auto* value = apvts.getRawParameterValue(ParamIds::scaleMode);
    return value != nullptr ? static_cast<int>(value->load()) : 0;
}

void BoomBapGeneratorAudioProcessor::setBassKeyRootChoice(int choice)
{
    setFloatParameterValue(ParamIds::keyRoot, static_cast<float>(juce::jlimit(0, 11, choice)));
}

void BoomBapGeneratorAudioProcessor::setBassScaleModeChoice(int choice)
{
    setFloatParameterValue(ParamIds::scaleMode, static_cast<float>(juce::jlimit(0, 2, choice)));
}

void BoomBapGeneratorAudioProcessor::auditionSub808Note(int pitch, int velocity, int lengthTicks)
{
    std::scoped_lock lock(projectMutex);
    if (!laneSampleBank.hasSamples(TrackType::Sub808))
        rescanLaneSamplesLocked();

    pendingPreviewNotes.push_back({ TrackType::Sub808,
                                    juce::jlimit(0, 127, pitch),
                                    juce::jlimit(1, 127, velocity),
                                    juce::jmax(1, lengthTicks) });
    if (pendingPreviewNotes.size() > 32)
        pendingPreviewNotes.erase(pendingPreviewNotes.begin(), pendingPreviewNotes.end() - 32);
}

void BoomBapGeneratorAudioProcessor::setSub808TrackNotes(TrackType track, const std::vector<Sub808NoteEvent>& notes)
{
    if (track != TrackType::Sub808)
        return;

    std::scoped_lock lock(projectMutex);
    project.params = buildParamsFromState(lastTransport);
    ProjectStateController::setSub808TrackNotes(project, track, notes);
    captureEditedTrackPerformanceBaseLocked(track);
    rebuildMidiCache();
}

void BoomBapGeneratorAudioProcessor::setSub808TrackNotes(const RuntimeLaneId& laneId, const std::vector<Sub808NoteEvent>& notes)
{
    std::scoped_lock lock(projectMutex);
    project.params = buildParamsFromState(lastTransport);
    ProjectStateController::setSub808TrackNotes(project, laneId, notes);
    if (const auto type = ProjectLaneAccess::backingTrackTypeForLaneId(project, laneId); type.has_value())
        captureEditedTrackPerformanceBaseLocked(*type);
    rebuildMidiCache();
}

void BoomBapGeneratorAudioProcessor::setSub808LaneSettings(TrackType track, const Sub808LaneSettings& settings)
{
    std::scoped_lock lock(projectMutex);
    ProjectStateController::setSub808LaneSettings(project, track, settings);
    rebuildMidiCache();
}

void BoomBapGeneratorAudioProcessor::setSub808LaneSettings(const RuntimeLaneId& laneId, const Sub808LaneSettings& settings)
{
    std::scoped_lock lock(projectMutex);
    ProjectStateController::setSub808LaneSettings(project, laneId, settings);
    rebuildMidiCache();
}

void BoomBapGeneratorAudioProcessor::setTrackSolo(TrackType track, bool value)
{
    std::scoped_lock lock(projectMutex);
    ProjectStateController::setTrackSolo(project, track, value);
    rebuildMidiCache();
}

void BoomBapGeneratorAudioProcessor::setTrackSolo(const RuntimeLaneId& laneId, bool value)
{
    const auto type = resolveTrackTypeFromLane(laneId);
    if (!type.has_value())
        return;

    setTrackSolo(*type, value);
}

void BoomBapGeneratorAudioProcessor::setTrackMuted(TrackType track, bool value)
{
    std::scoped_lock lock(projectMutex);
    ProjectStateController::setTrackMuted(project, track, value);
    rebuildMidiCache();
}

void BoomBapGeneratorAudioProcessor::setTrackMuted(const RuntimeLaneId& laneId, bool value)
{
    const auto type = resolveTrackTypeFromLane(laneId);
    if (!type.has_value())
        return;

    setTrackMuted(*type, value);
}

void BoomBapGeneratorAudioProcessor::setTrackLocked(TrackType track, bool value)
{
    std::scoped_lock lock(projectMutex);
    ProjectStateController::setTrackLocked(project, track, value);
}

void BoomBapGeneratorAudioProcessor::setTrackLocked(const RuntimeLaneId& laneId, bool value)
{
    const auto type = resolveTrackTypeFromLane(laneId);
    if (!type.has_value())
        return;

    setTrackLocked(*type, value);
}

void BoomBapGeneratorAudioProcessor::setTrackEnabled(TrackType track, bool value)
{
    std::scoped_lock lock(projectMutex);
    ProjectStateController::setTrackEnabled(project, track, value);
    rebuildMidiCache();
}

void BoomBapGeneratorAudioProcessor::setTrackEnabled(const RuntimeLaneId& laneId, bool value)
{
    const auto type = resolveTrackTypeFromLane(laneId);
    if (!type.has_value())
        return;

    setTrackEnabled(*type, value);
}

void BoomBapGeneratorAudioProcessor::setTrackLaneVolume(TrackType track, float volume)
{
    std::scoped_lock lock(projectMutex);
    ProjectStateController::setTrackLaneVolume(project, track, volume);
    rebuildMidiCache();
}

void BoomBapGeneratorAudioProcessor::setTrackLaneVolume(const RuntimeLaneId& laneId, float volume)
{
    const auto type = resolveTrackTypeFromLane(laneId);
    if (!type.has_value())
        return;

    setTrackLaneVolume(*type, volume);
}

void BoomBapGeneratorAudioProcessor::setTrackNotes(TrackType track, const std::vector<NoteEvent>& notes)
{
    std::scoped_lock lock(projectMutex);
    project.params = buildParamsFromState(lastTransport);
    ProjectStateController::setTrackNotes(project, track, notes);
    captureEditedTrackPerformanceBaseLocked(track);
    rebuildMidiCache();
}

void BoomBapGeneratorAudioProcessor::setTrackNotes(const RuntimeLaneId& laneId, const std::vector<NoteEvent>& notes)
{
    std::scoped_lock lock(projectMutex);
    project.params = buildParamsFromState(lastTransport);
    ProjectStateController::setTrackNotes(project, laneId, notes);
    if (const auto type = ProjectLaneAccess::backingTrackTypeForLaneId(project, laneId); type.has_value())
        captureEditedTrackPerformanceBaseLocked(*type);
    rebuildMidiCache();
}

void BoomBapGeneratorAudioProcessor::setSelectedTrack(TrackType track)
{
    std::scoped_lock lock(projectMutex);
    ProjectStateController::setSelectedTrack(project, track);
}

void BoomBapGeneratorAudioProcessor::setSelectedTrack(const RuntimeLaneId& laneId)
{
    std::scoped_lock lock(projectMutex);
    ProjectStateController::setSelectedTrack(project, laneId);
}

void BoomBapGeneratorAudioProcessor::setSoundModuleTrack(const std::optional<TrackType>& track)
{
    std::scoped_lock lock(projectMutex);
    ProjectStateController::setSoundModuleTrack(project, track);
}

std::optional<TrackType> BoomBapGeneratorAudioProcessor::getSoundModuleTrack() const
{
    std::scoped_lock lock(projectMutex);
    return SoundTargetController::toLegacyTrackTypeAlias(SoundTargetController::resolveProjectSelection(project));
}

void BoomBapGeneratorAudioProcessor::setSoundModuleTarget(const SoundTargetDescriptor& descriptor)
{
    std::scoped_lock lock(projectMutex);
    ProjectStateController::setSoundModuleTarget(project, descriptor);
}

SoundTargetDescriptor BoomBapGeneratorAudioProcessor::getSoundModuleTarget() const
{
    std::scoped_lock lock(projectMutex);
    return SoundTargetController::resolveProjectSelection(project);
}

float BoomBapGeneratorAudioProcessor::getSoundLayerGainReductionDb(const SoundTargetDescriptor& descriptor) const
{
    std::scoped_lock lock(projectMutex);

    const auto resolved = SoundTargetController::sanitizeDescriptor(project, descriptor);
    const auto soundState = sanitizeSoundLayer(SoundTargetController::resolveSoundState(project, resolved));
    if (!isCompressorAudiblyActive(soundState.compressor))
        return 0.0f;

    if (resolved.isGlobal())
        return globalFxRuntimeState.compressor.getLastGainReductionDb();

    if (const auto trackType = SoundTargetController::toLegacyTrackTypeAlias(resolved); trackType.has_value())
        return laneFxRuntimeStates[static_cast<size_t>(*trackType)].compressor.getLastGainReductionDb();

    return 0.0f;
}

void BoomBapGeneratorAudioProcessor::setTrackSoundLayer(TrackType track, const SoundLayerState& state)
{
    auto sanitizedState = sanitizeSoundLayer(state);
    sanitizedState.monstaFx.pendingChaosReseed = false;
    std::scoped_lock lock(projectMutex);
    ProjectStateController::setTrackSoundLayer(project, track, sanitizedState);
}

void BoomBapGeneratorAudioProcessor::setTrackSoundLayer(const RuntimeLaneId& laneId, const SoundLayerState& state)
{
    const auto type = resolveTrackTypeFromLane(laneId);
    if (!type.has_value())
        return;

    setTrackSoundLayer(*type, state);
}

void BoomBapGeneratorAudioProcessor::setGlobalSoundLayer(const SoundLayerState& state)
{
    auto sanitizedState = sanitizeSoundLayer(state);
    sanitizedState.monstaFx.pendingChaosReseed = false;
    std::scoped_lock lock(projectMutex);
    ProjectStateController::setGlobalSoundLayer(project, sanitizedState);
}

void BoomBapGeneratorAudioProcessor::setSoundLayerForTarget(const SoundTargetDescriptor& descriptor, const SoundLayerState& state)
{
    auto sanitizedState = sanitizeSoundLayer(state);
    sanitizedState.monstaFx.pendingChaosReseed = false;
    std::scoped_lock lock(projectMutex);
    ProjectStateController::setSoundLayerForTarget(project, descriptor, sanitizedState);
}

void BoomBapGeneratorAudioProcessor::setHatFxDragDensity(float density, bool lockDragDensity)
{
    std::scoped_lock lock(projectMutex);
    hatFxDragDensity = juce::jlimit(0.0f, 2.0f, density);
    hatFxDragDensityLocked = lockDragDensity;
}

float BoomBapGeneratorAudioProcessor::getHatFxDragDensity() const
{
    std::scoped_lock lock(projectMutex);
    return hatFxDragDensity;
}

bool BoomBapGeneratorAudioProcessor::isHatFxDragDensityLocked() const
{
    std::scoped_lock lock(projectMutex);
    return hatFxDragDensityLocked;
}

void BoomBapGeneratorAudioProcessor::clearTrack(TrackType track)
{
    std::scoped_lock lock(projectMutex);
    project.params = buildParamsFromState(lastTransport);
    ProjectStateController::clearTrack(project, track);
    captureEditedTrackPerformanceBaseLocked(track);
    rebuildMidiCache();
}

void BoomBapGeneratorAudioProcessor::clearTrack(const RuntimeLaneId& laneId)
{
    std::scoped_lock lock(projectMutex);
    project.params = buildParamsFromState(lastTransport);
    ProjectStateController::clearTrack(project, laneId);
    if (const auto type = ProjectLaneAccess::backingTrackTypeForLaneId(project, laneId); type.has_value())
        captureEditedTrackPerformanceBaseLocked(*type);
    rebuildMidiCache();
}

juce::File BoomBapGeneratorAudioProcessor::getLaneSampleDirectory(TrackType track) const
{
    std::scoped_lock lock(projectMutex);
    return sampleLibraryManager.getResolvedRootDirectory()
        .getChildFile(SampleLibraryManager::folderNameForGenre(project.params.genre))
        .getChildFile(SampleLibraryManager::folderNameForTrack(track));
}

juce::File BoomBapGeneratorAudioProcessor::getLaneSampleDirectory(const RuntimeLaneId& laneId) const
{
    const auto type = resolveTrackTypeFromLane(laneId);
    return type.has_value() ? getLaneSampleDirectory(*type) : juce::File {};
}

bool BoomBapGeneratorAudioProcessor::importLaneSample(TrackType track, const juce::File& sourceFile, juce::String* errorMessage)
{
    if (!sourceFile.existsAsFile())
    {
        if (errorMessage != nullptr)
            *errorMessage = "Sample file not found.";
        return false;
    }

    std::scoped_lock lock(projectMutex);
    auto targetDirectory = sampleLibraryManager.getResolvedRootDirectory()
        .getChildFile(SampleLibraryManager::folderNameForGenre(project.params.genre))
        .getChildFile(SampleLibraryManager::folderNameForTrack(track));
    if (!targetDirectory.createDirectory())
    {
        if (errorMessage != nullptr)
            *errorMessage = "Failed to create lane sample directory.";
        return false;
    }

    const auto targetFile = targetDirectory.getNonexistentChildFile(sourceFile.getFileNameWithoutExtension(),
                                                                    sourceFile.getFileExtension(),
                                                                    false);
    if (!sourceFile.copyFileTo(targetFile))
    {
        if (errorMessage != nullptr)
            *errorMessage = "Failed to copy sample into lane directory.";
        return false;
    }

    rescanLaneSamplesLocked();
    if (auto* state = findTrackState(track))
    {
        state->selectedSampleIndex = laneSampleBank.getSelectedIndex(track);
        state->selectedSampleName = laneSampleBank.getSelectedName(track);
    }

    return true;
}

bool BoomBapGeneratorAudioProcessor::importLaneSample(const RuntimeLaneId& laneId,
                                                      const juce::File& sourceFile,
                                                      juce::String* errorMessage)
{
    const auto type = resolveTrackTypeFromLane(laneId);
    if (!type.has_value())
    {
        if (errorMessage != nullptr)
            *errorMessage = "Lane does not have a backing track.";
        return false;
    }

    return importLaneSample(*type, sourceFile, errorMessage);
}

bool BoomBapGeneratorAudioProcessor::deleteSelectedLaneSample(TrackType track, juce::String* errorMessage)
{
    std::scoped_lock lock(projectMutex);
    const auto& samples = sampleLibraryManager.getSamples(track);
    const int selectedIndex = laneSampleBank.getSelectedIndex(track);
    if (selectedIndex < 0 || selectedIndex >= static_cast<int>(samples.size()))
    {
        if (errorMessage != nullptr)
            *errorMessage = "No selected sample to delete.";
        return false;
    }

    const auto targetFile = samples[static_cast<size_t>(selectedIndex)].file;
    if (!targetFile.existsAsFile() || !targetFile.deleteFile())
    {
        if (errorMessage != nullptr)
            *errorMessage = "Failed to delete selected sample.";
        return false;
    }

    rescanLaneSamplesLocked();
    if (auto* state = findTrackState(track))
    {
        state->selectedSampleIndex = laneSampleBank.getSelectedIndex(track);
        state->selectedSampleName = laneSampleBank.getSelectedName(track);
    }

    return true;
}

bool BoomBapGeneratorAudioProcessor::deleteSelectedLaneSample(const RuntimeLaneId& laneId, juce::String* errorMessage)
{
    const auto type = resolveTrackTypeFromLane(laneId);
    if (!type.has_value())
    {
        if (errorMessage != nullptr)
            *errorMessage = "Lane does not have a backing track.";
        return false;
    }

    return deleteSelectedLaneSample(*type, errorMessage);
}

bool BoomBapGeneratorAudioProcessor::exportFullPatternToFile(const juce::File& targetFile) const
{
    logDrag("exportFullPatternToFile begin file=" + targetFile.getFullPathName());
    PatternProject snapshot;
    {
        std::scoped_lock lock(projectMutex);
        snapshot = project;
    }

    const bool ok = MidiExportEngine::saveMultiTrackMidiFile(snapshot, targetFile, 960);
    logDrag("exportFullPatternToFile done ok=" + juce::String(ok ? "1" : "0"));
    return ok;
}

bool BoomBapGeneratorAudioProcessor::exportTrackToFile(TrackType track, const juce::File& targetFile) const
{
    logDrag("exportTrackToFile begin track=" + juce::String(static_cast<int>(track)) + " file=" + targetFile.getFullPathName());
    PatternProject snapshot;
    {
        std::scoped_lock lock(projectMutex);
        snapshot = project;
    }

    const bool ok = MidiExportEngine::saveMidiFile(snapshot, targetFile, track, 960, false, false);
    logDrag("exportTrackToFile done ok=" + juce::String(ok ? "1" : "0") + " track=" + juce::String(static_cast<int>(track)));
    return ok;
}

bool BoomBapGeneratorAudioProcessor::exportTrackToFile(const RuntimeLaneId& laneId, const juce::File& targetFile) const
{
    const auto type = resolveTrackTypeFromLane(laneId);
    return type.has_value() ? exportTrackToFile(*type, targetFile) : false;
}

bool BoomBapGeneratorAudioProcessor::exportLoopWavToFile(const juce::File& targetFile) const
{
    juce::ignoreUnused(targetFile);
    return false;
}

juce::File BoomBapGeneratorAudioProcessor::createTemporaryFullPatternMidiFile() const
{
    try
    {
        logDrag("createTemporaryFullPatternMidiFile begin");
        PatternProject snapshot;
        {
            std::scoped_lock lock(projectMutex);
            snapshot = project;
        }

        logDrag("createTemporaryFullPatternMidiFile snapshot tracks=" + juce::String(static_cast<int>(snapshot.tracks.size())));
        auto file = TemporaryMidiExportService::createTempFileForPattern(snapshot);
        logDrag("createTemporaryFullPatternMidiFile done exists=" + juce::String(file.existsAsFile() ? "1" : "0")
                + " path=" + file.getFullPathName());
        return file;
    }
    catch (...)
    {
        logDrag("createTemporaryFullPatternMidiFile exception");
        return {};
    }
}

juce::File BoomBapGeneratorAudioProcessor::createTemporaryTrackMidiFile(TrackType track) const
{
    try
    {
        logDrag("createTemporaryTrackMidiFile begin track=" + juce::String(static_cast<int>(track)));
        PatternProject snapshot;
        {
            std::scoped_lock lock(projectMutex);
            snapshot = project;
        }

        logDrag("createTemporaryTrackMidiFile snapshot tracks=" + juce::String(static_cast<int>(snapshot.tracks.size()))
                + " track=" + juce::String(static_cast<int>(track)));
        auto file = TemporaryMidiExportService::createTempFileForTrack(snapshot, track);
        logDrag("createTemporaryTrackMidiFile done exists=" + juce::String(file.existsAsFile() ? "1" : "0")
                + " track=" + juce::String(static_cast<int>(track))
                + " path=" + file.getFullPathName());
        return file;
    }
    catch (...)
    {
        logDrag("createTemporaryTrackMidiFile exception track=" + juce::String(static_cast<int>(track)));
        return {};
    }
}

juce::File BoomBapGeneratorAudioProcessor::createTemporaryTrackMidiFile(const RuntimeLaneId& laneId) const
{
    const auto type = resolveTrackTypeFromLane(laneId);
    return type.has_value() ? createTemporaryTrackMidiFile(*type) : juce::File {};
}

PatternProject BoomBapGeneratorAudioProcessor::getProjectSnapshot() const
{
    std::scoped_lock lock(projectMutex);
    return project;
}

EqDisplayAnalyzerState BoomBapGeneratorAudioProcessor::getEqDisplayAnalyzerState() const
{
    EqDisplayAnalyzerState state;
    state.rms = eqDisplayAnalyzerRms.load(std::memory_order_relaxed);
    state.active = eqDisplayAnalyzerActive.load(std::memory_order_relaxed);
    for (int index = 0; index < kEqDisplayAnalyzerBinCount; ++index)
        state.magnitudes[static_cast<size_t>(index)] = eqDisplayAnalyzerMagnitudes[static_cast<size_t>(index)].load(std::memory_order_relaxed);
    return state;
}

TransportSnapshot BoomBapGeneratorAudioProcessor::getLastTransportSnapshot() const
{
    std::scoped_lock lock(projectMutex);
    return lastTransport;
}

void BoomBapGeneratorAudioProcessor::setSampleAnalysisRequest(const SampleAnalysisRequest& request)
{
    std::scoped_lock lock(projectMutex);
    currentAnalysisRequest = request;
}

SampleAnalysisRequest BoomBapGeneratorAudioProcessor::getSampleAnalysisRequest() const
{
    std::scoped_lock lock(projectMutex);
    return currentAnalysisRequest;
}

bool BoomBapGeneratorAudioProcessor::analyzeCurrentSampleSource(juce::String* errorMessage)
{
    SampleAnalysisRequest request;
    {
        std::scoped_lock lock(projectMutex);
        request = currentAnalysisRequest;
    }

    if (request.source == SampleAnalysisRequest::SourceType::AudioFile)
        return analyzeAudioFile(request.audioFile, errorMessage);

    if (errorMessage != nullptr)
        *errorMessage = "Live input analysis is not available yet.";
    return false;
}

bool BoomBapGeneratorAudioProcessor::analyzeAudioFile(const juce::File& file, juce::String* errorMessage)
{
    SampleAnalysisRequest request;
    double hostBpm = 0.0;
    AnalysisMode mode = AnalysisMode::Off;
    {
        std::scoped_lock lock(projectMutex);
        request = currentAnalysisRequest;
        request.source = SampleAnalysisRequest::SourceType::AudioFile;
        request.audioFile = file;
        hostBpm = lastTransport.hasHostTempo && lastTransport.bpm > 0.0 ? lastTransport.bpm : static_cast<double>(project.params.bpm);
        mode = analysisMode;
    }

    if (mode == AnalysisMode::GenerateFromSample || mode == AnalysisMode::ExtractFromSample)
    {
        request.buildLaneEvidence = true;
        request.buildTranscription = true;
        request.buildGenerationHints = true;
        request.detectBassline = true;
        request.detectDrumEvents = true;
        request.usePercussiveHarmonicSeparation = true;
    }

    const auto bundle = sampleAnalyzer.analyzeAudioFileExtended(file, request, hostBpm, errorMessage);
    const int extractedBars = bundle.summary.valid ? juce::jlimit(1, 16, bundle.summary.analyzedBars) : 0;

    if (mode == AnalysisMode::ExtractFromSample && extractedBars > 0)
        setFloatParameterValue(ParamIds::bars, static_cast<float>(choiceIndexFromBars(extractedBars)));

    std::scoped_lock lock(projectMutex);
    currentAnalysisRequest = request;
    currentAnalysisBundle = bundle;
    currentAnalysisResult = bundle.summary;
    currentFeatureMap = bundle.featureMap;
    analysisReady = bundle.summary.valid;
    updateSampleAwareContextLocked();

    if (analysisReady && mode == AnalysisMode::ExtractFromSample)
        extractPatternFromAnalyzedSampleLocked();

    return bundle.summary.valid;
}

bool BoomBapGeneratorAudioProcessor::extractPatternFromAnalyzedSample()
{
    std::scoped_lock lock(projectMutex);
    return extractPatternFromAnalyzedSampleLocked();
}

void BoomBapGeneratorAudioProcessor::clearSampleAnalysis()
{
    std::scoped_lock lock(projectMutex);
    currentAnalysisBundle = {};
    currentAnalysisResult = {};
    currentFeatureMap = {};
    lastSampleApplyDebug.clear();
    analysisReady = false;
    updateSampleAwareContextLocked();
}

void BoomBapGeneratorAudioProcessor::setAnalysisMode(AnalysisMode mode)
{
    std::scoped_lock lock(projectMutex);
    analysisMode = mode;
    sampleAwareModeEnabled = mode == AnalysisMode::GenerateFromSample || mode == AnalysisMode::ExtractFromSample;

    if (sampleAwareModeEnabled)
    {
        currentAnalysisRequest.buildLaneEvidence = true;
        currentAnalysisRequest.buildTranscription = true;
        currentAnalysisRequest.buildGenerationHints = true;
        currentAnalysisRequest.detectBassline = true;
        currentAnalysisRequest.detectDrumEvents = true;
        currentAnalysisRequest.usePercussiveHarmonicSeparation = true;
    }

    updateSampleAwareContextLocked();
}

AnalysisMode BoomBapGeneratorAudioProcessor::getAnalysisMode() const
{
    std::scoped_lock lock(projectMutex);
    return analysisMode;
}

void BoomBapGeneratorAudioProcessor::setSampleAwareModeEnabled(bool enabled)
{
    std::scoped_lock lock(projectMutex);
    sampleAwareModeEnabled = enabled;
    updateSampleAwareContextLocked();
}

bool BoomBapGeneratorAudioProcessor::isSampleAwareModeEnabled() const
{
    std::scoped_lock lock(projectMutex);
    return sampleAwareModeEnabled;
}

bool BoomBapGeneratorAudioProcessor::isSampleAnalysisReady() const
{
    std::scoped_lock lock(projectMutex);
    return analysisReady;
}

void BoomBapGeneratorAudioProcessor::setSampleApplyMode(SampleApplyMode mode)
{
    setFloatParameterValue(ParamIds::sampleApplyMode,
                           static_cast<float>(choiceIndexFromSampleApplyMode(mode)));

    std::scoped_lock lock(projectMutex);
    updateSampleAwareContextLocked();
}

SampleApplyMode BoomBapGeneratorAudioProcessor::getSampleApplyMode() const
{
    return sampleApplyModeFromState(apvts);
}

void BoomBapGeneratorAudioProcessor::setSampleReactivity(float value)
{
    std::scoped_lock lock(projectMutex);
    currentSampleContext.reactivity = juce::jlimit(0.0f, 1.0f, value);
}

void BoomBapGeneratorAudioProcessor::setSupportVsContrast(float value)
{
    std::scoped_lock lock(projectMutex);
    currentSampleContext.supportVsContrast = juce::jlimit(0.0f, 1.0f, value);
}

SampleAnalysisResult BoomBapGeneratorAudioProcessor::getSampleAnalysisResult() const
{
    std::scoped_lock lock(projectMutex);
    return currentAnalysisResult;
}

AudioFeatureMap BoomBapGeneratorAudioProcessor::getAudioFeatureMap() const
{
    std::scoped_lock lock(projectMutex);
    return currentFeatureMap;
}

SampleAwareGenerationContext BoomBapGeneratorAudioProcessor::getSampleAwareGenerationContext() const
{
    std::scoped_lock lock(projectMutex);
    return currentSampleContext;
}

juce::String BoomBapGeneratorAudioProcessor::getGenerationDebugSummary() const
{
    std::scoped_lock lock(projectMutex);
    juce::StringArray lines;
    lines.add("Analysis mode: " + analysisModeDisplayName(analysisMode));
    lines.add("Sample-aware: " + juce::String(sampleAwareModeEnabled ? "on" : "off"));
    lines.add("Analysis ready: " + juce::String(analysisReady ? "yes" : "no"));
    lines.add("Detected BPM: " + juce::String(currentAnalysisResult.detectedBpm, 2));
    lines.add("Bars: " + juce::String(currentAnalysisResult.analyzedBars));
    lines.add("Support vs Contrast: " + juce::String(currentSampleContext.supportVsContrast, 2));
    lines.add("Reactivity: " + juce::String(currentSampleContext.reactivity, 2));
    lines.add("Apply mode: " + sampleApplyModeDisplayName(currentSampleContext.applyMode));
    lines.add("Apply weights: " + describeSampleApplyWeights(currentSampleContext.applyWeights));
    lines.add("Lane evidence: " + juce::String(static_cast<int>(currentAnalysisBundle.laneEvidence.steps.size())) + " steps");
    lines.add("Transcription: drums " + juce::String(static_cast<int>(currentAnalysisBundle.transcription.drumEvents.size()))
              + " | bass " + juce::String(static_cast<int>(currentAnalysisBundle.transcription.bassEvents.size())));
    lines.add("Hints: kick " + juce::String(static_cast<int>(currentAnalysisBundle.hints.kickStepWeights.size()))
              + " | snare " + juce::String(static_cast<int>(currentAnalysisBundle.hints.snareStepWeights.size()))
              + " | bass " + juce::String(static_cast<int>(currentAnalysisBundle.hints.bassStepWeights.size())));
    lines.add("Copy bias: drums " + juce::String(currentSampleContext.preferCopyDrums ? "yes" : "no")
              + " | bass " + juce::String(currentSampleContext.preferCopyBass ? "yes" : "no"));
    if (lastSampleApplyDebug.isNotEmpty())
        lines.add(lastSampleApplyDebug);

    const auto analysisSummary = lines.joinIntoString("\n");
    if (project.generationDebugReport.trim().isEmpty())
        return analysisSummary;

    return project.generationDebugReport + "\n\nAnalysis\n" + analysisSummary;
}

void BoomBapGeneratorAudioProcessor::updateSampleAwareContextLocked()
{
    currentAnalysisResult = currentAnalysisBundle.summary;
    currentFeatureMap = currentAnalysisBundle.featureMap;
    currentSampleContext.featureMap = currentAnalysisBundle.featureMap;
    currentSampleContext.laneEvidence = currentAnalysisBundle.laneEvidence;
    currentSampleContext.transcription = currentAnalysisBundle.transcription;
    currentSampleContext.hints = currentAnalysisBundle.hints;
    currentSampleContext.applyMode = sampleApplyModeFromState(apvts);
    currentSampleContext.applyWeights = makeSampleApplyWeights(currentSampleContext.applyMode, analysisMode);

    const bool modeUsesGuidance = analysisMode == AnalysisMode::GenerateFromSample
        || analysisMode == AnalysisMode::ExtractFromSample;
    const bool sampleLedDrums = currentSampleContext.applyWeights.exactCopy
        || currentSampleContext.applyWeights.extractedDrumsWeight >= currentSampleContext.applyWeights.generatedDrumsWeight;
    const bool sampleLedBass = currentSampleContext.applyWeights.exactCopy
        || currentSampleContext.applyWeights.extractedBassWeight >= currentSampleContext.applyWeights.generatedBassWeight;
    currentSampleContext.preferCopyDrums = modeUsesGuidance
        && currentAnalysisBundle.transcription.hasDetectedDrums
        && sampleLedDrums;
    currentSampleContext.preferCopyBass = modeUsesGuidance
        && currentAnalysisBundle.transcription.hasDetectedBass
        && sampleLedBass;
    currentSampleContext.enabled = sampleAwareModeEnabled && analysisReady && modeUsesGuidance;
}

bool BoomBapGeneratorAudioProcessor::applySampleAwarePostProcessLocked()
{
    lastSampleApplyDebug.clear();

    const bool modeUsesGuidance = analysisMode == AnalysisMode::GenerateFromSample
        || analysisMode == AnalysisMode::ExtractFromSample;
    if (!analysisReady || !modeUsesGuidance)
        return false;

    const auto extracted = ExtractPatternBuilder::build(currentAnalysisBundle);
    if (!extracted.hasAnyContent())
    {
        lastSampleApplyDebug = sampleApplySummaryLine(currentSampleContext) + "\nSample apply result: no extracted drum or bass pattern was available.";
        return false;
    }

    auto blendReport = PatternBlendEngine::apply(project, extracted, currentSampleContext.applyWeights);
    if (blendReport.changedTracks.empty())
    {
        lastSampleApplyDebug = sampleApplySummaryLine(currentSampleContext) + "\nSample apply result: current lanes already matched the extracted pattern.";
        return false;
    }

    SubstyleRuleReport ruleReport;
    if (!currentSampleContext.applyWeights.exactCopy)
    {
        ruleReport = SubstyleRuleEnforcer::enforce(project);
        blendReport.changedTracks.insert(ruleReport.changedTracks.begin(), ruleReport.changedTracks.end());
    }

    PatternPerformanceTransformEngine::captureBasePatterns(project, blendReport.changedTracks);

    lastSampleApplyDebug = sampleApplySummaryLine(currentSampleContext)
        + "\n" + describePatternBlendReport(blendReport);
    if (ruleReport.applied)
        lastSampleApplyDebug += "\n" + describeSubstyleRuleReport(ruleReport);

    return true;
}

bool BoomBapGeneratorAudioProcessor::extractPatternFromAnalyzedSampleLocked()
{
    if (!analysisReady)
        return false;

    const auto extracted = ExtractPatternBuilder::build(currentAnalysisBundle);
    if (!extracted.hasAnyContent())
        return false;

    const auto beforeProject = project;
    project.params = buildParamsFromState(lastTransport);
    if (extracted.bars > 0)
        ProjectStateController::setBars(project, extracted.bars);

    project.sampleContext = currentSampleContext;

    if (!currentSampleContext.applyWeights.exactCopy)
    {
        switch (project.params.genre)
        {
            case GenreType::Drill: drillEngine.generate(project); break;
            case GenreType::Rap: rapEngine.generate(project); break;
            case GenreType::Trap: trapEngine.generate(project); break;
            case GenreType::BoomBap:
            default: boomBapEngine.generate(project); break;
        }
    }

    if (!applySampleAwarePostProcessLocked())
        return false;

    project.sampleContext = currentSampleContext;
    project.phraseLengthBars = juce::jmax(1, extracted.bars);
    const juce::String extractLabel = currentSampleContext.applyWeights.exactCopy ? "sample_exact_copy" : "sample_blend";
    project.phraseRoleSummary = currentAnalysisBundle.summary.phraseBoundaryBars.empty()
        ? extractLabel
        : (extractLabel + " | boundaries " + juce::String(static_cast<int>(currentAnalysisBundle.summary.phraseBoundaryBars.size())));
    project.generationDebugReport = buildGenerationDebugReport("Extract From Sample",
                                                               beforeProject,
                                                               project,
                                                               std::nullopt,
                                                               sampleAwareModeEnabled,
                                                               analysisReady);
    if (lastSampleApplyDebug.isNotEmpty())
        project.generationDebugReport += "\n" + lastSampleApplyDebug;
    ++project.generationCounter;
    rebuildMidiCache();
    return true;
}

void BoomBapGeneratorAudioProcessor::applySelectedStylePreset(bool force)
{
    if (isShuttingDown())
        return;

    const auto* genreValue = apvts.getRawParameterValue(ParamIds::genre);
    const auto* boombapSubstyleValue = apvts.getRawParameterValue(ParamIds::boombapSubstyle);
    const auto* rapSubstyleValue = apvts.getRawParameterValue(ParamIds::rapSubstyle);
    const auto* trapSubstyleValue = apvts.getRawParameterValue(ParamIds::trapSubstyle);
    const auto* drillSubstyleValue = apvts.getRawParameterValue(ParamIds::drillSubstyle);

    const int genreChoice = genreValue != nullptr ? static_cast<int>(genreValue->load()) : 0;
    const int boombapChoice = boombapSubstyleValue != nullptr ? static_cast<int>(boombapSubstyleValue->load()) : 0;
    const int rapChoice = rapSubstyleValue != nullptr ? static_cast<int>(rapSubstyleValue->load()) : 0;
    const int trapChoice = trapSubstyleValue != nullptr ? static_cast<int>(trapSubstyleValue->load()) : 0;
    const int drillChoice = drillSubstyleValue != nullptr ? clampDrillSubstyleChoice(static_cast<int>(drillSubstyleValue->load())) : 0;

    const bool changed = force
        || genreChoice != lastAppliedGenreChoice
        || boombapChoice != lastAppliedBoomBapSubstyleChoice
        || rapChoice != lastAppliedRapSubstyleChoice
        || trapChoice != lastAppliedTrapSubstyleChoice
        || drillChoice != lastAppliedDrillSubstyleChoice;
    if (!changed)
        return;

    const bool genreChanged = genreChoice != lastAppliedGenreChoice;

    const auto genreType = genreFromChoice(genreChoice);
    int selectedSubstyle = boombapChoice;
    if (genreType == GenreType::Rap)
        selectedSubstyle = rapChoice;
    else if (genreType == GenreType::Trap)
        selectedSubstyle = trapChoice;
    else if (genreType == GenreType::Drill)
        selectedSubstyle = drillChoice;
    const auto& style = getGenreStyleDefaults(genreType, selectedSubstyle);

    setFloatParameterValue(ParamIds::swingPercent, style.swingDefault);
    setFloatParameterValue(ParamIds::velocityAmount, style.velocityDefault);
    setFloatParameterValue(ParamIds::timingAmount, style.timingDefault);
    setFloatParameterValue(ParamIds::humanizeAmount, style.humanizeDefault);
    setFloatParameterValue(ParamIds::densityAmount, style.densityDefault);

    {
        std::scoped_lock lock(projectMutex);
        project.params = buildParamsFromState(lastTransport);
        if (genreChanged)
            rescanLaneSamplesLocked();

        for (auto& track : project.tracks)
        {
            const auto& lane = getLaneStyleDefaults(style, track.type);
            track.enabled = lane.enabledByDefault;
            track.laneVolume = juce::jlimit(0.0f, 1.5f, lane.volumeDefault);
        }
        rebuildMidiCache();
    }

    lastAppliedGenreChoice = genreChoice;
    lastAppliedBoomBapSubstyleChoice = boombapChoice;
    lastAppliedRapSubstyleChoice = rapChoice;
    lastAppliedTrapSubstyleChoice = trapChoice;
    lastAppliedDrillSubstyleChoice = drillChoice;
}

GeneratorParams BoomBapGeneratorAudioProcessor::buildParamsFromState(const TransportSnapshot& snapshot) const
{
    GeneratorParams p;

    const auto bpmValue = apvts.getRawParameterValue(ParamIds::bpm);
    const auto syncValue = apvts.getRawParameterValue(ParamIds::syncDawTempo);
    const auto swingValue = apvts.getRawParameterValue(ParamIds::swingPercent);
    const auto velocityValue = apvts.getRawParameterValue(ParamIds::velocityAmount);
    const auto timingValue = apvts.getRawParameterValue(ParamIds::timingAmount);
    const auto humanizeValue = apvts.getRawParameterValue(ParamIds::humanizeAmount);
    const auto densityValue = apvts.getRawParameterValue(ParamIds::densityAmount);
    const auto tempoInterpretationValue = apvts.getRawParameterValue(ParamIds::tempoInterpretation);
    const auto barsValue = apvts.getRawParameterValue(ParamIds::bars);
    const auto genreValue = apvts.getRawParameterValue(ParamIds::genre);
    const auto keyRootValue = apvts.getRawParameterValue(ParamIds::keyRoot);
    const auto scaleModeValue = apvts.getRawParameterValue(ParamIds::scaleMode);
    const auto substyleValue = apvts.getRawParameterValue(ParamIds::boombapSubstyle);
    const auto rapSubstyleValue = apvts.getRawParameterValue(ParamIds::rapSubstyle);
    const auto trapSubstyleValue = apvts.getRawParameterValue(ParamIds::trapSubstyle);
    const auto drillSubstyleValue = apvts.getRawParameterValue(ParamIds::drillSubstyle);
    const auto seedValue = apvts.getRawParameterValue(ParamIds::seed);
    const auto seedLockValue = apvts.getRawParameterValue(ParamIds::seedLock);

    p.syncDawTempo = syncValue != nullptr && syncValue->load() > 0.5f;
    p.bpm = bpmValue != nullptr ? bpmValue->load() : 90.0f;
    if (p.syncDawTempo && snapshot.hasHostTempo && snapshot.bpm > 0.0)
        p.bpm = static_cast<float>(snapshot.bpm);

    p.swingPercent = swingValue != nullptr ? swingValue->load() : 56.0f;
    p.velocityAmount = velocityValue != nullptr ? velocityValue->load() : 0.5f;
    p.timingAmount = timingValue != nullptr ? timingValue->load() : 0.4f;
    p.humanizeAmount = humanizeValue != nullptr ? humanizeValue->load() : 0.3f;
    p.densityAmount = densityValue != nullptr ? densityValue->load() : 0.5f;
    p.tempoInterpretationMode = tempoInterpretationValue != nullptr ? static_cast<int>(tempoInterpretationValue->load()) : 0;
    p.bars = barsFromChoiceIndex(barsValue != nullptr ? static_cast<int>(barsValue->load()) : 1);
    p.genre = genreFromChoice(genreValue != nullptr ? static_cast<int>(genreValue->load()) : 0);
    p.keyRoot = keyRootValue != nullptr ? static_cast<int>(keyRootValue->load()) : 0;
    p.scaleMode = scaleModeValue != nullptr ? static_cast<int>(scaleModeValue->load()) : 0;
    p.boombapSubstyle = substyleValue != nullptr ? static_cast<int>(substyleValue->load()) : 0;
    p.rapSubstyle = rapSubstyleValue != nullptr ? static_cast<int>(rapSubstyleValue->load()) : 0;
    p.trapSubstyle = trapSubstyleValue != nullptr ? static_cast<int>(trapSubstyleValue->load()) : 0;
    p.drillSubstyle = drillSubstyleValue != nullptr ? clampDrillSubstyleChoice(static_cast<int>(drillSubstyleValue->load())) : 0;
    p.seed = seedValue != nullptr ? static_cast<int>(seedValue->load()) : 1;
    p.seedLock = seedLockValue != nullptr && seedLockValue->load() > 0.5f;

    return p;
}

void BoomBapGeneratorAudioProcessor::advanceSeedForGeneration(std::optional<TrackType> trackForRg)
{
    const auto seedLockValue = apvts.getRawParameterValue(ParamIds::seedLock);
    const bool seedLocked = seedLockValue != nullptr && seedLockValue->load() > 0.5f;
    if (seedLocked)
        return;

    const auto seedValue = apvts.getRawParameterValue(ParamIds::seed);
    int baseSeed = seedValue != nullptr ? static_cast<int>(seedValue->load()) : 1;
    baseSeed = juce::jlimit(1, 999999, baseSeed);

    const int delta = trackForRg.has_value()
        ? 7 + static_cast<int>(*trackForRg) * 3
        : 1;
    const int nextSeed = 1 + ((baseSeed - 1 + delta) % 999999);
    setSeedParameterValue(nextSeed);
}

void BoomBapGeneratorAudioProcessor::setSeedParameterValue(int newSeed)
{
    auto* parameter = apvts.getParameter(ParamIds::seed);
    if (parameter == nullptr)
        return;

    const float normalized = parameter->convertTo0to1(static_cast<float>(juce::jlimit(1, 999999, newSeed)));
    setParameterValueNotifyingHostIfNeeded(*parameter, normalized);
}

void BoomBapGeneratorAudioProcessor::setFloatParameterValue(const juce::String& paramId, float value)
{
    auto* parameter = apvts.getParameter(paramId);
    if (parameter == nullptr)
        return;

    float normalized = value;

    if (auto* choiceParameter = dynamic_cast<juce::AudioParameterChoice*>(parameter))
    {
        const auto& range = choiceParameter->getNormalisableRange();
        int choice = juce::roundToInt(value);
        choice = juce::jlimit(juce::roundToInt(range.start), juce::roundToInt(range.end), choice);

        if (paramId == ParamIds::drillSubstyle)
            choice = clampDrillSubstyleChoice(choice);

        normalized = choiceParameter->convertTo0to1(static_cast<float>(choice));
    }
    else if (auto* intParameter = dynamic_cast<juce::AudioParameterInt*>(parameter))
    {
        const auto range = intParameter->getRange();
        const int intValue = juce::jlimit(range.getStart(), range.getEnd(), juce::roundToInt(value));
        normalized = intParameter->convertTo0to1(static_cast<float>(intValue));
    }
    else if (auto* floatParameter = dynamic_cast<juce::AudioParameterFloat*>(parameter))
    {
        const auto& range = floatParameter->getNormalisableRange();
        const float legalValue = range.snapToLegalValue(juce::jlimit(range.start, range.end, value));
        normalized = floatParameter->convertTo0to1(legalValue);
    }
    else if (auto* boolParameter = dynamic_cast<juce::AudioParameterBool*>(parameter))
    {
        normalized = boolParameter->convertTo0to1(value >= 0.5f ? 1.0f : 0.0f);
    }
    else
    {
        normalized = parameter->convertTo0to1(value);
    }

    setParameterValueNotifyingHostIfNeeded(*parameter, normalized);
}

bool BoomBapGeneratorAudioProcessor::shouldSuppressHostParameterWrites() const noexcept
{
    return isShuttingDown()
        || audioRenderSuspended.load(std::memory_order_acquire);
}

void BoomBapGeneratorAudioProcessor::setParameterValueNotifyingHostIfNeeded(juce::AudioProcessorParameter& parameter,
                                                                            float normalizedValue) const
{
    if (shouldSuppressHostParameterWrites())
        return;

    const float clampedNormalizedValue = juce::jlimit(0.0f, 1.0f, normalizedValue);
    if (normalizedValuesEqual(parameter.getValue(), clampedNormalizedValue))
        return;

    parameter.beginChangeGesture();
    parameter.setValueNotifyingHost(clampedNormalizedValue);
    parameter.endChangeGesture();
}

BoomBapGeneratorAudioProcessor::LiveRenderUpdate BoomBapGeneratorAudioProcessor::syncLivePerformanceStateLocked(const GeneratorParams& liveParams)
{
    PatternPerformanceTransformEngine::backfillMissingPerformanceBaseParams(project, liveParams);

    const auto previousParams = project.params;
    const bool performanceChanged = PatternPerformanceTransformEngine::hasLivePerformanceParamChange(previousParams, liveParams);
    const bool playbackTimingChanged = PatternPerformanceTransformEngine::hasPlaybackTimingParamChange(previousParams, liveParams);

    project.params = liveParams;

    return { performanceChanged, playbackTimingChanged };
}

void BoomBapGeneratorAudioProcessor::captureEditedTrackPerformanceBaseLocked(TrackType trackType)
{
    if (auto* track = ProjectLaneAccess::findTrackState(project, trackType); track != nullptr)
        PatternPerformanceTransformEngine::captureBasePattern(*track, project.params);
}

void BoomBapGeneratorAudioProcessor::rebuildMidiCache()
{
    midiCache = buildMidiCacheForProject(project, currentSampleRate);
    previewEvents = buildPreviewEventsForProject(project, currentSampleRate);
    ++midiCacheRevision;
}

juce::MidiMessageSequence BoomBapGeneratorAudioProcessor::buildMidiCacheForProject(const PatternProject& sourceProject, double sampleRate) const
{
    auto normalizedProject = sourceProject;
    for (auto& track : normalizedProject.tracks)
    {
        if (track.type == TrackType::Sub808 && !track.notes.empty())
            track.sub808Notes = toSub808NoteEvents(track.notes);
    }

    auto tickSequence = MidiExportEngine::patternToSequence(normalizedProject, std::nullopt);
    juce::MidiMessageSequence normalized;

    for (int i = 0; i < tickSequence.getNumEvents(); ++i)
    {
        const auto* event = tickSequence.getEventPointer(i);
        if (event == nullptr)
            continue;

        auto message = event->message;
        const int eventTick = static_cast<int>(message.getTimeStamp());
        const int eventSample = ticksToSamples(eventTick, sampleRate, normalizedProject.params.bpm);
        message.setTimeStamp(static_cast<double>(eventSample));
        normalized.addEvent(message);
    }

    normalized.sort();
    normalized.updateMatchedPairs();
    return normalized;
}

std::vector<BoomBapGeneratorAudioProcessor::PreviewEvent> BoomBapGeneratorAudioProcessor::buildPreviewEventsForProject(const PatternProject& sourceProject,
                                                                                                                        double sampleRate) const
{
    std::vector<PreviewEvent> events;

    for (const auto& track : sourceProject.tracks)
    {
        if (!shouldIncludeTrackForPlayback(sourceProject, track))
            continue;

        const auto sub808Notes = track.type == TrackType::Sub808 ? sub808NotesForRead(track) : std::vector<Sub808NoteEvent> {};
        const auto noteCount = track.type == TrackType::Sub808 ? static_cast<int>(sub808Notes.size()) : static_cast<int>(track.notes.size());
        for (int noteIndex = 0; noteIndex < noteCount; ++noteIndex)
        {
            const NoteEvent note = track.type == TrackType::Sub808
                ? toLegacyNoteEvent(sub808Notes[static_cast<size_t>(noteIndex)])
                : track.notes[static_cast<size_t>(noteIndex)];

            PreviewEvent event;
            const int noteTicks = note.step * ticksPerStep() + note.microOffset;
            event.sample = ticksToSamples(noteTicks, sampleRate, sourceProject.params.bpm);
            event.track = track.type;
            event.pitch = juce::jlimit(0, 127, note.pitch);
            event.mono = track.sub808Settings.mono;
            event.cutItself = track.sub808Settings.cutItself;
            event.glideDurationSamples = static_cast<int>((static_cast<double>(track.sub808Settings.glideTimeMs) / 1000.0) * sampleRate);

            if (track.type == TrackType::Sub808)
            {
                event.legato = track.sub808Settings.overlapMode == Sub808OverlapMode::Legato || note.isLegato;
                event.glide = track.sub808Settings.overlapMode == Sub808OverlapMode::Glide || note.isSlide || note.glideToNext;

                const int endTick = noteTicks + juce::jmax(1, note.length) * ticksPerStep();
                event.endSample = ticksToSamples(endTick, sampleRate, sourceProject.params.bpm);
            }
            else
            {
                event.legato = note.isLegato;
                event.glide = note.isSlide || note.glideToNext;
            }

            event.gain = computePreviewEventGain(track, note);
            events.push_back(event);
        }
    }

    std::sort(events.begin(), events.end(), [](const PreviewEvent& a, const PreviewEvent& b)
    {
        return a.sample < b.sample;
    });

    return events;
}

float BoomBapGeneratorAudioProcessor::computePreviewEventGain(const TrackState& track, const NoteEvent& note) const
{
    const float velNorm = juce::jlimit(0.0f, 1.0f, static_cast<float>(note.velocity) / 127.0f);
    const bool snareFamily = track.type == TrackType::Snare || track.type == TrackType::ClapGhostSnare;
    const bool hatFxLane = track.type == TrackType::HatFX;
    const float velGain = std::pow(velNorm, note.isGhost ? 1.0f : 0.95f);
    const float ghostFactor = note.isGhost ? (snareFamily ? 0.62f : (hatFxLane ? 0.85f : 0.45f)) : 1.0f;
    const float laneGain = juce::jlimit(0.0f, 1.5f, track.laneVolume);

    float gain = laneGain * velGain * ghostFactor * 0.95f;
    if (snareFamily)
    {
        const float floor = laneGain * (note.isGhost ? 0.08f : 0.16f);
        gain = std::max(gain, floor);
    }
    else if (hatFxLane)
    {
        const float floor = laneGain * (note.isGhost ? 0.12f : 0.18f);
        gain = std::max(gain, floor);
    }

    return juce::jlimit(0.0f, 1.0f, gain);
}

void BoomBapGeneratorAudioProcessor::suspendAudioRenderAndWait() noexcept
{
    audioRenderSuspended.store(true, std::memory_order_release);
    waitForActiveProcessBlocks();
}

void BoomBapGeneratorAudioProcessor::resumeAudioRender() noexcept
{
    if (!isShuttingDown())
        audioRenderSuspended.store(false, std::memory_order_release);
}

void BoomBapGeneratorAudioProcessor::waitForActiveProcessBlocks() noexcept
{
    while (activeProcessBlockCount.load(std::memory_order_acquire) > 0)
        std::this_thread::yield();
}

void BoomBapGeneratorAudioProcessor::DrumReverbBlock::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 1000.0 ? spec.sampleRate : 44100.0;
    maximumBlockSize = juce::jmax(1, static_cast<int>(spec.maximumBlockSize));
    maximumPredelaySamples = juce::jmax(1, juce::roundToInt(sampleRate * 0.06));

    const int channels = juce::jmax(1, static_cast<int>(spec.numChannels));
    const int predelayBufferLength = maximumPredelaySamples + maximumBlockSize + 2;

    predelayBuffer.setSize(channels, predelayBufferLength, false, false, true);
    dryBuffer.setSize(channels, maximumBlockSize, false, false, true);
    earlyBuffer.setSize(channels, maximumBlockSize, false, false, true);
    tailBuffer.setSize(channels, maximumBlockSize, false, false, true);
    wetHighpassState.assign(static_cast<size_t>(channels), 0.0f);
    wetLowpassState.assign(static_cast<size_t>(channels), 0.0f);

    reset();
}

void BoomBapGeneratorAudioProcessor::DrumReverbBlock::reset()
{
    predelayWritePosition = 0;
    predelayBuffer.clear();
    dryBuffer.clear();
    earlyBuffer.clear();
    tailBuffer.clear();
    std::fill(wetHighpassState.begin(), wetHighpassState.end(), 0.0f);
    std::fill(wetLowpassState.begin(), wetLowpassState.end(), 0.0f);
    earlyReverb.reset();
    tailReverb.reset();
}

void BoomBapGeneratorAudioProcessor::DrumReverbBlock::ensureScratchCapacity(int channels, int samples)
{
    const int requiredChannels = juce::jmax(1, channels);
    const int requiredBlockSize = juce::jmax(1, samples);

    if (requiredBlockSize > maximumBlockSize)
        maximumBlockSize = requiredBlockSize;

    const int predelayBufferLength = juce::jmax(maximumPredelaySamples + maximumBlockSize + 2,
                                                maximumPredelaySamples + requiredBlockSize + 2);

    if (predelayBuffer.getNumChannels() != requiredChannels || predelayBuffer.getNumSamples() < predelayBufferLength)
    {
        predelayBuffer.setSize(requiredChannels, predelayBufferLength, false, false, true);
        predelayBuffer.clear();
        predelayWritePosition = 0;
    }

    if (dryBuffer.getNumChannels() != requiredChannels || dryBuffer.getNumSamples() < requiredBlockSize)
        dryBuffer.setSize(requiredChannels, requiredBlockSize, false, false, true);

    if (earlyBuffer.getNumChannels() != requiredChannels || earlyBuffer.getNumSamples() < requiredBlockSize)
        earlyBuffer.setSize(requiredChannels, requiredBlockSize, false, false, true);

    if (tailBuffer.getNumChannels() != requiredChannels || tailBuffer.getNumSamples() < requiredBlockSize)
        tailBuffer.setSize(requiredChannels, requiredBlockSize, false, false, true);

    if (wetHighpassState.size() != static_cast<size_t>(requiredChannels))
        wetHighpassState.assign(static_cast<size_t>(requiredChannels), 0.0f);

    if (wetLowpassState.size() != static_cast<size_t>(requiredChannels))
        wetLowpassState.assign(static_cast<size_t>(requiredChannels), 0.0f);
}

float BoomBapGeneratorAudioProcessor::DrumReverbBlock::processWetSample(float sample,
                                                                         int channel,
                                                                         float highPassCoeff,
                                                                         float lowPassCoeff)
{
    auto& highpassState = wetHighpassState[static_cast<size_t>(channel)];
    auto& lowpassState = wetLowpassState[static_cast<size_t>(channel)];

    highpassState = (1.0f - highPassCoeff) * sample + highPassCoeff * highpassState;
    const float highPassed = sample - highpassState;
    lowpassState = (1.0f - lowPassCoeff) * highPassed + lowPassCoeff * lowpassState;
    return lowpassState;
}

void BoomBapGeneratorAudioProcessor::DrumReverbBlock::process(juce::AudioBuffer<float>& buffer,
                                                               const DrumReverbState& state)
{
    if (buffer.getNumSamples() <= 0 || buffer.getNumChannels() <= 0)
        return;

    auto reverbState = state;
    sanitizeDrumReverbState(reverbState);
    if (!isDrumReverbAudiblyActive(reverbState))
        return;

    const int channels = buffer.getNumChannels();
    const int samples = buffer.getNumSamples();
    ensureScratchCapacity(channels, samples);

    dryBuffer.makeCopyOf(buffer, true);
    earlyBuffer.setSize(channels, samples, false, false, true);
    tailBuffer.setSize(channels, samples, false, false, true);
    earlyBuffer.clear();
    tailBuffer.clear();

    const int delaySamples = juce::jlimit(0,
                                          maximumPredelaySamples,
                                          juce::roundToInt((reverbState.predelayMs / 1000.0f) * static_cast<float>(sampleRate)));
    const int predelayBufferLength = predelayBuffer.getNumSamples();

    for (int sample = 0; sample < samples; ++sample)
    {
        int readPosition = predelayWritePosition - delaySamples;
        if (readPosition < 0)
            readPosition += predelayBufferLength;

        for (int channel = 0; channel < channels; ++channel)
        {
            const float input = dryBuffer.getSample(channel, sample);
            predelayBuffer.setSample(channel, predelayWritePosition, input);
            const float delayed = predelayBuffer.getSample(channel, readPosition);
            earlyBuffer.setSample(channel, sample, delayed);
            tailBuffer.setSample(channel, sample, delayed);
        }

        predelayWritePosition = (predelayWritePosition + 1) % predelayBufferLength;
    }

    juce::Reverb::Parameters earlyParameters;
    earlyParameters.roomSize = juce::jlimit(0.12f, 0.48f, 0.14f + reverbState.size * 0.26f + reverbState.erTail * 0.05f);
    earlyParameters.damping = juce::jlimit(0.45f, 0.92f, 0.58f + reverbState.size * 0.15f + reverbState.mix * 0.10f);
    earlyParameters.wetLevel = 1.0f;
    earlyParameters.dryLevel = 0.0f;
    earlyParameters.width = juce::jlimit(0.20f, 0.75f, 0.30f + reverbState.size * 0.22f);
    earlyParameters.freezeMode = 0.0f;

    juce::Reverb::Parameters tailParameters;
    tailParameters.roomSize = juce::jlimit(0.24f, 0.78f, 0.26f + reverbState.size * 0.44f + reverbState.erTail * 0.08f);
    tailParameters.damping = juce::jlimit(0.52f, 0.97f, 0.62f + reverbState.size * 0.18f + reverbState.mix * 0.12f);
    tailParameters.wetLevel = 1.0f;
    tailParameters.dryLevel = 0.0f;
    tailParameters.width = juce::jlimit(0.35f, 0.92f, 0.48f + reverbState.size * 0.24f);
    tailParameters.freezeMode = 0.0f;

    earlyReverb.setParameters(earlyParameters);
    tailReverb.setParameters(tailParameters);

    if (channels >= 2)
    {
        earlyReverb.processStereo(earlyBuffer.getWritePointer(0), earlyBuffer.getWritePointer(1), samples);
        tailReverb.processStereo(tailBuffer.getWritePointer(0), tailBuffer.getWritePointer(1), samples);
    }
    else
    {
        earlyReverb.processMono(earlyBuffer.getWritePointer(0), samples);
        tailReverb.processMono(tailBuffer.getWritePointer(0), samples);
    }

    const float macro = juce::jlimit(0.0f, 1.0f, reverbState.erTail);
    const float earlyWeight = std::cos(macro * juce::MathConstants<float>::pi * 0.5f);
    const float tailWeight = std::sin(macro * juce::MathConstants<float>::pi * 0.5f);
    const float earlyTrim = 0.64f + (1.0f - reverbState.size) * 0.08f;
    const float tailTrim = 0.56f + reverbState.size * 0.12f + macro * 0.08f;
    const float highPassCutoff = juce::jlimit(95.0f, 220.0f, 115.0f + (1.0f - macro) * 65.0f + reverbState.size * 20.0f);
    const float lowPassCutoff = juce::jlimit(3600.0f,
                                             12000.0f,
                                             10400.0f - reverbState.size * 2600.0f - reverbState.mix * 1700.0f - macro * 900.0f);
    const float highPassCoeff = std::exp(-juce::MathConstants<float>::twoPi * highPassCutoff / static_cast<float>(sampleRate));
    const float lowPassCoeff = std::exp(-juce::MathConstants<float>::twoPi * lowPassCutoff / static_cast<float>(sampleRate));
    const float dryGain = std::cos(reverbState.mix * juce::MathConstants<float>::pi * 0.5f);
    const float wetGain = std::sin(reverbState.mix * juce::MathConstants<float>::pi * 0.5f);

    for (int channel = 0; channel < channels; ++channel)
    {
        for (int sample = 0; sample < samples; ++sample)
        {
            const float wet = earlyBuffer.getSample(channel, sample) * earlyWeight * earlyTrim
                + tailBuffer.getSample(channel, sample) * tailWeight * tailTrim;
            const float filteredWet = processWetSample(wet, channel, highPassCoeff, lowPassCoeff);
            const float dry = dryBuffer.getSample(channel, sample);
            buffer.setSample(channel, sample, dry * dryGain + filteredWet * wetGain);
        }
    }
}

void BoomBapGeneratorAudioProcessor::DrumStereoFieldBlock::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 1000.0 ? spec.sampleRate : 44100.0;
    reset();
}

void BoomBapGeneratorAudioProcessor::DrumStereoFieldBlock::reset()
{
    lowSideState = 0.0f;
    airSideState = 0.0f;
}

void BoomBapGeneratorAudioProcessor::DrumStereoFieldBlock::process(juce::AudioBuffer<float>& buffer,
                                                                    const SoundLayerState& state)
{
    if (buffer.getNumSamples() <= 0 || buffer.getNumChannels() < 2)
        return;

    auto stereoState = state;
    sanitizeStereoFieldSettings(stereoState);
    if (!isStereoFieldAudiblyActive(stereoState))
        return;

    const float width = stereoState.width;
    const float focus = stereoState.stereoFieldFocus;
    const float edge = stereoState.stereoFieldEdge;
    const float lowCenterProtect = stereoState.stereoFieldLowCenterProtect;
    const float airSpread = stereoState.stereoFieldAirSpread;
    const bool monoSafe = stereoState.stereoFieldMonoSafe;

    const float widthExcess = juce::jmax(0.0f, width - 1.0f);
    const float lowCutoff = juce::jlimit(120.0f, 260.0f, 140.0f + (1.0f - lowCenterProtect) * 120.0f);
    const float airCutoff = juce::jlimit(2600.0f,
                                         9000.0f,
                                         3400.0f + airSpread * 2400.0f + widthExcess * 1200.0f + edge * 600.0f);
    const float lowCoeff = std::exp(-juce::MathConstants<float>::twoPi * lowCutoff / static_cast<float>(sampleRate));
    const float airCoeff = std::exp(-juce::MathConstants<float>::twoPi * airCutoff / static_cast<float>(sampleRate));

    float focusMidGain = 0.88f + focus * 0.28f;
    float focusSideGain = 1.14f - focus * 0.28f;
    float lowSideGain = width * focusSideGain * (1.0f - lowCenterProtect * widthExcess * 0.85f);
    float midSideGain = width * focusSideGain * (1.0f + edge * 0.12f);
    float airSideGain = width * focusSideGain * (1.0f + airSpread * (0.35f + widthExcess * 0.45f)) * (1.0f + edge * 0.55f);

    lowSideGain = juce::jmax(0.0f, lowSideGain);
    midSideGain = juce::jmax(0.0f, midSideGain);
    airSideGain = juce::jmax(0.0f, airSideGain);

    if (monoSafe)
    {
        lowSideGain = juce::jmin(lowSideGain, 1.10f);
        midSideGain = juce::jmin(midSideGain, 1.35f);
        airSideGain = juce::jmin(airSideGain, 1.55f);
    }

    const float postTrim = 1.0f / (1.0f + widthExcess * 0.12f + juce::jmax(0.0f, focus - 0.5f) * 0.10f + edge * 0.06f);
    const float panLeft = std::cos((stereoState.pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f);
    const float panRight = std::sin((stereoState.pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f);

    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getWritePointer(1);
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const float mid = 0.5f * (left[sample] + right[sample]);
        const float side = 0.5f * (left[sample] - right[sample]);

        lowSideState = side * (1.0f - lowCoeff) + lowSideState * lowCoeff;
        airSideState = side * (1.0f - airCoeff) + airSideState * airCoeff;

        const float lowSide = lowSideState;
        const float airSide = side - airSideState;
        const float midSide = airSideState - lowSide;

        float shapedAirSide = airSide * airSideGain;
        if (edge > 0.001f)
        {
            const float edgeDrive = 1.0f + edge * 1.15f;
            shapedAirSide = std::tanh(shapedAirSide * edgeDrive) / (0.92f + edge * 0.38f);
        }

        float processedMid = mid * focusMidGain;
        float processedSide = lowSide * lowSideGain + midSide * midSideGain + shapedAirSide;

        if (monoSafe)
        {
            const float softRange = 0.95f + widthExcess * 0.35f + edge * 0.20f;
            processedSide = softRange * std::tanh(processedSide / juce::jmax(0.25f, softRange));
            const float maxRatio = 1.15f + widthExcess * 0.35f + airSpread * 0.15f;
            const float sideLimit = std::abs(processedMid) * maxRatio + 0.05f;
            processedSide = juce::jlimit(-sideLimit, sideLimit, processedSide);
        }

        const float outputLeft = (processedMid + processedSide) * postTrim * panLeft;
        const float outputRight = (processedMid - processedSide) * postTrim * panRight;

        left[sample] = outputLeft;
        right[sample] = outputRight;
    }
}

void BoomBapGeneratorAudioProcessor::DrumTransientBlock::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 1000.0 ? spec.sampleRate : 44100.0;
    reset();
}

void BoomBapGeneratorAudioProcessor::DrumTransientBlock::reset()
{
    fastEnvelope = 0.0f;
    slowEnvelope = 0.0f;
    sustainEnvelope = 0.0f;
}

void BoomBapGeneratorAudioProcessor::DrumTransientBlock::process(juce::AudioBuffer<float>& buffer,
                                                                  const DrumTransientState& state)
{
    if (buffer.getNumSamples() <= 0 || buffer.getNumChannels() <= 0)
        return;

    auto transientState = state;
    sanitizeDrumTransientState(transientState);
    if (!isDrumTransientAudiblyActive(transientState))
        return;

    const bool smooth = transientState.smooth;
    const bool limit = transientState.limit;
    const float attackAmount = juce::jlimit(0.0f, 1.0f, transientState.attack);
    const float sustainAmount = transientState.sustain >= 0.25f
        ? juce::jlimit(0.0f, 1.0f, (transientState.sustain - 0.25f) / 0.75f)
        : -juce::jlimit(0.0f, 1.0f, (0.25f - transientState.sustain) / 0.25f);
    const float outputGain = juce::Decibels::decibelsToGain(transientState.gainDb);

    const float fastAttackCoeff = envelopeTimeCoefficient(sampleRate, smooth ? 1.8f : 0.45f);
    const float fastReleaseCoeff = envelopeTimeCoefficient(sampleRate, smooth ? 16.0f : 7.5f);
    const float slowAttackCoeff = envelopeTimeCoefficient(sampleRate, smooth ? 12.0f : 8.0f);
    const float slowReleaseCoeff = envelopeTimeCoefficient(sampleRate, smooth ? 110.0f : 82.0f);
    const float sustainAttackCoeff = envelopeTimeCoefficient(sampleRate, smooth ? 28.0f : 18.0f);
    const float sustainReleaseCoeff = envelopeTimeCoefficient(sampleRate, smooth ? 240.0f : 170.0f);
    const float attackDepth = smooth ? 1.25f : 1.95f;
    const float sustainBoostDepth = smooth ? 1.05f : 1.55f;
    const float sustainCutDepth = smooth ? 0.52f : 0.74f;
    const float ceiling = smooth ? 0.96f : 0.93f;
    const float limiterDrive = 1.15f
        + attackAmount * (smooth ? 0.65f : 1.0f)
        + juce::jmax(0.0f, sustainAmount) * 0.45f
        + juce::jmax(0.0f, transientState.gainDb) * 0.055f;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        float detector = 0.0f;
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            detector += std::abs(buffer.getSample(channel, sample));
        detector /= static_cast<float>(buffer.getNumChannels());

        fastEnvelope = followEnvelope(detector, fastEnvelope, fastAttackCoeff, fastReleaseCoeff);
        slowEnvelope = followEnvelope(detector, slowEnvelope, slowAttackCoeff, slowReleaseCoeff);
        sustainEnvelope = followEnvelope(detector, sustainEnvelope, sustainAttackCoeff, sustainReleaseCoeff);

        const float attackDelta = juce::jmax(0.0f, fastEnvelope - slowEnvelope);
        const float sustainDelta = juce::jmax(0.0f, sustainEnvelope - fastEnvelope);
        float attackNormalized = attackDelta / (slowEnvelope + 0.03f);
        float sustainNormalized = sustainDelta / (sustainEnvelope + 0.03f);

        attackNormalized = std::tanh(attackNormalized * (smooth ? 0.95f : 1.28f));
        sustainNormalized = std::tanh(sustainNormalized * (smooth ? 0.88f : 1.12f));

        float transientGain = 1.0f + attackAmount * attackDepth * attackNormalized;
        if (sustainAmount >= 0.0f)
            transientGain *= 1.0f + sustainAmount * sustainBoostDepth * sustainNormalized;
        else
            transientGain *= 1.0f + sustainAmount * sustainCutDepth * sustainNormalized;

        transientGain *= 1.0f / (1.0f + attackAmount * 0.16f + juce::jmax(0.0f, sustainAmount) * 0.10f);
        transientGain = juce::jlimit(0.32f, 4.0f, transientGain * outputGain);

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            float shaped = buffer.getSample(channel, sample) * transientGain;
            if (limit)
                shaped = softLimitSample(shaped, ceiling, limiterDrive);

            buffer.setSample(channel, sample, shaped);
        }
    }
}

SoundLayerState BoomBapGeneratorAudioProcessor::sanitizeSoundLayer(const SoundLayerState& state) const
{
    auto sanitized = state;
    sanitized.pan = juce::jlimit(-1.0f, 1.0f, sanitized.pan);
    sanitized.width = juce::jlimit(0.0f, 2.0f, sanitized.width);
    sanitizeStereoFieldSettings(sanitized);
    sanitized.eq.selectedBand = clampEqBandIndex(sanitized.eq.selectedBand);
    for (auto& band : sanitized.eq.bands)
    {
        band.freqHz = juce::jlimit(20.0f, 20000.0f, band.freqHz);
        band.gainDb = juce::jlimit(-24.0f, 24.0f, band.gainDb);
        band.q = juce::jlimit(0.1f, 10.0f, band.q);
        band.shape = static_cast<EqBandShape>(juce::jlimit(0, 2, static_cast<int>(band.shape)));
    }
    sanitized.compression = juce::jlimit(0.0f, 1.0f, sanitized.compression);
    sanitizeMonstaFxState(sanitized.monstaFx);
    sanitizeDrumReverbState(sanitized.drumReverb);
    sanitizeDrumTransientState(sanitized.drumTransient);
    sanitized.reverb = juce::jlimit(0.0f, 1.0f, sanitized.reverb);
    sanitized.gate = juce::jlimit(0.0f, 1.0f, sanitized.gate);
    sanitized.transient = juce::jlimit(0.0f, 1.0f, sanitized.transient);
    sanitized.drive = juce::jlimit(0.0f, 1.0f, sanitized.drive);
    reconcileLegacySoundLayerState(sanitized);
    return sanitized;
}

void BoomBapGeneratorAudioProcessor::prepareSoundFxRuntimeState(SoundFxRuntimeState& state)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = currentSampleRate > 1000.0 ? currentSampleRate : 44100.0;
    spec.maximumBlockSize = static_cast<juce::uint32>(getBlockSize() > 0 ? getBlockSize() : 1024);
    spec.numChannels = static_cast<juce::uint32>(juce::jmax(1, getTotalNumOutputChannels()));

    for (auto& eqBand : state.eqBands)
    {
        eqBand.reset();
        eqBand.prepare(spec);
        const auto neutralCoefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter(spec.sampleRate, 1000.0f, 1.0f, 1.0f);
        if (eqBand.state == nullptr)
            eqBand.state = neutralCoefficients;
        else
            *eqBand.state = *neutralCoefficients;
    }

    state.lowPass.reset();
    state.lowPass.prepare(spec);
    state.lowPass.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
    state.lowPass.setCutoffFrequency(18000.0f);

    state.highPass.reset();
    state.highPass.prepare(spec);
    state.highPass.setType(juce::dsp::StateVariableTPTFilterType::highpass);
    state.highPass.setCutoffFrequency(20.0f);

    state.compressor.reset();
    state.compressor.prepare(spec);
    state.monstaFx.prepare(spec);
    state.reverb.prepare(spec);
    state.transient.prepare(spec);
    state.stereoField.prepare(spec);
    resetSoundFxRuntimeState(state);
}

void BoomBapGeneratorAudioProcessor::resetSoundFxRuntimeState(SoundFxRuntimeState& state)
{
    for (auto& eqBand : state.eqBands)
        eqBand.reset();
    state.lowPass.reset();
    state.highPass.reset();
    state.compressor.reset();
    state.monstaFx.reset();
    state.reverb.reset();
    state.transient.reset();
    state.stereoField.reset();
    state.gateEnvelope = 0.0f;
}

void BoomBapGeneratorAudioProcessor::applySoundLayerFx(juce::AudioBuffer<float>& buffer,
                                                       const SoundLayerState& state,
                                                       SoundFxRuntimeState& runtime,
                                                       const MonstaFxTimelineContext& monstaTimeline)
{
    if (buffer.getNumSamples() <= 0 || buffer.getNumChannels() <= 0)
        return;

    const auto sanitized = sanitizeSoundLayer(state);
    const bool hasEq = soundLayerHasActiveEq(sanitized);
    const bool hasCompressor = isCompressorAudiblyActive(sanitized.compressor);
    const bool hasMonstaFx = isMonstaFxAudiblyActive(sanitized.monstaFx);
    const bool hasReverb = isDrumReverbAudiblyActive(sanitized.drumReverb);
    const bool hasTransient = isDrumTransientAudiblyActive(sanitized.drumTransient);
    const bool hasStereoField = soundLayerHasActiveStereoField(sanitized);
    if (!hasEq && !hasCompressor && !hasMonstaFx && !hasReverb && !hasTransient && !hasStereoField)
        return;

    const bool monstaPreTransient = sanitized.monstaFx.order <= 0;

    const auto applyEq = [&]()
    {
        juce::dsp::AudioBlock<float> block(buffer);
        for (int bandIndex = 0; bandIndex < kEqBandCount; ++bandIndex)
        {
            const auto& band = sanitized.eq.bands[static_cast<size_t>(bandIndex)];
            if (!band.enabled)
                continue;

            juce::dsp::IIR::Coefficients<float>::Ptr coefficients;
            switch (band.shape)
            {
                case EqBandShape::LowCut:
                    coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass(currentSampleRate, band.freqHz, band.q);
                    break;
                case EqBandShape::HighCut:
                    coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass(currentSampleRate, band.freqHz, band.q);
                    break;
                case EqBandShape::Bell:
                default:
                    coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter(currentSampleRate,
                                                                                       band.freqHz,
                                                                                       band.q,
                                                                                       juce::Decibels::decibelsToGain(band.gainDb));
                    break;
            }

            auto& eqBand = runtime.eqBands[static_cast<size_t>(bandIndex)];
            if (eqBand.state == nullptr)
                eqBand.state = coefficients;
            else
                *eqBand.state = *coefficients;
            juce::dsp::ProcessContextReplacing<float> context(block);
            eqBand.process(context);
        }
    };

    if (hasCompressor && sanitized.compressor.order <= 1)
        runtime.compressor.process(buffer, sanitized.compressor);

    if (hasEq)
        applyEq();

    if (hasCompressor && sanitized.compressor.order > 1)
        runtime.compressor.process(buffer, sanitized.compressor);

    if (hasMonstaFx && monstaPreTransient)
        runtime.monstaFx.process(buffer, sanitized.monstaFx, monstaTimeline);

    if (hasTransient)
        runtime.transient.process(buffer, sanitized.drumTransient);

    if (hasMonstaFx && !monstaPreTransient)
        runtime.monstaFx.process(buffer, sanitized.monstaFx, monstaTimeline);

    if (hasReverb)
        runtime.reverb.process(buffer, sanitized.drumReverb);

    if (hasStereoField)
        runtime.stereoField.process(buffer, sanitized);
}

void BoomBapGeneratorAudioProcessor::applyMasterFx(juce::AudioBuffer<float>& buffer)
{
    const auto* volParam = apvts.getRawParameterValue(ParamIds::masterVolume);

    const float masterVol = volParam != nullptr ? volParam->load() : 1.0f;

    buffer.applyGain(juce::jlimit(0.0f, 1.5f, masterVol));
}

int BoomBapGeneratorAudioProcessor::getPatternLengthSamples() const
{
    const int totalSteps = juce::jmax(1, project.params.bars * 16);
    return stepToSamples(totalSteps, currentSampleRate, project.params.bpm);
}

int BoomBapGeneratorAudioProcessor::getPatternLengthSamples(const PatternProject& sourceProject, double sampleRate) const
{
    const int totalSteps = juce::jmax(1, sourceProject.params.bars * 16);
    return stepToSamples(totalSteps, sampleRate, sourceProject.params.bpm);
}

TrackState* BoomBapGeneratorAudioProcessor::findTrackState(TrackType track)
{
    return ProjectLaneAccess::findTrackState(project, track);
}

const TrackState* BoomBapGeneratorAudioProcessor::findTrackState(TrackType track) const
{
    return ProjectLaneAccess::findTrackState(project, track);
}

TrackState* BoomBapGeneratorAudioProcessor::findTrackState(const RuntimeLaneId& laneId)
{
    return ProjectLaneAccess::findTrackState(project, laneId);
}

const TrackState* BoomBapGeneratorAudioProcessor::findTrackState(const RuntimeLaneId& laneId) const
{
    return ProjectLaneAccess::findTrackState(project, laneId);
}

std::optional<TrackType> BoomBapGeneratorAudioProcessor::resolveTrackTypeFromLane(const RuntimeLaneId& laneId) const
{
    std::scoped_lock lock(projectMutex);
    const auto type = ProjectLaneAccess::backingTrackTypeForLaneId(project, laneId);
    jassert(type.has_value());
    return type;
}

void BoomBapGeneratorAudioProcessor::serializePatternProjectToState(juce::ValueTree& state) const
{
    state.removeChild(state.getChildWithName("PATTERN_PROJECT"), nullptr);
    state.addChild(PatternProjectSerialization::serialize(project), -1, nullptr);
}

void BoomBapGeneratorAudioProcessor::restorePatternProjectFromState(const juce::ValueTree& state)
{
    const auto rootVersion = static_cast<int>(state.getProperty("root_schema_version", 1));
    juce::ignoreUnused(rootVersion);

    PatternProject restored;
    const bool hadPattern = PatternProjectSerialization::deserialize(state, restored);

    if (!hadPattern)
    {
        restored = createDefaultProject();

        // Migration path from older state snapshots that stored only track flags.
        const auto legacyTrackState = state.getChildWithName("TrackState");
        if (legacyTrackState.isValid())
        {
            for (int i = 0; i < legacyTrackState.getNumChildren(); ++i)
            {
                const auto node = legacyTrackState.getChild(i);
                const auto type = static_cast<TrackType>(static_cast<int>(node.getProperty("type", static_cast<int>(TrackType::Kick))));
                auto it = std::find_if(restored.tracks.begin(), restored.tracks.end(), [type](const TrackState& t) { return t.type == type; });
                if (it != restored.tracks.end())
                {
                    it->enabled = static_cast<bool>(node.getProperty("enabled", it->enabled));
                    it->muted = static_cast<bool>(node.getProperty("muted", false));
                    it->solo = static_cast<bool>(node.getProperty("solo", false));
                    it->locked = static_cast<bool>(node.getProperty("locked", false));
                }
            }
        }
    }

    // APVTS remains the parameter source of truth after restore.
    restored.params = buildParamsFromState(lastTransport);
    PatternProjectSerialization::validate(restored);
    PatternPerformanceTransformEngine::backfillMissingPerformanceBaseParams(restored, restored.params);
    project = std::move(restored);
}
} // namespace bbg

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new bbg::BoomBapGeneratorAudioProcessor();
}
