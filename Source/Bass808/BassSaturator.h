#pragma once

#include <array>

namespace bbg
{
// HPDG 808's own saturation. Taken from HamloMag (the maintainer's saturator): the six model curves
// (Classic, Tape, Tube, Iron, Germanium, Vinyl) and their harmonic signatures. Left out: HamloMag's
// machine around them (wow, band filters, noise, emphasis, oversampling), so there is no latency and
// nothing to jump when the model changes. A model change (or turning the saturator on / off) crossfades
// the old curve into the new one over 10 ms, so it never clicks.
class BassSaturator
{
public:
    struct Params
    {
        int model = 0;          // 0 off, 1 Classic, 2 Tape, 3 Tube, 4 Iron, 5 Germanium, 6 Vinyl
        float driveDb = 0.0f;   // 0 .. 48
        float harmonics = 0.5f; // 0 .. 1: the model's harmonic signature
        float evenOdd = 0.0f;   // -1 odd .. 1 even
        float mix = 1.0f;
    };

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    // In place, mono. Small signals keep their level whatever the drive (auto gain).
    void process(float* data, int numSamples, const Params&) noexcept;

    // For the display: a steady sine through the curve (the envelope at its peak), and the level of its
    // first nine harmonics (dB re the input) at inputDb.
    static void transferCurve(const Params&, std::array<float, 256>& in, std::array<float, 256>& out);
    static void harmonicProfile(const Params&, double inputDb, std::array<double, 9>& db);

private:
    struct State
    {
        double envelope = 0.0, play = 0.0, ironLow = 0.0;
        int hold = 0;
    };

    // one sample through `model`'s curve + its harmonic layer; the state moves only when advance is true
    double shape(double u, int model, double harmonics, double evenOdd, const State&) const noexcept;
    void advance(double u, State&) const noexcept;

    double sampleRate = 48000.0;
    double envelopeRelease = 0.999, ironCoefficient = 0.02, dcCoefficient = 0.999;
    int holdSamples = 1200, fadeSamples = 480;
    State state;
    double drive = 1.0, dcIn = 0.0, dcOut = 0.0;
    int activeModel = 0, fadeFrom = 0, fadeLeft = 0;
};
} // namespace bbg
