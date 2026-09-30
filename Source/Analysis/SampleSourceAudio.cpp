#include "SampleSourceAudio.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace bbg
{
bool SampleSourceAudio::load(const juce::File& sourceFile, juce::String* errorMessage)
{
    if (!sourceFile.existsAsFile())
    {
        if (errorMessage != nullptr)
            *errorMessage = "Audio file does not exist.";
        return false;
    }

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(sourceFile));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
    {
        if (errorMessage != nullptr)
            *errorMessage = reader == nullptr ? "Unsupported audio file format." : "Audio file is empty.";
        return false;
    }

    const int channels = juce::jlimit(1, 2, static_cast<int>(reader->numChannels));
    const int length = static_cast<int>(juce::jmin<juce::int64>(reader->lengthInSamples,
                                                                static_cast<juce::int64>(reader->sampleRate * kMaxLoadSeconds)));
    juce::AudioBuffer<float> buffer(channels, length);
    if (!reader->read(&buffer, 0, length, 0, true, channels > 1))
    {
        if (errorMessage != nullptr)
            *errorMessage = "Failed to read audio file samples.";
        return false;
    }

    loadFromBuffer(buffer, reader->sampleRate, sourceFile);
    return true;
}

void SampleSourceAudio::loadFromBuffer(const juce::AudioBuffer<float>& source, double rate, const juce::File& sourceFile)
{
    file = sourceFile;
    sampleRate = rate > 0.0 ? rate : 44100.0;
    audio = std::make_shared<const juce::AudioBuffer<float>>(source);
    durationSeconds = static_cast<double>(source.getNumSamples()) / sampleRate;

    std::vector<float> mono(static_cast<size_t>(source.getNumSamples()), 0.0f);
    const float channelGain = 1.0f / static_cast<float>(juce::jmax(1, source.getNumChannels()));
    for (int ch = 0; ch < source.getNumChannels(); ++ch)
    {
        const auto* data = source.getReadPointer(ch);
        for (int i = 0; i < source.getNumSamples(); ++i)
            mono[static_cast<size_t>(i)] += data[i] * channelGain;
    }

    buildPeaks(mono);
    buildOnsets(mono);
    estimateTempo();
}

void SampleSourceAudio::buildPeaks(const std::vector<float>& mono)
{
    const size_t blocks = (mono.size() + kPeakBlock - 1) / kPeakBlock;
    peakMin.assign(blocks, 0.0f);
    peakMax.assign(blocks, 0.0f);
    for (size_t b = 0; b < blocks; ++b)
    {
        float lo = 0.0f, hi = 0.0f;
        const size_t end = std::min(mono.size(), (b + 1) * kPeakBlock);
        for (size_t i = b * kPeakBlock; i < end; ++i)
        {
            lo = std::min(lo, mono[i]);
            hi = std::max(hi, mono[i]);
        }
        peakMin[b] = lo;
        peakMax[b] = hi;
    }
}

// Onset envelope: rise of the log energy of the first difference (clicks, snares, hats) plus a
// half-weighted rise of the full-band energy (kicks). Transients are its local peaks.
void SampleSourceAudio::buildOnsets(const std::vector<float>& mono)
{
    const size_t frames = mono.size() / kOnsetHop;
    onsetEnv.assign(frames, 0.0f);
    transients.clear();
    if (frames < 3)
        return;

    std::vector<float> logHigh(frames), logFull(frames);
    float prev = 0.0f;
    for (size_t f = 0; f < frames; ++f)
    {
        double eHigh = 0.0, eFull = 0.0;
        for (size_t i = f * kOnsetHop; i < (f + 1) * kOnsetHop; ++i)
        {
            const float x = mono[i];
            const float d = x - prev;
            prev = x;
            eHigh += static_cast<double>(d) * d;
            eFull += static_cast<double>(x) * x;
        }
        logHigh[f] = static_cast<float>(std::log(eHigh / kOnsetHop + 1.0e-9));
        logFull[f] = static_cast<float>(std::log(eFull / kOnsetHop + 1.0e-9));
    }
    for (size_t f = 1; f < frames; ++f)
        onsetEnv[f] = std::max(0.0f, logHigh[f] - logHigh[f - 1]) + 0.5f * std::max(0.0f, logFull[f] - logFull[f - 1]);

    const float globalMax = *std::max_element(onsetEnv.begin(), onsetEnv.end());
    if (globalMax <= 0.0f)
        return;

    constexpr int kWindow = 20;
    constexpr size_t kMinGap = 4; // ~46 ms at 44.1 kHz
    size_t lastPeak = 0;
    bool havePeak = false;
    for (size_t f = 1; f + 1 < frames; ++f)
    {
        const float v = onsetEnv[f];
        if (v < onsetEnv[f - 1] || v < onsetEnv[f + 1] || v < 0.12f * globalMax)
            continue;
        const size_t a = f > kWindow ? f - kWindow : 0;
        const size_t b = std::min(frames, f + kWindow + 1);
        const float localMean = std::accumulate(onsetEnv.begin() + static_cast<std::ptrdiff_t>(a),
                                                onsetEnv.begin() + static_cast<std::ptrdiff_t>(b), 0.0f)
            / static_cast<float>(b - a);
        if (v < localMean * 1.5f + 0.02f * globalMax)
            continue;
        if (havePeak && f - lastPeak < kMinGap)
        {
            if (v > onsetEnv[lastPeak])
            {
                transients.back() = static_cast<double>(f * kOnsetHop) / sampleRate;
                lastPeak = f;
            }
            continue;
        }
        transients.push_back(static_cast<double>(f * kOnsetHop) / sampleRate);
        lastPeak = f;
        havePeak = true;
    }
}

