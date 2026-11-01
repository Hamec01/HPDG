#include "BassEngine.h"

#include <algorithm>

#include "Audio/Synth808Voice.h"

namespace bbg
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kC2Hz = Synth808Voice::kC2Hz;
constexpr double kMaxSeconds = 8.0;      // without Sustain a note ends like the 8 s WAVs did
constexpr double kAutoRelease = 0.30;

// Male vowel formants F1-F3, Hz (Peterson & Barney): A, E, I, O, U.
constexpr std::array<std::array<double, 3>, 5> kVowels { {
    { 730.0, 1090.0, 2440.0 },
    { 530.0, 1840.0, 2480.0 },
    { 270.0, 2290.0, 3010.0 },
    { 570.0, 840.0, 2410.0 },
    { 300.0, 870.0, 2240.0 },
} };
constexpr std::array<double, 3> kFormantWeights { 1.0, 0.6, 0.3 };

double dbToGain(double db) { return std::pow(10.0, db / 20.0); }

double polyBlep(double t, double dt) noexcept
{
    if (t < dt)
    {
        t /= dt;
        return t + t - t * t - 1.0;
    }
    if (t > 1.0 - dt)
    {
        t = (t - 1.0) / dt;
        return t * t + t + t + 1.0;
    }
    return 0.0;
}

double saw(double t, double dt) noexcept { return 2.0 * t - 1.0 - polyBlep(t, dt); }

double wrap(double t) noexcept { return t - std::floor(t); }

// Synth layer gain: the peak of the 808 saturation for the oscillator's peak (1 + Sub, raised by Boost), both
// half-waves, put at 0.8 (as the pre-rendered WAVs were normalised).
float synthGainFor(const BassSettings& s)
{
    const double drive = juce::jmax(0.1f, s.drive);
    const double amplitude = (1.0 + juce::jlimit(0.0f, 1.0f, s.sub)) * std::pow(10.0, s.boostDb / 20.0);
    const double offset = std::tanh(drive * s.bias);
    const double up = std::tanh(drive * (amplitude + s.bias)) - offset;
    const double down = offset - std::tanh(drive * (s.bias - amplitude));
    const double peak = (1.0 - s.saturation) * amplitude + s.saturation * std::max(up, down) / std::tanh(drive);
    return static_cast<float>(0.8 / juce::jmax(0.05, peak));
}
} // namespace

