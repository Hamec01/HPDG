#include "DrumBreakTranscriber.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

#include <juce_dsp/juce_dsp.h>

#include "../Core/TimingGrid.h"

namespace bbg
{
namespace
{
constexpr int kFftOrder = 11;
constexpr int kFrameSize = 1 << kFftOrder; // 2048
constexpr int kHopSize = 256;
constexpr int kBandCount = 64;
constexpr int kLaneCount = 3; // 0 kick, 1 snare, 2 hat
constexpr std::array<TrackType, kLaneCount> kLanes { TrackType::Kick, TrackType::Snare, TrackType::HiHat };

// Relative activation (vs the lane's reference level) a hit must reach to be reported.
constexpr std::array<float, kLaneCount> kLaneThreshold { 0.10f, 0.10f, 0.08f };

struct BandLayout
{
    std::vector<int> binStart;
    std::vector<int> binEnd;
    std::vector<float> centerHz;

    int size() const { return static_cast<int>(centerHz.size()); }
};

BandLayout makeBands(double sampleRate)
{
    BandLayout layout;
    const double binHz = sampleRate / static_cast<double>(kFrameSize);
    const double lowHz = 25.0;
    const double highHz = std::min(18000.0, sampleRate * 0.5 * 0.95);
    const double ratio = std::pow(highHz / lowHz, 1.0 / kBandCount);

    double bandLow = lowHz;
    int nextBin = static_cast<int>(std::ceil(lowHz / binHz));
    for (int band = 0; band < kBandCount; ++band)
    {
        const double bandHigh = lowHz * std::pow(ratio, band + 1);
        const int endBin = static_cast<int>(std::ceil(bandHigh / binHz));
        if (endBin <= nextBin)
            continue; // too narrow for the FFT resolution: merge into the next band

        layout.binStart.push_back(nextBin);
        layout.binEnd.push_back(std::min(endBin, kFrameSize / 2));
        layout.centerHz.push_back(static_cast<float>(std::sqrt(bandLow * bandHigh)));
        nextBin = endBin;
        bandLow = bandHigh;
    }

    return layout;
}

using Matrix = std::vector<std::vector<float>>; // [frame][band] or [column][band]

Matrix computeBandSpectrogram(const std::vector<float>& mono, const BandLayout& layout)
{
    const int frameCount = static_cast<int>(mono.size() / kHopSize) + 1;
    Matrix frames(static_cast<size_t>(frameCount), std::vector<float>(static_cast<size_t>(layout.size()), 0.0f));

    juce::dsp::FFT fft(kFftOrder);
    std::vector<float> window(static_cast<size_t>(kFrameSize));
    for (int i = 0; i < kFrameSize; ++i)
        window[static_cast<size_t>(i)] = 0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi * static_cast<float>(i) / static_cast<float>(kFrameSize));

    std::vector<float> fftData(static_cast<size_t>(kFrameSize * 2));
    for (int frame = 0; frame < frameCount; ++frame)
    {
        std::fill(fftData.begin(), fftData.end(), 0.0f);
        const int start = frame * kHopSize - kFrameSize / 2; // frame is centred on frame * hop
        for (int i = 0; i < kFrameSize; ++i)
        {
            const int index = start + i;
            if (index >= 0 && index < static_cast<int>(mono.size()))
                fftData[static_cast<size_t>(i)] = mono[static_cast<size_t>(index)] * window[static_cast<size_t>(i)];
        }

        fft.performFrequencyOnlyForwardTransform(fftData.data());

        auto& bands = frames[static_cast<size_t>(frame)];
        for (int band = 0; band < layout.size(); ++band)
        {
            const int b0 = layout.binStart[static_cast<size_t>(band)];
            const int b1 = layout.binEnd[static_cast<size_t>(band)];
            float sum = 0.0f;
            for (int bin = b0; bin < b1; ++bin)
                sum += fftData[static_cast<size_t>(bin)];
            bands[static_cast<size_t>(band)] = sum / static_cast<float>(std::max(1, b1 - b0));
        }
    }

