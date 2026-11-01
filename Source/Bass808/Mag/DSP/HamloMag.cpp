#include "DSP/HamloMag.h"

#include <algorithm>
#include <cmath>

#include <juce_core/juce_core.h>

namespace hamlo::dsp
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

// HamloLaw signatures k_n (n = 2..7) and knees E_m per model.
constexpr std::array<std::array<double, 6>, mag::NumModels> kSignature { {
    { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 },           // Classic (unused)
    { 0.02, 0.10, 0.01, 0.03, 0.005, 0.01 },    // Tape: 3rd, a little 5th
    { 0.12, 0.03, 0.02, 0.008, 0.005, 0.002 },  // Tube: strong 2nd
    { 0.02, 0.09, 0.01, 0.025, 0.004, 0.008 },  // Iron: 3rd
    { 0.03, 0.10, 0.02, 0.06, 0.01, 0.03 },     // Germanium: 3rd, 5th, 7th
    { 0.06, 0.05, 0.01, 0.01, 0.003, 0.003 },   // Vinyl: 2nd and 3rd
} };
constexpr std::array<double, mag::NumModels> kKnee { 0.5, 0.6, 0.5, 0.7, 0.4, 0.8 };
constexpr std::array<double, 9> kHum { 1.0, 0.6, 0.5, 0.3, 0.25, 0.15, 0.12, 0.08, 0.06 }; // 50 Hz series

struct Coefficients
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
};

// RBJ cookbook. kind: 0 high-pass, 1 low-pass, 2 peaking, 3 low shelf (S = 1), 4 high shelf (S = 1).
Coefficients rbj(int kind, double hz, double q, double gainDb, double fs) noexcept
{
    const double w = 2.0 * kPi * std::clamp(hz, 5.0, 0.45 * fs) / fs;
    const double cw = std::cos(w), sw = std::sin(w);
    const double A = std::pow(10.0, gainDb / 40.0);
    double b0, b1, b2, a0, a1, a2;
    if (kind <= 1)
    {
        const double alpha = sw / (2.0 * q);
        b0 = kind == 0 ? (1.0 + cw) * 0.5 : (1.0 - cw) * 0.5;
        b1 = kind == 0 ? -(1.0 + cw) : 1.0 - cw;
        b2 = b0;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cw;
        a2 = 1.0 - alpha;
    }
    else if (kind == 2)
    {
        const double alpha = sw / (2.0 * q);
        b0 = 1.0 + alpha * A;
        b1 = -2.0 * cw;
        b2 = 1.0 - alpha * A;
        a0 = 1.0 + alpha / A;
        a1 = -2.0 * cw;
        a2 = 1.0 - alpha / A;
    }
    else
    {
        const double alpha = sw * 0.7071067811865476, sA = std::sqrt(A), ap = A + 1.0, am = A - 1.0;
        if (kind == 3)
        {
            b0 = A * (ap - am * cw + 2.0 * sA * alpha);
            b1 = 2.0 * A * (am - ap * cw);
            b2 = A * (ap - am * cw - 2.0 * sA * alpha);
            a0 = ap + am * cw + 2.0 * sA * alpha;
            a1 = -2.0 * (am + ap * cw);
            a2 = ap + am * cw - 2.0 * sA * alpha;
        }
        else
        {
            b0 = A * (ap + am * cw + 2.0 * sA * alpha);
            b1 = -2.0 * A * (am + ap * cw);
            b2 = A * (ap + am * cw - 2.0 * sA * alpha);
            a0 = ap - am * cw + 2.0 * sA * alpha;
            a1 = 2.0 * (am - ap * cw);
            a2 = ap - am * cw - 2.0 * sA * alpha;
        }
    }
    return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

int modelOf(const FxSlotParams& p) noexcept
{
    return std::clamp(static_cast<int>(std::lround(p.values[slot::kSatModel])), 0, mag::NumModels - 1);
}

int speedOf(const FxSlotParams& p) noexcept
{
    return std::clamp(static_cast<int>(std::lround(p.values[slot::kSatSpeed])), 0, 2);
}

double tapeWidth(const FxSlotParams& p) noexcept
{
    // under-bias widens the hysteresis (more distortion, as on a real machine)
    return 0.02 + 0.22 * (1.0 - std::clamp(static_cast<double>(p.values[slot::kSatTapeBias]), 0.0, 1.0));
}

double driveOf(const FxSlotParams& p) noexcept
{
    const double db = std::clamp(static_cast<double>(p.values[slot::kDriveDb]), 0.0, 48.0) + (p.values[slot::kSatPunish] >= 0.5f ? 20.0 : 0.0);
    return std::pow(10.0, db / 20.0);
}
} // namespace