// ------------------------------------------------------------------------------------------ presets
const std::vector<BassPreset>& bassPresets()
{
    static const std::vector<BassPreset> table = []
    {
        std::vector<BassPreset> list;
        // 808: the ten presets fitted to 672 pro one-shots, as they are in Synth808Voice
        for (const auto& p : Synth808Voice::presets())
        {
            BassSettings s;
            s.punchSemitones = p.punchSemitones;
            s.punchMs = p.punchMs;
            s.drive = p.drive;
            s.bias = p.bias;
            s.saturation = p.saturation;
            s.toneHz = p.cutoffHz;
            s.holdSeconds = p.holdSeconds;
            s.decaySeconds = p.decaySeconds;
            s.attackMs = p.attackMs;
            s.clickDb = p.clickDb;
            s.boostDb = p.boostDb;
            list.push_back({ "808", p.name, s });
        }
        const auto add = [&list](const char* genre, const char* name, auto&& edit)
        {
            BassSettings s;
            edit(s);
            list.push_back({ genre, name, s });
        };
        // Boom Bap (278 one-shots): even-rich (h2 -9 dB, h3 -17), short (-6 dB at 0.30 s, -24 at 0.71),
        // the tone closes after the attack (centroid 1.74 -> 1.33 x f0), a plucked attack (click -29 dB).
        add("Boom Bap", "Upright", [](BassSettings& s) {
            s.wave = BassSettings::Triangle; s.punchSemitones = 3.0f; s.punchMs = 25.0f;
            s.drive = 3.0f; s.bias = 0.3f; s.saturation = 0.85f; s.toneHz = 2500.0f; s.clickDb = -26.0f;
            s.attackMs = 3.0f; s.holdSeconds = 0.06f; s.decaySeconds = 0.16f;
            s.filterType = BassSettings::LP12; s.cutoffHz = 450.0f; s.filterKey = 1.0f; s.filterEnvOct = 2.5f; s.filterDecayMs = 160.0f;
            s.satModel = 3; s.satDriveDb = 14.0f; s.satHarmonics = 0.9f; s.satEvenOdd = 0.8f; s.gainDb = 4.0f;
        });
        add("Boom Bap", "Dusty", [](BassSettings& s) {
            s.wave = BassSettings::Sine; s.shape = 0.2f; s.punchSemitones = 2.0f; s.punchMs = 30.0f;
            s.drive = 2.5f; s.bias = 0.2f; s.saturation = 0.7f; s.toneHz = 1500.0f; s.clickDb = -28.0f;
            s.attackMs = 4.0f; s.holdSeconds = 0.08f; s.decaySeconds = 0.18f;
            s.filterType = BassSettings::LP24; s.cutoffHz = 1000.0f; s.filterKey = 1.0f; s.filterEnvOct = 2.0f; s.filterDecayMs = 200.0f;
            s.satModel = 2; s.satDriveDb = 10.0f; s.satHarmonics = 0.6f; s.satEvenOdd = 0.2f; s.gainDb = 2.5f;
        });
        add("Boom Bap", "Stab", [](BassSettings& s) {
            s.wave = BassSettings::Saw; s.punchSemitones = 0.0f; s.drive = 1.5f; s.bias = 0.1f; s.saturation = 0.3f;
            s.toneHz = 16000.0f; s.clickDb = -60.0f; s.attackMs = 2.0f; s.holdSeconds = 0.03f; s.decaySeconds = 0.12f;
            s.filterType = BassSettings::LP24; s.cutoffHz = 200.0f; s.filterKey = 1.0f; s.resonance = 0.2f;
            s.filterEnvOct = 2.5f; s.filterDecayMs = 160.0f;
            s.satModel = 3; s.satDriveDb = 5.0f; s.satHarmonics = 0.5f; s.satEvenOdd = 0.4f; s.gainDb = -2.0f;
        });
        // Techno (100 one-shots): very short (-6 dB at 0.16 s, -24 at 0.33), a kick-like click (-22 dB),
        // h2 -20 / h3 -22, the tone closes fast (centroid 1.58 -> 1.17 x f0).
        add("Techno", "Rumble", [](BassSettings& s) {
            s.wave = BassSettings::Sine; s.shape = 0.15f; s.punchSemitones = 8.0f; s.punchMs = 25.0f;
            s.drive = 3.0f; s.bias = 0.05f; s.saturation = 0.6f; s.toneHz = 600.0f; s.clickDb = -34.0f;
            s.attackMs = 1.0f; s.holdSeconds = 0.04f; s.decaySeconds = 0.065f;
            s.satModel = 4; s.satDriveDb = 10.0f; s.satHarmonics = 0.5f; s.gainDb = 6.0f;
        });
        add("Techno", "Acid", [](BassSettings& s) {
            s.wave = BassSettings::Saw; s.punchSemitones = 0.0f; s.drive = 1.2f; s.bias = 0.0f; s.saturation = 0.2f;
            s.toneHz = 16000.0f; s.clickDb = -60.0f; s.attackMs = 1.0f; s.holdSeconds = 0.0f; s.decaySeconds = 0.15f;
            s.sustain = 0.35f; s.releaseMs = 40.0f;
            s.filterType = BassSettings::LP24; s.cutoffHz = 200.0f; s.filterKey = 0.5f; s.resonance = 0.8f;
            s.filterEnvOct = 3.0f; s.filterDecayMs = 160.0f;
            s.satModel = 5; s.satDriveDb = 6.0f; s.mode = BassSettings::Glide; s.glideMs = 70.0f; s.gainDb = -2.5f;
        });
        add("Techno", "Pulse", [](BassSettings& s) {
            s.wave = BassSettings::Square; s.shape = 0.3f; s.punchSemitones = 2.0f; s.punchMs = 15.0f;
            s.drive = 1.5f; s.bias = 0.0f; s.saturation = 0.3f; s.toneHz = 16000.0f; s.clickDb = -36.0f;
            s.attackMs = 1.0f; s.holdSeconds = 0.03f; s.decaySeconds = 0.09f;
            s.filterType = BassSettings::LP12; s.cutoffHz = 240.0f; s.filterKey = 1.0f; s.resonance = 0.3f;
            s.filterEnvOct = 2.0f; s.filterDecayMs = 80.0f;
            s.satModel = 2; s.satDriveDb = 6.0f; s.gainDb = -1.5f;
        });
        // DnB (71 one-shots): low (median f0 41 Hz), rich (6 harmonics within 30 dB, a quarter 10+), bright
        // then darker (centroid 3.8 -> 2.3 x f0), punch 10 st, long (-4 dB at 0.5 s).
        add("DnB", "Sub", [](BassSettings& s) {
            s.wave = BassSettings::Sine; s.punchSemitones = 16.0f; s.punchMs = 35.0f;
            s.drive = 1.5f; s.bias = 0.05f; s.saturation = 0.3f; s.toneHz = 400.0f; s.clickDb = -26.0f;
            s.attackMs = 2.0f; s.holdSeconds = 0.3f; s.decaySeconds = 1.0f; s.sustain = 0.7f; s.releaseMs = 60.0f;
            s.phantom = 0.35f; s.gainDb = -1.5f;
        });
        add("DnB", "Reese", [](BassSettings& s) {
            s.wave = BassSettings::Reese; s.shape = 0.4f; s.sub = 0.5f; s.subOctave = 1; s.punchSemitones = 0.0f;
            s.drive = 1.5f; s.bias = 0.0f; s.saturation = 0.3f; s.toneHz = 16000.0f; s.clickDb = -60.0f;
            s.attackMs = 3.0f; s.holdSeconds = 0.2f; s.decaySeconds = 0.6f; s.sustain = 0.85f; s.releaseMs = 80.0f;
            s.filterType = BassSettings::LP24; s.cutoffHz = 900.0f; s.filterKey = 1.0f; s.resonance = 0.25f;
            s.filterEnvOct = 1.5f; s.filterDecayMs = 400.0f; s.lfoRateHz = 0.25f; s.lfoDepth = 0.6f; s.lfoRetrigger = false;
            s.satModel = 2; s.satDriveDb = 6.0f; s.phantom = 0.2f; s.gainDb = -1.0f;
        });
        add("DnB", "Talk", [](BassSettings& s) {
            s.wave = BassSettings::Saw; s.shape = 0.3f; s.sub = 0.5f; s.subOctave = 1; s.punchSemitones = 0.0f;
            s.drive = 1.5f; s.bias = 0.0f; s.saturation = 0.3f; s.toneHz = 16000.0f; s.clickDb = -60.0f;
            s.attackMs = 3.0f; s.holdSeconds = 0.2f; s.decaySeconds = 0.6f; s.sustain = 0.9f; s.releaseMs = 60.0f;
            s.filterType = BassSettings::Vowel; s.vowel = 0.0f; s.resonance = 0.5f; s.lfoSync = 4; s.lfoDepth = 2.0f;
            s.satModel = 5; s.satDriveDb = 8.0f; s.gainDb = 4.0f;
        });
        // Kick -> 808: drop a kick on the SAMPLE screen
        add("Kick to 808", "Kick + 808", [](BassSettings& s) {
            s.sampleLevelDb = 0.0f; s.sampleFollow = false; s.handoffMs = 70.0f;
            s.holdSeconds = 0.25f; s.decaySeconds = 0.45f;
        });
        add("Kick to 808", "Kick Stretch", [](BassSettings& s) {
            s.synthLevelDb = -60.0f; s.sampleLevelDb = 0.0f; s.sampleFollow = true; s.sampleLoop = true;
            s.sampleLoopStart = 0.3f; s.holdSeconds = 0.3f; s.decaySeconds = 0.5f; s.clickDb = -60.0f;
            s.satModel = 3; s.satDriveDb = 6.0f;
        });
        return list;
    }();
    return table;
}