    return frames;
}

double frameTime(int frame, double sampleRate)
{
    return static_cast<double>(frame * kHopSize) / sampleRate;
}

int timeToFrame(double seconds, double sampleRate, int frameCount)
{
    const int frame = static_cast<int>(std::lround(seconds * sampleRate / kHopSize));
    return juce::jlimit(0, frameCount - 1, frame);
}

std::vector<float> computeOnsetFunction(const Matrix& spectrogram)
{
    float globalMax = 1.0e-9f;
    for (const auto& frame : spectrogram)
        for (const float value : frame)
            globalMax = std::max(globalMax, value);

    const float gain = 1000.0f / globalMax;
    Matrix logSpec(spectrogram.size());
    for (size_t frame = 0; frame < spectrogram.size(); ++frame)
    {
        logSpec[frame].resize(spectrogram[frame].size());
        for (size_t band = 0; band < spectrogram[frame].size(); ++band)
            logSpec[frame][band] = std::log1p(gain * spectrogram[frame][band]);
    }

    std::vector<float> odf(spectrogram.size(), 0.0f);
    for (size_t frame = 2; frame < spectrogram.size(); ++frame)
    {
        float flux = 0.0f;
        for (size_t band = 0; band < logSpec[frame].size(); ++band)
        {
            // Compare against the max of the two previous frames (vibrato-robust, SuperFlux-like).
            const float reference = std::max(logSpec[frame - 1][band], logSpec[frame - 2][band]);
            flux += std::max(0.0f, logSpec[frame][band] - reference);
        }
        odf[frame] = flux;
    }

    const float maxOdf = *std::max_element(odf.begin(), odf.end());
    if (maxOdf > 0.0f)
        for (auto& value : odf)
            value /= maxOdf;

    return odf;
}

std::vector<int> pickOnsetFrames(const std::vector<float>& odf)
{
    std::vector<int> peaks;
    const int count = static_cast<int>(odf.size());
    constexpr int kLocalRadius = 4;   // ~23 ms at 44.1 kHz
    constexpr int kMeanBefore = 25;
    constexpr int kMeanAfter = 8;

    for (int frame = 2; frame < count; ++frame)
    {
        const float value = odf[static_cast<size_t>(frame)];
        if (value < 0.035f)
            continue;

        bool isMax = true;
        for (int other = std::max(0, frame - kLocalRadius); other <= std::min(count - 1, frame + kLocalRadius); ++other)
        {
            if (other == frame)
                continue;
            const float otherValue = odf[static_cast<size_t>(other)];
            if (otherValue > value || (otherValue == value && other < frame))
            {
                isMax = false;
                break;
            }
        }
        if (!isMax)
            continue;

        float mean = 0.0f;
        int meanCount = 0;
        for (int other = std::max(0, frame - kMeanBefore); other <= std::min(count - 1, frame + kMeanAfter); ++other)
        {
            mean += odf[static_cast<size_t>(other)];
            ++meanCount;
        }
        mean /= static_cast<float>(std::max(1, meanCount));

        if (value >= mean + 0.03f)
            peaks.push_back(frame);
    }

    return peaks;
}

// Finds the attack start on the waveform around a frame-level onset estimate.
// Uses the first difference of the signal so low sustained energy (kick tails, bass) does not
// hide a later attack.
double refineOnsetTime(const std::vector<float>& mono, double sampleRate, double estimate)
{
    const int blockSize = std::max(8, static_cast<int>(sampleRate * 0.0005)); // 0.5 ms
    const int searchStart = std::max(1, static_cast<int>((estimate - 0.030) * sampleRate));
    const int searchEnd = std::min(static_cast<int>(mono.size()) - 1, static_cast<int>((estimate + 0.030) * sampleRate));
    if (searchEnd - searchStart < blockSize * 4)
        return estimate;

    std::vector<float> energy;
    for (int start = searchStart; start + blockSize <= searchEnd; start += blockSize)
    {
        float sum = 0.0f;
        for (int i = start; i < start + blockSize; ++i)
        {
            const float diff = mono[static_cast<size_t>(i)] - mono[static_cast<size_t>(i - 1)];
            sum += diff * diff;
        }
        energy.push_back(sum);
    }

    // The attack normally lies within +15 ms of the estimate; only when that part is nearly
    // silent (the frame estimate came early) is the peak taken from the extended +30 ms range,
    // so a following hit is not grabbed by mistake.
    const int blocks = static_cast<int>(energy.size());
    const int primaryBlocks = juce::jlimit(1, blocks, static_cast<int>(0.045 * sampleRate) / blockSize);
    const auto fullPeak = std::max_element(energy.begin(), energy.end());
    const auto primaryPeak = std::max_element(energy.begin(), energy.begin() + primaryBlocks);
    const auto chosenPeak = *primaryPeak >= 0.2f * *fullPeak ? primaryPeak : fullPeak;
    const int peak = static_cast<int>(std::distance(energy.begin(), chosenPeak));
    const float peakEnergy = energy[static_cast<size_t>(peak)];
    if (peakEnergy <= 0.0f)
        return estimate;

    int attack = -1;
    for (int block = peak; block >= 0; --block)
    {
        if (energy[static_cast<size_t>(block)] < 0.12f * peakEnergy)
        {
            attack = block + 1;
            break;
        }
    }

    if (attack < 0)
    {
        // No quiet gap before the attack (dense material): take the steepest relative rise.
        float bestRise = 0.0f;
        attack = peak;
        for (int block = 3; block <= peak; ++block)
        {
            const float before = (energy[static_cast<size_t>(block - 1)] + energy[static_cast<size_t>(block - 2)] + energy[static_cast<size_t>(block - 3)]) / 3.0f;
            const float rise = energy[static_cast<size_t>(block)] / (before + 1.0e-12f);
            if (rise > bestRise)
            {
                bestRise = rise;
                attack = block;
            }
        }
    }

    juce::ignoreUnused(blocks);
    return static_cast<double>(searchStart + attack * blockSize) / sampleRate;
}

// Generic starting templates; the NMF adapts them to the actual kit of the file.
std::vector<float> makeTemplate(int lane, const BandLayout& layout)
{
    std::vector<float> shape(static_cast<size_t>(layout.size()), 0.0f);
    for (int band = 0; band < layout.size(); ++band)
    {
        const double hz = layout.centerHz[static_cast<size_t>(band)];
        const double octave = std::log2(hz);
        auto bump = [octave](double centerHz, double widthOctaves)
        {
            const double d = (octave - std::log2(centerHz)) / widthOctaves;
            return std::exp(-0.5 * d * d);
        };
        auto highPass = [hz](double cornerHz, double slope)
        {
            return 1.0 / (1.0 + std::pow(cornerHz / hz, slope));
        };
        auto lowPass = [hz](double cornerHz, double slope)
        {
            return 1.0 / (1.0 + std::pow(hz / cornerHz, slope));
        };

        double value = 0.0;
        switch (lane)
        {
            case 0: // kick: sub/low body + faint beater click
                value = 1.0 * bump(65.0, 0.7) + 0.05 * bump(3000.0, 1.2);
                break;
            case 1: // snare: low-mid body + broadband noise up to ~10 kHz
                value = 0.55 * bump(210.0, 0.8) + 0.35 * highPass(700.0, 2.0) * lowPass(9000.0, 3.0);
                break;
            case 2: // hat: bright, almost nothing below ~3 kHz
            default:
                value = 0.45 * highPass(6500.0, 5.0);
                break;
        }
        shape[static_cast<size_t>(band)] = static_cast<float>(value + 1.0e-4);
    }

    const float sum = std::accumulate(shape.begin(), shape.end(), 0.0f);
    for (auto& value : shape)
        value /= sum;
    return shape;
}

struct NmfResult
{
    Matrix templates;    // [lane][band]
    Matrix activations;  // [column][lane]
    float fit = 0.0f;
};

// Kick activations are often 20-50x a hat's, so even a tiny kick template leak above a few
// kHz would swallow a hat played on the same onset: the kick mask is deliberately steep.
Matrix makeMasks(const BandLayout& layout)
{
    const int bands = layout.size();
    Matrix masks(kLaneCount, std::vector<float>(static_cast<size_t>(bands), 1.0f));
    for (int band = 0; band < bands; ++band)
    {
        const double hz = layout.centerHz[static_cast<size_t>(band)];
        masks[0][static_cast<size_t>(band)] = static_cast<float>(1.0 / (1.0 + std::pow(hz / 2500.0, 6.0)));
        masks[2][static_cast<size_t>(band)] = static_cast<float>(1.0 / (1.0 + std::pow(1800.0 / hz, 4.0)));
    }
    return masks;
}

void normalizeMasked(std::vector<float>& templ, const std::vector<float>& mask)
{
    for (size_t band = 0; band < templ.size(); ++band)
        templ[band] = std::max(1.0e-7f, templ[band] * mask[band]);
    const float sum = std::accumulate(templ.begin(), templ.end(), 0.0f);
    for (auto& value : templ)
        value /= sum;
}

// V columns = per-onset spectral increase vectors. KL multiplicative updates. Without
// fixedTemplates the templates start generic and adapt (anchored, masked); with them only the
// activations are solved.
NmfResult runNmf(const Matrix& columns, const BandLayout& layout, const Matrix* fixedTemplates = nullptr)
{
    NmfResult result;
    const int bands = layout.size();
    const int count = static_cast<int>(columns.size());
    const auto masks = makeMasks(layout);

    Matrix base(kLaneCount);
    for (int lane = 0; lane < kLaneCount; ++lane)
    {
        auto templ = fixedTemplates != nullptr ? (*fixedTemplates)[static_cast<size_t>(lane)] : makeTemplate(lane, layout);
        normalizeMasked(templ, masks[static_cast<size_t>(lane)]);
        base[static_cast<size_t>(lane)] = std::move(templ);
    }
    result.templates = base;
    result.activations.assign(static_cast<size_t>(count), std::vector<float>(kLaneCount, 0.0f));

    for (int column = 0; column < count; ++column)
    {
        const float total = std::accumulate(columns[static_cast<size_t>(column)].begin(), columns[static_cast<size_t>(column)].end(), 0.0f);
        for (int lane = 0; lane < kLaneCount; ++lane)
            result.activations[static_cast<size_t>(column)][static_cast<size_t>(lane)] = std::max(1.0e-6f, total / kLaneCount);
    }

    if (count == 0)
        return result;

    auto& W = result.templates;
    auto& H = result.activations;
    Matrix ratio(static_cast<size_t>(count), std::vector<float>(static_cast<size_t>(bands), 0.0f));

    auto computeRatio = [&]()
    {
        for (int column = 0; column < count; ++column)
        {
            for (int band = 0; band < bands; ++band)
            {
                float model = 1.0e-9f;
                for (int lane = 0; lane < kLaneCount; ++lane)
                    model += W[static_cast<size_t>(lane)][static_cast<size_t>(band)] * H[static_cast<size_t>(column)][static_cast<size_t>(lane)];
                ratio[static_cast<size_t>(column)][static_cast<size_t>(band)] = columns[static_cast<size_t>(column)][static_cast<size_t>(band)] / model;
            }
        }
    };

    constexpr int kIterations = 80;
    constexpr int kFixedIterations = 20;
    constexpr float kAnchor = 0.35f;

    for (int iteration = 0; iteration < kIterations; ++iteration)
    {
        computeRatio();
        for (int column = 0; column < count; ++column)
        {
            for (int lane = 0; lane < kLaneCount; ++lane)
            {
                float numerator = 0.0f;
                float denominator = 0.0f;
                for (int band = 0; band < bands; ++band)
                {
                    numerator += W[static_cast<size_t>(lane)][static_cast<size_t>(band)] * ratio[static_cast<size_t>(column)][static_cast<size_t>(band)];
                    denominator += W[static_cast<size_t>(lane)][static_cast<size_t>(band)];
                }
                H[static_cast<size_t>(column)][static_cast<size_t>(lane)] *= numerator / std::max(1.0e-9f, denominator);
            }
        }

        if (iteration < kFixedIterations || fixedTemplates != nullptr)
            continue;

        computeRatio();
        for (int lane = 0; lane < kLaneCount; ++lane)
        {
            float activationSum = 0.0f;
            for (int column = 0; column < count; ++column)
                activationSum += H[static_cast<size_t>(column)][static_cast<size_t>(lane)];
            if (activationSum <= 1.0e-9f)
                continue;

            auto& templ = W[static_cast<size_t>(lane)];
            for (int band = 0; band < bands; ++band)
            {
                float numerator = 0.0f;
                for (int column = 0; column < count; ++column)
                    numerator += ratio[static_cast<size_t>(column)][static_cast<size_t>(band)] * H[static_cast<size_t>(column)][static_cast<size_t>(lane)];
                templ[static_cast<size_t>(band)] *= numerator / activationSum;
            }

            // Lanes co-occur all the time (hat on every kick/snare), so an unconstrained template
            // would learn its neighbour's spectrum. Masks keep each lane in its own region.
            const auto& mask = masks[static_cast<size_t>(lane)];
            for (int band = 0; band < bands; ++band)
                templ[static_cast<size_t>(band)] *= mask[static_cast<size_t>(band)];

            const float sum = std::accumulate(templ.begin(), templ.end(), 0.0f);
            for (int band = 0; band < bands; ++band)
            {
                float adapted = templ[static_cast<size_t>(band)] / std::max(1.0e-9f, sum);
                const float anchor = base[static_cast<size_t>(lane)][static_cast<size_t>(band)];
                if (lane == 1 && layout.centerHz[static_cast<size_t>(band)] > 6000.0f)
                    adapted = std::min(adapted, anchor * 1.2f); // snare may not grow a hat-like top
                templ[static_cast<size_t>(band)] = (1.0f - kAnchor) * adapted + kAnchor * anchor;
            }
        }
    }

    // Fit = share of the onset energy the three lanes explain (min(V, WH) / V).
    double explained = 0.0;
    double total = 0.0;
    for (int column = 0; column < count; ++column)
    {
        for (int band = 0; band < bands; ++band)
        {
            float model = 0.0f;
            for (int lane = 0; lane < kLaneCount; ++lane)
                model += W[static_cast<size_t>(lane)][static_cast<size_t>(band)] * H[static_cast<size_t>(column)][static_cast<size_t>(lane)];
            const float value = columns[static_cast<size_t>(column)][static_cast<size_t>(band)];
            explained += std::min(value, model);
            total += value;
        }
    }
    result.fit = total > 0.0 ? static_cast<float>(explained / total) : 0.0f;
    return result;
}

float percentile(std::vector<float> values, float fraction)
{
    if (values.empty())
        return 0.0f;
    const size_t index = static_cast<size_t>(std::clamp(fraction, 0.0f, 1.0f) * static_cast<float>(values.size() - 1));
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(index), values.end());
    return values[index];
}

