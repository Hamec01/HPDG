#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Analysis/DrumBreakTranscriber.h"
#include "../Analysis/SampleSourceAudio.h"

namespace bbg
{
// Compact sample-analysis strip for the VST3 editor: drop / open an audio file, pick
// "Copy break" (Kick / Snare / HiHat transcribed with the original groove) or "Guide"
// (the sample steers the genre engine), analyze, and see what was heard on a K/S/H timeline.
// The analyzed fragment's waveform is drawn muted behind the dots; clicking a dot plays that hit,
// clicking elsewhere plays one beat from there. "Trim..." opens the fragment editor, and the
// optional "Play w/ HPDG" box plays the fragment along with the generated pattern.
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
        std::shared_ptr<const SampleSourceAudio> source; // decoded file (waveform, audition)
        double trimStartSeconds = 0.0;                   // analyzed fragment in the file
        double trimEndSeconds = 0.0;                     // <= start: whole file
        bool playWithPattern = false;
    };

    SampleBreakStripComponent();

    void setState(const State& state);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

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
    std::function<void()> onTrim;
    std::function<void(bool)> onPlayWithPatternChanged;
    std::function<void(double, double)> onAuditionRange; // seconds in the file

    // File time of a timeline x (analysis ready), or of the fragment position otherwise.
    double timeForTimelineX(float x) const;
    // One-line data-quality warning for the current analysis (empty when fine).
    juce::String qualityWarning() const;

private:
    void paintTimeline(juce::Graphics& g, juce::Rectangle<int> area) const;
    void paintWaveform(juce::Graphics& g, juce::Rectangle<int> area) const;
    juce::Rectangle<int> timelineDotsArea() const;
    double fragmentStart() const;
    double fragmentEnd() const;
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
    juce::TextButton trimButton { "Trim..." };
    juce::ToggleButton playWithToggle { "Play w/ HPDG" };
    mutable std::vector<std::pair<float, float>> wavePeaks;
    juce::Label quantizeLabel;
    juce::Slider quantizeSlider;
};
} // namespace bbg
