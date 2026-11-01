#include "DSP/Saturator.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace hamlo::dsp
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
} // namespace

// ------------------------------------------------------------------------------------- Saturator
int SaturatorProcessor::latencySamples(int oversamplingIndex)
{
    // Measured, not taken from Oversampling::getLatencyInSamples(): for 4x (two half-band stages) that
    // reports 60 samples while the chain delays by 61 (M6 test "measured latency equals reported").
    // White noise through up -> down without shaping; the latency is the lag of maximum correlation.
    static const std::array<int, 3> table = []
    {
        using OS = juce::dsp::Oversampling<float>;
        constexpr int n = 4096, maxLag = 400;
        std::vector<float> input(static_cast<size_t>(n));
        std::uint32_t state = 12345u;
        for (auto& v : input)
        {
            state = state * 1664525u + 1013904223u;
            v = static_cast<float>(state >> 8) / static_cast<float>(1u << 24) - 0.5f;
        }
        std::array<int, 3> result { 0, 0, 0 };
        for (int stages = 1; stages <= 2; ++stages)
        {
            OS os(1, static_cast<size_t>(stages), OS::filterHalfBandFIREquiripple, true, true);
            os.initProcessing(static_cast<size_t>(n));
            auto output = input;
            float* channel[] { output.data() };
            juce::dsp::AudioBlock<float> block(channel, 1, static_cast<size_t>(n));
            os.processSamplesUp(block);
            os.processSamplesDown(block);
            double best = -1.0;
            for (int lag = 0; lag < maxLag; ++lag)
            {
                double c = 0.0;
                for (int i = 0; i + lag < n; ++i)
                    c += static_cast<double>(output[static_cast<size_t>(i + lag)]) * input[static_cast<size_t>(i)];
                if (c > best)
                {
                    best = c;
                    result[static_cast<size_t>(stages)] = lag;
                }
            }
        }
        return result;
    }();
    return table[static_cast<size_t>(std::clamp(oversamplingIndex, 0, 2))];
}

int SaturatorProcessor::latencySamples(const FxSlotParams& p, double sampleRate)
{
    return latencySamples(static_cast<int>(std::lround(p.values[slot::kOversampling]))) + MagProcessor::latencySamples(p, sampleRate);
}

void SaturatorProcessor::prepare(double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate;
    using OS = juce::dsp::Oversampling<float>;
    for (int i = 0; i < 2; ++i)
    {
        oversamplers[static_cast<size_t>(i)] = std::make_unique<OS>(kMaxChannels, static_cast<size_t>(i + 1), OS::filterHalfBandFIREquiripple, true, true);
        oversamplers[static_cast<size_t>(i)]->initProcessing(static_cast<size_t>(std::min(maxBlockSize, kOversamplingChunk)));
    }
    dcCoefficient = 1.0 - 2.0 * kPi * 10.0 / sampleRate;
    latencySamples(0); // build the latency table here, never on the audio thread
    mag.prepare(sampleRate, 1);
    magFactor = 1;
    reset();
}

void SaturatorProcessor::reset() noexcept
{
    for (auto& os : oversamplers)
        if (os != nullptr)
            os->reset();
    dcIn = {};
    dcOut = {};
    mag.reset();
}

void SaturatorProcessor::shape(float* data, int n, float g, float bias, float norm, int curve) noexcept
{
    const auto f = [curve](float u)
    {
        switch (curve)
        {
            case 1: return u >= 0.0f ? std::tanh(u) : std::tanh(1.4f * u) / 1.4f;
            case 2: return std::clamp(u, -1.0f, 1.0f);
            default: return std::tanh(u);
        }
    };
    const float offset = f(bias);
    for (int i = 0; i < n; ++i)
        data[i] = (f(g * data[i] + bias) - offset) * norm;
}