// Second pass: a break repeats the same kick / snare / hat, so the cleanest onsets of each lane
// describe that drum far better than a generic shape (e.g. a kick with a lot of 150-300 Hz body
// that the generic snare template would otherwise claim).
Matrix learnExemplarTemplates(const Matrix& columns, const NmfResult& first)
{
    Matrix templates = first.templates;
    const size_t count = columns.size();
    if (count < 4)
        return templates;

    std::array<float, kLaneCount> reference {};
    for (int lane = 0; lane < kLaneCount; ++lane)
    {
        std::vector<float> values;
        for (const auto& activation : first.activations)
            values.push_back(activation[static_cast<size_t>(lane)]);
        reference[static_cast<size_t>(lane)] = std::max(1.0e-9f, percentile(values, 0.98f));
    }

    for (int lane = 0; lane < kLaneCount; ++lane)
    {
        std::vector<std::pair<float, size_t>> candidates;
        for (size_t column = 0; column < count; ++column)
        {
            std::array<float, kLaneCount> rel {};
            for (int other = 0; other < kLaneCount; ++other)
                rel[static_cast<size_t>(other)] = first.activations[column][static_cast<size_t>(other)] / reference[static_cast<size_t>(other)];

            const float own = rel[static_cast<size_t>(lane)];
            if (own < 0.35f)
                continue;
            float strongestOther = 0.0f;
            for (int other = 0; other < kLaneCount; ++other)
                if (other != lane)
                    strongestOther = std::max(strongestOther, rel[static_cast<size_t>(other)]);
            const float purity = own - strongestOther;
            if (purity > 0.15f)
                candidates.emplace_back(purity, column);
        }

        if (candidates.size() < 2)
            continue;

        std::sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) { return left.first > right.first; });
        const size_t take = std::min<size_t>(12, std::max<size_t>(2, static_cast<size_t>(std::ceil(0.4 * static_cast<double>(candidates.size())))));

        std::vector<float> exemplar(columns.front().size(), 0.0f);
        for (size_t index = 0; index < take; ++index)
        {
            const auto& column = columns[candidates[index].second];
            const float sum = std::max(1.0e-9f, std::accumulate(column.begin(), column.end(), 0.0f));
            for (size_t band = 0; band < exemplar.size(); ++band)
                exemplar[band] += column[band] / sum / static_cast<float>(take);
        }

        auto& templ = templates[static_cast<size_t>(lane)];
        for (size_t band = 0; band < exemplar.size(); ++band)
            templ[band] = 0.85f * exemplar[band] + 0.15f * templ[band];
    }

    return templates;
}

float laneTempoWeight(TrackType lane)
{
    return lane == TrackType::HiHat ? 0.5f : 1.0f;
}

// Weighted share of hits that sit on a (possibly swung) sixteenth grid.
float gridFit(const std::vector<BreakDrumHit>& hits, double bpm, double origin, float swing)
{
    const double sixteenthSeconds = 15.0 / bpm;
    double fit = 0.0;
    double weightSum = 0.0;
    for (const auto& hit : hits)
    {
        const double position = (hit.timeSeconds - origin) / sixteenthSeconds;
        const double base = std::floor(position);
        double best = 1.0e9;
        for (int k = -1; k <= 2; ++k)
        {
            const double slot = base + k;
            const bool odd = (static_cast<long long>(slot) % 2 + 2) % 2 == 1;
            const double gridPosition = slot + (odd ? swing : 0.0);
            best = std::min(best, std::abs(position - gridPosition));
        }

        const double deviationMs = best * sixteenthSeconds * 1000.0;
        const double weight = static_cast<double>(laneTempoWeight(hit.lane) * (0.3f + hit.strength));
        fit += weight * std::exp(-0.5 * (deviationMs / 9.0) * (deviationMs / 9.0));
        weightSum += weight;
    }
    if (weightSum <= 0.0)
        return 0.0f;

    // A finer grid (faster tempo) catches random onsets more easily: rescale so that randomly
    // placed onsets score ~0 at every tempo and double-time gets no free advantage.
    const double raw = fit / weightSum;
    const double chance = std::min(0.9, 9.0 * std::sqrt(2.0 * juce::MathConstants<double>::pi) / (sixteenthSeconds * 1000.0));
    return static_cast<float>(std::max(0.0, (raw - chance) / (1.0 - chance)));
}

// Share of strong snare weight on beats 2 and 4, plus kick on beat 1.
float backbeatScore(const std::vector<BreakDrumHit>& hits, double bpm, double origin)
{
    double onBackbeat = 0.0;
    double snareTotal = 0.0;
    double kickOnOne = 0.0;
    double kickTotal = 0.0;
    for (const auto& hit : hits)
    {
        if (hit.strength < 0.45f)
            continue;

        const double beat = std::fmod((hit.timeSeconds - origin) * bpm / 60.0 + 4000.0, 4.0);
        if (hit.lane == TrackType::Snare)
        {
            snareTotal += hit.strength;
            if (std::abs(beat - 1.0) < 0.14 || std::abs(beat - 3.0) < 0.14)
                onBackbeat += hit.strength;
        }
        else if (hit.lane == TrackType::Kick)
        {
            kickTotal += hit.strength;
            if (beat < 0.14 || beat > 3.86)
                kickOnOne += hit.strength;
        }
    }

    const double snarePart = snareTotal > 0.0 ? onBackbeat / snareTotal : 0.5;
    const double kickPart = kickTotal > 0.0 ? std::min(1.0, 3.0 * kickOnOne / kickTotal) : 0.5;
    return static_cast<float>(0.75 * snarePart + 0.25 * kickPart);
}