// --------------------------------------------------------------------------------------- filters
void BassVoice::Biquad::lowPass(double rate, double hz, double q)
{
    const double w0 = 2.0 * kPi * juce::jlimit(10.0, 0.45 * rate, hz) / rate;
    const double alpha = std::sin(w0) / (2.0 * q), cosw = std::cos(w0), a0 = 1.0 + alpha;
    b0 = (1.0 - cosw) * 0.5 / a0;
    b1 = (1.0 - cosw) / a0;
    b2 = b0;
    a1 = -2.0 * cosw / a0;
    a2 = (1.0 - alpha) / a0;
}

void BassVoice::Biquad::highPass(double rate, double hz, double q)
{
    const double w0 = 2.0 * kPi * juce::jlimit(10.0, 0.45 * rate, hz) / rate;
    const double alpha = std::sin(w0) / (2.0 * q), cosw = std::cos(w0), a0 = 1.0 + alpha;
    b0 = (1.0 + cosw) * 0.5 / a0;
    b1 = -(1.0 + cosw) / a0;
    b2 = b0;
    a1 = -2.0 * cosw / a0;
    a2 = (1.0 - alpha) / a0;
}

void BassVoice::Svf::tick(double v0, double g, double k, double& low, double& band, double& high) noexcept
{
    const double a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1, a3 = g * a2;
    const double v3 = v0 - ic2;
    const double v1 = a1 * ic1 + a2 * v3;
    const double v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0 * v1 - ic1;
    ic2 = 2.0 * v2 - ic2;
    low = v2;
    band = v1;
    high = v0 - k * v1 - v2;
}

// ----------------------------------------------------------------------------------------- voice
void BassVoice::prepare(double newSampleRate)
{
    sampleRate = newSampleRate > 1000.0 ? newSampleRate : 48000.0;
    dcCoefficient = std::exp(-2.0 * kPi * 5.0 / sampleRate);
    phantomRelease = std::exp(-1.0 / (0.03 * sampleRate));
    grainLength = 2 * juce::jmax(16, static_cast<int>(0.0225 * sampleRate)); // 45 ms, even
    active = false;
}

void BassVoice::setSettings(const BassSettings& newSettings, float newSynthGain)
{
    settings = newSettings;
    synthGain = newSynthGain;
    tanhDrive = std::tanh(static_cast<double>(settings.drive));
    tanhBias = std::tanh(static_cast<double>(settings.drive) * settings.bias);
    if (!active)
        return;
    ampDecay = std::exp(-std::log(2.0) / (juce::jmax(0.01f, settings.decaySeconds) * sampleRate));
    holdSamples = static_cast<std::int64_t>(juce::jmax(0.0f, settings.holdSeconds) * sampleRate);
    punchDecay = std::exp(-1.0 / (juce::jmax(0.1f, settings.punchMs) * 0.001 * sampleRate));
    filterEnvDecay = std::exp(std::log(0.1) / (juce::jmax(1.0f, settings.filterDecayMs) * 0.001 * sampleRate));
    updateNoteFilters();
    updateRegion();
}

void BassVoice::updateNoteFilters()
{
    const double hz = noteHz();
    toneOpen = settings.toneHz >= 15000.0f;
    if (!toneOpen)
    {
        static constexpr std::array<double, 4> qs { 0.54119610, 1.30656296, 0.54119610, 1.30656296 };
        const double cutoff = settings.toneHz * (hz / kC2Hz);
        for (size_t i = 0; i < tone.size(); ++i)
            tone[i].lowPass(sampleRate, cutoff, qs[i]);
    }
    if (std::abs(hz - phantomHz) > 0.002 * hz)
    {
        phantomBand.lowPass(sampleRate, 1.5 * hz, 0.707);
        phantomHz = hz;
    }
}

