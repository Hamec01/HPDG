#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Core/TrackInfo.h"
#include "../Core/TrackState.h"
#include "DragGestureButton.h"

namespace bbg
{
class TrackRowComponent : public juce::Component
{
public:
    explicit TrackRowComponent(const TrackInfo& info);

    void paint(juce::Graphics& g) override;
    void resized() override;

    void syncFromState(const TrackState& state);

    std::function<void(TrackType)> onRegenerate;
    std::function<void(TrackType, bool)> onSoloChanged;
    std::function<void(TrackType, bool)> onMuteChanged;
    std::function<void(TrackType)> onClear;
    std::function<void(TrackType, bool)> onLockChanged;
    std::function<void(TrackType, bool)> onEnableChanged;
    std::function<void(TrackType)> onDrag;
    std::function<void(TrackType)> onDragGesture;
    std::function<void(TrackType)> onExport;
    std::function<void(TrackType)> onPrevSample;
    std::function<void(TrackType)> onNextSample;
    std::function<void(TrackType, float)> onLaneVolumeChanged;
    std::function<void(int)> onBassKeyChanged;
    std::function<void(int)> onBassScaleChanged;

    void setBassControls(int keyRootChoice, int scaleModeChoice);

private:
    static juce::String roleLabelForTrack(TrackType type);

    TrackInfo trackInfo;
    TrackType trackType;

    juce::Label nameLabel;
    juce::Label roleLabel;
    juce::TextButton rgButton { "RG" };
    juce::ToggleButton soloButton { "S" };
    juce::ToggleButton muteButton { "M" };
    juce::TextButton clearButton { "C" };
    juce::ToggleButton lockButton { "L" };
    juce::ToggleButton enableButton { "E" };
    juce::Label volumeLabel;
    juce::Slider volumeSlider;
    juce::TextButton prevSampleButton { "<" };
    juce::Label sampleNameLabel;
    juce::TextButton nextSampleButton { ">" };
    juce::Label bassKeyLabel;
    juce::ComboBox bassKeyCombo;
    juce::Label bassScaleLabel;
    juce::ComboBox bassScaleCombo;
    DragGestureButton dragButton { "Drag" };
    juce::TextButton exportButton { "Export" };
};
} // namespace bbg
