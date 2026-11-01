#pragma once

#include <memory>
#include <vector>

#include <juce_audio_utils/juce_audio_utils.h>

#include "Bass808Processor.h"

namespace bbg
{
// HPDG 808's window: preset and mode on top, the sound knobs in three groups (pitch, tone, shape),
// the performance knobs, and on-screen keys to play it without a MIDI keyboard.
class Bass808Editor : public juce::AudioProcessorEditor
{
public:
    explicit Bass808Editor(Bass808Processor& processor);
    ~Bass808Editor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void addKnob(const char* id, const char* text);

    Bass808Processor& bass;
    juce::LookAndFeel_V4 look;
    juce::ComboBox presetBox;
    juce::ComboBox modeBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment;
    std::vector<std::unique_ptr<Knob>> knobs;
    juce::MidiKeyboardComponent keyboard;
};
} // namespace bbg