namespace mag
{
const char* modelName(int m) noexcept
{
    static constexpr const char* names[] { "Classic", "Tape", "Tube", "Iron", "Germanium", "Vinyl" };
    return names[std::clamp(m, 0, NumModels - 1)];
}

const char* speedName(int m, int s) noexcept
{
    static constexpr const char* tape[] { "19", "9.5", "4.76" };
    static constexpr const char* vinyl[] { "33", "45", "78" };
    return (m == Vinyl ? vinyl : tape)[std::clamp(s, 0, 2)];
}

bool hasWow(int m) noexcept
{
    return m == Tape || m == Vinyl;
}

LawSettings lawSettings(int model, double harmonics, double evenOdd) noexcept
{
    LawSettings s;
    const auto& k = kSignature[static_cast<size_t>(std::clamp(model, 0, NumModels - 1))];
    const double H = 2.0 * std::clamp(harmonics, 0.0, 1.0), b = std::clamp(evenOdd, -1.0, 1.0);
    for (size_t i = 0; i < k.size(); ++i)
    {
        const bool even = (i % 2) == 0; // i = 0 -> n = 2
        s.weights[i] = H * k[i] * std::max(0.0, even ? 1.0 + b : 1.0 - b);
    }
    s.knee = kKnee[static_cast<size_t>(std::clamp(model, 0, NumModels - 1))];
    return s;
}

void Core::prepare(double sampleRate) noexcept
{
    envelopeRelease = std::exp(-1.0 / (0.050 * sampleRate));
    holdSamples = static_cast<int>(std::lround(0.025 * sampleRate));
    ironCoefficient = 1.0 - std::exp(-2.0 * kPi * 150.0 / sampleRate);
    reset();
}

double Core::shape(double u, int model) noexcept
{
    switch (model)
    {
        case Tube: return u > 0.0 ? std::tanh(u) : std::tanh(0.6 * u) / 0.6; // grid conduction squeezes the positive swing
        case Germanium:
        {
            const double p = u > 0.0 ? 4.0 : 3.4, knee = u > 0.0 ? 1.0 : 0.94; // a slight asymmetry: odd-rich, some even
            return u / std::pow(1.0 + std::pow(std::abs(u / knee), p), 1.0 / p);
        }
        case Vinyl: return std::tanh(u + 0.15 * u * u) - std::tanh(0.0);
        default: return std::tanh(u);
    }
}

void Core::follow(double x) noexcept
{
    const double a = std::abs(x);
    if (a >= envelope)
    {
        envelope = a;
        hold = holdSamples;
    }
    else if (hold > 0)
        --hold;
    else
        envelope = std::max(a, envelope * envelopeRelease);
}

static double lawLayer(double x, double envelope, double level, const LawSettings& s) noexcept
{
    if (envelope < 1.0e-9)
        return 0.0;
    const double u = std::clamp(x / envelope, -1.0, 1.0);
    double tPrev = u, t = 2.0 * u * u - 1.0; // T1, T2
    double sum = 0.0, power = level * level;  // level^n, n = 2
    for (size_t i = 0; i < s.weights.size(); ++i)
    {
        sum += s.weights[i] * power * t;
        const double next = 2.0 * u * t - tPrev;
        tPrev = t;
        t = next;
        power *= level;
    }
    return s.knee * sum;
}

double Core::levelFor(const LawSettings& s) noexcept
{
    // tanh(E / E_m) only when E or the knee moved (the envelope is held flat on steady material)
    if (envelope != levelEnvelope || s.knee != levelKnee)
    {
        levelEnvelope = envelope;
        levelKnee = s.knee;
        level = std::tanh(envelope / s.knee);
    }
    return level;
}

double Core::harmonics(double x, const LawSettings& s) noexcept
{
    follow(x);
    return lawLayer(x, envelope, levelFor(s), s);
}

double Core::process(double x, int model, const LawSettings& s, double width) noexcept
{
    follow(x);
    double y;
    if (model == Tape)
    {
        // rate-independent hysteresis (play operator, Prandtl-Ishlinskii): the magnetisation follows the
        // field only after it has moved by more than the loop's half width. The width opens with the
        // level (w E^2 / (E + 0.5)): quiet passages stay linear, as AC bias makes them on a real machine.
        const double half = width * envelope * envelope / (envelope + 0.5);
        play = std::clamp(play, x - half, x + half);
        y = 0.5 * (std::tanh(x) + std::tanh(play));
    }
    else if (model == Iron)
    {
        // core flux follows the low frequencies: they meet the knee three times sooner
        ironLow += ironCoefficient * (x - ironLow);
        y = std::tanh(3.0 * ironLow) / 3.0 + std::tanh(x - ironLow);
    }
    else
        y = shape(x, model);
    return y + lawLayer(x, envelope, levelFor(s), s);
}

void transferCurve(const FxSlotParams& p, std::array<float, 256>& in, std::array<float, 256>& out)
{
    const int model = std::max(1, modelOf(p));
    const auto s = lawSettings(model, p.values[slot::kSatHarmonics], p.values[slot::kSatEvenOdd]);
    const double g = driveOf(p), norm = p.values[slot::kAutoGain] >= 0.5f ? 1.0 / g : 1.0;
    Core c;
    c.prepare(4096.0); // two cycles of 256 points at "16 Hz": the envelope holds the peak
    for (int cycle = 0; cycle < 2; ++cycle)
        for (size_t i = 0; i < in.size(); ++i)
        {
            const double x = std::sin(2.0 * kPi * static_cast<double>(i) / in.size());
            const double y = c.process(g * x, model, s, tapeWidth(p)) * norm;
            in[i] = static_cast<float>(x);
            out[i] = static_cast<float>(y);
        }
}

void harmonicProfile(const FxSlotParams& p, double inputDb, std::array<double, 9>& db)
{
    const int model = std::max(1, modelOf(p));
    const auto s = lawSettings(model, p.values[slot::kSatHarmonics], p.values[slot::kSatEvenOdd]);
    const double g = driveOf(p), norm = p.values[slot::kAutoGain] >= 0.5f ? 1.0 / g : 1.0;
    constexpr int n = 4096, bin = 43; // 503.9 Hz at 48 kHz: nine harmonics stay below Nyquist
    const double amplitude = std::pow(10.0, inputDb / 20.0);
    Core c;
    c.prepare(48000.0);
    std::array<double, 9> re {}, im {};
    for (int warm = 0; warm < 2; ++warm)
        for (int i = 0; i < n; ++i)
        {
            const double phase = 2.0 * kPi * bin * i / n;
            const double y = c.process(g * amplitude * std::sin(phase), model, s, tapeWidth(p)) * norm;
            if (warm == 0)
                continue;
            for (size_t h = 0; h < db.size(); ++h)
            {
                re[h] += y * std::cos(phase * (h + 1));
                im[h] += y * std::sin(phase * (h + 1));
            }
        }
    for (size_t h = 0; h < db.size(); ++h)
        db[h] = 20.0 * std::log10(std::sqrt(re[h] * re[h] + im[h] * im[h]) * 2.0 / n / amplitude + 1.0e-12);
}
} // namespace mag

