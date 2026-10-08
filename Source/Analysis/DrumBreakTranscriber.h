#pragma once

#include <array>
#include <vector>

#include <juce_core/juce_core.h>

#include "../Core/TrackType.h"

namespace bbg
{
// Drum-break (drum loop) transcription: audio -> Kick / Snare / HiHat events in PPQ 960 ticks.
//
// The pipeline works in real time (seconds) first and only maps to ticks at the very end, so
// the tempo is never derived from a grid that already assumed a tempo:
//   1. band spectrogram + log spectral flux -> onset candidates (seconds)
//   2. onset time refined on the waveform (~0.5 ms)
//   3. per-onset spectral increase vectors -> semi-adaptive NMF with Kick/Snare/Hat templates
//      (multi-label: one onset may be kick + hat at once)
//   4. tempo candidates from loop length (N bars) + grid fit; decided by grid fit, loop length,
//      snare backbeat, a mild tempo prior and the host tempo as a hint (never as ground truth)
//   5. hits -> absolute ticks from the loop origin -> gridTick + timingOffsetTicks
// Only the three core lanes are produced; genre engines own every other lane.
struct BreakDrumHit
{
    TrackType lane = TrackType::Kick;
    double timeSeconds = 0.0;
    float activation = 0.0f;  // raw NMF activation
    float strength = 0.0f;    // 0..1 relative to the loudest hit of the same lane
    float confidence = 0.0f;  // 0..1
    int tick = 0;             // absolute position from loop origin (PPQ 960), before quantize
    int gridTick = 0;
    int timingOffsetTicks = 0;
    int velocity = 100;
    int onsetIndex = -1;
    bool inferred = false;    // added by pattern completion (e.g. hat masked by a snare)
};

struct BreakTempoCandidate
{
    double bpm = 0.0;
    int bars = 0;
    float swing = 0.0f;       // odd-sixteenth delay as a fraction of a sixteenth
    float gridFit = 0.0f;
    float lengthFit = 0.0f;
    float backbeat = 0.0f;
    float prior = 0.0f;
    float host = 0.0f;
    float label = 0.0f;       // 1: the tempo the sample's name / acid chunk states
    float score = 0.0f;
};

struct DrumBreakOptions
{
    double hostBpm = 0.0;        // hint only
    double labelBpm = 0.0;       // tempo the sample states (name / acid chunk): strong hint, checked against the audio
    double forcedBpm = 0.0;      // > 0: skip detection and use this tempo
    double minBpm = 55.0;
    double maxBpm = 200.0;
    double preferredBpm = 92.0;  // centre of the mild tempo prior
    float quantizeAmount = 0.0f; // 0 = keep original groove, 1 = hard quantize
};

struct DrumBreakAnalysis
{
    bool valid = false;

    double sampleRate = 44100.0;
    double durationSeconds = 0.0;

    double bpm = 0.0;            // the loop's tempo (what the pattern is written at)
    // Tempo the hits' musical slots are read on, when the playing drifts from the stated tempo
    // (a 79.4 BPM performance in a file rendered at 80, or a typed tempo): slots come from this
    // grid, timing offsets keep the real hit times at `bpm`. 0 = same as bpm.
    double gridBpm = 0.0;
    float tempoConfidence = 0.0f;
    bool bpmMatchesHost = false;
    int bars = 0;
    double originSeconds = 0.0;  // time of beat 1 of bar 1 (may be before the first hit)
    bool exactLoop = false;      // file is a trimmed loop of whole bars
    float swingPercent = 50.0f;
    std::vector<BreakTempoCandidate> tempoCandidates;

    std::vector<double> onsetTimes;
    std::vector<std::array<float, 3>> onsetLaneLevels; // K/S/H activation relative to lane reference
    std::vector<float> onsetHighLevels;                // spectral increase above 6 kHz per onset
    std::vector<BreakDrumHit> hits;

    float sustainRatio = 0.0f;  // how much energy never decays (tonal / pads) - low for drums
    float highBandShare = 0.0f; // share of power above 4 kHz (hats / snare noise) - ~0 for tonal loops
    float templateFit = 0.0f;   // how well K/S/H templates explain the onsets
    float drumLoopConfidence = 0.0f;

    int countLane(TrackType lane) const;
    juce::String describe(bool includeHits) const;
};

class DrumBreakTranscriber
{
public:
    DrumBreakAnalysis analyze(const std::vector<float>& mono,
                              double sampleRate,
                              const DrumBreakOptions& options) const;

    // Re-derives ticks (and quantization) for an existing analysis at a different tempo or
    // quantize amount without re-running the audio stages.
    static void assignTicks(DrumBreakAnalysis& analysis, float quantizeAmount);
};
} // namespace bbg
