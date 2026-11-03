#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "Bass808/BassEngine.h"
#include "Bass808/BassSettingsIO.h"

namespace bbg
{
// HPDG 808: the bass instrument on its own (VST3 / Standalone), for any DAW and any genre. The sound is
// BassEngine (synth layer + sample layer, filter, saturator, phantom harmonics); the plugin maps its
// parameters, keeps the loaded sample in the project, and feeds the display.
class Bass808Processor : public juce::AudioProcessor
{
public:
    Bass808Processor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "HPDG 808"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 1.0; }

    int getNumPrograms() override { return static_cast<int>(bassPresets().size()); }
    int getCurrentProgram() override { return currentPreset; }
    void setCurrentProgram(int index) override { applyPreset(index); }
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Sets every parameter from the preset (the loaded sample stays).
    void applyPreset(int index);
    BassSettings currentSettings() const;
    // Every parameter from these settings (a user preset, HPDG's Sub808 lane).
    void writeSettings(const BassSettings&);
    // A user preset file (BassSettingsIO.h): its settings, and its sample when it has one.
    void applyUserPreset(const BassPresetFile& preset);
    // What the sound is called: the factory preset, or the user preset last loaded / saved.
    juce::String presetName() const;
    void setUserPresetName(const juce::String& name) { userPresetName = name; }
    bool isUserPreset() const noexcept { return userPresetName.isNotEmpty(); }
    // Message thread: the sample layer's file as it is (HPDG's lane hands its sample over).
    void setSampleDirect(std::shared_ptr<const BassSample> sample);

    // Message thread. A new sample: markers back to the whole file, the layer turned up if it was off.
    bool loadSample(const juce::File& file, juce::String& error);
    void clearSample();
    std::shared_ptr<const BassSample> getSample() const { return engine.getSample(); }

    // The last `count` output samples (count <= kScopeSize), for the live scope.
    static constexpr int kScopeSize = 1 << 14;
    void copyScope(float* destination, int count) const;
    int lastNote() const noexcept { return lastPlayedNote.load(); }
    float recentPeak() const noexcept { return outputPeak.load(); }

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState; // the editor's PLAY button

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void setParameter(const char* id, float value);
    void retire(std::shared_ptr<const BassSample> old);

    BassEngine engine;
    int currentPreset = 0;
    juce::String userPresetName;
    std::vector<float> mono;
    std::array<float, kScopeSize> scope {};
    std::atomic<int> scopeWrite { 0 };
    std::atomic<int> lastPlayedNote { 36 };
    std::atomic<float> outputPeak { 0.0f };
    std::vector<std::shared_ptr<const BassSample>> retired; // freed here, never on the audio thread

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Bass808Processor)
};
} // namespace bbg
