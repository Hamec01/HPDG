#include "BassSample.h"

#include <algorithm>
#include <cmath>

namespace bbg
{
namespace
{
std::shared_ptr<BassSample> build(std::vector<float> mono, double sampleRate, const juce::String& name);

std::shared_ptr<const BassSample> fromReader(juce::AudioFormatReader& reader, const juce::String& name, const juce::String& path)
{
    const auto length = static_cast<int>(std::min<juce::int64>(reader.lengthInSamples,
                                                               static_cast<juce::int64>(BassSample::kMaxSeconds * reader.sampleRate)));
    if (length < 64 || reader.sampleRate < 8000.0)
        return nullptr;
    const int channels = static_cast<int>(juce::jlimit(1u, 8u, reader.numChannels));
    juce::AudioBuffer<float> buffer(channels, length);
    reader.read(&buffer, 0, length, 0, true, true);
    std::vector<float> mono(static_cast<size_t>(length), 0.0f);
    for (int ch = 0; ch < channels; ++ch)
    {
        const auto* in = buffer.getReadPointer(ch);
        for (int i = 0; i < length; ++i)
            mono[static_cast<size_t>(i)] += in[i] / static_cast<float>(channels);
    }
    auto sample = build(std::move(mono), reader.sampleRate, name);
    if (sample != nullptr)
        sample->path = path;
    return sample;
}

std::shared_ptr<BassSample> build(std::vector<float> mono, double sampleRate, const juce::String& name)
{
    if (mono.size() < 64)
        return nullptr;
    auto sample = std::make_shared<BassSample>();
    sample->sampleRate = sampleRate;
    sample->name = name;
    // the leading silence is not part of the hit: start at the first sample above -50 dB of the peak
    float peak = 0.0f;
    for (auto v : mono)
        peak = std::max(peak, std::abs(v));
    if (peak < 1.0e-6f)
        return nullptr;
    size_t first = 0;
    while (first + 64 < mono.size() && std::abs(mono[first]) < peak * 0.003f)
        ++first;
    first = first > 32 ? first - 32 : 0;
    mono.erase(mono.begin(), mono.begin() + static_cast<std::ptrdiff_t>(first));
    for (auto& v : mono)
        v /= peak; // peak 1: the Level knob means the same for every file
    sample->rootHz = BassSample::detectRootHz(mono, sampleRate);
    sample->data = std::move(mono);
    return sample;
}
} // namespace

std::shared_ptr<const BassSample> BassSample::fromBuffer(std::vector<float> mono, double sampleRate, const juce::String& name)
{
    return build(std::move(mono), sampleRate, name);
}

std::shared_ptr<const BassSample> BassSample::fromFile(const juce::File& file, juce::String& error)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr)
    {
        error = "Not an audio file HPDG 808 can read: " + file.getFileName();
        return nullptr;
    }
    auto sample = fromReader(*reader, file.getFileNameWithoutExtension(), file.getFullPathName());
    if (sample == nullptr)
        error = "The file is silent or too short: " + file.getFileName();
    return sample;
}

std::shared_ptr<const BassSample> BassSample::fromWavMemory(const juce::MemoryBlock& wav, const juce::String& name, const juce::String& path)
{
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(new juce::MemoryInputStream(wav, false), true));
    if (reader == nullptr)
        return nullptr;
    return fromReader(*reader, name, path);
}

juce::MemoryBlock BassSample::toWavMemory() const
{
    juce::MemoryBlock block;
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(new juce::MemoryOutputStream(block, false), sampleRate, 1, 24, {}, 0));
    if (writer != nullptr)
    {
        const float* channels[] { data.data() };
        writer->writeFromFloatArrays(channels, 1, static_cast<int>(data.size()));
        writer.reset(); // flushes into `block`
    }
    return block;
}

double BassSample::detectRootHz(const std::vector<float>& mono, double sampleRate)
{
    // Analyse the body: from 40 ms after the peak (past a kick's click and pitch drop), up to 400 ms,
    // decimated 4x (the band of interest is below 400 Hz).
    size_t peakAt = 0;
    for (size_t i = 0; i < std::min(mono.size(), static_cast<size_t>(0.2 * sampleRate)); ++i)
        if (std::abs(mono[i]) > std::abs(mono[peakAt]))
            peakAt = i;
    size_t begin = peakAt + static_cast<size_t>(0.04 * sampleRate);
    size_t end = std::min(mono.size(), begin + static_cast<size_t>(0.4 * sampleRate));
    if (end < begin + static_cast<size_t>(0.08 * sampleRate))
    {
        begin = peakAt;
        end = std::min(mono.size(), begin + static_cast<size_t>(0.4 * sampleRate));
    }
    constexpr int decimation = 4;
    const double rate = sampleRate / decimation;
    std::vector<double> x;
    for (size_t i = begin; i + decimation <= end; i += decimation)
    {
        double sum = 0.0;
        for (int k = 0; k < decimation; ++k)
            sum += mono[i + static_cast<size_t>(k)];
        x.push_back(sum / decimation);
    }
    const int minLag = static_cast<int>(rate / 400.0);
    const int maxLag = static_cast<int>(rate / 25.0);
    const int n = static_cast<int>(x.size());
    if (n < maxLag + minLag + 8)
        return 0.0;
    std::vector<double> nsdf(static_cast<size_t>(maxLag + 2), 0.0);
    double best = 0.0;
    for (int lag = minLag; lag <= maxLag + 1; ++lag)
    {
        double acf = 0.0, energy = 0.0;
        for (int i = 0; i + lag < n; ++i)
        {
            acf += x[static_cast<size_t>(i)] * x[static_cast<size_t>(i + lag)];
            energy += x[static_cast<size_t>(i)] * x[static_cast<size_t>(i)] + x[static_cast<size_t>(i + lag)] * x[static_cast<size_t>(i + lag)];
        }
        nsdf[static_cast<size_t>(lag)] = energy > 0.0 ? 2.0 * acf / energy : 0.0;
        best = std::max(best, nsdf[static_cast<size_t>(lag)]);
    }
    if (best < 0.5)
        return 0.0;
    // the first local maximum close to the best one: the period, not a multiple of it
    for (int lag = minLag + 1; lag <= maxLag; ++lag)
    {
        const double a = nsdf[static_cast<size_t>(lag - 1)], b = nsdf[static_cast<size_t>(lag)], c = nsdf[static_cast<size_t>(lag + 1)];
        if (b >= a && b >= c && b >= 0.9 * best)
        {
            const double denominator = a - 2.0 * b + c;
            const double shift = std::abs(denominator) > 1.0e-12 ? 0.5 * (a - c) / denominator : 0.0;
            return rate / (lag + shift);
        }
    }
    return 0.0;
}
} // namespace bbg