void SaturatorProcessor::process(float* const* channels, int numChannels, int numSamples, const FxSlotParams& p) noexcept
{
    if (std::lround(p.values[slot::kSatModel]) > 0)
    {
        const int os = std::clamp(static_cast<int>(std::lround(p.values[slot::kOversampling])), 0, 2);
        if (os != activeOversampling)
        {
            if (os > 0)
                oversamplers[static_cast<size_t>(os - 1)]->reset();
            activeOversampling = os;
        }
        const int factor = 1 << os;
        mag.beginBlock(p, factor);
        if (factor != magFactor)
        {
            mag.prepareCores(sampleRate * factor); // coefficients only, no allocation
            magFactor = factor;
        }
        for (int ch = 0; ch < numChannels; ++ch)
            mag.pre(channels[ch], numSamples, ch);
        if (os == 0)
            for (int ch = 0; ch < numChannels; ++ch)
                mag.core(channels[ch], numSamples, ch);
        else
        {
            auto& sampler = *oversamplers[static_cast<size_t>(os - 1)];
            for (int start = 0; start < numSamples; start += kOversamplingChunk)
            {
                const int n = std::min(kOversamplingChunk, numSamples - start);
                juce::dsp::AudioBlock<float> block(channels, static_cast<size_t>(numChannels), static_cast<size_t>(start), static_cast<size_t>(n));
                auto up = sampler.processSamplesUp(block);
                for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
                    mag.core(up.getChannelPointer(ch), static_cast<int>(up.getNumSamples()), static_cast<int>(ch));
                sampler.processSamplesDown(block);
            }
        }
        mag.post(channels, numChannels, numSamples);
        return;
    }

    const float g = std::pow(10.0f, std::clamp(p.values[slot::kDriveDb], 0.0f, 48.0f) / 20.0f);
    const int curve = std::clamp(static_cast<int>(std::lround(p.values[slot::kCurve])), 0, 2);
    const float bias = std::clamp(p.values[slot::kBias], -0.9f, 0.9f);
    const float trim = std::pow(10.0f, std::clamp(p.values[slot::kTrimDb], -24.0f, 24.0f) / 20.0f);
    float slopeAtBias = 1.0f;
    if (curve == 0 || (curve == 1 && bias >= 0.0f))
        slopeAtBias = 1.0f - std::tanh(bias) * std::tanh(bias);
    else if (curve == 1)
        slopeAtBias = 1.0f - std::tanh(1.4f * bias) * std::tanh(1.4f * bias);
    const float norm = (p.values[slot::kAutoGain] > 0.5f ? 1.0f / (g * slopeAtBias) : 1.0f) * trim;

    const int os = std::clamp(static_cast<int>(std::lround(p.values[slot::kOversampling])), 0, 2);
    if (os != activeOversampling)
    {
        if (os > 0)
            oversamplers[static_cast<size_t>(os - 1)]->reset();
        activeOversampling = os;
    }

    if (os == 0)
        for (int ch = 0; ch < numChannels; ++ch)
            shape(channels[ch], numSamples, g, bias, norm, curve);
    else
    {
        auto& sampler = *oversamplers[static_cast<size_t>(os - 1)];
        for (int start = 0; start < numSamples; start += kOversamplingChunk)
        {
            const int n = std::min(kOversamplingChunk, numSamples - start);
            juce::dsp::AudioBlock<float> block(channels, static_cast<size_t>(numChannels), static_cast<size_t>(start), static_cast<size_t>(n));
            auto up = sampler.processSamplesUp(block);
            for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
                shape(up.getChannelPointer(ch), static_cast<int>(up.getNumSamples()), g, bias, norm, curve);
            sampler.processSamplesDown(block);
        }
    }

    // 10 Hz DC blocker: y = x - x1 + R y1
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto& x1 = dcIn[static_cast<size_t>(ch)];
        auto& y1 = dcOut[static_cast<size_t>(ch)];
        for (int i = 0; i < numSamples; ++i)
        {
            const double x = channels[ch][i];
            const double y = x - x1 + dcCoefficient * y1;
            x1 = x;
            y1 = y;
            channels[ch][i] = static_cast<float>(y);
        }
    }
}
} // namespace hamlo::dsp