// Autocorrelation of the onset envelope over 70..180 BPM with a mild prior around 100 BPM, then
// the beat phase and the most accented of the four beats as the bar start.
void SampleSourceAudio::estimateTempo()
{
    estimatedBpm = 0.0;
    beatPhaseSeconds = 0.0;
    downbeatSeconds = 0.0;
    tempoConfidence = 0.0f;
    const size_t n = onsetEnv.size();
    const double framesPerSecond = sampleRate / kOnsetHop;
    if (n < static_cast<size_t>(framesPerSecond * 3.0))
        return;

    const float mean = std::accumulate(onsetEnv.begin(), onsetEnv.end(), 0.0f) / static_cast<float>(n);
    std::vector<float> e(n);
    for (size_t i = 0; i < n; ++i)
        e[i] = onsetEnv[i] - mean;

    const auto lagForBpm = [&](double bpm) { return 60.0 * framesPerSecond / bpm; };
    const int minLag = juce::jmax(2, static_cast<int>(std::floor(lagForBpm(180.0))));
    const int maxLag = static_cast<int>(std::ceil(lagForBpm(70.0)));
    double r0 = 0.0;
    for (size_t i = 0; i < n; ++i)
        r0 += static_cast<double>(e[i]) * e[i];
    if (r0 <= 0.0)
        return;

    std::vector<double> r(static_cast<size_t>(maxLag + 2), 0.0);
    for (int lag = minLag - 1; lag <= maxLag + 1; ++lag)
    {
        double s = 0.0;
        for (size_t i = 0; i + static_cast<size_t>(lag) < n; ++i)
            s += static_cast<double>(e[i]) * e[i + static_cast<size_t>(lag)];
        r[static_cast<size_t>(lag)] = s / r0;
    }

    int bestLag = minLag;
    double bestScore = -1.0e9;
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        const double bpm = 60.0 * framesPerSecond / lag;
        const double octaves = std::log2(bpm / 100.0);
        const double prior = std::exp(-0.5 * (octaves / 0.8) * (octaves / 0.8));
        const double score = r[static_cast<size_t>(lag)] * (0.6 + 0.4 * prior);
        if (score > bestScore)
        {
            bestScore = score;
            bestLag = lag;
        }
    }

    // Parabolic refinement of the lag.
    const double y0 = r[static_cast<size_t>(bestLag - 1)], y1 = r[static_cast<size_t>(bestLag)], y2 = r[static_cast<size_t>(bestLag + 1)];
    const double denom = y0 - 2.0 * y1 + y2;
    const double lag = bestLag + (std::abs(denom) > 1.0e-12 ? juce::jlimit(-0.5, 0.5, 0.5 * (y0 - y2) / denom) : 0.0);
    estimatedBpm = 60.0 * framesPerSecond / lag;
    tempoConfidence = static_cast<float>(juce::jlimit(0.0, 1.0, y1 * 2.0));

    // Beat phase: the offset whose comb of beats collects the most onset energy.
    const int period = juce::jmax(1, static_cast<int>(std::lround(lag)));
    double bestPhaseSum = -1.0;
    int bestPhase = 0;
    for (int p = 0; p < period; ++p)
    {
        double s = 0.0;
        for (double t = p; t < static_cast<double>(n); t += lag)
            s += onsetEnv[static_cast<size_t>(t)];
        if (s > bestPhaseSum)
        {
            bestPhaseSum = s;
            bestPhase = p;
        }
    }
    beatPhaseSeconds = bestPhase / framesPerSecond;

    // Bar start: the beat of four that is most accented over the whole file.
    double bestBeatSum = -1.0;
    int bestBeat = 0;
    for (int b = 0; b < 4; ++b)
    {
        double s = 0.0;
        for (double t = bestPhase + b * lag; t < static_cast<double>(n); t += 4.0 * lag)
        {
            const auto i = static_cast<size_t>(t);
            s += onsetEnv[i] + (i + 1 < n ? onsetEnv[i + 1] : 0.0f);
        }
        if (s > bestBeatSum)
        {
            bestBeatSum = s;
            bestBeat = b;
        }
    }
    downbeatSeconds = beatPhaseSeconds + bestBeat * 60.0 / estimatedBpm;
}