// Bar-phase evidence: the 2 & 4 backbeat or the half-time one (one snare on beat 3, trap / halftime
// DnB at their written tempo). Used only to place beat 1 at a given tempo: the tempo score keeps the
// 2 & 4 backbeat. With "2 & 4 only" a trap loop's beat 1 moved a quarter so the beat-3 snare read
// as beat 2 (docs/audit/SAMPLE_ANALYSIS_STAGE.md step 1).
float phaseBackbeatScore(const std::vector<BreakDrumHit>& hits, double bpm, double origin)
{
    double onThree = 0.0;
    double snareTotal = 0.0;
    double kickOnOne = 0.0;
    double kickTotal = 0.0;
    for (const auto& hit : hits)
    {
        if (hit.strength < 0.45f)
            continue;
        const double beat = std::fmod((hit.timeSeconds - origin) * bpm / 60.0 + 4000.0, 4.0);
        if (hit.lane == TrackType::Snare)
        {
            snareTotal += hit.strength;
            if (std::abs(beat - 2.0) < 0.14)
                onThree += hit.strength;
        }
        else if (hit.lane == TrackType::Kick)
        {
            kickTotal += hit.strength;
            if (beat < 0.14 || beat > 3.86)
                kickOnOne += hit.strength;
        }
    }
    const double snarePart = snareTotal > 0.0 ? onThree / snareTotal : 0.5;
    const double kickPart = kickTotal > 0.0 ? std::min(1.0, 3.0 * kickOnOne / kickTotal) : 0.5;
    const float halfTime = static_cast<float>(0.75 * snarePart + 0.25 * kickPart);
    return std::max(backbeatScore(hits, bpm, origin), halfTime);
}

// Half / double-time evidence (docs/audit/SAMPLE_ANALYSIS_STAGE.md, step 2): onsets per sixteenth
// at this tempo. Real loops at their written tempo stay below ~0.85 (Boom Bap median 0.67, p90
// 0.76); a half-time reading of a Trap / DnB loop needs 32nds for its hat rolls and ghosts (Trap
// 0.8-2.0, median 1.1-1.3), so a rate above 0.85 says the tempo is too slow to write the loop.
// 0 = clearly playable, 1 = too dense by 0.3 or more onsets per sixteenth.
float subdivisionOverload(const std::vector<BreakDrumHit>& hits, double bpm)
{
    std::vector<double> times;
    times.reserve(hits.size());
    for (const auto& hit : hits)
        times.push_back(hit.timeSeconds);
    std::sort(times.begin(), times.end());
    int onsets = 0;
    double last = -1.0;
    for (const double time : times)
    {
        if (last >= 0.0 && time - last < 0.015) // one stroke on several lanes
            continue;
        ++onsets;
        last = time;
    }
    if (onsets < 4)
        return 0.0f;

    const double sixteenthSeconds = 15.0 / bpm;
    const double rate = onsets / ((times.back() - times.front()) / sixteenthSeconds + 1.0);
    return static_cast<float>(juce::jlimit(0.0, 1.0, (rate - 0.85) / 0.3));
}

float lengthFit(double loopSeconds, double bpm, int& barsOut)
{
    const double bars = loopSeconds * bpm / 240.0;
    const double rounded = std::round(bars);
    barsOut = static_cast<int>(rounded);
    if (rounded < 1.0)
        return 0.0f;
    const double deviation = (bars - rounded) / 0.02; // 2% of a bar
    return static_cast<float>(std::exp(-0.5 * deviation * deviation));
}

struct PhasedFit
{
    float fit = 0.0f;
    float swing = 0.0f;
    double phase = 0.0; // eighth-grid phase in seconds, in [0, eighth): swing shifts the 16th after it
};

// Grid fit with the grid phase searched too: a break rarely starts exactly on a grid line
// (pickups, leading hats, untrimmed audio), so phase 0 would punish the right tempo. The phase
// spans a full eighth because swing only delays the second sixteenth of each eighth.
PhasedFit bestPhasedFit(const std::vector<BreakDrumHit>& hits, double bpm, bool fine)
{
    const double sixteenth = 15.0 / bpm;
    const std::array<float, 4> coarseSwings { 0.0f, 0.12f, 0.24f, 0.36f };
    PhasedFit best;
    best.fit = -1.0f;

    constexpr int kCoarseSteps = 24; // per sixteenth
    for (int step = 0; step < 2 * kCoarseSteps; ++step)
    {
        const double phase = sixteenth * step / kCoarseSteps;
        for (const float swing : coarseSwings)
        {
            const float fit = gridFit(hits, bpm, phase, swing) - swing * 0.01f;
            if (fit > best.fit)
                best = { fit, swing, phase };
        }
    }

    if (fine)
    {
        const double coarsePhase = best.phase;
        for (int step = -8; step <= 8; ++step)
        {
            const double phase = coarsePhase + sixteenth * step / (kCoarseSteps * 8.0);
            for (float swing = 0.0f; swing <= 0.42f; swing += 0.02f)
            {
                const float fit = gridFit(hits, bpm, phase, swing) - swing * 0.01f;
                if (fit > best.fit)
                    best = { fit, swing, phase };
            }
        }
    }

    // Alias: hits that all sit on "swung" odd sixteenths are just a straight grid shifted by
    // (1 + swing) sixteenths. Prefer the straight reading when it fits as well, otherwise the
    // downbeat search would only see the wrong parity.
    if (fine && best.swing > 0.0f)
    {
        const double aliasPhase = best.phase + sixteenth * (1.0 + best.swing);
        PhasedFit straight { -1.0f, 0.0f, aliasPhase };
        for (int step = -12; step <= 12; ++step)
        {
            const double phase = aliasPhase + sixteenth * step / (kCoarseSteps * 8.0);
            const float fit = gridFit(hits, bpm, phase, 0.0f);
            if (fit > straight.fit)
                straight = { fit, 0.0f, phase };
        }
        if (straight.fit >= best.fit - 0.02f)
            best = straight;
    }

    best.fit += best.swing * 0.01f;
    best.phase = std::fmod(best.phase + sixteenth * 64.0, 2.0 * sixteenth);
    return best;
}

struct BreakContext
{
    double durationSeconds = 0.0;
    double leadSilenceEnd = 0.0; // start of audible material (loop start for trimmed loops)
    double firstHit = 0.0;
};

BreakTempoCandidate evaluateCandidate(const std::vector<BreakDrumHit>& hits,
                                      double bpm,
                                      const BreakContext& context,
                                      const DrumBreakOptions& options,
                                      bool refine,
                                      double& originOut)
{
    BreakTempoCandidate candidate;
    candidate.bpm = bpm;
    const double loopSeconds = context.durationSeconds - context.leadSilenceEnd;
    // Only a file that starts right on a hit is a trimmed loop; for an untrimmed chunk the
    // file length says nothing about bars.
    const bool trimmed = context.firstHit - context.leadSilenceEnd < 0.050 && context.firstHit < 0.3;

    if (refine)
    {
        double best = bpm;
        float bestScore = -1.0f;
        for (double delta = -0.02; delta <= 0.02001; delta += 0.002)
        {
            const double trial = bpm * (1.0 + delta);
            int bars = 0;
            // Lean towards an exact loop length when the fit is tied.
            const float score = bestPhasedFit(hits, trial, false).fit + 0.02f * lengthFit(loopSeconds, trial, bars);
            if (score > bestScore)
            {
                bestScore = score;
                best = trial;
            }
        }
        candidate.bpm = best;
        for (double delta = -0.002; delta <= 0.00201; delta += 0.0004)
        {
            const double trial = best * (1.0 + delta);
            int bars = 0;
            const float score = bestPhasedFit(hits, trial, false).fit + 0.02f * lengthFit(loopSeconds, trial, bars);
            if (score > bestScore)
            {
                bestScore = score;
                candidate.bpm = trial;
            }
        }
    }

    const auto phased = bestPhasedFit(hits, candidate.bpm, true);
    candidate.gridFit = phased.fit;
    candidate.swing = phased.swing;
    candidate.lengthFit = lengthFit(loopSeconds, candidate.bpm, candidate.bars);

    // Bar phase: which of the 16 grid positions is beat 1. Backbeat (snares on 2 & 4, kick on
    // 1) decides; a loop that starts right on a hit gets a bonus for starting on the downbeat.
    // With swing only eighth positions are tried so the swing parity found above stays valid;
    // straight time has no parity, so every sixteenth is a candidate.
    const double sixteenth = 15.0 / candidate.bpm;
    const double bar = 16.0 * sixteenth;
    double bestOrigin = phased.phase;
    float bestBarScore = -1.0f;
    float bestBackbeat = 0.0f;
    float tempoBarScore = -1.0f;
    const int slotStep = phased.swing < 0.03f ? 1 : 2;
    for (int slot = 0; slot < 16; slot += slotStep)
    {
        const double origin = phased.phase + slot * sixteenth;
        const float backbeat = backbeatScore(hits, candidate.bpm, origin);
        // A file whose first hit is right at its start is a trimmed loop: it starts on beat 1.
        float startBonus = 0.0f;
        if (trimmed)
        {
            double startOffset = std::fmod(context.firstHit - origin + bar * 64.0, bar);
            startOffset = std::min(startOffset, bar - startOffset);
            startBonus = startOffset < 0.035 ? 0.3f : 0.0f;
        }
        // The tempo score keeps the 2 & 4 reading (unchanged); beat 1 also accepts the half-time one.
        if (backbeat + startBonus > tempoBarScore)
        {
            tempoBarScore = backbeat + startBonus;
            bestBackbeat = backbeat;
        }
        const float barScore = phaseBackbeatScore(hits, candidate.bpm, origin) + startBonus;
        if (barScore > bestBarScore)
        {
            bestBarScore = barScore;
            bestOrigin = origin;
        }
    }

    // Latest downbeat not after the first hit (+30 ms tolerance for early pushes).
    double origin = bestOrigin;
    while (origin > context.firstHit + 0.030)
        origin -= bar;
    while (origin + bar <= context.firstHit + 0.030)
        origin += bar;
    originOut = origin;

    candidate.backbeat = bestBackbeat;
    const double octaves = std::log2(candidate.bpm / options.preferredBpm);
    candidate.prior = static_cast<float>(std::exp(-0.5 * (octaves / 0.45) * (octaves / 0.45)));
    if (options.hostBpm > 20.0 && std::abs(candidate.bpm / options.hostBpm - 1.0) < 0.006)
        candidate.host = 1.0f;

    candidate.score = 1.00f * candidate.gridFit
        + (trimmed ? 0.70f : 0.0f) * candidate.lengthFit
        + 0.60f * candidate.backbeat
        + 0.35f * candidate.prior
        + 0.08f * candidate.host
        - 0.50f * subdivisionOverload(hits, candidate.bpm);
    return candidate;
}

