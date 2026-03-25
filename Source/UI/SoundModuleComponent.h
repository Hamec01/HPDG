#pragma once

#include <functional>
#include <optional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Core/SoundLayerState.h"
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
                  const std::optional<TrackType>& selectedTarget,
                  const SoundLayerState& soundState);

    std::function<void(const std::optional<TrackType>&)> onSoundTargetChanged;
    std::function<void(const std::optional<TrackType>&, const SoundLayerState&)> onSoundLayerChanged;

private:
    juce::Label titleLabel;
    juce::Label targetLabel;
    juce::ComboBox targetCombo;

    juce::Label eqLabel;
    juce::Label compLabel;
    juce::Label reverbLabel;
    juce::Label gateLabel;
    juce::Label transientLabel;
    juce::Label driveLabel;

    juce::Slider eqSlider;
    juce::Slider compSlider;
    juce::Slider reverbSlider;
    juce::Slider gateSlider;
    juce::Slider transientSlider;
    juce::Slider driveSlider;

    std::vector<TrackType> targetTracks;
    std::optional<TrackType> currentTarget;
    SoundLayerState currentSoundState;

    void setupSlider(juce::Slider& slider,
                     double min,
                     double max,
                     double step,
                     juce::Colour trackColour,
                     juce::Colour thumbColour);
    void emitSoundLayerChange();
};
} // namespace bbg
