#include "Synth808Voice.h"

#include <cmath>

namespace bbg
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kMaxSeconds = 8.0;
constexpr double kReleaseSeconds = 0.30;
} // namespace

const std::array<Synth808Params, Synth808Voice::kPresetCount>& Synth808Voice::presets()
{
    // name, punch st, punch ms, drive, bias, saturation, cutoff Hz at C2, key track, hold s, decay s,
    // attack ms, click dB, boost dB
    static const std::array<Synth808Params, kPresetCount> table { {
        { "ATL 1", 18.0f, 40.0f, 4.5f, 0.08f, 0.80f, 800.0f, 1.0f, 0.05f, 0.32f, 2.0f, -30.0f, 0.0f },
        { "ATL 2", 20.0f, 36.0f, 6.0f, 0.12f, 0.85f, 1000.0f, 1.0f, 0.10f, 0.26f, 2.0f, -30.0f, 0.0f },
        { "ATL 3", 18.0f, 40.0f, 3.5f, 0.05f, 0.75f, 700.0f, 1.0f, 0.35f, 0.45f, 2.0f, -40.0f, 0.0f },
        { "Dark 1", 20.0f, 40.0f, 3.8f, 0.06f, 0.75f, 500.0f, 1.0f, 0.25f, 0.45f, 3.0f, -46.0f, 0.0f },
        { "Dark 2", 16.0f, 32.0f, 3.2f, 0.00f, 0.80f, 550.0f, 1.0f, 0.40f, 0.45f, 3.0f, -50.0f, 0.0f },
        { "Dark 3", 20.0f, 26.0f, 3.0f, 0.05f, 0.70f, 450.0f, 1.0f, 1.00f, 0.80f, 3.0f, -48.0f, 0.0f },
        { "Boom", 23.0f, 24.0f, 3.2f, 0.07f, 0.70f, 650.0f, 1.0f, 1.20f, 1.00f, 3.0f, -32.0f, 0.0f },
        { "Round", 14.5f, 29.0f, 2.0f, 0.00f, 0.70f, 600.0f, 1.0f, 0.50f, 0.30f, 3.0f, -46.0f, 0.0f },
        { "Clean", 2.5f, 115.0f, 0.8f, 0.15f, 0.15f, 400.0f, 1.0f, 0.10f, 0.45f, 2.0f, -36.0f, 6.0f },
        { "Knock", 20.0f, 40.0f, 6.0f, 0.12f, 0.85f, 1000.0f, 1.0f, 0.12f, 0.28f, 1.5f, -24.0f, 0.0f },
    } };
    return table;
}

int Synth808Voice::presetIndexForName(const juce::String& name)
{
    auto stripped = name.trim();
    if (stripped.startsWithIgnoreCase("HPDG Sub808 "))
        stripped = stripped.substring(12);
    stripped = stripped.upToFirstOccurrenceOf(" - ", false, false).trim();
    for (int i = 0; i < kPresetCount; ++i)
        if (stripped.equalsIgnoreCase(presets()[static_cast<size_t>(i)].name))
            return i;
    return -1;
}

float Synth808Voice::presetGain(int presetIndex)
{
    static const std::array<float, kPresetCount> gains = []
    {
        std::array<float, kPresetCount> result {};
        for (int i = 0; i < kPresetCount; ++i)
        {
            Synth808Voice voice;
            voice.prepare(48000.0);
            voice.setParams(presets()[static_cast<size_t>(i)], 1.0f);
            voice.noteOn(kC2Hz, 1.0f);
            float peak = 1.0e-6f;
            for (int n = 0; n < 48000 / 2; ++n) // the peak is in the attack
                peak = std::max(peak, std::abs(voice.renderSample()));
            result[static_cast<size_t>(i)] = 0.80f / peak;
        }
        return result;
    }();
    return gains[static_cast<size_t>(juce::jlimit(0, kPresetCount - 1, presetIndex))];
}

