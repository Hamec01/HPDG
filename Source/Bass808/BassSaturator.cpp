#include "BassSaturator.h"

#include <algorithm>
#include <cmath>

namespace bbg
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

// HamloMag's harmonic signatures k_n (n = 2..7) and knees per model (index = model - 1)
constexpr std::array<std::array<double, 6>, 6> kSignature { {
    { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 },           // Classic: the curve alone
    { 0.02, 0.10, 0.01, 0.03, 0.005, 0.01 },    // Tape: 3rd, a little 5th
    { 0.12, 0.03, 0.02, 0.008, 0.005, 0.002 },  // Tube: strong 2nd
    { 0.02, 0.09, 0.01, 0.025, 0.004, 0.008 },  // Iron: 3rd
    { 0.03, 0.10, 0.02, 0.06, 0.01, 0.03 },     // Germanium: 3rd, 5th, 7th
    { 0.06, 0.05, 0.01, 0.01, 0.003, 0.003 },   // Vinyl: 2nd and 3rd
} };
constexpr std::array<double, 6> kKnee { 0.5, 0.6, 0.5, 0.7, 0.4, 0.8 };
constexpr double kTapeWidth = 0.13; // HamloMag's hysteresis width at its default bias

double driveGain(float driveDb) noexcept
{
    return std::pow(10.0, std::clamp(static_cast<double>(driveDb), 0.0, 48.0) / 20.0);
}
} // namespace

void BassSaturator::prepare(double newSampleRate) noexcept
{
    sampleRate = newSampleRate > 1000.0 ? newSampleRate : 48000.0;
    envelopeRelease = std::exp(-1.0 / (0.050 * sampleRate));
    holdSamples = static_cast<int>(std::lround(0.025 * sampleRate));
    ironCoefficient = 1.0 - std::exp(-2.0 * kPi * 150.0 / sampleRate);
    dcCoefficient = 1.0 - 2.0 * kPi * 10.0 / sampleRate;
    fadeSamples = std::max(1, static_cast<int>(std::lround(0.010 * sampleRate)));
    reset();
}

void BassSaturator::reset() noexcept
{
    state = {};
    drive = 1.0;
    dcIn = dcOut = 0.0;
    activeModel = fadeFrom = 0;
    fadeLeft = 0;
}

void BassSaturator::advance(double u, State& s) const noexcept
{
    // peak envelope: held 25 ms, then 50 ms release (HamloMag's follower)
    const double a = std::abs(u);
    if (a >= s.envelope)
    {
        s.envelope = a;
        s.hold = holdSamples;
    }
    else if (s.hold > 0)
        --s.hold;
    else
        s.envelope = std::max(a, s.envelope * envelopeRelease);
    // Tape's hysteresis and Iron's low band run whatever the model, so switching to them starts warm
    const double half = kTapeWidth * s.envelope * s.envelope / (s.envelope + 0.5);
    s.play = std::clamp(s.play, u - half, u + half);
    s.ironLow += ironCoefficient * (u - s.ironLow);
}

double BassSaturator::shape(double u, int model, double harmonics, double evenOdd, const State& s) const noexcept
{
    double y;
    switch (model)
    {
        case 0: return u;
        case 2: y = 0.5 * (std::tanh(u) + std::tanh(s.play)); break;                     // Tape: hysteresis
        case 3: y = u > 0.0 ? std::tanh(u) : std::tanh(0.6 * u) / 0.6; break;            // Tube: squeezed positive swing
        case 4: y = std::tanh(3.0 * s.ironLow) / 3.0 + std::tanh(u - s.ironLow); break;  // Iron: the lows saturate first
        case 5:                                                                           // Germanium: odd-rich, a little even
        {
            const double p = u > 0.0 ? 4.0 : 3.4, knee = u > 0.0 ? 1.0 : 0.94;
            y = u / std::pow(1.0 + std::pow(std::abs(u / knee), p), 1.0 / p);
            break;
        }
        case 6: y = std::tanh(u + 0.15 * u * u); break;                                  // Vinyl: 2nd and 3rd
        default: y = std::tanh(u); break;                                                 // Classic
    }

    // the model's harmonic signature: Chebyshev T2..T7 of the waveform normalised by its envelope, growing
    // with the level (tanh of envelope / knee)
    const auto index = static_cast<size_t>(std::clamp(model, 1, 6) - 1);
    if (s.envelope < 1.0e-9)
        return y;
    const double x = std::clamp(u / s.envelope, -1.0, 1.0);
    const double level = std::tanh(s.envelope / kKnee[index]);
    const double H = 2.0 * std::clamp(harmonics, 0.0, 1.0), b = std::clamp(evenOdd, -1.0, 1.0);
    double previous = x, t = 2.0 * x * x - 1.0, power = level * level, sum = 0.0;
    for (size_t i = 0; i < kSignature[index].size(); ++i)
    {
        const bool even = (i % 2) == 0; // i = 0: T2
        sum += H * kSignature[index][i] * std::max(0.0, even ? 1.0 + b : 1.0 - b) * power * t;
        const double next = 2.0 * x * t - previous;
        previous = t;
        t = next;
        power *= level;
    }
    return y + kKnee[index] * sum;
}

