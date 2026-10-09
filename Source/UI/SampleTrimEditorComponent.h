#pragma once

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Analysis/SampleSourceAudio.h"

namespace bbg
{
// Trim editor for a dropped sample: choose the fragment the analyzer reads (drum break copy or
// guide generation with bass). The file is never cut - only the selection is stored.
//  - full-width waveform, mouse wheel zooms around the cursor, shift+wheel / scrollbar scrolls;
//  - drag on empty space selects, drag the edges to adjust, drag inside to move;
//  - snap to beat / bar of the detected tempo grid and to transients (hits);
//  - Space plays / stops the selection (looped) with a playhead; Enter analyzes;
//  - auto-select finds the most drum-like fragment of a long song;
//  - length hint on the selection: "4.00 bars · 8.5 s".
class SampleTrimEditorComponent final : public juce::Component,
                                        private juce::Timer
{
public:
    enum class Snap
    {
        Off = 1,
        Beat = 2,
        Bar = 3
    };

    // knownBpm / knownAnchorSeconds: the analysed (or typed) tempo and beat 1, so the grid
    // matches the sample panel instead of a separate rough estimate. knownBpmIsManual: the
    // tempo was typed by the user (kept on Analyze).
    SampleTrimEditorComponent(std::shared_ptr<const SampleSourceAudio> source,
                              juce::Range<double> initialSelection,
                              bool guideMode,
                              double knownBpm = 0.0,
                              double knownAnchorSeconds = -1.0,
                              bool knownBpmIsManual = false);
    ~SampleTrimEditorComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    void visibilityChanged() override;

    std::function<void(juce::Range<double>)> onAnalyzeSelection;
    std::function<void()> onCancel;
    std::function<void(juce::Range<double>)> onPlaySelection;
    std::function<void()> onStopPlayback;
    std::function<double()> getPlayheadSeconds; // < 0 when stopped

    // --- model (also used by tests) ---
    juce::Range<double> getSelection() const { return selection; }
    void setSelection(juce::Range<double> newSelection);
    double getGridBpm() const { return gridBpm; }
    double getTypedBpm() const { return bpmTypedByUser ? gridBpm : 0.0; } // 0 = detect on Analyze
    void setGridBpm(double bpm);
    // The selection is exactly the loop: tempo = whole bars (nearest the current grid) x 240 /
    // its length, bar 1 at its start; the tempo counts as typed (used by Analyze selection).
    void fitBpmToSelection();
    double getGridAnchor() const { return gridAnchor; }
    void setSnap(Snap newSnap);
    void setSnapToHits(bool enabled);
    double snapTime(double seconds) const;           // applies the current snap settings
    double selectionBars() const;                    // selection length in bars of the grid tempo
    juce::String selectionInfo() const;              // "4.00 bars · 8.50 s · 0:42.10 - 0:50.60"
    juce::String qualityWarning() const;             // empty when fine
    void autoSelect(int bars);
    void selectWholeFile();

    static constexpr double kMaxSelectionSeconds = 64.0;

private:
    class WaveView;
    friend class WaveView;

    void timerCallback() override;
    void updateInfo();
    void togglePlayback();
    void finishAnalyze();
    bool isPlaying() const;

    std::shared_ptr<const SampleSourceAudio> source;
    juce::Range<double> selection;
    bool guideMode = false;
    double gridBpm = 120.0;
    double gridAnchor = 0.0;
    Snap snap = Snap::Beat;
    bool snapToHits = true;

    std::unique_ptr<WaveView> waveView;
    juce::ScrollBar scrollBar { false };
    std::unique_ptr<juce::ScrollBar::Listener> scrollListener;
    bool wasPlaying = false;
    bool bpmTypedByUser = false;
    juce::Label titleLabel;
    juce::Label bpmCaption;
    juce::Label bpmValue;
    juce::TextButton halfButton { "1/2" };
    juce::TextButton doubleButton { "x2" };
    juce::TextButton fitButton { "BPM = selection" };
    juce::TextButton anchorButton { "Bar 1 = selection start" };
    juce::ComboBox snapCombo;
    juce::ToggleButton hitsToggle { "Snap to hits" };
    juce::ComboBox lengthCombo;
    juce::TextButton autoButton { "Auto-select" };
    juce::TextButton wholeButton { "Whole file" };
    juce::Label infoLabel;
    juce::Label hintLabel;
    juce::TextButton playButton { "Play [Space]" };
    juce::TextButton analyzeButton { "Analyze selection" };
    juce::TextButton cancelButton { "Cancel" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SampleTrimEditorComponent)
};
} // namespace bbg
