#pragma once

#include <array>
#include <cmath>
#include <memory>
#include <vector>

#include <juce_core/juce_core.h>

#include "Bass808/BassSample.h"
#include "DSP/AlignmentDelay.h"
#include "DSP/Saturator.h"

namespace bbg
{
// HPDG 808's sound engine (2026-11-01, second pass): a mono bass instrument for any genre.
//
//   SYNTH layer: oscillator (Sine with FM growl, Triangle, Saw, Square / PWM, Reese = two detuned saws whose
//                beats move the tone) + a sine one octave below, the 808 pitch punch, the 808 saturation and
//                key-tracked tone low-pass of Synth808Voice, the click layer
//   SAMPLE layer: any WAV (a kick, a bass one-shot): trim, loop (whole periods of its pitch), tune, follows
//                the keys or not, stretch without a pitch change (two-grain overlap-add), reverse, polarity
//   MIX:          both layered, or the sample hands over to the synth (a kick's attack into an 808 body)
//   -> amp envelope (attack, hold, -6 dB per Decay, Sustain, Release)
//   -> FILTER: LP12, LP24, HP12, BP, Vowel (three fixed formants: the vowel's resonances stay put whatever the
//                note - Helmholtz), cutoff moved by the key, an envelope and the LFO
//   -> PHANTOM: harmonics 2-5 of the fundamental (envelope-normalised Chebyshev), so the sub is heard on small
//                speakers (the missing fundamental)
//   -> bus: the HamloMag saturator (copied from HPEQ: Classic, Tape, Tube, Iron, Germanium, Vinyl), dry / wet
//
// The measurements behind the presets: docs/audit/BASS808_CORPUS.md.
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

    // play
    int mode = Glide;
    float glideMs = 120.0f;
    float tuneSemitones = 0.0f;
    float gainDb = 0.0f;
    float velocity = 0.5f;
};

struct BassPreset
{
    const char* genre;
    const char* name;
    BassSettings settings;
};

// 808 (the ten fitted to 672 one-shots), Boom Bap, Techno, DnB, Kick -> 808.
const std::vector<BassPreset>& bassPresets();

class BassVoice
{
public:
    void prepare(double sampleRate);
    void noteOn(double frequencyHz, float velocity, const BassSettings&, std::shared_ptr<const BassSample>);
    void glideTo(double frequencyHz, int glideSamples);
    void noteOff(int fadeSamples);
    bool isActive() const noexcept { return active; }
    void setSettings(const BassSettings&, float synthGain);
    // Adds this voice to `out`; lfo: the LFO per sample (-1 .. 1).
    void render(float* out, const float* lfo, int numSamples);

private:
    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0, z1 = 0.0, z2 = 0.0;
        void lowPass(double rate, double hz, double q);
        void highPass(double rate, double hz, double q);
        double process(double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void clear() noexcept { z1 = z2 = 0.0; }
    };
    struct Svf // Zavalishin / Cytomic trapezoidal state-variable filter
    {
        double ic1 = 0.0, ic2 = 0.0;
        void tick(double v0, double g, double k, double& low, double& band, double& high) noexcept;
        void clear() noexcept { ic1 = ic2 = 0.0; }
    };
    struct Grain
    {
        double position = 0.0;
        int age = 0;
        bool on = false;
    };

    double noteHz() const noexcept { return baseHz * std::exp2(semitones / 12.0); }
    void updateNoteFilters();
    void updateRegion();
    double readRegion(double position) const noexcept;
    double synthSample(double level) noexcept;
    double clickSample() noexcept;
    double sampleSample() noexcept;
    double filterSample(double x, double lfo) noexcept;

    BassSettings settings;
    std::shared_ptr<const BassSample> sample;
    double sampleRate = 48000.0;
    float synthGain = 1.0f, velocity = 1.0f;
    bool active = false;

    double baseHz = 65.4, semitones = 0.0, targetSemitones = 0.0, glideStep = 0.0;
    int glideRemaining = 0;

