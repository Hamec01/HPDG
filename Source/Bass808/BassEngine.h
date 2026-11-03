#pragma once

#include <array>
#include <cmath>
#include <memory>
#include <vector>

#include <juce_core/juce_core.h>

#include "Bass808/BassSample.h"
#include "Bass808/BassSettings.h"
#include "Bass808/BassSaturator.h"

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
//   -> bus: the saturator (BassSaturator: HamloMag's Classic, Tape, Tube, Iron, Germanium, Vinyl curves), dry / wet
//
// The measurements behind the presets: docs/audit/BASS808_CORPUS.md.
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
    // linearRate: the playback rate moves in a straight line (HPDG's old sample player), not the pitch
    void glideTo(double frequencyHz, int glideSamples, bool linearRate = false);
    void noteOff(int fadeSamples);
    bool isActive() const noexcept { return active; }
    void setSettings(const BassSettings&, float synthGain);
    // Adds this voice to `out`; lfo: the LFO per sample (-1 .. 1).
    void render(float* out, const float* lfo, int numSamples);

    struct Biquad // RBJ, also the engine's bus filter
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

private:
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
    bool glideLinearRate = false;
    double glideRatio = 1.0, glideRatioStep = 0.0;

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
// one in 3 ms, the LFO, the saturator bus. Used by the plugin, its display and Tests/Bass808Lab.cpp.
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
    // HPDG's Sub808 lane: each note says how it joins the one sounding (the lane's overlap mode, a slide),
    // instead of the settings' mode / glide.
    void noteOn(int note, float velocity, int playMode, float glideMs);
    void noteOff(int note);
    void allNotesOff();
    void render(float* mono, int numSamples);
    bool isActive() const noexcept { return voices[0].isActive() || voices[1].isActive(); }

    // The saturator works sample by sample: no latency.
    int latencySamples() const noexcept { return 0; }

    // For the display and the lab: one note held `seconds` long, the latency removed.
    static std::vector<float> renderNote(const BassSettings&, std::shared_ptr<const BassSample>, int note, double seconds, double sampleRate);
    static double noteHz(int note, float tuneSemitones);
    // The saturator's settings (also drawn by the display).
    static BassSaturator::Params saturatorParams(const BassSettings&) noexcept;

private:
    void startNote(int note, float velocity, bool fromHeldStack);
    void releaseNote(int note);
    void renderChunk(float* mono, int numSamples);
    double lfoHz() const noexcept;
    int playMode() const noexcept { return modeOverride >= 0 ? modeOverride : settings.mode; }
    void processBusFilter(float* mono, int numSamples) noexcept;

    BassSettings settings;
    std::shared_ptr<const BassSample> sample;
    double sampleRate = 48000.0, tempo = 120.0, lfoPhase = 0.0;
    std::array<BassVoice, 2> voices;
    int currentVoice = 0;
    std::array<int, 16> held {};
    int heldCount = 0, sounding = -1;
    float lastVelocity = 1.0f;
    int modeOverride = -1;
    float glideOverrideMs = 0.0f;

    std::array<BassVoice::Biquad, 2> busStages {};
    double busPosition = 0.0;       // the smoothed busFilter
    int busKind = 0;                // -1 low-pass, 1 high-pass, 0 off

    BassSaturator saturator;
    std::vector<float> lfoBuffer;
    int chunkSize = 512;
};
} // namespace bbg