void BassVoice::updateRegion()
{
    if (sample == nullptr)
        return;
    const double n = static_cast<double>(sample->data.size());
    double s0 = juce::jlimit(0.0, 1.0, static_cast<double>(settings.sampleStart)) * n;
    double s1 = juce::jlimit(0.0, 1.0, static_cast<double>(settings.sampleEnd)) * n;
    if (s1 < s0 + 64.0)
    {
        s1 = std::min(n, s0 + 64.0);
        s0 = std::max(0.0, s1 - 64.0);
    }
    regionStart = s0;
    regionEnd = s1;
    // the loop: from the marker to the end, made a whole number of periods of the sample's pitch, so it
    // repeats without a step in the waveform
    const double marker = juce::jlimit(s0, s1 - 32.0, static_cast<double>(settings.sampleLoopStart) * n);
    double length = settings.sampleReverse ? marker - s0 : s1 - marker;
    if (sample->rootHz > 0.0)
    {
        const double period = sample->sampleRate / sample->rootHz;
        if (length > 1.5 * period)
            length = std::max(period, std::round(length / period) * period);
    }
    loopLength = juce::jlimit(2.0, s1 - s0, length);
    loopStart = s1 - loopLength;
    loopFade = juce::jmax(1.0, std::min(0.4 * loopLength, 0.01 * sample->sampleRate));
}

double BassVoice::readRegion(double position) const noexcept
{
    if (sample == nullptr || position < regionStart)
        return 0.0;
    const auto& data = sample->data;
    const int size = static_cast<int>(data.size());
    const auto at = [&](double p)
    {
        if (settings.sampleReverse)
            p = regionStart + regionEnd - 1.0 - p;
        const int i = static_cast<int>(std::floor(p));
        const double f = p - i;
        const auto x = [&](int k) { return static_cast<double>(data[static_cast<size_t>(juce::jlimit(0, size - 1, k))]); };
        const double y0 = x(i - 1), y1 = x(i), y2 = x(i + 1), y3 = x(i + 2);
        const double c1 = 0.5 * (y2 - y0);
        const double c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3;
        const double c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    };
    if (settings.sampleLoop && loopLength >= 2.0)
    {
        if (position >= regionEnd)
            position = loopStart + std::fmod(position - loopStart, loopLength);
        const double fadeFrom = regionEnd - loopFade;
        if (position > fadeFrom)
        {
            const double t = (position - fadeFrom) / loopFade;
            return (1.0 - t) * at(position) + t * at(position - loopLength);
        }
        return at(position);
    }
    if (position >= regionEnd - 1.0)
        return 0.0;
    const double endFade = 0.002 * sample->sampleRate, tail = regionEnd - position;
    return tail < endFade ? at(position) * tail / endFade : at(position);
}

void BassVoice::noteOn(double frequencyHz, float newVelocity, const BassSettings& newSettings, std::shared_ptr<const BassSample> newSample)
{
    sample = std::move(newSample);
    active = false;
    setSettings(newSettings, synthGain);
    baseHz = juce::jmax(1.0, frequencyHz);
    semitones = targetSemitones = 0.0;
    glideRemaining = 0;
    velocity = newVelocity;

    phase = 0.0; // a zero crossing: no click
    phase2 = 0.25;
    phaseSub = 0.0;
    amp = 1.0;
    ampDecay = std::exp(-std::log(2.0) / (juce::jmax(0.01f, settings.decaySeconds) * sampleRate));
    holdSamples = static_cast<std::int64_t>(juce::jmax(0.0f, settings.holdSeconds) * sampleRate);
    punch = settings.punchSemitones;
    punchDecay = std::exp(-1.0 / (juce::jmax(0.1f, settings.punchMs) * 0.001 * sampleRate));
    boost = dbToGain(settings.boostDb) - 1.0;
    boostDecay = std::exp(-1.0 / (juce::jmax(20.0f, settings.punchMs) * 1.5e-3 * sampleRate));
    click = settings.clickDb > -59.5f ? dbToGain(settings.clickDb) : 0.0;
    clickDecay = std::exp(-1.0 / (0.004 * sampleRate)); // ~4 ms: a kick's beater, not a tone
    clickBand[0].highPass(sampleRate, 1500.0, 0.707);
    clickBand[1].lowPass(sampleRate, 6000.0, 0.707);
    for (auto& stage : clickBand)
        stage.clear();
    for (auto& stage : tone)
        stage.clear();
    dcIn = dcOut = 0.0;

    elapsed = 0;
    attackSamples = juce::jmax(1, static_cast<int>(settings.attackMs * 0.001 * sampleRate));
    maxSamples = static_cast<std::int64_t>(kMaxSeconds * sampleRate);
    fadeRemaining = fadeTotal = 0;

    filterEnv = 1.0;
    filterEnvDecay = std::exp(std::log(0.1) / (juce::jmax(1.0f, settings.filterDecayMs) * 0.001 * sampleRate));
    for (auto& f : svf)
        f.clear();
    for (auto& f : formant)
        f.clear();
    vowelLow.lowPass(sampleRate, 150.0, 0.707);
    vowelLow.clear();

    phantomEnvelope = phantomDcIn = phantomDcOut = 0.0;
    phantomHold = 0;
    phantomHz = 0.0;
    phantomBand.clear();

    updateRegion();
    contentPosition = regionStart;
    grains[0] = { regionStart, grainLength / 2, true }; // at the top of its window: the attack is not faded
    grains[1] = { regionStart, 0, true };
    sinceGrain = 0;
    nextGrain = 0;
    sampleAge = 0.0;

    updateNoteFilters();
    active = true;
}