void SampleSourceAudio::getPeaks(double t0, double t1, int bins, std::vector<std::pair<float, float>>& out) const
{
    out.assign(static_cast<size_t>(juce::jmax(0, bins)), { 0.0f, 0.0f });
    if (!isLoaded() || bins <= 0 || t1 <= t0)
        return;

    const double samplesPerBin = (t1 - t0) * sampleRate / bins;
    const int total = audio->getNumSamples();
    const int channels = audio->getNumChannels();

    for (int bin = 0; bin < bins; ++bin)
    {
        const double s0 = t0 * sampleRate + bin * samplesPerBin;
        const double s1 = s0 + samplesPerBin;
        if (s1 <= 0.0 || s0 >= total)
            continue;

        float lo = 0.0f, hi = 0.0f;
        if (samplesPerBin < kPeakBlock * 2)
        {
            // Close zoom: read the samples themselves.
            const int a = juce::jlimit(0, total - 1, static_cast<int>(std::floor(s0)));
            const int b = juce::jlimit(a + 1, total, static_cast<int>(std::ceil(s1)));
            for (int i = a; i < b; ++i)
            {
                float v = 0.0f;
                for (int ch = 0; ch < channels; ++ch)
                    v += audio->getSample(ch, i);
                v /= static_cast<float>(channels);
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
        }
        else
        {
            const auto blocks = static_cast<int>(peakMin.size());
            const int a = juce::jlimit(0, blocks - 1, static_cast<int>(s0 / kPeakBlock));
            const int b = juce::jlimit(a + 1, blocks, static_cast<int>(std::ceil(s1 / kPeakBlock)));
            for (int i = a; i < b; ++i)
            {
                lo = std::min(lo, peakMin[static_cast<size_t>(i)]);
                hi = std::max(hi, peakMax[static_cast<size_t>(i)]);
            }
        }
        out[static_cast<size_t>(bin)] = { lo, hi };
    }
}

double SampleSourceAudio::snapToTransient(double t, double maxDistanceSeconds) const
{
    if (transients.empty())
        return t;
    const auto it = std::lower_bound(transients.begin(), transients.end(), t);
    double best = t;
    double bestDistance = maxDistanceSeconds;
    if (it != transients.end() && std::abs(*it - t) <= bestDistance)
    {
        best = *it;
        bestDistance = std::abs(*it - t);
    }
    if (it != transients.begin() && std::abs(*(it - 1) - t) <= bestDistance)
        best = *(it - 1);
    return best;
}

double SampleSourceAudio::snapToGrid(double t, double bpm, double anchorSeconds, int beatsPerStep)
{
    if (bpm <= 0.0 || beatsPerStep <= 0)
        return t;
    const double step = beatsPerStep * 60.0 / bpm;
    return anchorSeconds + std::round((t - anchorSeconds) / step) * step;
}

juce::Range<double> SampleSourceAudio::suggestRegion(int bars) const
{
    const double bpm = estimatedBpm > 0.0 ? estimatedBpm : 120.0;
    const double barSeconds = 240.0 / bpm;
    const double length = juce::jmax(1, bars) * barSeconds;
    if (!isLoaded() || durationSeconds <= length * 1.1)
        return { 0.0, durationSeconds };

    const double framesPerSecond = sampleRate / kOnsetHop;
    const double lagFrames = 60.0 * framesPerSecond / bpm;
    double firstBar = std::fmod(downbeatSeconds, barSeconds);
    if (firstBar < 0.0)
        firstBar += barSeconds;

    const float globalMean = onsetEnv.empty() ? 0.0f
        : std::accumulate(onsetEnv.begin(), onsetEnv.end(), 0.0f) / static_cast<float>(onsetEnv.size());

    // Mean onset energy over [a, b) seconds.
    const auto meanEnergy = [&](double a, double b)
    {
        const auto fa = static_cast<size_t>(juce::jmax(0.0, a) * framesPerSecond);
        const auto fb = std::min(onsetEnv.size(), static_cast<size_t>(juce::jmax(0.0, b) * framesPerSecond));
        if (fb <= fa)
            return 0.0;
        double sum = 0.0;
        for (size_t f = fa; f < fb; ++f)
            sum += onsetEnv[f];
        return sum / static_cast<double>(fb - fa);
    };

    // Candidates on every beat. Starts off the estimated bar grid are only taken where a drum
    // section begins (a quiet / tonal bar before it): a section start is the strongest bar-1 cue,
    // stronger than the accent guess (kick on 1 and 3 cannot tell beat 1 from beat 3).
    const double beatSeconds = 60.0 / bpm;
    double firstBeat = std::fmod(downbeatSeconds, beatSeconds);
    if (firstBeat < 0.0)
        firstBeat += beatSeconds;

    double bestScore = -1.0;
    double bestStart = firstBar;
    for (double start = firstBeat; start + length <= durationSeconds + 1.0e-6; start += beatSeconds)
    {
        const double barsFromGrid = (start - firstBar) / barSeconds;
        const bool onBarGrid = std::abs(barsFromGrid - std::round(barsFromGrid)) < 0.05;
        const double before = meanEnergy(start - barSeconds, start - 0.03);
        const double firstBarEnergy = meanEnergy(start, start + barSeconds);
        const double contrast = firstBarEnergy / juce::jmax(1.0e-6, before);
        const bool sectionStart = start >= barSeconds * 0.5 && contrast >= 2.0;
        if (!onBarGrid && !sectionStart)
            continue;

        const auto f0 = static_cast<size_t>(start * framesPerSecond);
        const auto f1 = std::min(onsetEnv.size(), static_cast<size_t>((start + length) * framesPerSecond));
        if (f1 <= f0 + static_cast<size_t>(lagFrames) + 2)
            continue;

        // Density of transient energy, relative to the whole file.
        double sum = 0.0, sumSq = 0.0, corr = 0.0;
        const auto lag = static_cast<size_t>(std::lround(lagFrames));
        for (size_t f = f0; f < f1; ++f)
        {
            const double v = onsetEnv[f];
            sum += v;
            sumSq += v * v;
            if (f + lag < f1)
                corr += v * onsetEnv[f + lag];
        }
        const double density = globalMean > 0.0f ? (sum / static_cast<double>(f1 - f0)) / globalMean : 0.0;
        // Regularity: how strongly the pulse repeats every beat inside the window.
        const double regularity = sumSq > 0.0 ? corr / sumSq : 0.0;
        // Beats that actually carry a hit.
        int beats = 0, beatsWithHit = 0;
        for (double b = start; b < start + length - 1.0e-6; b += 60.0 / bpm)
        {
            ++beats;
            const auto hit = std::lower_bound(transients.begin(), transients.end(), b - 0.045);
            if (hit != transients.end() && *hit <= b + 0.045)
                ++beatsWithHit;
        }
        const double onBeat = beats > 0 ? static_cast<double>(beatsWithHit) / beats : 0.0;
        double score = std::sqrt(juce::jmax(0.0, density)) * (0.3 + regularity) * (0.4 + onBeat);
        if (sectionStart)
            score *= 1.0 + 0.5 * juce::jmin(1.0, (contrast - 2.0) / 4.0 + 0.5);
        if (score > bestScore)
        {
            bestScore = score;
            bestStart = start;
        }
    }
    return { bestStart, juce::jmin(durationSeconds, bestStart + length) };
}
} // namespace bbg