// Final tempo polish: a least-squares line through the strong kick / snare hits against their
// grid slots. The grid search stops at ~0.05% steps, which over 4-8 bars still drifts by 10-20
// ms; the regression removes that drift (and re-centres the origin).
void refineTempoLeastSquares(const std::vector<BreakDrumHit>& hits, double& bpm, double& origin, float swing)
{
    for (int pass = 0; pass < 2; ++pass)
    {
        const double sixteenth = 15.0 / bpm;
        double sumX = 0.0, sumY = 0.0, sumXX = 0.0, sumXY = 0.0;
        int count = 0;
        for (const auto& hit : hits)
        {
            if (hit.lane == TrackType::HiHat || hit.strength < 0.4f)
                continue;

            const double position = (hit.timeSeconds - origin) / sixteenth;
            double bestSlot = 0.0;
            double bestError = 1.0e9;
            for (int k = -1; k <= 1; ++k)
            {
                const double slot = std::round(position) + k;
                const bool odd = (static_cast<long long>(slot) % 2 + 2) % 2 == 1;
                const double error = std::abs(position - (slot + (odd ? swing : 0.0)));
                if (error < bestError)
                {
                    bestError = error;
                    bestSlot = slot;
                }
            }
            if (bestError * sixteenth > 0.030)
                continue; // off-grid hit (flam, fill): not a tempo witness

            const bool odd = (static_cast<long long>(bestSlot) % 2 + 2) % 2 == 1;
            const double x = bestSlot + (odd ? swing : 0.0);
            sumX += x;
            sumY += hit.timeSeconds;
            sumXX += x * x;
            sumXY += x * hit.timeSeconds;
            ++count;
        }

        const double denominator = count * sumXX - sumX * sumX;
        if (count < 6 || std::abs(denominator) < 1.0e-9)
            return;

        const double slope = (count * sumXY - sumX * sumY) / denominator; // seconds per sixteenth
        const double fittedBpm = 15.0 / slope;
        if (!(slope > 0.0) || std::abs(fittedBpm / bpm - 1.0) > 0.015)
            return;

        const double intercept = (sumY - slope * sumX) / count;
        bpm = fittedBpm;
        origin = intercept;
    }
}

float computeSustainRatio(const Matrix& spectrogram)
{
    if (spectrogram.empty())
        return 0.0f;

    const size_t bands = spectrogram.front().size();
    double floorSum = 0.0;
    double meanSum = 0.0;
    std::vector<float> column(spectrogram.size());
    for (size_t band = 0; band < bands; ++band)
    {
        double mean = 0.0;
        for (size_t frame = 0; frame < spectrogram.size(); ++frame)
        {
            column[frame] = spectrogram[frame][band];
            mean += column[frame];
        }
        mean /= static_cast<double>(spectrogram.size());
        floorSum += percentile(column, 0.25f);
        meanSum += mean;
    }
    return meanSum > 0.0 ? static_cast<float>(floorSum / meanSum) : 0.0f;
}

// A hat played together with a snare is spectrally buried in the snare's own noise. When the
// hats form a regular stream around that snare/kick and the onset still gained enough energy
// above 6 kHz, the hat is restored (flagged as inferred).
void completeMaskedHats(DrumBreakAnalysis& analysis)
{
    const int loopTicks = analysis.bars * TimingGrid::TicksPerBar4_4;
    if (loopTicks <= 0)
        return;

    std::vector<float> soloHatHigh;
    std::vector<int> hatTicks;
    for (const auto& hit : analysis.hits)
    {
        if (hit.lane != TrackType::HiHat)
            continue;
        hatTicks.push_back(hit.gridTick);
        const bool shared = std::any_of(analysis.hits.begin(), analysis.hits.end(), [&](const BreakDrumHit& other)
        {
            return other.lane != TrackType::HiHat && other.onsetIndex == hit.onsetIndex;
        });
        if (!shared && hit.onsetIndex >= 0 && hit.onsetIndex < static_cast<int>(analysis.onsetHighLevels.size()))
            soloHatHigh.push_back(analysis.onsetHighLevels[static_cast<size_t>(hit.onsetIndex)]);
    }
    if (soloHatHigh.size() < 3)
        return;

    const float typicalHatHigh = percentile(soloHatHigh, 0.5f);
    auto hatAt = [&](int tick)
    {
        const int wrapped = ((tick % loopTicks) + loopTicks) % loopTicks;
        return std::find(hatTicks.begin(), hatTicks.end(), wrapped) != hatTicks.end();
    };
    auto hatVelocityNear = [&](int tick)
    {
        int sum = 0;
        int count = 0;
        for (const auto& hit : analysis.hits)
        {
            if (hit.lane != TrackType::HiHat)
                continue;
            const int distance = std::abs(hit.gridTick - tick);
            if (std::min(distance, loopTicks - distance) <= TimingGrid::Eighth)
            {
                sum += hit.velocity;
                ++count;
            }
        }
        return count > 0 ? sum / count : 90;
    };

    std::vector<BreakDrumHit> added;
    for (const auto& hit : analysis.hits)
    {
        if (hit.lane == TrackType::HiHat || hit.onsetIndex < 0 || hatAt(hit.gridTick))
            continue;
        if (std::any_of(added.begin(), added.end(), [&](const BreakDrumHit& other) { return other.gridTick == hit.gridTick; }))
            continue;

        bool streamExpectsHat = false;
        for (const int spacing : { TimingGrid::Sixteenth, TimingGrid::Eighth })
        {
            if (hit.gridTick % spacing != 0)
                continue;
            if (hatAt(hit.gridTick - spacing) && hatAt(hit.gridTick + spacing))
                streamExpectsHat = true;
        }
        if (!streamExpectsHat)
            continue;

        const float high = analysis.onsetHighLevels[static_cast<size_t>(hit.onsetIndex)];
        if (high < 0.3f * typicalHatHigh)
            continue;

        BreakDrumHit hat = hit;
        hat.lane = TrackType::HiHat;
        hat.velocity = hatVelocityNear(hit.gridTick);
        hat.strength = static_cast<float>(hat.velocity) / 127.0f;
        hat.confidence = juce::jlimit(0.2f, 0.6f, 0.3f * high / std::max(1.0e-6f, typicalHatHigh));
        hat.activation = 0.0f;
        hat.inferred = true;
        added.push_back(hat);
    }

    analysis.hits.insert(analysis.hits.end(), added.begin(), added.end());
    std::sort(analysis.hits.begin(), analysis.hits.end(), [](const BreakDrumHit& left, const BreakDrumHit& right)
    {
        if (left.gridTick != right.gridTick)
            return left.gridTick < right.gridTick;
        return static_cast<int>(left.lane) < static_cast<int>(right.lane);
    });
}
} // namespace

