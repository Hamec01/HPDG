#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Analysis/DrumBreakTranscriber.h"

namespace bbg
{
// Compact sample-analysis strip for the VST3 editor: drop / open an audio file, pick
// "Copy break" (Kick / Snare / HiHat transcribed with the original groove) or "Guide"
// (the sample steers the genre engine), analyze, and see what was heard on a K/S/H timeline.
class SampleBreakStripComponent final : public juce::Component,
                                        public juce::FileDragAndDropTarget
{
public:
    enum class Mode
    {
        CopyBreak = 1,
        Guide = 2
    };

    struct State
    {
        juce::File file;
        Mode mode = Mode::CopyBreak;
        float quantize = 0.0f;
        bool analysisReady = false;
        juce::String error;
        DrumBreakAnalysis analysis;
        juce::String harmonyText; // e.g. "A minor 82% | bass 14/16"
    };

    SampleBreakStripComponent();

    void setState(const State& state);

    void paint(juce::Graphics& g) override;
    void resized() override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    static constexpr int kPreferredHeight = 92;

    std::function<void()> onChooseFile;
    std::function<void(const juce::File&)> onFileDropped;
    std::function<void(Mode)> onModeChanged;
    std::function<void(float)> onQuantizeChanged;
    std::function<void()> onAnalyze;
    std::function<void()> onClear;

private:
    void paintTimeline(juce::Graphics& g, juce::Rectangle<int> area) const;
    juce::String resultText() const;

    State state;
    bool dropHighlight = false;

    juce::Rectangle<int> dropZoneBounds;
    juce::Rectangle<int> resultBounds;
    juce::Rectangle<int> timelineBounds;

    juce::TextButton openButton { "Open..." };
    juce::ComboBox modeCombo;
    juce::TextButton analyzeButton { "Analyze" };
    juce::TextButton clearButton { "Clear" };
    juce::Label quantizeLabel;
    juce::Slider quantizeSlider;
};
} // namespace bbg
