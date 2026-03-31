#pragma once

#include <array>
#include <functional>
#include <optional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Core/SoundLayerState.h"
#include "../Core/SoundTargetDescriptor.h"
#include "../Core/TrackRegistry.h"

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
    juce::ComboBox targetCombo;

    juce::Label toneSectionLabel;
    juce::Label spaceSectionLabel;
    juce::Label textureSectionLabel;

    juce::Label eqLabel;
    juce::Label compLabel;
    juce::Label reverbLabel;
    juce::Label gateLabel;
    juce::Label transientLabel;
    juce::Label driveLabel;

    juce::Label eqValueLabel;
    juce::Label compValueLabel;
    juce::Label reverbValueLabel;
    juce::Label gateValueLabel;
    juce::Label transientValueLabel;
    juce::Label driveValueLabel;

    juce::Slider eqSlider;
    juce::Slider compSlider;
    juce::Slider reverbSlider;
    juce::Slider gateSlider;
    juce::Slider transientSlider;
    juce::Slider driveSlider;

    std::vector<SoundTargetDescriptor> targetDescriptors;
    SoundTargetDescriptor currentTarget;
    SoundLayerState currentSoundState;
    bool targetAvailable = true;
    std::array<juce::Rectangle<int>, 3> moduleGroupBounds {};

    void setupSlider(juce::Slider& slider,
                     double min,
                     double max,
                     double step,
                     juce::Colour trackColour,
                     juce::Colour thumbColour);
    void setControlsEnabled(bool shouldEnable);
    void updateTargetPresentation(const std::vector<TrackState>& tracks,
                                  const SoundTargetDescriptor& selectedTarget,
                                  bool targetMatchedInCombo,
                                  const juce::String& matchedTargetName);
    void updateValueLabels();
    void emitSoundLayerChange();
};
} // namespace bbg