void BassVoice::glideTo(double frequencyHz, int glideSamples)
{
    if (!active)
        return;
    targetSemitones = 12.0 * std::log2(juce::jmax(1.0, frequencyHz) / baseHz);
    fadeRemaining = fadeTotal = 0; // a continuation cancels the previous note's release
    if (glideSamples <= 0)
    {
        semitones = targetSemitones;
        glideRemaining = 0;
        updateNoteFilters();
        return;
    }
    glideRemaining = glideSamples;
    glideStep = (targetSemitones - semitones) / static_cast<double>(glideSamples);
}

void BassVoice::noteOff(int fadeSamples)
{
    if (!active || (fadeTotal > 0 && fadeRemaining <= fadeSamples))
        return;
    fadeTotal = fadeRemaining = juce::jmax(1, fadeSamples);
}

double BassVoice::synthSample(double level) noexcept
{
    const double dt = noteHz() * std::exp2(punch / 12.0) / sampleRate;
    punch *= punchDecay;
    const double shape = juce::jlimit(0.0f, 1.0f, settings.shape);
    double osc = 0.0;
    switch (settings.wave)
    {
        case BassSettings::Triangle:
        {
            phase = wrap(phase + dt);
            const double w = 0.5 - 0.45 * shape;
            osc = phase < w ? -1.0 + 2.0 * phase / w : 1.0 - 2.0 * (phase - w) / (1.0 - w);
            break;
        }
        case BassSettings::Saw:
        {
            phase = wrap(phase + dt);
            osc = saw(phase, dt);
            if (shape > 0.001)
            {
                const double dt2 = dt * std::exp2(shape * 25.0 / 1200.0);
                phase2 = wrap(phase2 + dt2);
                osc = 0.5 * (osc + saw(phase2, dt2));
            }
            break;
        }
        case BassSettings::Square:
        {
            phase = wrap(phase + dt);
            const double width = 0.5 - 0.42 * shape;
            osc = (phase < width ? 1.0 : -1.0) + polyBlep(phase, dt) - polyBlep(wrap(phase - width + 1.0), dt);
            break;
        }
        case BassSettings::Reese:
        {
            // two saws (4 .. 50 cents apart): their beats are the Reese's movement
            const double half = (4.0 + 46.0 * shape) / 2400.0;
            const double dtA = dt * std::exp2(-half), dtB = dt * std::exp2(half);
            phase = wrap(phase + dtA);
            phase2 = wrap(phase2 + dtB);
            osc = 0.5 * (saw(phase, dtA) + saw(phase2, dtB));
            break;
        }
        default:
            phase = wrap(phase + dt);
            osc = std::sin(2.0 * kPi * phase + 2.5 * shape * std::sin(2.0 * kPi * phase)); // FM 1:1: the growl
            break;
    }
    if (settings.sub > 0.001f)
    {
        phaseSub = wrap(phaseSub + (settings.subOctave == 1 ? dt : 0.5 * dt));
        osc += settings.sub * std::sin(2.0 * kPi * phaseSub);
    }

    const double x = level * (1.0 + boost) * osc;
    boost *= boostDecay;
    const double drive = settings.drive;
    const double saturated = (std::tanh(drive * (x + settings.bias)) - tanhBias) / tanhDrive;
    double out = (1.0 - settings.saturation) * x + settings.saturation * saturated;
    if (!toneOpen)
        for (auto& stage : tone)
            out = stage.process(out);
    const double dc = out - dcIn + dcCoefficient * dcOut;
    dcIn = out;
    dcOut = dc;
    return dc * synthGain;
}

double BassVoice::clickSample() noexcept
{
    // the kick-attack layer, added after the filter: a beater is not darkened by the bass's filter
    if (click <= 1.0e-5)
        return 0.0;
    noise = noise * 1664525u + 1013904223u;
    double burst = (static_cast<double>(noise >> 8) / 8388608.0 - 1.0) * click;
    for (auto& stage : clickBand)
        burst = stage.process(burst);
    click *= clickDecay;
    return 4.0 * burst * synthGain; // the band keeps about 1/4 of the noise's level
}

