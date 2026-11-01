#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

#include "DSP/MagSettings.h"

namespace hamlo::dsp
{
// HamloMag (module pass 4, docs/fx-addon/SATURATOR_DESIGN.md): the saturator behind the Saturator
// module's models 1..5. An insert; the oversampling around the core is the M6 saturator's.
//
//   x -> drive (+ punish) -> model pre-emphasis ---> [oversampled: core + HamloLaw harmonics]
//     -> de-emphasis, head bump, high loss -> wow / flutter (TAPE, VINYL; a 4 ms modulated delay that
//     is reported as latency) -> + noises (hiss, 50 Hz hum, crackle; ALWAYS or FOLLOW the input)
//     -> tone tilt -> low cut (+ thump) / high cut (12 or 24 dB/oct) -> DC block -> auto gain, output
//
// The HamloLaw (§2 of the design note): with E the peak envelope of the driven signal x (held 25 ms), u = x / E and
// l = tanh(E / E_m), the harmonic layer adds  h = E_m * sum_{n=2..7} H k_n l^n T_n(u)  (T_n Chebyshev),
// so a tone of amplitude A gets exactly harmonic n at E_m H k_n tanh(A / E_m)^n: each harmonic is set on
// its own, rises (n-1) dB per dB above the tone below the knee, like a real device, and settles at the
// model's signature E_m H k_n when driven (bounded at any drive).
// Everything runs per sample from a seeded noise generator: renders do not depend on the block size.
namespace mag
{
enum Model { Classic = 0, Tape, Tube, Iron, Germanium, Vinyl, NumModels };

const char* modelName(int) noexcept;          // "Classic", "Tape", "Tube", "Iron", "Germanium", "Vinyl"
const char* speedName(int model, int speed) noexcept; // "19", "9.5", "4.76" (cm/s) or "33", "45", "78" (rpm)
bool hasWow(int model) noexcept;              // TAPE and VINYL run through the wow delay
constexpr double kWowBaseSeconds = 0.004;     // centre of the wow delay = its latency

struct LawSettings
{
    std::array<double, 6> weights {}; // H k_n for n = 2..7 (even / odd balance applied)
    double knee = 0.5;                // E_m
};
LawSettings lawSettings(int model, double harmonics, double evenOdd) noexcept;

// Model core + HamloLaw harmonics at one sample rate (the oversampled one in the processor).
struct Core
{
    double envelope = 0.0, envelopeRelease = 0.999; // peak: instant attack, 25 ms hold, 50 ms release
    int hold = 0, holdSamples = 1;
    double play = 0.0;      // tape hysteresis (play operator)
    double ironLow = 0.0, ironCoefficient = 0.0;
    void prepare(double sampleRate) noexcept;
    void reset() noexcept
    {
        envelope = play = ironLow = 0.0;
        hold = 0;
        levelEnvelope = levelKnee = -1.0;
    }
    // x: the driven signal. width: tape hysteresis width.
    double process(double x, int model, const LawSettings&, double width) noexcept;
    double harmonics(double x, const LawSettings&) noexcept; // the HamloLaw layer alone (updates the envelope)
    void follow(double x) noexcept;                          // the envelope step (process() and harmonics() call it)
    double levelFor(const LawSettings&) noexcept;            // tanh(E / E_m), cached
    double level = 0.0, levelEnvelope = -1.0, levelKnee = -1.0;
    static double shape(double u, int model) noexcept;       // memoryless part of the core
};

// For the display: the transfer at the current drive (two sine cycles, the second one returned, so
// the tape loop shows) and the harmonic profile (dB re the input tone, H1 … H9) of the core + law for
// a tone at `inputDb` dBFS.
void transferCurve(const FxSlotParams&, std::array<float, 256>& in, std::array<float, 256>& out);
void harmonicProfile(const FxSlotParams&, double inputDb, std::array<double, 9>& db);
} // namespace mag

class MagProcessor
{
public:
    void prepare(double sampleRate, int oversamplingFactor);
    void prepareCores(double coreSampleRate) noexcept; // after an oversampling change (no allocation)
    void reset() noexcept;

    // Base-rate stages around the oversampled core. `channel` 0 / 1.
    void beginBlock(const FxSlotParams&, int oversamplingFactor) noexcept;
    void pre(float* data, int n, int channel) noexcept;          // drive, pre-emphasis; input meter
    void core(float* data, int n, int channel) noexcept;         // at the oversampled rate
    void post(float* const* channels, int numChannels, int n) noexcept; // everything after the core

    float inputLevelDb() const noexcept { return meterInDb.load(std::memory_order_relaxed); }
    float outputLevelDb() const noexcept { return meterOutDb.load(std::memory_order_relaxed); }

    // The wow delay plus the low-frequency group delay of the tape / vinyl bandwidth low-passes, rounded
    // (oversampling excluded).
    static int latencySamples(const FxSlotParams&, double sampleRate) noexcept;

private:
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        double process(double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void setFrom(const Biquad& c) noexcept { b0 = c.b0, b1 = c.b1, b2 = c.b2, a1 = c.a1, a2 = c.a2; }
    };
    struct Channel
    {
        Biquad preEmphasis, deEmphasis, bump, highLoss, highLoss2, bandHigh, bandLow, bandLow2;
        Biquad tiltLow, tiltHigh, lowCut1, lowCut2, highCut1, highCut2, thump;
        mag::Core coreState;
        std::vector<float> wow;
        double dcIn = 0.0, dcOut = 0.0;
        double pink0 = 0.0, pink1 = 0.0, pink2 = 0.0, hissHp = 0.0;
        double click = 0.0, clickDecay = 0.0, pop = 0.0;
        std::uint32_t rng = 1u;
    };

    double nextUniform(Channel&) noexcept; // [0, 1)
    void design() noexcept;

    double sampleRate = 48000.0;
    std::array<Channel, 2> channels;
    int wowSize = 0, wowWrite = 0;

    // per block
    int model = mag::Tape, speed = 0, osFactor = 1;
    mag::LawSettings law;
    double driveTarget = 1.0, drive = 1.0, driveStart = 1.0, postDrive = 1.0, trim = 1.0, width = 0.1;
    bool primed = false;
    bool shellac = false; // VINYL at 78 rpm: a 4th-order band
    bool autoGain = true, steep = false, thumpOn = false, follow = true, highOpen = true;
    double tone = 0.0, lowCutHz = 20.0, highCutHz = 20000.0;
    double wowAmount = 0.0, hissLevel = 0.0, humLevel = 0.0, crackleRate = 0.0, crackleLevel = 0.0;
    double emphasisDb = 0.0, emphasisHz = 3000.0;
    FxSlotParams designedFor {};
    bool designed = false;

    // running state shared by both channels
    double wowPhase = 0.0, flutterPhase = 0.0, humPhase = 0.0;
    double noiseGate = 0.0, inputEnvelope = 0.0;
    double inPeak = 0.0;
    std::atomic<float> meterInDb { -120.0f }, meterOutDb { -120.0f };
};
} // namespace hamlo::dsp