    // synth
    double phase = 0.0, phase2 = 0.0, phaseSub = 0.0, punch = 0.0, punchDecay = 1.0, boost = 0.0, boostDecay = 1.0;
    double click = 0.0, clickDecay = 1.0;
    std::uint32_t noise = 0x12345678u;
    std::array<Biquad, 2> clickBand {};
    std::array<Biquad, 4> tone {};
    bool toneOpen = false;
    double tanhDrive = 1.0, tanhBias = 0.0, dcIn = 0.0, dcOut = 0.0, dcCoefficient = 0.9995;

    // envelope
    double amp = 1.0, ampDecay = 1.0;
    std::int64_t elapsed = 0, holdSamples = 0, maxSamples = 0;
    int attackSamples = 1, fadeRemaining = 0, fadeTotal = 0;

    // sample
    double contentPosition = 0.0, regionStart = 0.0, regionEnd = 0.0, loopStart = 0.0, loopLength = 0.0, loopFade = 1.0;
    std::array<Grain, 2> grains {};
    int grainLength = 1, sinceGrain = 0, nextGrain = 0;
    double sampleAge = 0.0;

    // filter
    std::array<Svf, 2> svf {};
    std::array<Svf, 3> formant {};
    Biquad vowelLow;
    double filterEnv = 0.0, filterEnvDecay = 1.0;

    // phantom
    Biquad phantomBand;
    double phantomEnvelope = 0.0, phantomRelease = 0.999, phantomDcIn = 0.0, phantomDcOut = 0.0, phantomHz = 0.0;
    int phantomHold = 0;
};

// The instrument: mono (held-note stack, last note priority), two voices so a new note chokes the old
// one in 3 ms, the LFO, the HamloMag bus. Used by the plugin, its display and Tests/Bass808Lab.cpp.
class BassEngine
{
public:
    void prepare(double sampleRate, int maxBlockSize);
    void setSettings(const BassSettings&);
    const BassSettings& getSettings() const noexcept { return settings; }
    // From the message thread; the audio thread picks it up at the next note.
    void setSample(std::shared_ptr<const BassSample>);
    std::shared_ptr<const BassSample> getSample() const;
    void setTempo(double bpm) noexcept { tempo = bpm > 20.0 ? bpm : 120.0; }

    void noteOn(int note, float velocity);
    void noteOff(int note);
    void allNotesOff();
    void render(float* mono, int numSamples);

    // Constant (the largest the saturator can need), so the host's delay compensation never changes.
    int latencySamples() const noexcept { return maxLatency; }

    // For the display and the lab: one note held `seconds` long, the latency removed.
    static std::vector<float> renderNote(const BassSettings&, std::shared_ptr<const BassSample>, int note, double seconds, double sampleRate);
    static double noteHz(int note, float tuneSemitones);
    // The HamloMag slot for these settings (also drawn by the display).
    static hamlo::dsp::FxSlotParams saturatorParams(const BassSettings&) noexcept;

private:
    void startNote(int note, float velocity, bool fromHeldStack);
    void releaseNote(int note);
    void renderChunk(float* mono, int numSamples);
    double lfoHz() const noexcept;

    BassSettings settings;
    std::shared_ptr<const BassSample> sample;
    double sampleRate = 48000.0, tempo = 120.0, lfoPhase = 0.0;
    std::array<BassVoice, 2> voices;
    int currentVoice = 0;
    std::array<int, 16> held {};
    int heldCount = 0, sounding = -1;
    float lastVelocity = 1.0f;

    hamlo::dsp::SaturatorProcessor saturator;
    hamlo::dsp::AlignmentDelay dryDelay, outputDelay;
    bool saturatorWasOn = false;
    int maxLatency = 0;
    std::vector<float> lfoBuffer, dryBuffer;
    int chunkSize = 512;
};
} // namespace bbg