// Low-pass stages of the tape / vinyl bandwidth: cutoff and count (2nd-order Butterworth each).
static std::pair<double, int> bandwidthOf(int model, int speed, double fs) noexcept
{
    double hz = 20000.0;
    int stages = 0;
    if (model == mag::Tape)
    {
        hz = std::array<double, 3> { 16000.0, 10000.0, 6000.0 }[static_cast<size_t>(speed)];
        stages = 1;
    }
    else if (model == mag::Vinyl)
    {
        hz = std::array<double, 3> { 14000.0, 18000.0, 5000.0 }[static_cast<size_t>(speed)];
        stages = speed == 2 ? 2 : 1;
    }
    return { std::min(hz, 0.45 * fs), stages };
}

int MagProcessor::latencySamples(const FxSlotParams& p, double sampleRate) noexcept
{
    const int model = modelOf(p);
    if (!mag::hasWow(model))
        return 0;
    // + the low-frequency group delay of the bandwidth low-passes (sqrt 2 / w_c per stage), so the dry
    // path lines up with the body of the signal
    const auto [hz, stages] = bandwidthOf(model, speedOf(p), sampleRate);
    const double groupDelay = stages * std::sqrt(2.0) * sampleRate / (2.0 * kPi * hz);
    return static_cast<int>(std::lround(mag::kWowBaseSeconds * sampleRate + groupDelay));
}