void BassSaturator::process(float* data, int numSamples, const Params& p) noexcept
{
    const int model = std::clamp(p.model, 0, 6);
    if (model != activeModel)
    {
        // crossfade from what is heard now (mid-fade: from the model faded to)
        fadeFrom = activeModel;
        activeModel = model;
        fadeLeft = fadeSamples;
    }
    if (activeModel == 0 && fadeLeft == 0 && std::abs(dcOut) < 1.0e-9 && std::abs(dcIn) < 1.0e-9)
    {
        state.envelope = 0.0;
        drive = 1.0;
        return; // off and settled: the input untouched
    }

    const double target = model == 0 ? 1.0 : driveGain(p.driveDb);
    const double glide = 1.0 - std::exp(-1.0 / (0.02 * sampleRate));
    const double mix = std::clamp(static_cast<double>(p.mix), 0.0, 1.0);
    for (int i = 0; i < numSamples; ++i)
    {
        drive += glide * (target - drive);
        const double x = data[i];
        const double u = drive * x;
        advance(u, state);
        double y = shape(u, activeModel, p.harmonics, p.evenOdd, state);
        if (fadeLeft > 0)
        {
            const double t = static_cast<double>(fadeLeft) / fadeSamples; // 1 -> 0
            y = t * shape(u, fadeFrom, p.harmonics, p.evenOdd, state) + (1.0 - t) * y;
            --fadeLeft;
        }
        // the change the saturator makes, auto-gained and without DC (the curves are not symmetric);
        // off, the change is zero and the input passes exactly
        const double change = y / drive - x;
        const double blocked = change - dcIn + dcCoefficient * dcOut;
        dcIn = change;
        dcOut = blocked;
        data[i] = static_cast<float>(x + mix * blocked);
    }
}

void BassSaturator::transferCurve(const Params& p, std::array<float, 256>& in, std::array<float, 256>& out)
{
    BassSaturator s;
    s.prepare(4096.0); // two cycles of 256 points at 16 Hz: the envelope holds the peak
    const int model = std::max(1, p.model);
    const double g = driveGain(p.driveDb);
    for (int cycle = 0; cycle < 2; ++cycle)
        for (size_t i = 0; i < in.size(); ++i)
        {
            const double x = std::sin(2.0 * kPi * static_cast<double>(i) / static_cast<double>(in.size()));
            s.advance(g * x, s.state);
            in[i] = static_cast<float>(x);
            out[i] = static_cast<float>(s.shape(g * x, model, p.harmonics, p.evenOdd, s.state) / g);
        }
}

void BassSaturator::harmonicProfile(const Params& p, double inputDb, std::array<double, 9>& db)
{
    BassSaturator s;
    s.prepare(48000.0);
    const int model = std::max(1, p.model);
    const double g = driveGain(p.driveDb), amplitude = std::pow(10.0, inputDb / 20.0);
    constexpr int n = 4096, bin = 43; // 503.9 Hz at 48 kHz: nine harmonics stay below Nyquist
    std::array<double, 9> re {}, im {};
    for (int warm = 0; warm < 2; ++warm)
        for (int i = 0; i < n; ++i)
        {
            const double phase = 2.0 * kPi * bin * i / n;
            const double u = g * amplitude * std::sin(phase);
            s.advance(u, s.state);
            const double y = s.shape(u, model, p.harmonics, p.evenOdd, s.state) / g;
            if (warm == 0)
                continue;
            for (size_t h = 0; h < db.size(); ++h)
            {
                re[h] += y * std::cos(phase * static_cast<double>(h + 1));
                im[h] += y * std::sin(phase * static_cast<double>(h + 1));
            }
        }
    for (size_t h = 0; h < db.size(); ++h)
        db[h] = 20.0 * std::log10(std::sqrt(re[h] * re[h] + im[h] * im[h]) * 2.0 / n / amplitude + 1.0e-12);
}
} // namespace bbg