double BassVoice::sampleSample() noexcept
{
    if (sample == nullptr)
        return 0.0;
    const double sourceRatio = sample->sampleRate / sampleRate;
    const double root = sample->rootHz > 0.0 ? sample->rootHz : kC2Hz;
    const double pitch = (settings.sampleFollow ? noteHz() / root : 1.0)
                         * std::exp2((settings.sampleTune + settings.sampleFine / 100.0) / 12.0) * sourceRatio;
    double value = 0.0;
    if (!settings.sampleLockTime && std::abs(settings.sampleStretch - 1.0f) < 0.001f)
    {
        value = readRegion(contentPosition);
        contentPosition += pitch;
    }
    else
    {
        // time and pitch apart: 45 ms grains read at the pitch, started every 22.5 ms from a position that
        // moves at the speed the length asks for (Hann windows at half overlap add up to 1)
        const double speed = (settings.sampleLockTime ? sourceRatio : pitch) / juce::jlimit(0.25f, 4.0f, settings.sampleStretch);
        if (++sinceGrain >= grainLength / 2)
        {
            sinceGrain = 0;
            double start = contentPosition;
            const auto& playing = grains[static_cast<size_t>(nextGrain ^ 1)];
            if (sample->rootHz > 0.0 && playing.on)
            {
                // start a whole number of the sample's periods away from where the playing grain reads: the two
                // overlap in phase (no beating, no lost cycles) - a known-period WSOLA
                const double period = sample->sampleRate / sample->rootHz;
                start = playing.position + std::round((contentPosition - playing.position) / period) * period;
            }
            grains[static_cast<size_t>(nextGrain)] = { start, 0, true };
            nextGrain ^= 1;
        }
        for (auto& grain : grains)
        {
            if (!grain.on)
                continue;
            const double window = 0.5 - 0.5 * std::cos(2.0 * kPi * grain.age / grainLength);
            value += window * readRegion(grain.position);
            grain.position += pitch;
            if (++grain.age >= grainLength)
                grain.on = false;
        }
        contentPosition += speed;
    }
    const double fadeIn = 0.001 * sampleRate; // a start inside the waveform does not click
    if (regionStart > 0.5 && sampleAge < fadeIn)
        value *= sampleAge / fadeIn;
    sampleAge += 1.0;
    return settings.sampleInvert ? -value : value;
}

double BassVoice::filterSample(double x, double lfo) noexcept
{
    const double env = filterEnv;
    filterEnv *= filterEnvDecay;
    const auto type = settings.filterType;
    if (type != BassSettings::Vowel && settings.cutoffHz >= 19999.0f && settings.filterEnvOct == 0.0f && settings.lfoDepth == 0.0f)
        return x;
    double low = 0.0, band = 0.0, high = 0.0;
    if (type == BassSettings::Vowel)
    {
        const double position = juce::jlimit(0.0, 4.0, settings.vowel + settings.filterEnvOct * env + settings.lfoDepth * lfo);
        const int i = std::min(3, static_cast<int>(position));
        const double f = position - i;
        const double k = 1.0 / (5.0 + 15.0 * settings.resonance);
        double y = 0.7 * vowelLow.process(x); // the sub stays under the vowel
        for (size_t n = 0; n < formant.size(); ++n)
        {
            const double hz = kVowels[static_cast<size_t>(i)][n] * (1.0 - f) + kVowels[static_cast<size_t>(i + 1)][n] * f;
            const double g = std::tan(kPi * std::min(hz, 0.45 * sampleRate) / sampleRate);
            formant[n].tick(x, g, k, low, band, high);
            y += 4.0 * kFormantWeights[n] * band * k;
        }
        return y;
    }
    const double cutoff = juce::jlimit(16.0, 0.45 * sampleRate,
                                       settings.cutoffHz * std::exp2(settings.filterKey * std::log2(noteHz() / kC2Hz)
                                                                     + settings.filterEnvOct * env + settings.lfoDepth * lfo));
    const double g = std::tan(kPi * cutoff / sampleRate);
    const double k = 1.41421356 * (1.0 - settings.resonance) + 0.06 * settings.resonance;
    svf[0].tick(x, g, k, low, band, high);
    switch (type)
    {
        case BassSettings::LP24:
        {
            const double first = low;
            svf[1].tick(first, g, 1.41421356, low, band, high);
            return low;
        }
        case BassSettings::HP12: return high;
        case BassSettings::BP: return band * k;
        default: return low;
    }
}

