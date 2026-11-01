#pragma once

#include <juce_core/juce_core.h>

namespace bbg
{
// Every control of HPDG 808's sound (the engine in BassEngine.h). Plain data: HPDG's Sub808 lane keeps one,
// the plugin builds one from its parameters, BassSettingsIO.h reads and writes it.
struct BassSettings
{
    enum Wave { Sine = 0, Triangle, Saw, Square, Reese, NumWaves };
    enum FilterType { LP12 = 0, LP24, HP12, BP, Vowel, NumFilters };
    enum PlayMode { Retrigger = 0, Legato, Glide };

    // synth layer
    int wave = Sine;
    float shape = 0.0f;           // Sine: FM depth; Triangle: skew; Saw: second saw detune; Square: pulse width; Reese: detune
    float sub = 0.0f;             // a sine under the oscillator
    int subOctave = 0;            // 0: one octave down, 1: at the note (under a Reese / saw, where -1 octave is below hearing)
    float synthLevelDb = 0.0f;    // -60: off
    float punchSemitones = 18.0f;
    float punchMs = 40.0f;
    float drive = 4.5f;           // 808 saturation (odd), bias (even), wet blend
    float bias = 0.08f;
    float saturation = 0.8f;
    float toneHz = 800.0f;        // key-tracked 4th-order low-pass at C2; >= 15 kHz: open
    float clickDb = -30.0f;       // <= -60: off
    float boostDb = 0.0f;

    // envelope
    float attackMs = 2.0f;
    float holdSeconds = 0.05f;
    float decaySeconds = 0.32f;   // -6 dB per this long
    float sustain = 0.0f;         // level the decay settles at while the key is held
    float releaseMs = 30.0f;

    // sample layer
    float sampleLevelDb = -60.0f; // -60: off
    float sampleTune = 0.0f;      // semitones
    float sampleFine = 0.0f;      // cents
    float sampleStart = 0.0f;     // fractions of the file
    float sampleEnd = 1.0f;
    bool sampleLoop = false;
    float sampleLoopStart = 0.5f;
    float sampleStretch = 1.0f;   // length x (0.25 .. 4)
    bool sampleLockTime = false;  // true: the same length on every note (pitch moves, time does not)
    bool sampleFollow = true;     // false: every key plays the sample at its own pitch
    bool sampleReverse = false;
    bool sampleInvert = false;
    float handoffMs = 0.0f;       // 0: layered; else the sample plays this long, then crossfades into the synth

    // filter
    int filterType = LP12;
    float cutoffHz = 20000.0f;
    float resonance = 0.0f;
    float filterEnvOct = 0.0f;
    float filterDecayMs = 200.0f; // to 10 %
    float filterKey = 0.0f;
    float vowel = 0.0f;           // 0 A, 1 E, 2 I, 3 O, 4 U

    // LFO -> cutoff (octaves) / vowel (steps)
    float lfoRateHz = 1.0f;
    int lfoSync = 0;              // 0 free, then 1/1 1/2 1/4 1/8 1/16 1/4T 1/8T 1/16T
    float lfoDepth = 0.0f;
    bool lfoRetrigger = true;

    // HamloMag
    int satModel = 0;             // 0 off, 1 Classic, 2 Tape, 3 Tube, 4 Iron, 5 Germanium, 6 Vinyl
    float satDriveDb = 0.0f;
    float satHarmonics = 0.5f;
    float satEvenOdd = 0.0f;
    float satMix = 1.0f;

    float phantom = 0.0f;

    // bus filter after the saturator (HPDG's lane strip): -1 .. 0 closes the highs 20 kHz -> 200 Hz,
    // 0 .. 1 opens a low cut 20 -> 400 Hz; 0: off
    float busFilter = 0.0f;

    // play
    int mode = Glide;
    float glideMs = 120.0f;
    float tuneSemitones = 0.0f;
    float gainDb = 0.0f;
    float velocity = 0.5f;
};
} // namespace bbg
