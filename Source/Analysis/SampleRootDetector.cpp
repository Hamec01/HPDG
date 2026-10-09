#include "SampleRootDetector.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace bbg
{
namespace
{
constexpr double kHopSeconds = 0.010;
constexpr double kWindowSeconds = 0.080;
constexpr double kMinHz = 25.0;
constexpr double kMaxHz = 500.0;
constexpr double kMaxSeconds = 2.0;      // a one-shot's settled pitch is there long before
constexpr double kAfterPeakSeconds = 0.03;
constexpr double kClusterSemitones = 0.35;

struct Frame
{
    double time = 0.0;
    double midi = 0.0;
    double energy = 0.0;
    double aperiodicity = 1.0;
};

// One YIN frame: cumulative-mean-normalised difference, first dip under the threshold,
// parabolic refinement. Returns 0 when no lag qualifies.
double yinFrame(const float* x, int window, int minLag, int maxLag, double threshold, double sampleRate, double& aperiodicity)
{
    std::vector<double> diff(static_cast<size_t>(maxLag + 2), 0.0);
    for (int lag = 1; lag <= maxLag + 1; ++lag)
    {
        double sum = 0.0;
        for (int j = 0; j < window; ++j)
        {
            const double d = static_cast<double>(x[j]) - x[j + lag];
            sum += d * d;
        }
        diff[static_cast<size_t>(lag)] = sum;
    }
    std::vector<double> cmnd(static_cast<size_t>(maxLag + 2), 1.0);
    double running = 0.0;
    for (int lag = 1; lag <= maxLag + 1; ++lag)
    {
        running += diff[static_cast<size_t>(lag)];
        cmnd[static_cast<size_t>(lag)] = running > 0.0 ? diff[static_cast<size_t>(lag)] * lag / running : 1.0;
    }
    for (int lag = minLag; lag < maxLag; ++lag)
    {
        if (cmnd[static_cast<size_t>(lag)] >= threshold)
            continue;
        while (lag + 1 < maxLag && cmnd[static_cast<size_t>(lag + 1)] < cmnd[static_cast<size_t>(lag)])
            ++lag;
        // The first dip can be an upper harmonic when the fundamental is weak (distorted 808s):
        // a clearly deeper dip at 2x / 4x the lag is the real period (one or two octaves lower;
        // 3x would change the pitch class).
        const int firstLag = lag;
        for (int k = 2; k <= 4; k *= 2)
        {
            const int centre = firstLag * k;
            if (centre + 2 >= maxLag)
                break;
            int best = centre;
            for (int l = centre - 2; l <= centre + 2; ++l)
                if (cmnd[static_cast<size_t>(l)] < cmnd[static_cast<size_t>(best)])
                    best = l;
            if (cmnd[static_cast<size_t>(best)] < 0.8 * cmnd[static_cast<size_t>(lag)])
                lag = best;
        }
        const double a = cmnd[static_cast<size_t>(lag - 1)];
        const double b = cmnd[static_cast<size_t>(lag)];
        const double c = cmnd[static_cast<size_t>(lag + 1)];
        const double denominator = a - 2.0 * b + c;
        const double shift = std::abs(denominator) > 1.0e-12 ? 0.5 * (a - c) / denominator : 0.0;
        aperiodicity = b;
        return sampleRate / (lag + juce::jlimit(-0.5, 0.5, shift));
    }
    return 0.0;
}

std::vector<Frame> track(const float* x, int n, double sampleRate, double threshold)
{
    const int window = static_cast<int>(kWindowSeconds * sampleRate);
    const int hop = std::max(1, static_cast<int>(kHopSeconds * sampleRate));
    const int maxLag = static_cast<int>(sampleRate / kMinHz);
    const int minLag = std::max(2, static_cast<int>(sampleRate / kMaxHz));
    std::vector<Frame> frames;
    for (int start = 0; start + window + maxLag + 2 < n; start += hop)
    {
        double energy = 0.0;
        for (int j = 0; j < window; ++j)
            energy += static_cast<double>(x[start + j]) * x[start + j];
        Frame frame;
        frame.time = start / sampleRate;
        frame.energy = std::sqrt(energy / window);
        const double hz = yinFrame(x + start, window, minLag, maxLag, threshold, sampleRate, frame.aperiodicity);
        frame.midi = hz > 0.0 ? 69.0 + 12.0 * std::log2(hz / 440.0) : -1.0;
        frames.push_back(frame);
    }
    return frames;
}

SampleRoot settle(const std::vector<Frame>& frames)
{
    SampleRoot root;
    if (frames.empty())
        return root;
    double peakEnergy = 0.0;
    double peakTime = 0.0;
    for (const auto& frame : frames)
        if (frame.energy > peakEnergy)
        {
            peakEnergy = frame.energy;
            peakTime = frame.time;
        }

    std::vector<std::pair<double, double>> voiced; // midi, weight
    for (const auto& frame : frames)
        if (frame.midi > 0.0 && frame.energy > 0.1 * peakEnergy && frame.time >= peakTime + kAfterPeakSeconds)
            voiced.emplace_back(frame.midi, frame.energy * (1.0 - std::min(1.0, frame.aperiodicity)));
    if (voiced.empty())
        return root;

    double totalWeight = 0.0;
    for (const auto& v : voiced)
        totalWeight += v.second;
    // The settled note: the pitch whose +-35 cent neighbourhood carries the most weight.
    double bestWeight = -1.0;
    double centre = voiced.front().first;
    for (const auto& candidate : voiced)
    {
        double weight = 0.0;
        for (const auto& v : voiced)
            if (std::abs(v.first - candidate.first) <= kClusterSemitones)
                weight += v.second;
        if (weight > bestWeight)
        {
            bestWeight = weight;
            centre = candidate.first;
        }
    }
    std::vector<std::pair<double, double>> cluster;
    for (const auto& v : voiced)
        if (std::abs(v.first - centre) <= kClusterSemitones)
            cluster.push_back(v);
    std::sort(cluster.begin(), cluster.end());
    double half = 0.0;
    for (const auto& v : cluster)
        half += v.second;
    half *= 0.5;
    double running = 0.0;
    for (const auto& v : cluster)
    {
        running += v.second;
        if (running >= half)
        {
            root.midi = v.first;
            break;
        }
    }
    root.valid = true;
    root.confidence = totalWeight > 0.0 ? static_cast<float>(bestWeight / totalWeight) : 0.0f;
    return root;
}
} // namespace

SampleRoot SampleRootDetector::detect(const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    const int n = std::min(buffer.getNumSamples(), static_cast<int>(kMaxSeconds * sampleRate));
    if (n <= 0 || buffer.getNumChannels() <= 0)
        return {};
    std::vector<float> mono(static_cast<size_t>(n), 0.0f);
    const float scale = 1.0f / static_cast<float>(buffer.getNumChannels());
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const auto* data = buffer.getReadPointer(channel);
        for (int i = 0; i < n; ++i)
            mono[static_cast<size_t>(i)] += data[i] * scale;
    }
    return detect(mono.data(), n, sampleRate);
}

SampleRoot SampleRootDetector::detect(const float* mono, int numSamples, double sampleRate)
{
    if (mono == nullptr || sampleRate < 8000.0)
        return {};
    const int n = std::min(numSamples, static_cast<int>(kMaxSeconds * sampleRate));
    // Bass fundamentals stay under 500 Hz: work at ~8 kHz (box-filtered decimation), which makes
    // YIN about 30x cheaper when a sample bank loads.
    const int factor = std::max(1, static_cast<int>(sampleRate / 8000.0));
    const double rate = sampleRate / factor;
    std::vector<float> low(static_cast<size_t>(n / factor), 0.0f);
    for (size_t i = 0; i < low.size(); ++i)
    {
        float sum = 0.0f;
        for (int k = 0; k < factor; ++k)
            sum += mono[i * static_cast<size_t>(factor) + static_cast<size_t>(k)];
        low[i] = sum / static_cast<float>(factor);
    }
    const int m = static_cast<int>(low.size());
    auto root = settle(track(low.data(), m, rate, 0.15));
    if (!root.valid)
        root = settle(track(low.data(), m, rate, 0.35)); // synth basses: rich upper harmonics
    return root;
}
} // namespace bbg