void BassVoice::render(float* out, const float* lfo, int numSamples)
{
    const double synthLevel = settings.synthLevelDb > -59.5f ? dbToGain(settings.synthLevelDb) : 0.0;
    const double sampleLevel = sample != nullptr && settings.sampleLevelDb > -59.5f ? dbToGain(settings.sampleLevelDb) : 0.0;
    const double sustain = juce::jlimit(0.0f, 1.0f, settings.sustain);
    const double handoff = settings.handoffMs * 0.001 * sampleRate;
    const double handoffFade = juce::jmax(0.005 * sampleRate, 0.5 * handoff);
    for (int i = 0; i < numSamples && active; ++i)
    {
        if (glideRemaining > 0)
        {
            semitones = --glideRemaining > 0 ? semitones + glideStep : targetSemitones;
            updateNoteFilters();
        }
        const double level = sustain + (1.0 - sustain) * amp;
        if (elapsed >= holdSamples)
            amp *= ampDecay;

        double toSample = 1.0, toSynth = 1.0;
        if (handoff > 0.0 && sampleLevel > 0.0)
        {
            const double t = static_cast<double>(elapsed) - handoff;
            const double x = juce::jlimit(0.0, 1.0, t / handoffFade);
            toSample = std::cos(0.5 * kPi * x); // equal power
            toSynth = std::sin(0.5 * kPi * x);
        }
        const double synth = synthLevel > 0.0 ? synthSample(level) * synthLevel : 0.0;
        const double played = sampleLevel > 0.0 ? sampleSample() * level * sampleLevel : 0.0;
        const double mix = toSynth * synth + toSample * played;
        double y = filterSample(mix, lfo[i]);
        if (synthLevel > 0.0)
            y += toSynth * synthLevel * clickSample();

        if (settings.phantom > 0.001f)
        {
            // harmonics 2-5 of what is under 1.5 x the note: T_n(x / E) gives exactly harmonic n of a sine,
            // E (the peak envelope) keeps the level following the note
            const double lowBand = phantomBand.process(mix);
            const double magnitude = std::abs(lowBand);
            if (magnitude > phantomEnvelope)
            {
                phantomEnvelope = magnitude;
                phantomHold = static_cast<int>(1.1 * sampleRate / noteHz()); // a period: x / E stays the sine itself
            }
            else if (phantomHold > 0)
                --phantomHold;
            else
                phantomEnvelope *= phantomRelease;
            if (phantomEnvelope > 1.0e-6)
            {
                const double u = juce::jlimit(-1.0, 1.0, lowBand / phantomEnvelope), u2 = u * u;
                const double t2 = 2.0 * u2 - 1.0, t3 = u * (4.0 * u2 - 3.0);
                const double t4 = 8.0 * u2 * u2 - 8.0 * u2 + 1.0, t5 = u * (16.0 * u2 * u2 - 20.0 * u2 + 5.0);
                const double h = phantomEnvelope * settings.phantom * (0.55 * t2 + 0.45 * t3 + 0.3 * t4 + 0.2 * t5);
                const double dc = h - phantomDcIn + dcCoefficient * phantomDcOut;
                phantomDcIn = h;
                phantomDcOut = dc;
                y += dc;
            }
        }

        if (elapsed < attackSamples)
            y *= 0.5 - 0.5 * std::cos(kPi * static_cast<double>(elapsed) / attackSamples);
        if (sustain <= 0.0)
        {
            if (elapsed == maxSamples - static_cast<std::int64_t>(kAutoRelease * sampleRate))
                noteOff(static_cast<int>(kAutoRelease * sampleRate));
            if (elapsed > holdSamples && amp < 1.0e-5 && fadeTotal == 0)
                active = false;
        }
        ++elapsed;
        if (fadeTotal > 0)
        {
            if (fadeRemaining <= 0)
            {
                active = false;
                break;
            }
            y *= static_cast<double>(fadeRemaining) / fadeTotal;
            --fadeRemaining;
        }
        out[i] += static_cast<float>(y) * velocity;
    }
}

// ---------------------------------------------------------------------------------------- engine
double BassEngine::noteHz(int note, float tuneSemitones)
{
    return 440.0 * std::pow(2.0, (note + tuneSemitones - 69.0) / 12.0);
}

hamlo::dsp::FxSlotParams BassEngine::saturatorParams(const BassSettings& settings) noexcept
{
    namespace slot = hamlo::dsp::slot;
    hamlo::dsp::FxSlotParams p;
    p.active = true;
    auto& v = p.values;
    v[slot::kDriveDb] = settings.satDriveDb;
    v[slot::kOversampling] = 1.0f; // 2x
    v[slot::kAutoGain] = 1.0f;
    v[slot::kSatModel] = static_cast<float>(juce::jlimit(0, 5, settings.satModel - 1));
    v[slot::kSatLowCutHz] = 10.0f; // the saturator's own filters stay out of the bass
    v[slot::kSatHighCutHz] = 20000.0f;
    v[slot::kSatHarmonics] = settings.satHarmonics;
    v[slot::kSatEvenOdd] = settings.satEvenOdd;
    v[slot::kSatTapeBias] = 0.5f;
    v[slot::kSatNoiseFollow] = 1.0f;
    return p;
}

void BassEngine::prepare(double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate > 1000.0 ? newSampleRate : 48000.0;
    chunkSize = juce::jlimit(16, 1024, maxBlockSize);
    for (auto& voice : voices)
        voice.prepare(sampleRate);
    saturator.prepare(sampleRate, chunkSize);
    maxLatency = 0;
    for (int model = 1; model <= 6; ++model)
    {
        auto probe = settings;
        probe.satModel = model;
        maxLatency = std::max(maxLatency, hamlo::dsp::SaturatorProcessor::latencySamples(saturatorParams(probe), sampleRate));
    }
    dryDelay.prepare(maxLatency + 8);
    outputDelay.prepare(maxLatency + 8);
    lfoBuffer.assign(static_cast<size_t>(chunkSize), 0.0f);
    dryBuffer.assign(static_cast<size_t>(chunkSize), 0.0f);
    heldCount = 0;
    sounding = -1;
    lfoPhase = 0.0;
    saturatorWasOn = false;
    setSettings(settings);
}

void BassEngine::setSettings(const BassSettings& newSettings)
{
    settings = newSettings;
    const float synthGain = synthGainFor(settings);
    for (auto& voice : voices)
        voice.setSettings(settings, synthGain);
}

void BassEngine::setSample(std::shared_ptr<const BassSample> newSample)
{
    std::atomic_store(&sample, std::move(newSample));
}

std::shared_ptr<const BassSample> BassEngine::getSample() const
{
    return std::atomic_load(&sample);
}