float Synth808Voice::autoGain(const Synth808Params& settings)
{
    const double drive = juce::jmax(0.1f, settings.drive);
    const double peak = (1.0 - settings.saturation)
        + settings.saturation * (std::tanh(drive * (1.0 + settings.bias)) - std::tanh(drive * settings.bias)) / std::tanh(drive);
    return static_cast<float>(0.80 / juce::jmax(0.05, peak));
}

void Synth808Voice::Biquad::setLowPass(double rate, double frequency, double q)
{
    const double w0 = 2.0 * kPi * frequency / rate;
    const double alpha = std::sin(w0) / (2.0 * q);
    const double cosw = std::cos(w0);
    const double a0 = 1.0 + alpha;
    b0 = (1.0 - cosw) * 0.5 / a0;
    b1 = (1.0 - cosw) / a0;
    b2 = b0;
    a1 = -2.0 * cosw / a0;
    a2 = (1.0 - alpha) / a0;
}

void Synth808Voice::Biquad::setHighPass(double rate, double frequency, double q)
{
    const double w0 = 2.0 * kPi * frequency / rate;
    const double alpha = std::sin(w0) / (2.0 * q);
    const double cosw = std::cos(w0);
    const double a0 = 1.0 + alpha;
    b0 = (1.0 + cosw) * 0.5 / a0;
    b1 = -(1.0 + cosw) / a0;
    b2 = b0;
    a1 = -2.0 * cosw / a0;
    a2 = (1.0 - alpha) / a0;
}

double Synth808Voice::Biquad::process(double x)
{
    const double y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
}

void Synth808Voice::prepare(double newSampleRate)
{
    sampleRate = newSampleRate > 1000.0 ? newSampleRate : 48000.0;
    dcCoeff = std::exp(-2.0 * kPi * 5.0 / sampleRate); // 5 Hz: removes the saturation's offset only
    active = false;
}

void Synth808Voice::setParams(const Synth808Params& newParams, float outputGain)
{
    params = newParams;
    gain = outputGain;
    tanhDrive = std::tanh(static_cast<double>(params.drive));
    tanhBias = std::tanh(static_cast<double>(params.drive) * params.bias);
    if (active) // knobs turned while a note rings: tone and decay follow at once
    {
        ampDecay = std::exp(-std::log(2.0) / (juce::jmax(0.01f, params.decaySeconds) * sampleRate));
        holdSamples = static_cast<int64_t>(juce::jmax(0.0f, params.holdSeconds) * sampleRate);
        punchDecay = std::exp(-1.0 / (juce::jmax(0.1f, params.punchMs) * 0.001 * sampleRate));
        updateFilter();
    }
}

void Synth808Voice::updateFilter()
{
    const double noteHz = baseHz * std::exp2(semitones / 12.0);
    const double cutoff = juce::jlimit(20.0, 0.45 * sampleRate,
                                       params.cutoffHz * std::pow(noteHz / kC2Hz, static_cast<double>(params.keyTrack)));
    static constexpr std::array<double, 4> qs { 0.54119610, 1.30656296, 0.54119610, 1.30656296 };
    for (size_t i = 0; i < lowPass.size(); ++i)
        lowPass[i].setLowPass(sampleRate, cutoff, qs[i]);
}

