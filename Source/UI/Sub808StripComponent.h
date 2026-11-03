#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Core/Sub808Types.h"
#include "HardwareKnob.h"

namespace bbg
{
// The bass strip under the rack: a thin line with an arrow; open, it shows four big knobs on top of the
// Sub808 lane's HPDG 808 sound - Filter (left closes the highs, right cuts the lows), Drive (saturator,
// with its model), Length, Gain and Sidechain (the kick ducks the bass) - and EDIT, which opens the full HPDG 808 editor.
class Sub808StripComponent final : public juce::Component
{
public:
    static constexpr int kClosedHeight = 16;
    static constexpr int kOpenHeight = 118;

    Sub808StripComponent();

    std::function<void(const Sub808SoundState&)> onChange;   // every knob move
    std::function<void(bool open)> onOpenChanged;            // the host lays the rack out again
    std::function<void()> onEdit;

    bool isOpen() const noexcept { return open; }
    void setOpen(bool shouldBeOpen);
    int getPreferredHeight() const noexcept { return open ? kOpenHeight : kClosedHeight; }
    // From the processor; does not call onChange. soundName: what the lane plays (bank sample or preset).
    void setState(const Sub808SoundState& state, const juce::String& soundName);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    struct Knob
    {
        RotaryKnobSlider slider;
        juce::Label caption, value;
    };

    void setupKnob(Knob& knob, const juce::String& caption, double min, double max, double reset);
    void knobMoved();
    void updateValueTexts();
    juce::Rectangle<int> arrowBar() const { return getLocalBounds().removeFromTop(kClosedHeight); }

    Knob filter, drive, length, gain, sidechain;
    juce::ComboBox driveModel;
    juce::TextButton editButton { "EDIT 808" };
    juce::Label soundLabel;
    Sub808SoundState state;
    bool open = false;
    bool updating = false;
};
} // namespace bbg