int DrumBreakAnalysis::countLane(TrackType lane) const
{
    return static_cast<int>(std::count_if(hits.begin(), hits.end(), [lane](const BreakDrumHit& hit) { return hit.lane == lane; }));
}

juce::String DrumBreakAnalysis::describe(bool includeHits) const
{
    juce::StringArray lines;
    if (!valid)
        return "Drum break analysis: invalid";

    lines.add("Drum break: bpm " + juce::String(bpm, 2)
              + (gridBpm > 20.0 ? " (played at " + juce::String(gridBpm, 2) + ")" : juce::String())
              + " | bars " + juce::String(bars)
              + " | tempo conf " + juce::String(tempoConfidence, 2)
              + " | swing " + juce::String(swingPercent, 1) + "%"
              + " | origin " + juce::String(originSeconds * 1000.0, 1) + " ms"
              + (exactLoop ? " | exact loop" : " | free break")
              + " | length " + juce::String(durationSeconds, 3) + " s");
    lines.add("Drum loop confidence " + juce::String(drumLoopConfidence, 2)
              + " (sustain " + juce::String(sustainRatio, 2)
              + ", template fit " + juce::String(templateFit, 2) + ")"
              + " | onsets " + juce::String(static_cast<int>(onsetTimes.size()))
              + " | K " + juce::String(countLane(TrackType::Kick))
              + " S " + juce::String(countLane(TrackType::Snare))
              + " H " + juce::String(countLane(TrackType::HiHat)));

    juce::String candidatesLine = "Tempo candidates:";
    for (const auto& candidate : tempoCandidates)
    {
        candidatesLine << " [" << juce::String(candidate.bpm, 2) << " bpm/" << candidate.bars << "bar"
                       << " score " << juce::String(candidate.score, 2)
                       << " grid " << juce::String(candidate.gridFit, 2)
                       << " len " << juce::String(candidate.lengthFit, 2)
                       << " bb " << juce::String(candidate.backbeat, 2)
                       << (candidate.host > 0.0f ? " host" : "") << "]";
    }
    lines.add(candidatesLine);

    if (includeHits)
    {
        for (const auto& hit : hits)
        {
            const int bar = hit.tick / TimingGrid::TicksPerBar4_4;
            const double sixteenth = static_cast<double>(hit.tick % TimingGrid::TicksPerBar4_4) / TimingGrid::Sixteenth;
            lines.add(juce::String(toString(hit.lane)).paddedRight(' ', 6)
                      + " t=" + juce::String(hit.timeSeconds, 4)
                      + " bar " + juce::String(bar + 1)
                      + " 16th " + juce::String(sixteenth + 1.0, 2)
                      + " grid " + juce::String(hit.gridTick)
                      + " off " + juce::String(hit.timingOffsetTicks)
                      + " vel " + juce::String(hit.velocity)
                      + " conf " + juce::String(hit.confidence, 2));
        }
    }

    return lines.joinIntoString("\n");
}

