#pragma once

#include <array>
#include <memory>
#include <vector>

#include <juce_audio_utils/juce_audio_utils.h>

#include "Bass808Processor.h"
#include "Bass808Screen.h"
#include "Bass808Widgets.h"

namespace bbg
{
// HPDG 808's window, in HPDG's Dark skin: preset / mode / play keys on top, four tabs (SYNTH, SAMPLE,
// MIX, FILTER · FX) that switch the screen and the controls under it. Drop an audio file anywhere on the
// window to load it into the sample layer.
class Bass808Editor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit Bass808Editor(Bass808Processor& processor);
    ~Bass808Editor() override;

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;

    // 0 SYNTH, 1 SAMPLE, 2 MIX, 3 FILTER / FX
    void showTab(int index);

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray&, int, int) override;
    void fileDragExit(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray& files, int, int) override;

private:
    class PlayButton;

    void timerCallback() override;
    void selectTab(int index);
    void stepPreset(int delta);
    void chooseSample();
    void loadSample(const juce::File& file);
    void fillPresetBox();
    void savePreset();

    Bass808Processor& bass;
    b808::Look look;
    juce::ComboBox presetBox;
    juce::TextButton previousPreset { "<" }, nextPreset { ">" }, saveButton { "SAVE" };
    juce::Array<juce::File> userPresets;
    static constexpr int kUserPresetId = 1001;
    std::unique_ptr<b808::ParamCombo> mode;
    std::unique_ptr<PlayButton> play;
    std::vector<std::unique_ptr<b808::ParamKnob>> headerKnobs;
    std::array<juce::TextButton, 4> tabs;
    Bass808Screen screen;
    std::array<std::unique_ptr<b808::Page>, 4> pages;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String message;
    int messageTicks = 0;
    bool dragOver = false;
    int shownPreset = -1;
};
} // namespace bbg
