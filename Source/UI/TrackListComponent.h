#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Analysis/SampleAnalysisRequest.h"
#include "../Core/RuntimeLaneProfile.h"
#include "../Core/SoundLayerState.h"
#include "LaneHeaderComponent.h"

namespace bbg
{
class TrackListComponent : public juce::Component
                         , public juce::FileDragAndDropTarget
{
public:
    TrackListComponent();
    void paint(juce::Graphics& g) override;

    void resized() override;

    void setTracks(const RuntimeLaneProfile& profile,
                   const std::vector<TrackState>& tracks,
                   int bassKeyRootChoice,
                   int bassScaleModeChoice);
    void setLaneDisplayOrder(const std::vector<RuntimeLaneId>& order);
    void setShowAnalysisPanel(bool shouldShow);
    void setHatFxDragUiState(float density, bool locked);
    void setRowHeight(int newHeight);
    void setDisplayMode(LaneRackDisplayMode mode);
    int getRowHeight() const { return rowHeight; }
    int getVisibleRowCount() const { return static_cast<int>(rows.size()); }
    int getLaneSectionHeight() const;
    int getContentHeight() const;
    std::function<void()> onAddLaneRequested;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    void setAnalysisPanelState(SampleAnalysisRequest::SourceType source,
                               AnalysisMode mode,
                               int barsToCapture,
                               SampleAnalysisRequest::TempoHandling tempoHandling,
                               float reactivity,
                               float supportVsContrast,
                               const juce::String& fileLabel,
                               const juce::String& statusText,
                               const juce::String& detailsText,
                               const juce::String& debugText,
                               bool analysisReady);
    void setSoundPanelState(const std::vector<TrackState>& tracks,
                            const std::optional<TrackType>& selectedTarget,
                            const SoundLayerState& soundState);

    std::function<void(const RuntimeLaneId&)> onRegenerateTrack;
    std::function<void(const RuntimeLaneId&)> onImportMidiLaneTrack;
    std::function<void(const RuntimeLaneId&, bool)> onSoloTrack;
    std::function<void(const RuntimeLaneId&, bool)> onMuteTrack;
    std::function<void(const RuntimeLaneId&)> onClearTrack;
    std::function<void(const RuntimeLaneId&, bool)> onLockTrack;
    std::function<void(const RuntimeLaneId&, bool)> onEnableTrack;
    std::function<void(const RuntimeLaneId&)> onDragTrack;
    std::function<void(const RuntimeLaneId&)> onDragTrackGesture;
    std::function<void(const RuntimeLaneId&, float)> onDragDensityTrack;
    std::function<void(const RuntimeLaneId&, bool)> onDragDensityLockTrack;
    std::function<void(const RuntimeLaneId&)> onExportTrack;
    std::function<void(const RuntimeLaneId&)> onPrevSampleTrack;
    std::function<void(const RuntimeLaneId&)> onNextSampleTrack;
    std::function<void(const RuntimeLaneId&)> onSampleMenuTrack;
    std::function<void(const RuntimeLaneId&)> onTrackNameClicked;
    std::function<void(const RuntimeLaneId&)> onRenameLaneRequested;
    std::function<void(const RuntimeLaneId&)> onDeleteLaneRequested;
    std::function<void(const RuntimeLaneId&, float)> onLaneVolumeTrack;
    std::function<void(const RuntimeLaneId&, const SoundLayerState&)> onLaneSoundTrack;
    std::function<void(const RuntimeLaneId&)> onLaneMoveUpTrack;
    std::function<void(const RuntimeLaneId&)> onLaneMoveDownTrack;
    std::function<void(int)> onBassKeyChanged;
    std::function<void(int)> onBassScaleChanged;
    std::function<void(const RuntimeLaneId&, const Sub808LaneSettings&)> onSub808SettingsChanged;

    std::function<void(const std::optional<TrackType>&)> onSoundTargetChanged;
    std::function<void(const std::optional<TrackType>&, const SoundLayerState&)> onSoundLayerChanged;

    std::function<void(SampleAnalysisRequest::SourceType)> onAnalysisSourceChanged;
    std::function<void(AnalysisMode)> onAnalysisModeChanged;
    std::function<void(int)> onAnalysisBarsChanged;
    std::function<void(SampleAnalysisRequest::TempoHandling)> onAnalysisTempoHandlingChanged;
    std::function<void(float)> onAnalysisReactivityChanged;
    std::function<void(float)> onSupportVsContrastChanged;
    std::function<void()> onChooseAnalysisFile;
    std::function<void(const juce::File&)> onAnalysisFileDropped;
    std::function<void()> onRunAnalysis;

private:
    void setupAnalysisPanel();
    void setupSoundPanel();

    std::vector<std::unique_ptr<LaneHeaderComponent>> rows;
    std::vector<RuntimeLaneId> laneDisplayOrder;
    juce::TextButton addLaneButton { "+ Lane" };
    bool showAnalysisPanel = true;
    int rowHeight = 30;
    int rulerHeight = 24;
    int analysisPanelHeight = 340;
    int soundPanelHeight = 232;
    LaneRackDisplayMode displayMode = LaneRackDisplayMode::Full;
    float hatFxDragDensity = 1.0f;
    bool hatFxDragLocked = false;
    bool analysisFileDropHighlight = false;

    juce::Label analysisTitleLabel;
    juce::Label sourceLabel;
    juce::Label modeLabel;
    juce::Label barsLabel;
    juce::Label tempoLabel;
    juce::Label reactivityLabel;
    juce::Label supportLabel;
    juce::Label fileLabel;
    juce::Label statusLabel;
    juce::Label detailsLabel;
    juce::Label debugLabel;

    juce::ComboBox sourceCombo;
    juce::ComboBox modeCombo;
    juce::ComboBox barsCombo;
    juce::ComboBox tempoCombo;
    juce::Slider reactivitySlider;
    juce::Slider supportSlider;
    juce::TextButton chooseFileButton { "Open File" };
    juce::TextButton analyzeButton { "Analyze" };
    juce::TextEditor debugTextBox;

    juce::Label soundTitleLabel;
    juce::Label soundTargetLabel;
    juce::ComboBox soundTargetCombo;
    juce::Label panLabel;
    juce::Label widthLabel;
    juce::Label eqLabel;
    juce::Label compLabel;
    juce::Label reverbLabel;
    juce::Label gateLabel;
    juce::Label transientLabel;
    juce::Label driveLabel;
    juce::Slider panSlider;
    juce::Slider widthSlider;
    juce::Slider eqSlider;
    juce::Slider compSlider;
    juce::Slider reverbSlider;
    juce::Slider gateSlider;
    juce::Slider transientSlider;
    juce::Slider driveSlider;
    std::vector<TrackType> soundTargetTracks;
    std::optional<TrackType> currentSoundTarget;
};
} // namespace bbg
