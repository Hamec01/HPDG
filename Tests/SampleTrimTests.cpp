// Sample source + trim editor tests: waveform peaks, transients, tempo grid, suggested drum
// fragment of a long "song", snapping and the trim editor's selection model.
#include <cmath>
#include <iostream>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Analysis/SampleSourceAudio.h"
#include "UI/SampleTrimEditorComponent.h"

namespace
{
int failures = 0;

void check(bool condition, const juce::String& what)
{
    std::cout << (condition ? "  ok   " : "  FAIL ") << what << std::endl;
    if (!condition)
        ++failures;
}

constexpr double kRate = 44100.0;
constexpr double kBpm = 100.0;
constexpr double kPadSeconds = 12.0;
constexpr double kDrumSeconds = 19.2; // 8 bars at 100 BPM

// 12 s of a soft tonal pad, 8 bars of drums at 100 BPM (kick on 1 and 3, snare on 2 and 4,
// hats on eighths), then 12 s of pad again.
juce::AudioBuffer<float> makeSong()
{
    const int total = static_cast<int>((kPadSeconds * 2.0 + kDrumSeconds) * kRate);
    juce::AudioBuffer<float> buffer(2, total);
    buffer.clear();
    juce::Random random(7);
    for (int i = 0; i < total; ++i)
    {
        const double t = i / kRate;
        float v = 0.08f * static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * t)
                                             + 0.5 * std::sin(2.0 * juce::MathConstants<double>::pi * 277.0 * t));
        if (t >= kPadSeconds && t < kPadSeconds + kDrumSeconds)
        {
            v *= 0.3f;
            const double local = t - kPadSeconds;
            const double eighth = 30.0 / kBpm;
            const int index = static_cast<int>(local / eighth);
            const double since = local - index * eighth;
            const int beatInBar = (index / 2) % 4;
            const bool onBeat = index % 2 == 0;
            if (onBeat && (beatInBar == 0 || beatInBar == 2)) // kick
                v += 0.9f * static_cast<float>(std::exp(-since * 25.0) * std::sin(2.0 * juce::MathConstants<double>::pi * 55.0 * since));
            if (onBeat && (beatInBar == 1 || beatInBar == 3)) // snare
                v += 0.6f * static_cast<float>(std::exp(-since * 30.0)) * (random.nextFloat() * 2.0f - 1.0f);
            v += 0.25f * static_cast<float>(std::exp(-since * 120.0)) * (random.nextFloat() * 2.0f - 1.0f); // hat
        }
        buffer.setSample(0, i, v);
        buffer.setSample(1, i, v);
    }
    return buffer;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::cout << "SampleSourceAudio" << std::endl;
    auto source = std::make_shared<bbg::SampleSourceAudio>();
    source->loadFromBuffer(makeSong(), kRate);
    check(source->isLoaded(), "song loads");
    check(std::abs(source->getDurationSeconds() - (kPadSeconds * 2.0 + kDrumSeconds)) < 0.01, "duration");

    // Peaks: silence-free pad is quiet, the drum part is loud.
    std::vector<std::pair<float, float>> peaks;
    source->getPeaks(0.0, source->getDurationSeconds(), 400, peaks);
    check(peaks.size() == 400, "peaks for 400 pixels");
    float padMax = 0.0f, drumMax = 0.0f;
    for (size_t i = 0; i < peaks.size(); ++i)
    {
        const double t = (i + 0.5) / 400.0 * source->getDurationSeconds();
        const float amplitude = juce::jmax(peaks[i].second, -peaks[i].first);
        if (t < kPadSeconds - 0.5)
            padMax = juce::jmax(padMax, amplitude);
        else if (t > kPadSeconds + 0.5 && t < kPadSeconds + kDrumSeconds - 0.5)
            drumMax = juce::jmax(drumMax, amplitude);
    }
    check(drumMax > padMax * 2.0f, "waveform shows the drums louder than the pad");
    source->getPeaks(kPadSeconds, kPadSeconds + 0.01, 50, peaks); // close zoom reads raw samples
    check(peaks.size() == 50, "close-zoom peaks");

    // Transients sit in the drum part, on the eighth-note grid.
    int inDrums = 0, onGrid = 0;
    for (const auto t : source->getTransients())
    {
        if (t < kPadSeconds - 0.05 || t > kPadSeconds + kDrumSeconds + 0.05)
            continue;
        ++inDrums;
        const double eighth = 30.0 / kBpm;
        const double phase = std::fmod(t - kPadSeconds, eighth);
        if (juce::jmin(phase, eighth - phase) < 0.03)
            ++onGrid;
    }
    std::cout << "  transients in drums: " << inDrums << ", on grid: " << onGrid
              << ", total: " << source->getTransients().size() << std::endl;
    check(inDrums >= 40, "most eighth-note hits are detected");
    check(onGrid >= inDrums * 8 / 10, "detected hits are on the grid");

    // Tempo (octave errors are allowed: the grid would still line up).
    const double bpm = source->getEstimatedBpm();
    std::cout << "  estimated bpm: " << bpm << "  confidence " << source->getTempoConfidence() << std::endl;
    const double octave = std::log2(bpm / kBpm);
    check(std::abs(octave - std::round(octave)) < 0.03, "tempo estimate ~100 BPM (or an octave of it)");

    // The suggested fragment is the drum part.
    const auto region = source->suggestRegion(4);
    std::cout << "  suggested: " << region.getStart() << " - " << region.getEnd() << std::endl;
    check(region.getStart() >= kPadSeconds - 0.3 && region.getEnd() <= kPadSeconds + kDrumSeconds + 0.3,
          "auto-select lands inside the drum part of the song");
    check(std::abs(region.getLength() - 4.0 * 240.0 / bpm) < 0.01, "auto-select length is 4 bars of the detected tempo");

    // Snapping.
    const double kickTime = kPadSeconds + 4.0 * 60.0 / kBpm; // bar 2 downbeat
    check(std::abs(source->snapToTransient(kickTime + 0.03, 0.08) - kickTime) < 0.02, "snap to the nearest hit");
    check(source->snapToTransient(kickTime + 0.2, 0.05) == kickTime + 0.2, "no hit near: position unchanged");
    check(std::abs(bbg::SampleSourceAudio::snapToGrid(1.26, 120.0, 0.0, 1) - 1.5) < 1.0e-9, "snap to beat");
    check(std::abs(bbg::SampleSourceAudio::snapToGrid(1.26, 120.0, 0.0, 4) - 2.0) < 1.0e-9, "snap to bar");
    check(std::abs(bbg::SampleSourceAudio::snapToGrid(0.9, 120.0, 0.25, 4) - 0.25) < 1.0e-9, "bar grid follows the anchor");

    // Short file: the suggestion is the whole file.
    auto shortSource = std::make_shared<bbg::SampleSourceAudio>();
    juce::AudioBuffer<float> shortBuffer(1, static_cast<int>(kRate * 3.0));
    shortBuffer.clear();
    shortSource->loadFromBuffer(shortBuffer, kRate);
    const auto shortRegion = shortSource->suggestRegion(8);
    check(shortRegion.getStart() == 0.0 && std::abs(shortRegion.getEnd() - 3.0) < 1.0e-6, "short file: whole file suggested");

    std::cout << "SampleTrimEditorComponent" << std::endl;
    {
        // Declared before the editor: its destructor calls onStopPlayback.
        bool playing = false;
        juce::Range<double> played;
        bbg::SampleTrimEditorComponent editor(source, {}, false);
        editor.setSize(1000, 380);
        const auto auto8 = editor.getSelection();
        check(auto8.getLength() > 1.0, "opens with an auto-selected fragment");
        check(auto8.getStart() >= kPadSeconds - 0.3, "... inside the drums");

        editor.setGridBpm(kBpm);
        editor.setSelection({ kPadSeconds, kPadSeconds + 4.0 * 2.4 });
        check(std::abs(editor.selectionBars() - 4.0) < 1.0e-6, "selection length in bars");
        check(editor.selectionInfo().startsWith("4.00 bars"), "length hint: " + editor.selectionInfo());
        check(editor.qualityWarning().isEmpty(), "4 bars of drums: no warning");

        editor.setSelection({ kPadSeconds, kPadSeconds + 2.4 });
        check(editor.qualityWarning().contains("Less than 2 bars"), "1 bar: short-fragment warning");
        editor.setSelection({ 1.0, 1.0 + 4.0 * 2.4 });
        check(editor.qualityWarning().contains("Few hits"), "pad-only fragment: few-hits warning");
        editor.setSelection({ 0.0, 200.0 });
        check(editor.getSelection().getLength() <= bbg::SampleTrimEditorComponent::kMaxSelectionSeconds + 1.0e-9,
              "selection capped at the analyzer maximum");

        editor.setSnap(bbg::SampleTrimEditorComponent::Snap::Bar);
        editor.setSnapToHits(false);
        const double anchor = editor.getGridAnchor();
        const double snapped = editor.snapTime(anchor + 2.4 * 3.0 + 0.4);
        check(std::abs(snapped - (anchor + 2.4 * 3.0)) < 1.0e-6, "bar snapping in the editor");

        editor.selectWholeFile();
        check(std::abs(editor.getSelection().getLength() - juce::jmin(64.0, source->getDurationSeconds())) < 1.0e-6, "whole file (capped)");

        juce::Range<double> analyzed;
        editor.onAnalyzeSelection = [&analyzed](juce::Range<double> r) { analyzed = r; };
        editor.setSelection({ kPadSeconds, kPadSeconds + 9.6 });
        editor.keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
        check(analyzed == editor.getSelection(), "Enter sends the selection to the analyzer");

        editor.getPlayheadSeconds = [&playing] { return playing ? 13.0 : -1.0; };
        editor.onPlaySelection = [&](juce::Range<double> r) { played = r; playing = true; };
        editor.onStopPlayback = [&] { playing = false; };
        editor.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey));
        check(playing && played == editor.getSelection(), "Space plays the selection");
        editor.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey));
        check(!playing, "Space again stops");
    }

    std::cout << (failures == 0 ? "ALL PASSED" : juce::String(failures) + " FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
