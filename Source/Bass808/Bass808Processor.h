#pragma once

#include <array>

#include <juce_audio_processors/juce_audio_processors.h>

#include "../Audio/Synth808Voice.h"

namespace bbg
{
// HPDG 808 (2026-11-01): HPDG's 808 synth as its own small instrument plugin (VST3 / Standalone),
// for any DAW and any genre. MIDI in, a mono 808 out: one note at a time, Retrigger / Legato /
// Glide like the Sub808 lane, ten presets fitted to pro 808 one-shots (Synth808Voice), every
// setting a knob.
class Bass808Processor : public juce::AudioProcessor
{
public:
    enum class Mode
    {
        Retrigger = 0,
        Legato,
        Glide
    };

    Bass808Processor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "HPDG 808"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return Synth808Voice::kPresetCount; }
    int getCurrentProgram() override { return currentPreset; }
    void setCurrentProgram(int index) override { applyPreset(index); }
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Sets every sound knob to the preset's values.
    void applyPreset(int index);

    juce::MidiKeyboardState keyboardState; // the editor's on-screen keys
    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    Synth808Params currentParams() const;
    void handleMidi(const juce::MidiMessage& message);
    void startNote(int note, float velocity, bool fromHeldStack);
    void releaseNote(int note);
    double noteHz(int note) const;
    int msToSamples(float ms) const;
    float paramValue(const char* id) const;

    std::array<Synth808Voice, 2> voices; // the sounding note + the one choking out (3 ms)
    int currentVoice = 0;
    std::array<int, 16> held {};         // notes held, the last on top (mono, last-note priority)
    int heldCount = 0;
    int sounding = -1;
    float lastVelocity = 1.0f;
    double sampleRate = 48000.0;
    int currentPreset = 0;
};
} // namespace bbg