DrumBreakAnalysis DrumBreakTranscriber::analyze(const std::vector<float>& monoInput,
                                                double sampleRate,
                                                const DrumBreakOptions& options) const
{
    DrumBreakAnalysis analysis;
    analysis.sampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    analysis.durationSeconds = static_cast<double>(monoInput.size()) / analysis.sampleRate;
    if (monoInput.size() < static_cast<size_t>(kFrameSize) || analysis.durationSeconds < 0.4)
        return analysis;

    // DC removal only; relative dynamics are kept intact.
    std::vector<float> mono(monoInput);
    const double mean = std::accumulate(mono.begin(), mono.end(), 0.0) / static_cast<double>(mono.size());
    float peak = 0.0f;
    for (auto& sample : mono)
    {
        sample -= static_cast<float>(mean);
        peak = std::max(peak, std::abs(sample));
    }
    if (peak < 1.0e-5f)
        return analysis;

    // Leading silence so a hit at 0.000 s (every trimmed loop starts with one) still produces
    // an onset peak. Internal times are in the padded domain until the onsets are stored.
    const int padSamples = kFrameSize * 2;
    const double padSeconds = static_cast<double>(padSamples) / analysis.sampleRate;
    mono.insert(mono.begin(), static_cast<size_t>(padSamples), 0.0f);

    const auto layout = makeBands(analysis.sampleRate);
    const auto spectrogram = computeBandSpectrogram(mono, layout);
    const int frameCount = static_cast<int>(spectrogram.size());
    const auto odf = computeOnsetFunction(spectrogram);
    const auto onsetFrames = pickOnsetFrames(odf);

    // 1. onset times on the waveform, merged when closer than 15 ms
    struct Onset { double time; float odf; };
    std::vector<Onset> onsets;
    for (const int frame : onsetFrames)
    {
        const double refined = refineOnsetTime(mono, analysis.sampleRate, frameTime(frame, analysis.sampleRate));
        const float strength = odf[static_cast<size_t>(frame)];
        if (!onsets.empty() && refined - onsets.back().time < 0.015)
        {
            if (strength > onsets.back().odf)
                onsets.back() = { refined, strength };
            continue;
        }
        onsets.push_back({ refined, strength });
    }

    // 2. per-onset spectral increase: after-attack spectrum minus before-attack spectrum
    const int bands = layout.size();
    Matrix columns;
    columns.reserve(onsets.size());
    for (const auto& onset : onsets)
    {
        const int preFrame = timeToFrame(onset.time - 0.030, analysis.sampleRate, frameCount);
        const int postStart = timeToFrame(onset.time + 0.018, analysis.sampleRate, frameCount);
        const int postEnd = timeToFrame(onset.time + 0.032, analysis.sampleRate, frameCount);
        std::vector<float> column(static_cast<size_t>(bands), 0.0f);
        for (int band = 0; band < bands; ++band)
        {
            float post = 0.0f;
            for (int frame = postStart; frame <= postEnd; ++frame)
                post = std::max(post, spectrogram[static_cast<size_t>(frame)][static_cast<size_t>(band)]);
            const float pre = onset.time - 0.030 >= 0.0 ? spectrogram[static_cast<size_t>(preFrame)][static_cast<size_t>(band)] : 0.0f;
            column[static_cast<size_t>(band)] = std::max(0.0f, post - pre);
        }
        float high = 0.0f;
        for (int band = 0; band < bands; ++band)
            if (layout.centerHz[static_cast<size_t>(band)] > 6000.0f)
                high += column[static_cast<size_t>(band)];
        analysis.onsetHighLevels.push_back(high);
        columns.push_back(std::move(column));
        analysis.onsetTimes.push_back(std::max(0.0, onset.time - padSeconds));
    }

    // 3. NMF lane activations
    const auto firstPass = runNmf(columns, layout);
    const auto exemplarTemplates = learnExemplarTemplates(columns, firstPass);
    const auto nmf = runNmf(columns, layout, &exemplarTemplates);
    analysis.templateFit = nmf.fit;
    std::array<float, kLaneCount> reference {};
    for (int lane = 0; lane < kLaneCount; ++lane)
    {
        std::vector<float> values;
        for (const auto& activation : nmf.activations)
            values.push_back(activation[static_cast<size_t>(lane)]);
        reference[static_cast<size_t>(lane)] = std::max(1.0e-9f, percentile(values, 0.98f));
    }

    for (size_t column = 0; column < nmf.activations.size(); ++column)
    {
        std::array<float, 3> levels {};
        for (int lane = 0; lane < kLaneCount; ++lane)
            levels[static_cast<size_t>(lane)] = nmf.activations[column][static_cast<size_t>(lane)] / reference[static_cast<size_t>(lane)];
        analysis.onsetLaneLevels.push_back(levels);
    }

    // Cross-lane leakage: with one kit repeated through the loop, a drum that partly matches
    // another lane's template leaks a near-constant fraction into it (e.g. every kick also
    // yields snare 0.15). The median ratio over onsets dominated by the source lane measures
    // that fraction; it is subtracted before thresholding so ghost notes survive but leaks don't.
    std::array<std::array<float, kLaneCount>, kLaneCount> leak {};
    for (int source = 0; source < kLaneCount; ++source)
    {
        // Hats genuinely sound together with most kicks and snares, so a "leak" into the hat
        // lane cannot be told apart from real co-occurrence: only kick/snare targets are corrected.
        for (int target = 0; target < 2; ++target)
        {
            if (source == target)
                continue;
            std::vector<float> ratios;
            for (const auto& levels : analysis.onsetLaneLevels)
                if (levels[static_cast<size_t>(source)] >= 0.5f)
                    ratios.push_back(levels[static_cast<size_t>(target)] / levels[static_cast<size_t>(source)]);
            if (ratios.size() >= 3)
                leak[static_cast<size_t>(source)][static_cast<size_t>(target)] = std::min(0.5f, percentile(ratios, 0.5f));
        }
    }

    for (size_t column = 0; column < analysis.onsetLaneLevels.size(); ++column)
    {
        const auto& levels = analysis.onsetLaneLevels[column];
        for (int lane = 0; lane < kLaneCount; ++lane)
        {
            const float activation = nmf.activations[column][static_cast<size_t>(lane)];
            float expectedLeak = 0.0f;
            for (int source = 0; source < kLaneCount; ++source)
                if (source != lane)
                    expectedLeak += leak[static_cast<size_t>(source)][static_cast<size_t>(lane)] * levels[static_cast<size_t>(source)];
            const float relative = levels[static_cast<size_t>(lane)] - 1.2f * expectedLeak;
            // A kick + hat onset leaks broadband mid energy into the snare template. A snare in
            // unison with a strong kick is played loud, so quiet "snares" there are rejected.
            float threshold = kLaneThreshold[static_cast<size_t>(lane)];
            if (lane == 1 && levels[0] >= 0.5f)
                threshold = std::max(threshold, 0.35f);
            if (relative < threshold)
                continue;

            BreakDrumHit hit;
            hit.lane = kLanes[static_cast<size_t>(lane)];
            hit.timeSeconds = analysis.onsetTimes[column];
            hit.onsetIndex = static_cast<int>(column);
            hit.activation = activation;
            hit.strength = std::min(1.0f, relative);
            hit.confidence = juce::jlimit(0.0f, 1.0f, (relative - threshold) / 0.4f);
            const float db = 20.0f * std::log10(std::max(1.0e-4f, relative));
            hit.velocity = juce::jlimit(20, 127, static_cast<int>(std::lround(127.0f + 2.5f * db)));
            analysis.hits.push_back(hit);
        }
    }

    if (analysis.hits.empty())
        return analysis;

    // 4. context: leading silence, first hits
    BreakContext context;
    context.durationSeconds = analysis.durationSeconds;
    context.firstHit = analysis.hits.front().timeSeconds;
    if (context.firstHit > 0.004 && context.firstHit < 0.25)
    {
        const int end = static_cast<int>(context.firstHit * analysis.sampleRate);
        float leadPeak = 0.0f;
        for (int i = padSamples; i < padSamples + end; ++i)
            leadPeak = std::max(leadPeak, std::abs(mono[static_cast<size_t>(i)]));
        if (leadPeak < peak * 0.03f)
            context.leadSilenceEnd = context.firstHit;
    }
    const double loopSeconds = analysis.durationSeconds - context.leadSilenceEnd;

    // 5. tempo
    std::vector<double> seeds;
    if (options.forcedBpm > 20.0)
    {
        seeds.push_back(options.forcedBpm);
    }
    else
    {
        for (const int bars : { 1, 2, 3, 4, 6, 8, 12, 16 })
        {
            const double bpm = 240.0 * bars / loopSeconds;
            if (bpm >= options.minBpm && bpm <= options.maxBpm)
                seeds.push_back(bpm);
        }

        // Grid-fit peaks, independent of the loop length (for untrimmed breaks).
        std::vector<std::pair<float, double>> scan;
        for (double bpm = options.minBpm; bpm <= options.maxBpm; bpm += 0.25)
            scan.emplace_back(bestPhasedFit(analysis.hits, bpm, false).fit, bpm);
        std::vector<std::pair<float, double>> peaks;
        for (size_t i = 1; i + 1 < scan.size(); ++i)
            if (scan[i].first >= scan[i - 1].first && scan[i].first >= scan[i + 1].first)
                peaks.push_back(scan[i]);
        std::sort(peaks.begin(), peaks.end(), [](const auto& left, const auto& right) { return left.first > right.first; });
        for (size_t i = 0; i < std::min<size_t>(8, peaks.size()); ++i)
            seeds.push_back(peaks[i].second);

        // Octave / triple relatives of every seed: which one is right is decided by the full
        // score (backbeat, length, prior), not by the grid fit alone.
        const auto baseSeeds = seeds;
        for (const double seed : baseSeeds)
            for (const double factor : { 0.5, 2.0, 2.0 / 3.0, 1.5 })
                if (seed * factor >= options.minBpm && seed * factor <= options.maxBpm)
                    seeds.push_back(seed * factor);

        if (options.hostBpm > 20.0)
            for (const double factor : { 0.5, 1.0, 2.0 })
                if (options.hostBpm * factor >= options.minBpm && options.hostBpm * factor <= options.maxBpm)
                    seeds.push_back(options.hostBpm * factor);
    }

    std::vector<std::pair<BreakTempoCandidate, double>> evaluated; // candidate + its origin
    std::vector<double> triedSeeds;
    for (const double seed : seeds)
    {
        const bool duplicateSeed = std::any_of(triedSeeds.begin(), triedSeeds.end(), [&](double other)
        {
            return std::abs(other / seed - 1.0) < 0.003;
        });
        if (duplicateSeed)
            continue;
        triedSeeds.push_back(seed);

        double origin = 0.0;
        auto candidate = evaluateCandidate(analysis.hits, seed, context, options, options.forcedBpm <= 20.0, origin);
        auto existing = std::find_if(evaluated.begin(), evaluated.end(), [&](const auto& other)
        {
            return std::abs(other.first.bpm / candidate.bpm - 1.0) < 0.004;
        });
        if (existing == evaluated.end())
            evaluated.emplace_back(candidate, origin);
        else if (candidate.score > existing->first.score)
            *existing = { candidate, origin };
    }

    std::sort(evaluated.begin(), evaluated.end(), [](const auto& left, const auto& right)
    {
        return left.first.score > right.first.score;
    });
    for (size_t i = 0; i < std::min<size_t>(6, evaluated.size()); ++i)
        analysis.tempoCandidates.push_back(evaluated[i].first);

    const auto& best = analysis.tempoCandidates.front();
    analysis.bpm = best.bpm;
    analysis.originSeconds = evaluated.front().second;
    analysis.bpmMatchesHost = best.host > 0.0f;
    analysis.swingPercent = 50.0f * (1.0f + best.swing);
    const float runnerUp = analysis.tempoCandidates.size() > 1 ? analysis.tempoCandidates[1].score : 0.0f;
    // A handful of events (chord changes in a pad, a lone stab) fit almost any grid: scale the
    // confidence by how much rhythmic evidence there is (full credit from 16 hits on).
    const float evidence = juce::jlimit(0.25f, 1.0f, static_cast<float>(analysis.hits.size()) / 16.0f);
    analysis.tempoConfidence = evidence * juce::jlimit(0.0f, 1.0f, 0.5f * best.gridFit + 0.2f * best.backbeat + 0.15f * best.lengthFit + std::min(0.3f, best.score - runnerUp));

    // An exactly trimmed loop: whole number of bars, starting on its first hit.
    analysis.exactLoop = best.lengthFit >= 0.5f
        && std::abs(analysis.originSeconds - context.leadSilenceEnd) < 0.035;

    // A trimmed loop's length already pins the tempo exactly (and must keep looping seamlessly):
    // snap to the whole-bar tempo (the grid search alone lands a few tenths of a BPM off, which
    // drifts by tens of ms over 4 bars) and count the length as strong evidence. Free breaks get
    // the regression polish.
    // A loop file (starts right on a hit) whose length is a whole number of bars at the detected
    // tempo is a loop even when its beat 1 is not at the start (pickup / anacrusis).
    const bool startsOnHit = context.firstHit - context.leadSilenceEnd < 0.050 && context.firstHit < 0.3;
    const bool wholeBarLoop = analysis.exactLoop || (startsOnHit && best.lengthFit >= 0.5f);
    if (wholeBarLoop && options.forcedBpm <= 20.0 && best.bars > 0 && loopSeconds > 0.0)
    {
        const double loopBpm = 240.0 * best.bars / loopSeconds;
        if (std::abs(loopBpm / analysis.bpm - 1.0) < 0.02)
        {
            const double scale = analysis.bpm / loopBpm;
            analysis.originSeconds = context.leadSilenceEnd + (analysis.originSeconds - context.leadSilenceEnd) * scale;
            analysis.bpm = loopBpm;
            analysis.tempoConfidence = std::max(analysis.tempoConfidence, evidence * juce::jlimit(0.0f, 0.9f, 0.45f + 0.5f * best.gridFit));
        }
    }
    else if (options.forcedBpm <= 20.0)
        refineTempoLeastSquares(analysis.hits, analysis.bpm, analysis.originSeconds, best.swing);
    if (options.forcedBpm > 20.0)
        analysis.tempoConfidence = 1.0f; // typed by the user

    // Loops and samples are made at whole BPM; an estimate a few hundredths / tenths off (86.94,
    // a file a few samples longer than its bars) shows up as 85.9 / 173.9. Snap to the whole BPM
    // when it is close and the hits fit its grid as well.
    if (options.forcedBpm <= 20.0)
    {
        const double whole = std::round(analysis.bpm);
        if (std::abs(whole - analysis.bpm) <= 0.25 && whole > 20.0)
        {
            const float fitHere = bestPhasedFit(analysis.hits, analysis.bpm, true).fit;
            const float fitWhole = bestPhasedFit(analysis.hits, whole, true).fit;
            if (fitWhole >= fitHere * 0.97f)
            {
                analysis.originSeconds = context.leadSilenceEnd + (analysis.originSeconds - context.leadSilenceEnd) * (analysis.bpm / whole);
                analysis.bpm = whole;
            }
        }
    }

    if (options.forcedBpm > 20.0)
    {
        // A typed tempo states the loop's tempo; the playing may still sit a little off it (a
        // 79.4 BPM performance typed as 80). Read the hits' slots on the tempo they really fit
        // within +-1.5 %, keep their real times at the typed tempo (assignTicks).
        const float typedFit = bestPhasedFit(analysis.hits, analysis.bpm, true).fit;
        double playedBpm = analysis.bpm;
        float playedFit = typedFit;
        for (double delta = -0.015; delta <= 0.01501; delta += 0.0005)
        {
            const double trial = analysis.bpm * (1.0 + delta);
            const float fit = bestPhasedFit(analysis.hits, trial, false).fit;
            if (fit > playedFit)
            {
                playedFit = fit;
                playedBpm = trial;
            }
        }
        if (playedFit >= typedFit + 0.10f)
            analysis.gridBpm = playedBpm;

        // A loop file that starts right on a hit starts on beat 1 (the user's tempo + the file
        // start are the intent), unless that reading breaks the backbeat. Without this a
        // symmetric groove (K S K S) could be read from beat 3 with half a bar of silence.
        if (startsOnHit)
        {
            const double readBpm = analysis.gridBpm > 20.0 ? analysis.gridBpm : analysis.bpm;
            double searched = 0.0;
            evaluateCandidate(analysis.hits, readBpm, context, options, false, searched);
            const double atStart = context.firstHit;
            if (phaseBackbeatScore(analysis.hits, readBpm, atStart) >= phaseBackbeatScore(analysis.hits, readBpm, searched) - 0.35f)
                analysis.originSeconds = atStart;
            else
                analysis.originSeconds = searched;
        }
    }
    else if (startsOnHit && loopSeconds > 0.0 && std::abs(analysis.bpm - std::round(analysis.bpm)) > 0.1)
    {
        // (Only when the measured tempo is not whole itself: a whole measured tempo with a file a
        // little longer than its bars - a tail - must not be pulled to the neighbouring BPM.)
        // A loop rendered from a DAW is a whole number of bars at a whole tempo: its length says
        // 79.95 -> 80 while the hits may be played at 79.4. State the whole tempo (the session's),
        // read the slots on the played tempo; the drift stays in the timing offsets.
        double bestWhole = 0.0;
        for (const int bars : { 1, 2, 3, 4, 6, 8, 12, 16 })
        {
            const double lengthBpm = 240.0 * bars / loopSeconds;
            const double whole = std::round(lengthBpm);
            const double off = std::abs(whole / analysis.bpm - 1.0);
            if (std::abs(lengthBpm - whole) <= 0.08 && off > 0.001 && off <= 0.012
                && (bestWhole <= 0.0 || off < std::abs(bestWhole / analysis.bpm - 1.0)))
                bestWhole = whole;
        }
        if (bestWhole > 20.0)
        {
            analysis.gridBpm = analysis.bpm;
            analysis.bpm = bestWhole;
        }
    }

    if (wholeBarLoop && best.bars > 0)
    {
        analysis.bars = best.bars;
    }
    else
    {
        double lastHit = 0.0;
        for (const auto& hit : analysis.hits)
            lastHit = std::max(lastHit, hit.timeSeconds);
        analysis.bars = static_cast<int>(std::ceil((lastHit - analysis.originSeconds) * analysis.bpm / 240.0 + 0.01));
    }
    analysis.bars = juce::jlimit(1, 16, analysis.bars);

    // 6. drum-loop confidence (percussive, well explained by K/S/H, rhythmically tight)
    analysis.sustainRatio = computeSustainRatio(spectrogram);
    const float percussive = juce::jlimit(0.0f, 1.0f, 1.0f - 2.2f * analysis.sustainRatio);
    analysis.drumLoopConfidence = juce::jlimit(0.0f, 1.0f, 0.45f * percussive + 0.35f * analysis.templateFit + 0.20f * best.gridFit);

    assignTicks(analysis, options.quantizeAmount);
    completeMaskedHats(analysis);
    analysis.valid = !analysis.hits.empty();
    return analysis;
}

