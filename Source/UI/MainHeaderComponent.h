#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "DragGestureButton.h"

namespace bbg
{
class MainHeaderComponent : public juce::Component
{
public:
    enum class HeaderControlsMode
    {
        Expanded,
        Compact,
        Hidden
    };

    MainHeaderComponent();
    void paint(juce::Graphics& g) override;
    void resized() override;

    void setBpmLocked(bool locked);
    void setPreviewPlaying(bool isPlaying);
    void setStandaloneWindowMaximized(bool isMaximized);
    void setStandaloneWindowButtonVisible(bool visible);
    void setHeaderControlsMode(HeaderControlsMode mode);
    void setStartPlayWithDawEnabled(bool enabled);
    void setGridModeIndicatorText(const juce::String& text);
    void setPreviewPlaybackModeId(int id);
    HeaderControlsMode getHeaderControlsMode() const { return controlsMode; }
    int getPreferredHeight() const;
    void setHatFxDensityState(float density, bool locked);

    std::function<void()> onGeneratePressed;
    std::function<void()> onMutatePressed;
    std::function<void()> onClearAllPressed;
    std::function<void(bool)> onPlayToggled;
    std::function<void()> onExportFullPressed;
    std::function<void()> onExportLoopWavPressed;
    std::function<void()> onDragFullPressed;
    std::function<void()> onDragFullGesture;
    std::function<void()> onTransportToStart;
    std::function<void()> onTransportStepBack;
    std::function<void()> onTransportStepForward;
    std::function<void()> onTransportToEnd;
    std::function<void()> onToggleStandaloneWindow;
    std::function<void(float, float)> onZoomChanged;
    std::function<void(int)> onGridResolutionChanged;
    std::function<void(int)> onPreviewPlaybackModeChanged;
    std::function<void(HeaderControlsMode)> onHeaderControlsModeChanged;
    std::function<void(bool)> onStartPlayWithDawToggled;

    juce::Slider bpmSlider;
    juce::ToggleButton bpmLockToggle { "Lock" };
    juce::ToggleButton syncTempoToggle { "Sync" };
    juce::Slider swingSlider;
    juce::Slider velocitySlider;
    juce::Slider timingSlider;
    juce::Slider humanizeSlider;
    juce::Slider densitySlider;
    juce::ComboBox tempoInterpretationCombo;
    juce::ComboBox barsCombo;
    juce::ComboBox genreCombo;
    juce::ComboBox substyleCombo;
    juce::Slider seedSlider;
    juce::ComboBox gridResolutionCombo;
    juce::ComboBox previewPlaybackModeCombo;
    juce::ToggleButton seedLockToggle { "Seed Lock" };
    juce::TextButton standaloneWindowButton { "Max" };
    juce::Slider zoomSlider;
    juce::Slider laneHeightSlider;
    juce::Slider masterVolumeSlider;
    juce::Slider masterCompressorSlider;
    juce::Slider masterLofiSlider;
    juce::TextButton playButton { "Play" };
    juce::TextButton transportToStartButton { "|<" };
    juce::TextButton transportStepBackButton { "<<" };
    juce::TextButton transportStepForwardButton { ">>" };
    juce::TextButton transportToEndButton { ">|" };
    juce::TextButton mutateButton { "Mutate" };
    juce::TextButton clearAllButton { "Clear All" };
    juce::TextButton exportFullButton { "Export Full" };
    juce::TextButton exportLoopWavButton { "Export Loop WAV" };
    DragGestureButton dragFullButton { "Drag Full" };
    juce::TextButton generateButton { "Generate Pattern" };
    juce::ToggleButton startPlayWithDawToggle { "Start play with DAW" };

private:
    void setupSlider(juce::Slider& slider, double min, double max, double step, const juce::String& suffix = {});
    void updateAdvancedModeButtonText();

    juce::Label titleLabel;
    juce::Label subtitleLabel;

    juce::Label bpmLabel;
    juce::Label swingLabel;
    juce::Label velocityLabel;
    juce::Label timingLabel;
    juce::Label humanizeLabel;
    juce::Label densityLabel;
    juce::Label tempoInterpretationLabel;
    juce::Label barsLabel;
    juce::Label genreLabel;
    juce::Label substyleLabel;
    juce::Label seedLabel;
    juce::Label gridResolutionLabel;
    juce::Label previewPlaybackModeLabel;
    juce::Label zoomLabel;
    juce::Label gridModeIndicatorLabel;
    juce::Label hatFxDensityLabel;
    juce::Label hatFxDensityValueLabel;
    juce::Label laneHeightLabel;
    juce::Label masterSectionLabel;
    juce::Label masterVolumeLabel;
    juce::Label masterCompressorLabel;
    juce::Label masterLofiLabel;
    juce::TextButton advancedModeButton { "ADV" };

public:
    juce::Slider hatFxDensitySlider;
    juce::ToggleButton hatFxDensityLockToggle { "Lk" };

    HeaderControlsMode controlsMode = HeaderControlsMode::Expanded;
};
} // namespace bbg
