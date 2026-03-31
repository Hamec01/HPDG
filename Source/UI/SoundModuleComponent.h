#pragma once

#include <array>
#include <functional>
#include <optional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Core/SoundLayerState.h"
#include "../Core/SoundTargetDescriptor.h"
#include "../Core/TrackRegistry.h"
#include "../Core/TrackState.h"

namespace bbg
{
class SoundModuleComponent : public juce::Component
{
public:
    SoundModuleComponent();

    void paint(juce::Graphics& g) override;
    void resized() override;

    void setState(const std::vector<TrackState>& tracks,
                  const SoundTargetDescriptor& selectedTarget,
                  const SoundLayerState& soundState);

    std::function<void(const SoundTargetDescriptor&)> onSoundTargetChanged;
    std::function<void(const SoundTargetDescriptor&, const SoundLayerState&)> onSoundLayerChanged;

private:
    juce::Label titleLabel;
    juce::Label targetLabel;
    juce::Label targetSummaryLabel;
    juce::Label targetModeLabel;
    juce::Label targetStatusLabel;
    juce::Label chainSummaryLabel;
    juce::ComboBox targetCombo;
    juce::TextButton bypassAllButton;
    juce::TextButton resetTargetButton;

    juce::Label compressorTitleLabel;
    juce::ToggleButton compressorEnableButton;
    juce::ComboBox compressorOrderCombo;
    std::array<juce::Label, 6> compressorLabels;
    std::array<juce::Slider, 6> compressorSliders;

    juce::Label reverbTitleLabel;
    juce::ToggleButton reverbEnableButton;
    juce::ComboBox reverbOrderCombo;
    std::array<juce::Label, 4> reverbLabels;
    std::array<juce::Slider, 4> reverbSliders;

    juce::Label transientTitleLabel;
    juce::ToggleButton transientEnableButton;
    juce::ComboBox transientOrderCombo;
    std::array<juce::Label, 3> transientSliderLabels;
    std::array<juce::Slider, 3> transientSliders;
    juce::ToggleButton transientSmoothButton;
    juce::ToggleButton transientLimitButton;

    juce::Label eqTitleLabel;
    juce::ToggleButton eqEnableButton;
    juce::ComboBox eqOrderCombo;
    juce::Label eqPlaceholderLabel;

    juce::Label eqBandLabel;
    juce::ComboBox eqBandSelector;
    juce::ToggleButton eqBandEnableButton;
    juce::Label eqFreqLabel;
    juce::Slider eqFreqSlider;
    juce::Label eqGainLabel;
    juce::Slider eqGainSlider;
    juce::Label eqQLabel;
    juce::Slider eqQSlider;
    juce::Label eqShapeLabel;
    juce::ComboBox eqShapeCombo;

    juce::Label stereoTitleLabel;
    juce::Label panLabel;
    juce::Slider panSlider;
    juce::Label widthLabel;
    juce::Slider widthSlider;

    std::vector<SoundTargetDescriptor> targetDescriptors;
    SoundTargetDescriptor currentTarget;
    SoundLayerState currentSoundState;
    bool targetAvailable = true;
    bool suppressCallbacks = false;

    std::array<juce::Rectangle<int>, 3> moduleGroupBounds {};
    juce::Rectangle<int> eqDisplayBounds;
    juce::Rectangle<int> eqEditorBounds;
    juce::Rectangle<int> stereoBounds;

    void setupSectionTitle(juce::Label& label, const juce::String& text);
    void setupControlLabel(juce::Label& label, const juce::String& text);
    void setupLinearSlider(juce::Slider& slider,
                           double min,
                           double max,
                           double step,
                           const juce::String& suffix = {});
    void setupToggle(juce::ToggleButton& button, const juce::String& text, const juce::String& tooltip = {});
    void populateOrderCombo(juce::ComboBox& combo);
    void setControlsEnabled(bool shouldEnable);
    void refreshControlsFromState();
    void updateTargetPresentation(const std::vector<TrackState>& tracks,
                                  const SoundTargetDescriptor& selectedTarget,
                                  bool targetMatchedInCombo,
                                  const juce::String& matchedTargetName);
    void updateChainSummary();
    void emitSoundLayerChange();

    EqBandState& currentEqBand();
    const EqBandState& currentEqBand() const;
};
} // namespace bbg
