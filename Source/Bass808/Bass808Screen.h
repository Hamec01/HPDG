#pragma once

#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Bass808/Bass808Processor.h"

namespace bbg
{
// HPDG 808's screen, one view per tab:
//   SYNTH    the whole note as it sounds now (rendered from the knobs) and its waveform up close; the live
//            output over it while a note plays
//   SAMPLE   the loaded sample with its start / loop / end markers (drag them)
//   MIX      the synth layer, the sample layer and the sum over the first 0.6 s; drag the handoff point
//   FX       the filter's response with the note's harmonics under it; the saturator's curve and harmonics
class Bass808Screen final : public juce::Component, private juce::Timer
{
public:
    enum Mode { Synth = 0, Sample, Mix, Effects };

    explicit Bass808Screen(Bass808Processor&);
    void setMode(Mode newMode);

    void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void renderPreviews();
    juce::Rectangle<float> plotArea() const;

    void paintSynth(juce::Graphics&, juce::Rectangle<float>);
    void paintSample(juce::Graphics&, juce::Rectangle<float>);
    void paintMix(juce::Graphics&, juce::Rectangle<float>);
    void paintEffects(juce::Graphics&, juce::Rectangle<float>);

    // sample markers: 0 start, 1 loop, 2 end
    float markerValue(int marker) const;
    const char* markerId(int marker) const;
    int markerAt(juce::Point<float>) const;
    void setParameter(const char* id, float value);

    Bass808Processor& bass;
    Mode mode = Synth;
    BassSettings settings;
    std::vector<float> snapshot;
    std::shared_ptr<const BassSample> renderedSample;
    int renderedNote = -1;
    int settleTicks = -1;

    static constexpr double kRate = 32000.0, kSeconds = 1.6, kMixSeconds = 0.6;
    std::vector<float> full, synthLayer, sampleLayer;
    std::vector<float> live;
    bool liveActive = false;

    int dragging = -1, hovering = -1;
    const char* dragId = nullptr;
};
} // namespace bbg