void MagProcessor::prepareCores(double coreSampleRate) noexcept
{
    for (auto& ch : channels)
        ch.coreState.prepare(coreSampleRate);
}

void MagProcessor::prepare(double newSampleRate, int oversamplingFactor)
{
    sampleRate = newSampleRate;
    wowSize = static_cast<int>(std::ceil(2.2 * mag::kWowBaseSeconds * sampleRate)) + 8;
    for (size_t c = 0; c < channels.size(); ++c)
    {
        channels[c].wow.assign(static_cast<size_t>(wowSize), 0.0f);
        channels[c].coreState.prepare(sampleRate * oversamplingFactor);
    }
    reset();
}

void MagProcessor::reset() noexcept
{
    std::uint32_t seed = 0x9E3779B9u;
    for (auto& ch : channels)
    {
        std::fill(ch.wow.begin(), ch.wow.end(), 0.0f);
        for (auto* b : { &ch.preEmphasis, &ch.deEmphasis, &ch.bump, &ch.highLoss, &ch.highLoss2, &ch.bandHigh, &ch.bandLow, &ch.bandLow2, &ch.tiltLow, &ch.tiltHigh,
                         &ch.lowCut1, &ch.lowCut2, &ch.highCut1, &ch.highCut2, &ch.thump })
            b->z1 = b->z2 = 0.0;
        ch.coreState.reset();
        ch.dcIn = ch.dcOut = 0.0;
        ch.pink0 = ch.pink1 = ch.pink2 = ch.hissHp = 0.0;
        ch.click = ch.clickDecay = ch.pop = 0.0;
        ch.rng = seed;
        seed = seed * 1664525u + 1013904223u;
    }
    wowWrite = 0;
    wowPhase = flutterPhase = humPhase = 0.0;
    noiseGate = inputEnvelope = 0.0;
    designed = false;
    primed = false;
    meterInDb.store(-120.0f, std::memory_order_relaxed);
    meterOutDb.store(-120.0f, std::memory_order_relaxed);
}

double MagProcessor::nextUniform(Channel& ch) noexcept
{
    ch.rng ^= ch.rng << 13;
    ch.rng ^= ch.rng >> 17;
    ch.rng ^= ch.rng << 5;
    return static_cast<double>(ch.rng) / 4294967296.0;
}

void MagProcessor::beginBlock(const FxSlotParams& p, int oversamplingFactor) noexcept
{
    osFactor = std::max(1, oversamplingFactor);
    model = std::max(1, modelOf(p));
    speed = speedOf(p);
    law = mag::lawSettings(model, p.values[slot::kSatHarmonics], p.values[slot::kSatEvenOdd]);
    width = tapeWidth(p);
    driveTarget = driveOf(p);
    if (!primed)
    {
        drive = postDrive = driveTarget; // no glide from a default on the first block
        primed = true;
    }
    driveStart = drive;
    autoGain = p.values[slot::kAutoGain] >= 0.5f;
    trim = std::pow(10.0, std::clamp(static_cast<double>(p.values[slot::kTrimDb]), -24.0, 24.0) / 20.0);
    tone = std::clamp(static_cast<double>(p.values[slot::kSatTone]), -1.0, 1.0);
    lowCutHz = std::clamp(static_cast<double>(p.values[slot::kSatLowCutHz]), 10.0, 1000.0);
    highCutHz = std::clamp(static_cast<double>(p.values[slot::kSatHighCutHz]), 1000.0, 20000.0);
    thumpOn = p.values[slot::kSatThump] >= 0.5f;
    steep = p.values[slot::kSatSteep] >= 0.5f;
    follow = p.values[slot::kSatNoiseFollow] >= 0.5f;
    const double speedWow = model == mag::Tape ? std::array<double, 3> { 1.0, 1.4, 2.0 }[static_cast<size_t>(speed)] : 1.0;
    wowAmount = std::clamp(static_cast<double>(p.values[slot::kSatWow]), 0.0, 1.0) * speedWow;
    const auto level = [](double knob, double topDb) { return knob <= 0.0 ? 0.0 : std::pow(10.0, (topDb - 60.0 * (1.0 - knob)) / 20.0); };
    hissLevel = level(std::clamp(static_cast<double>(p.values[slot::kSatHiss]), 0.0, 1.0), model == mag::Tape && speed == 2 ? -32.0 : -36.0);
    humLevel = level(std::clamp(static_cast<double>(p.values[slot::kSatHum]), 0.0, 1.0), -40.0);
    const double crackle = std::clamp(static_cast<double>(p.values[slot::kSatCrackle]), 0.0, 1.0);
    crackleRate = 30.0 * crackle * crackle * (model == mag::Vinyl && speed == 2 ? 2.0 : 1.0);
    crackleLevel = crackle <= 0.0 ? 0.0 : 0.25 * std::sqrt(crackle);

    // filter designs only when the settings move (the comparison is on the raw parameters)
    bool same = designed;
    for (int i = 0; same && i < static_cast<int>(p.values.size()); ++i)
        same = p.values[static_cast<size_t>(i)] == designedFor.values[static_cast<size_t>(i)];
    if (!same)
    {
        designedFor = p;
        design();
        designed = true;
    }
}