void Synth808Voice::noteOn(double frequencyHz, float newVelocity)
{
    baseHz = juce::jmax(1.0, frequencyHz);
    semitones = targetSemitones = 0.0;
    glideRemaining = 0;
    velocity = newVelocity;
    phase = 0.0; // starts at a zero crossing: no click
    amp = 1.0;
    ampDecay = std::exp(-std::log(2.0) / (juce::jmax(0.01f, params.decaySeconds) * sampleRate));
    holdSamples = static_cast<int64_t>(juce::jmax(0.0f, params.holdSeconds) * sampleRate);
    punch = params.punchSemitones;
    boost = std::pow(10.0, params.boostDb / 20.0) - 1.0;
    boostDecay = std::exp(-1.0 / (juce::jmax(20.0f, params.punchMs) * 1.5e-3 * sampleRate));
    click = params.clickDb > -89.0f ? std::pow(10.0, params.clickDb / 20.0) : 0.0;
    clickDecay = std::exp(-1.0 / (0.004 * sampleRate)); // ~4 ms: a kick's beater, not a tone
    clickBand[0].setHighPass(sampleRate, 1500.0, 0.707);
    clickBand[1].setLowPass(sampleRate, juce::jmin(6000.0, 0.45 * sampleRate), 0.707);
    for (auto& stage : clickBand)
        stage.z1 = stage.z2 = 0.0;
    punchDecay = std::exp(-1.0 / (juce::jmax(0.1f, params.punchMs) * 0.001 * sampleRate));
    elapsed = 0;
    attackSamples = juce::jmax(1, static_cast<int>(params.attackMs * 0.001 * sampleRate));
    maxSamples = static_cast<int64_t>(kMaxSeconds * sampleRate);
    releaseSamples = static_cast<int>(kReleaseSeconds * sampleRate);
    fadeRemaining = fadeTotal = 0;
    for (auto& stage : lowPass)
        stage.z1 = stage.z2 = 0.0;
    dcIn = dcOut = 0.0;
    updateFilter();
    active = true;
}

void Synth808Voice::glideTo(double frequencyHz, int glideSamples)
{
    if (!active)
        return;
    targetSemitones = 12.0 * std::log2(juce::jmax(1.0, frequencyHz) / baseHz);
    fadeRemaining = fadeTotal = 0; // a continuation cancels the previous note's release
    if (glideSamples <= 0)
    {
        semitones = targetSemitones;
        glideRemaining = 0;
        updateFilter();
        return;
    }
    glideRemaining = glideSamples;
    glideStep = (targetSemitones - semitones) / static_cast<double>(glideSamples);
}

void Synth808Voice::noteOff(int fadeSamples)
{
    if (!active)
        return;
    if (fadeTotal > 0 && fadeRemaining <= fadeSamples)
        return;
    fadeTotal = fadeRemaining = juce::jmax(1, fadeSamples);
}

float Synth808Voice::renderSample()
{
    if (!active)
        return 0.0f;

    if (glideRemaining > 0)
    {
        semitones = --glideRemaining > 0 ? semitones + glideStep : targetSemitones;
        updateFilter();
    }

    const double ratio = std::exp2((semitones + punch) / 12.0);
    phase += baseHz * ratio / sampleRate;
    phase -= std::floor(phase);
    punch *= punchDecay;

    const double x = amp * (1.0 + boost) * std::sin(2.0 * kPi * phase);
    boost *= boostDecay;
    if (elapsed >= holdSamples)
        amp *= ampDecay;
    const double drive = params.drive;
    const double saturated = (std::tanh(drive * (x + params.bias)) - tanhBias) / tanhDrive;
    double out = (1.0 - params.saturation) * x + params.saturation * saturated;
    for (auto& stage : lowPass)
        out = stage.process(out);

    const double dc = out - dcIn + dcCoeff * dcOut;
    dcIn = out;
    dcOut = dc;
    out = dc;

    if (elapsed < attackSamples)
        out *= 0.5 - 0.5 * std::cos(kPi * static_cast<double>(elapsed) / attackSamples);
    if (click > 1.0e-5)
    {
        noise = noise * 1664525u + 1013904223u;
        double burst = (static_cast<double>(noise >> 8) / 8388608.0 - 1.0) * click;
        for (auto& stage : clickBand)
            burst = stage.process(burst);
        out += 4.0 * burst; // the band keeps about 1/4 of the noise's level
        click *= clickDecay;
    }
    if (elapsed == maxSamples - releaseSamples)
        noteOff(releaseSamples);
    ++elapsed;

    if (fadeTotal > 0)
    {
        if (fadeRemaining <= 0)
        {
            active = false;
            return 0.0f;
        }
        out *= static_cast<double>(fadeRemaining) / fadeTotal;
        --fadeRemaining;
    }
    return static_cast<float>(out) * gain * velocity;
}

void Synth808Voice::renderAdd(float* output, int numSamples)
{
    for (int i = 0; i < numSamples && active; ++i)
        output[i] += renderSample();
}
} // namespace bbg