void DrumBreakTranscriber::assignTicks(DrumBreakAnalysis& analysis, float quantizeAmount)
{
    if (analysis.bpm <= 0.0 || analysis.bars <= 0)
        return;

    const int loopTicks = analysis.bars * TimingGrid::TicksPerBar4_4;
    const double ticksPerSecond = analysis.bpm / 60.0 * TimingGrid::PPQ;
    // Slots are read on the tempo the hits were played at (gridBpm), times stay at bpm.
    const double slotTicksPerSecond = (analysis.gridBpm > 20.0 ? analysis.gridBpm : analysis.bpm) / 60.0 * TimingGrid::PPQ;
    const float keep = 1.0f - juce::jlimit(0.0f, 1.0f, quantizeAmount);

    std::vector<BreakDrumHit> placed;
    placed.reserve(analysis.hits.size());
    for (auto hit : analysis.hits)
    {
        int tick = static_cast<int>(std::lround((hit.timeSeconds - analysis.originSeconds) * ticksPerSecond));

        // In an exact loop, hits just before the loop end belong to the next downbeat (pushed
        // early): wrap them. Anything past the analysed bars is dropped.
        if (analysis.exactLoop && tick >= loopTicks - 45 && tick < loopTicks + 60)
            tick -= loopTicks;
        if (tick >= loopTicks)
            continue;
        if (tick < -60)
            continue;

        int slotTick = static_cast<int>(std::lround((hit.timeSeconds - analysis.originSeconds) * slotTicksPerSecond));
        if (analysis.exactLoop && slotTick >= loopTicks - 45 && slotTick < loopTicks + 60)
            slotTick -= loopTicks;
        const int nearestSixteenth = static_cast<int>(std::lround(static_cast<double>(slotTick) / TimingGrid::Sixteenth)) * TimingGrid::Sixteenth;
        int gridTick = nearestSixteenth;
        if (std::abs(slotTick - nearestSixteenth) > 100)
            gridTick = static_cast<int>(std::lround(static_cast<double>(slotTick) / TimingGrid::ThirtySecond)) * TimingGrid::ThirtySecond;
        gridTick = juce::jlimit(0, loopTicks - TimingGrid::ThirtySecond, gridTick);

        hit.tick = tick;
        hit.gridTick = gridTick;
        hit.timingOffsetTicks = static_cast<int>(std::lround(static_cast<float>(tick - gridTick) * keep));

        const bool duplicate = std::any_of(placed.begin(), placed.end(), [&](const BreakDrumHit& other)
        {
            return other.lane == hit.lane && other.gridTick == hit.gridTick;
        });
        if (duplicate)
            continue;

        placed.push_back(hit);
    }

    std::sort(placed.begin(), placed.end(), [](const BreakDrumHit& left, const BreakDrumHit& right)
    {
        if (left.gridTick != right.gridTick)
            return left.gridTick < right.gridTick;
        return static_cast<int>(left.lane) < static_cast<int>(right.lane);
    });
    analysis.hits = std::move(placed);
}
} // namespace bbg