void MagProcessor::design() noexcept
{
    const double fs = sampleRate;
    // emphasis around the core: the highs reach the knee first (tape: NAB-like; vinyl: RIAA-like, stronger)
    emphasisDb = model == mag::Tape ? std::array<double, 3> { 6.0, 8.0, 10.0 }[static_cast<size_t>(speed)] : model == mag::Vinyl ? 12.0 : 0.0;
    emphasisHz = model == mag::Vinyl ? 2100.0 : 3000.0;
    const auto pre = rbj(4, emphasisHz, 0.707, emphasisDb, fs), deE = rbj(4, emphasisHz, 0.707, -emphasisDb, fs);
    const std::array<double, 3> bumpHz { 60.0, 45.0, 35.0 }, lossHz { 16000.0, 10000.0, 6000.0 };
    const auto bump = rbj(2, model == mag::Tape ? bumpHz[static_cast<size_t>(speed)] : 60.0, 1.0, model == mag::Tape ? 2.5 : 0.0, fs);
    juce::ignoreUnused(lossHz);
    const auto [loss, stages] = bandwidthOf(model, speed, fs);
    shellac = stages == 2;
    const auto highLoss = rbj(1, loss, 0.707, 0.0, fs);
    const auto bandLow = rbj(0, shellac ? 100.0 : 5.0, 0.707, 0.0, fs);
    const auto tiltLow = rbj(3, 1000.0, 0.707, -3.0 * tone, fs), tiltHigh = rbj(4, 1000.0, 0.707, 3.0 * tone, fs);
    const auto lowCut = rbj(0, lowCutHz, steep ? 0.5412 : 0.7071, 0.0, fs), lowCut2 = rbj(0, lowCutHz, 1.3066, 0.0, fs);
    highOpen = highCutHz >= std::min(19900.0, 0.45 * fs);
    const auto highCut = rbj(1, highCutHz, steep ? 0.5412 : 0.7071, 0.0, fs), highCut2 = rbj(1, highCutHz, 1.3066, 0.0, fs);
    const auto thump = rbj(2, lowCutHz * 1.2, 1.2, 4.0, fs);
    const auto set = [](Biquad& b, const Coefficients& c) { b.b0 = c.b0, b.b1 = c.b1, b.b2 = c.b2, b.a1 = c.a1, b.a2 = c.a2; };
    for (auto& ch : channels)
    {
        set(ch.preEmphasis, pre);
        set(ch.deEmphasis, deE);
        set(ch.bump, bump);
        set(ch.highLoss, highLoss);
        set(ch.highLoss2, highLoss);
        set(ch.bandLow, bandLow);
        set(ch.bandLow2, bandLow);
        set(ch.tiltLow, tiltLow);
        set(ch.tiltHigh, tiltHigh);
        set(ch.lowCut1, lowCut);
        set(ch.lowCut2, lowCut2);
        set(ch.highCut1, highCut);
        set(ch.highCut2, highCut2);
        set(ch.thump, thump);
    }
}

