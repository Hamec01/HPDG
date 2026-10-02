#pragma once

#include <vector>

#include <juce_core/juce_core.h>

namespace bbg
{
// One note of a line heard in the sample (seconds on the sample's own timeline).
struct SampleLineNote
{
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    int midiNote = 60;
    float strength = 0.0f;    // 0..1: loudness of the note's harmonic series vs the loudest in the line
    float confidence = 0.0f;  // 0..1: how clearly this pitch dominated its register while it sounded
    bool attacked = false;    // a real attack was heard at the start (not a pitch change inside a sound)

    double durationSeconds() const noexcept { return endSeconds - startSeconds; }
};

struct SampleLines
{
    std::vector<SampleLineNote> bass;    // G0..G3: what the low end plays
    std::vector<SampleLineNote> melody;  // E3..E6: the predominant melodic voice
    double tuningCents = 0.0;            // the sample's offset from A440 (records are often off)

    bool empty() const noexcept { return bass.empty() && melody.empty(); }
    float bassCoverage(double fromSeconds, double toSeconds) const;   // share of time a bass note sounds
    float melodyCoverage(double fromSeconds, double toSeconds) const;
    juce::String describe() const;
};

// Note-level transcription of the bass line and the lead melody of a (polyphonic) sample, for a
// bass that "understands" the sample. The approach follows bass / melody transcription
// literature (Goto's PreFEst, Ryynanen & Klapuri 2008, Salamon & Gomez "Melodia" 2012):
//   0. Tuning: records / pitched samples are often tens of cents off A440, which puts notes
//      between two semitones; the sample's own tuning is estimated first and used throughout.
//   1. Harmonic / percussive separation (median filtering, Fitzgerald 2010): kicks / snares /
//      hats are wide in frequency and short in time, sustained notes the opposite, so a soft
//      mask from a time-median vs a frequency-median spectrum keeps the notes only. Without
//      it a kick's 50-60 Hz body reads as a bass note.
//   2. Pitch salience by harmonic summation per semitone: a note scores the energy at its
//      fundamental and its harmonics. A long window (16384) resolves the low register, a
//      shorter one (8192) gives the melody its timing. A note must have its own fundamental,
//      so the octave below (whose 2nd harmonic it is) cannot win on harmonics alone.
//   3. Viterbi tracking over "note or silence" states: jumping costs more than staying and
//      large leaps cost more than steps, so the line does not flicker frame to frame; notes
//      outside the sample's key get a small penalty.
//   4. Segmentation into notes, splitting re-attacks of the same pitch (a dip then a rise in
//      that note's salience) and dropping fragments shorter than a note can be.
class SampleLineTranscriber
{
public:
    // keyRoot / scaleMode (0 minor, 1 major) bias out-of-key pitches when keyKnown.
    SampleLines transcribe(const std::vector<float>& mono,
                           double sampleRate,
                           int keyRoot = 0,
                           int scaleMode = 0,
                           bool keyKnown = false) const;

    // How far (-50..+50 cents) the sample's notes sit from the A440 grid; 0 when unclear.
    static double estimateTuningCents(const std::vector<float>& mono, double sampleRate);
};
} // namespace bbg
