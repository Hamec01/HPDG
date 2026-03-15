#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "DragGestureButton.h"

namespace bbg
{
class MainHeaderComponent : public juce::Component
{
public:
    MainHeaderComponent();
    void paint(juce::Graphics& g) override;
    void resized() override;

    void setBpmLocked(bool locked);
    void setPreviewPlaying(bool isPlaying);
    void setStandaloneWindowMaximized(bool isMaximized);
    void setStandaloneWindowButtonVisible(bool visible);

    std::function<void()> onGeneratePressed;
    std::function<void()> onMutatePressed;
    std::function<void(bool)> onPlayToggled;
    std::function<void()> onExportFullPressed;
    std::function<void()> onDragFullPressed;
    std::function<void()> onDragFullGesture;
    std::function<void()> onToggleStandaloneWindow;
    std::function<void(float, float)> onZoomChanged;
    std::function<void(int)> onGridResolutionChanged;

    juce::Slider bpmSlider;
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
    juce::ToggleButton seedLockToggle { "Seed Lock" };
    juce::TextButton standaloneWindowButton { "Max" };
    juce::Slider zoomSlider;
    juce::Slider laneHeightSlider;
    juce::Slider masterVolumeSlider;
    juce::Slider masterCompressorSlider;
    juce::Slider masterLofiSlider;
    juce::TextButton playButton { "Play" };
    juce::TextButton mutateButton { "Mutate" };
    juce::TextButton exportFullButton { "Export Full" };
    DragGestureButton dragFullButton { "Drag Full" };
    juce::TextButton generateButton { "Generate Pattern" };

private:
    void setupSlider(juce::Slider& slider, double min, double max, double step, const juce::String& suffix = {});

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
    juce::Label zoomLabel;
    juce::Label gridModeIndicatorLabel;
    juce::Label laneHeightLabel;
    juce::Label masterSectionLabel;
    juce::Label masterVolumeLabel;
    juce::Label masterCompressorLabel;
    juce::Label masterLofiLabel;
};
} // namespace bbg