void MagProcessor::pre(float* data, int n, int channel) noexcept
{
    auto& ch = channels[static_cast<size_t>(channel)];
    const double smooth = 1.0 - std::exp(-1.0 / (0.02 * sampleRate));
    double g = driveStart;
    for (int i = 0; i < n; ++i)
    {
        g += smooth * (driveTarget - g);
        inPeak = std::max(inPeak, static_cast<double>(std::abs(data[i])));
        double x = g * data[i];
        if (emphasisDb != 0.0)
            x = ch.preEmphasis.process(x);
        data[i] = static_cast<float>(x);
    }
    drive = g; // both channels glide from driveStart the same way
}

void MagProcessor::core(float* data, int n, int channel) noexcept
{
    auto& c = channels[static_cast<size_t>(channel)].coreState;
    for (int i = 0; i < n; ++i)
        data[i] = static_cast<float>(c.process(data[i], model, law, width));
}

void MagProcessor::post(float* const* io, int numChannels, int n) noexcept
{
    const double fs = sampleRate;
    const double base = mag::kWowBaseSeconds * fs;
    const bool wow = mag::hasWow(model);
    const double wowHz = model == mag::Vinyl ? std::array<double, 3> { 100.0 / 3.0 / 60.0, 45.0 / 60.0, 78.0 / 60.0 }[static_cast<size_t>(speed)] : 0.6;
    const double wowDepth = wowAmount * (model == mag::Vinyl ? 2.0e-3 : 1.2e-3) * fs;
    const double flutterDepth = model == mag::Tape ? wowAmount * 0.06e-3 * fs : 0.0;
    const double gateAttack = 1.0 - std::exp(-1.0 / (0.005 * fs)), gateRelease = 1.0 - std::exp(-1.0 / (0.3 * fs));
    const double envRelease = std::exp(-1.0 / (0.05 * fs));
    const double hpHiss = 1.0 - std::exp(-2.0 * kPi * 1000.0 / fs);
    const double driveSmooth = 1.0 - std::exp(-1.0 / (0.02 * fs)); // the same glide as pre(), sample by sample
    const double clickDecayFast = std::exp(-1.0 / (0.0003 * fs)), popDecay = std::exp(-1.0 / (0.004 * fs));
    double outPeak = 0.0;

    for (int i = 0; i < n; ++i)
    {
        postDrive += driveSmooth * (driveTarget - postDrive);
        const double norm = (autoGain ? 1.0 / postDrive : 1.0) * trim;
        // input envelope for FOLLOW (taken after the core: the dry is gone by now, the level is close enough)
        double level = 0.0;
        for (int c = 0; c < numChannels; ++c)
            level = std::max(level, static_cast<double>(std::abs(io[c][i])) / std::max(1.0, postDrive));
        inputEnvelope = std::max(level, inputEnvelope * envRelease);
        const double gateTarget = !follow ? 1.0 : (inputEnvelope > 0.001 ? 1.0 : 0.0); // -60 dBFS
        noiseGate += (gateTarget > noiseGate ? gateAttack : gateRelease) * (gateTarget - noiseGate);

        // shared modulators
        wowPhase += wowHz / fs;
        wowPhase -= std::floor(wowPhase);
        flutterPhase += 7.0 / fs;
        flutterPhase -= std::floor(flutterPhase);
        humPhase += 50.0 / fs;
        humPhase -= std::floor(humPhase);
        double wowSin = 0.0, wowCos = 0.0, flutterSin = 0.0; // the right channel's wow is a quarter turn later
        if (wow)
        {
            wowSin = std::sin(2.0 * kPi * wowPhase);
            wowCos = std::cos(2.0 * kPi * wowPhase);
            flutterSin = flutterDepth > 0.0 ? std::sin(2.0 * kPi * flutterPhase) : 0.0;
        }
        double hum = 0.0;
        if (humLevel > 0.0)
        {
            for (size_t k = 0; k < kHum.size(); ++k)
                hum += kHum[k] * std::sin(2.0 * kPi * humPhase * static_cast<double>(k + 1) + 0.7 * static_cast<double>(k));
            const double r = std::abs(std::sin(2.0 * kPi * humPhase));
            hum = humLevel * (0.4 * hum + 0.3 * std::pow(r, 16.0)); // + rectifier buzz at 100 Hz
        }

        for (int c = 0; c < 2; ++c)
        {
            auto& ch = channels[static_cast<size_t>(c)];
            double x = c < numChannels ? io[c][i] : 0.0;
            if (emphasisDb != 0.0)
                x = ch.deEmphasis.process(x);
            x = ch.bump.process(x);
            if (model == mag::Tape || model == mag::Vinyl)
                x = ch.highLoss.process(x);
            x = ch.bandLow.process(x);
            if (shellac) // 78 rpm: a 4th-order 100 Hz - 5 kHz band
                x = ch.bandLow2.process(ch.highLoss2.process(x));

            if (wow)
            {
                ch.wow[static_cast<size_t>(wowWrite)] = static_cast<float>(x);
                const double mod = wowDepth * (c == 0 ? wowSin : wowCos) + flutterDepth * flutterSin;
                const double d = base + mod;
                const int whole = static_cast<int>(d);
                const double f = d - whole;
                int i0 = wowWrite - whole;
                if (i0 < 0)
                    i0 += wowSize;
                const int i1 = i0 == 0 ? wowSize - 1 : i0 - 1;
                x = ch.wow[static_cast<size_t>(i0)] + f * (ch.wow[static_cast<size_t>(i1)] - ch.wow[static_cast<size_t>(i0)]);
            }

            // Soviet analog noises (in the device, before the output filters)
            double noise = 0.0;
            if (hissLevel > 0.0)
            {
                const double white = 2.0 * nextUniform(ch) - 1.0;
                double hiss;
                if (model == mag::Tape || model == mag::Germanium)
                {
                    ch.hissHp += hpHiss * (white - ch.hissHp);
                    hiss = model == mag::Tape ? white - 0.7 * ch.hissHp : white; // tape hiss leans to the highs
                }
                else
                {
                    ch.pink0 = 0.99765 * ch.pink0 + white * 0.0990460; // Kellet's economy pink
                    ch.pink1 = 0.96300 * ch.pink1 + white * 0.2965164;
                    ch.pink2 = 0.57000 * ch.pink2 + white * 1.0526913;
                    hiss = (ch.pink0 + ch.pink1 + ch.pink2 + white * 0.1848) * 0.3;
                }
                noise += hissLevel * hiss;
            }
            if (crackleRate > 0.0)
            {
                if (nextUniform(ch) < crackleRate / fs)
                {
                    const double r = nextUniform(ch);
                    const double a = crackleLevel * (0.15 + 0.85 * r * r * r) * (nextUniform(ch) < 0.5 ? -1.0 : 1.0);
                    if (nextUniform(ch) < 0.1)
                        ch.pop += a * 0.6;
                    else
                        ch.click += a;
                }
                noise += ch.click * (2.0 * nextUniform(ch) - 1.0) + ch.pop;
                ch.click *= clickDecayFast;
                ch.pop *= popDecay;
            }
            noise += hum;
            x += noiseGate * noise * (autoGain ? postDrive : 1.0); // output-referred: the drive does not change the noise level

            // tone tilt, cuts, thump
            x = ch.tiltHigh.process(ch.tiltLow.process(x));
            x = ch.lowCut1.process(x);
            if (steep)
                x = ch.lowCut2.process(x);
            if (thumpOn)
                x = ch.thump.process(x);
            if (!highOpen)
            {
                x = ch.highCut1.process(x);
                if (steep)
                    x = ch.highCut2.process(x);
            }
            // DC blocker (10 Hz), output
            const double y = x - ch.dcIn + (1.0 - 2.0 * kPi * 10.0 / fs) * ch.dcOut;
            ch.dcIn = x;
            ch.dcOut = y;
            if (c < numChannels)
            {
                io[c][i] = static_cast<float>(y * norm);
                outPeak = std::max(outPeak, std::abs(y * norm));
            }
        }
        if (++wowWrite == wowSize)
            wowWrite = 0;
    }
    meterInDb.store(static_cast<float>(20.0 * std::log10(inPeak + 1.0e-6)), std::memory_order_relaxed);
    meterOutDb.store(static_cast<float>(20.0 * std::log10(outPeak + 1.0e-6)), std::memory_order_relaxed);
    inPeak = 0.0;
}
} // namespace hamlo::dsp