double BassEngine::lfoHz() const noexcept
{
    static constexpr std::array<double, 9> beats { 0.0, 4.0, 2.0, 1.0, 0.5, 0.25, 2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0 };
    const int sync = juce::jlimit(0, 8, settings.lfoSync);
    return sync == 0 ? static_cast<double>(settings.lfoRateHz) : tempo / 60.0 / beats[static_cast<size_t>(sync)];
}

void BassEngine::startNote(int note, float velocity, bool fromHeldStack)
{
    auto& voice = voices[static_cast<size_t>(currentVoice)];
    const auto hz = noteHz(note, settings.tuneSemitones);
    if (sounding >= 0 && voice.isActive() && settings.mode != BassSettings::Retrigger)
    {
        // Legato / Glide: the note goes on, its pitch moves (an 808 slide)
        voice.glideTo(hz, settings.mode == BassSettings::Glide ? static_cast<int>(settings.glideMs * 0.001 * sampleRate) : 0);
    }
    else if (!fromHeldStack)
    {
        voice.noteOff(static_cast<int>(0.003 * sampleRate)); // mono: the previous note chokes, no click
        currentVoice = 1 - currentVoice;
        const float sensitivity = settings.velocity;
        voices[static_cast<size_t>(currentVoice)].noteOn(hz, 1.0f - sensitivity + sensitivity * velocity, settings, getSample());
        if (settings.lfoRetrigger)
            lfoPhase = 0.0;
    }
    else
    {
        return;
    }
    sounding = note;
}

void BassEngine::releaseNote(int note)
{
    int write = 0;
    for (int i = 0; i < heldCount; ++i)
        if (held[static_cast<size_t>(i)] != note)
            held[static_cast<size_t>(write++)] = held[static_cast<size_t>(i)];
    heldCount = write;
    if (note != sounding)
        return;
    if (heldCount > 0 && settings.mode != BassSettings::Retrigger)
    {
        startNote(held[static_cast<size_t>(heldCount - 1)], lastVelocity, true); // back to the key still held
        return;
    }
    voices[static_cast<size_t>(currentVoice)].noteOff(juce::jmax(1, static_cast<int>(settings.releaseMs * 0.001 * sampleRate)));
    sounding = -1;
}

void BassEngine::noteOn(int note, float velocity)
{
    releaseNote(note); // a repeated key moves to the top of the stack
    if (heldCount == static_cast<int>(held.size()))
    {
        std::move(held.begin() + 1, held.end(), held.begin());
        --heldCount;
    }
    held[static_cast<size_t>(heldCount++)] = note;
    lastVelocity = velocity;
    startNote(note, velocity, false);
}

void BassEngine::noteOff(int note)
{
    releaseNote(note);
}

void BassEngine::allNotesOff()
{
    heldCount = 0;
    sounding = -1;
    for (auto& voice : voices)
        voice.noteOff(static_cast<int>(0.003 * sampleRate));
}

void BassEngine::render(float* mono, int numSamples)
{
    for (int start = 0; start < numSamples; start += chunkSize)
        renderChunk(mono + start, std::min(chunkSize, numSamples - start));
}

void BassEngine::renderChunk(float* out, int n)
{
    std::fill(out, out + n, 0.0f);
    const double step = lfoHz() / sampleRate;
    for (int i = 0; i < n; ++i)
    {
        lfoBuffer[static_cast<size_t>(i)] = static_cast<float>(std::sin(2.0 * kPi * lfoPhase));
        lfoPhase = wrap(lfoPhase + step);
    }
    for (auto& voice : voices)
        voice.render(out, lfoBuffer.data(), n);

    float* channel[] { out };
    const bool saturatorOn = settings.satModel > 0;
    if (saturatorOn)
    {
        if (!saturatorWasOn)
        {
            saturator.reset();
            dryDelay.reset();
        }
        const auto params = saturatorParams(settings);
        std::copy(out, out + n, dryBuffer.begin());
        saturator.process(channel, 1, n, params);
        const int latency = hamlo::dsp::SaturatorProcessor::latencySamples(params, sampleRate);
        float* dry[] { dryBuffer.data() };
        dryDelay.process(dry, dry, 1, n, latency);
        const float wet = juce::jlimit(0.0f, 1.0f, settings.satMix);
        for (int i = 0; i < n; ++i)
            out[i] = wet * out[i] + (1.0f - wet) * dryBuffer[static_cast<size_t>(i)];
        outputDelay.process(channel, channel, 1, n, maxLatency - latency);
    }
    else
    {
        outputDelay.process(channel, channel, 1, n, maxLatency);
    }
    saturatorWasOn = saturatorOn;
    const auto gain = static_cast<float>(dbToGain(settings.gainDb));
    for (int i = 0; i < n; ++i)
        out[i] *= gain;
}

std::vector<float> BassEngine::renderNote(const BassSettings& noteSettings, std::shared_ptr<const BassSample> noteSample, int note,
                                          double seconds, double rate)
{
    BassEngine engine;
    engine.settings = noteSettings;
    engine.prepare(rate, 512);
    engine.setSample(std::move(noteSample));
    engine.noteOn(note, 1.0f);
    const int latency = engine.latencySamples();
    std::vector<float> buffer(static_cast<size_t>(seconds * rate) + static_cast<size_t>(latency), 0.0f);
    engine.render(buffer.data(), static_cast<int>(buffer.size()));
    buffer.erase(buffer.begin(), buffer.begin() + latency);
    return buffer;
}
} // namespace bbg
