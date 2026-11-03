#pragma once

#include <array>

#include <juce_core/juce_core.h>

namespace bbg
{
// HPDG's 808 as a synth voice (2026-11-01), standalone: not wired into the Sub808 lane yet
// (Tests/Synth808Lab.cpp renders and measures it first). Same recipe as the pre-rendered style
// 808s of tools/make_synth_sub.py (a sine body, soft asymmetric saturation for the low-mid, a
// low-pass, a long hold, a pitch punch at the attack), but computed per note: the punch, the hold
// and the filter stay the same on every note. The WAVs change speed with the pitch, so an octave
// down the punch and the decay stretch 2x and the harmonics fall ("it farts"), an octave up they
// shrink 2x ("almost a pluck") - maintainer, 2026-10-28.
struct Synth808Params
{
    const char* name = "ATL 1";
    float punchSemitones = 3.0f; // pitch at the attack, falls back to the note
    float punchMs = 12.0f;       // time constant of that fall
    float drive = 2.8f;          // saturation
    float bias = 0.12f;          // asymmetry: even harmonics (2nd, 4th)
    float saturation = 0.60f;    // saturated / clean blend
    float cutoffHz = 420.0f;     // low-pass at C2
    float keyTrack = 1.0f;       // 1: the cutoff follows the note (same tone on every note)
    float holdSeconds = 0.2f;    // plateau before the decay
    float decaySeconds = 0.35f;  // then -6 dB per this long
    float attackMs = 3.0f;
    float clickDb = -90.0f;      // kick-attack layer (a short filtered noise burst), against the body
    float boostDb = 0.0f;        // the attack louder than the body, falling with the punch (808 kick)
};

class Synth808Voice
{
public:
    static constexpr int kPresetCount = 10;
    static constexpr double kC2Hz = 65.40639132514966;

    // ATL 1-3, Dark 1-3, Boom, Round, Clean, Knock: fitted to 672 pro 808 one-shots
    // (tools/bass808_corpus.py, docs/audit/BASS808_CORPUS.md).
    static const std::array<Synth808Params, kPresetCount>& presets();
    // "ATL 1" ... "Dark 3" (also "HPDG Sub808 ATL 1 - C"), -1 when it is none of them.
    static int presetIndexForName(const juce::String& name);
    // Output gain that puts a C2 note's peak at 0.8, like the normalized WAVs.
    static float presetGain(int presetIndex);
    // The same aim for any settings (the HPDG 808 plugin's knobs), from the saturation curve alone.
    static float autoGain(const Synth808Params& settings);

    void prepare(double sampleRate);
    void setParams(const Synth808Params& newParams, float outputGain);

    void noteOn(double frequencyHz, float velocity);
    // Legato / glide: the note goes on (no new attack); glideSamples 0 jumps.
    void glideTo(double frequencyHz, int glideSamples);
    void noteOff(int fadeSamples);
    bool isActive() const noexcept { return active; }

    float renderSample();
    void renderAdd(float* output, int numSamples);

private:
    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;
        void setLowPass(double sampleRate, double frequency, double q);
        void setHighPass(double sampleRate, double frequency, double q);
        double process(double x);
    };

    void updateFilter();

    Synth808Params params;
    double sampleRate = 48000.0;
    float gain = 1.0f;
    float velocity = 1.0f;
    bool active = false;

    double baseHz = kC2Hz;
    double semitones = 0.0;       // the note against baseHz (glides move it)
    double targetSemitones = 0.0;
    double glideStep = 0.0;
    int glideRemaining = 0;

    double phase = 0.0;
    double amp = 1.0;
    int64_t holdSamples = 0;
    double click = 0.0;
    double clickDecay = 0.0;
    uint32_t noise = 0x12345678u;
    std::array<Biquad, 2> clickBand {};
    double ampDecay = 1.0;
    double punch = 0.0;
    double punchDecay = 1.0;
    double boost = 0.0;
    double boostDecay = 1.0;
    double tanhBias = 0.0;
    double tanhDrive = 1.0;
    int64_t elapsed = 0;
    int attackSamples = 1;
    int64_t maxSamples = 0;       // like the 8 s WAVs: a 0.3 s release at the end
    int releaseSamples = 1;

    int fadeRemaining = 0;
    int fadeTotal = 0;

    std::array<Biquad, 4> lowPass {}; // two 4th-order Butterworths: the WAVs' zero-phase 4th order
    double dcIn = 0.0, dcOut = 0.0, dcCoeff = 0.9995;
};
} // namespace bbg
