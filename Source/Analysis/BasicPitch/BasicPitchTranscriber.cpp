#include "BasicPitchTranscriber.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <thread>

#if defined(_M_X64) || defined(__x86_64__) || defined(__SSE2__)
 #include <emmintrin.h>
 #define HPDG_BASIC_PITCH_SSE 1
#endif

#include "BasicPitchWeights.h"

namespace bbg
{
namespace
{
namespace bp = basic_pitch;

// basic_pitch.constants / models
constexpr int kHop = 256;
constexpr int kWindowSamples = 22050 * 2 - kHop;            // AUDIO_N_SAMPLES, 43844
constexpr int kWindowFrames = 172;                          // model output frames per window
constexpr int kOverlapFrames = 30;
constexpr int kOverlapSamples = kOverlapFrames * kHop;      // 7680
constexpr int kWindowHop = kWindowSamples - kOverlapSamples; // 36164
constexpr int kBinsPerOctave = 36;
constexpr int kOctaves = 9;
constexpr int kCqtBins = 309;
constexpr int kKernel = 256;
constexpr int kStackBins = BasicPitchFrames::kContours; // 264
constexpr int kHarmonics = 8;
constexpr float kBatchNormEpsilon = 1.0e-3f; // keras default

// One BatchNormalization as scale / shift per channel.
struct Norm
{
    std::vector<float> scale, shift;
};

Norm makeNorm(const float* gamma, const float* beta, const float* mean, const float* variance, int channels)
{
    Norm n;
    for (int c = 0; c < channels; ++c)
    {
        const float s = gamma[c] / std::sqrt(variance[c] + kBatchNormEpsilon);
        n.scale.push_back(s);
        n.shift.push_back(beta[c] - mean[c] * s);
    }
    return n;
}

// The non-zero span of each top-octave CQT kernel (the rest are zeros: shorter windows for higher bins).
struct KernelSpan
{
    int begin = 0, end = 0;
};

struct Model
{
    std::array<KernelSpan, kBinsPerOctave> spans {};
    std::array<float, kCqtBins> lengthScale {};
    Norm norm0, norm2, norm3;

    Model()
    {
        for (int k = 0; k < kBinsPerOctave; ++k)
        {
            int begin = kKernel, end = 0;
            for (int n = 0; n < kKernel; ++n)
                if (bp::kCqtKernelReal[k * kKernel + n] != 0.0f || bp::kCqtKernelImag[k * kKernel + n] != 0.0f)
                {
                    begin = std::min(begin, n);
                    end = n + 1;
                }
            spans[static_cast<size_t>(k)] = { begin, std::max(begin, end) };
        }
        for (int b = 0; b < kCqtBins; ++b)
            lengthScale[static_cast<size_t>(b)] = std::sqrt(bp::kCqtLengths[b]);
        norm0 = makeNorm(bp::k_batch_normalization_gamma, bp::k_batch_normalization_beta,
                         bp::k_batch_normalization_moving_mean, bp::k_batch_normalization_moving_variance, 1);
        norm2 = makeNorm(bp::k_batch_normalization_2_gamma, bp::k_batch_normalization_2_beta,
                         bp::k_batch_normalization_2_moving_mean, bp::k_batch_normalization_2_moving_variance, 8);
        norm3 = makeNorm(bp::k_batch_normalization_3_gamma, bp::k_batch_normalization_3_beta,
                         bp::k_batch_normalization_3_moving_mean, bp::k_batch_normalization_3_moving_variance, 32);
    }
};

const Model& model()
{
    static const Model m;
    return m;
}

// sum x[n] * w[n]
inline float dot(const float* x, const float* w, int count)
{
    int n = 0;
    float sum = 0.0f;
#if HPDG_BASIC_PITCH_SSE
    __m128 a = _mm_setzero_ps(), b = _mm_setzero_ps();
    for (; n + 8 <= count; n += 8)
    {
        a = _mm_add_ps(a, _mm_mul_ps(_mm_loadu_ps(x + n), _mm_loadu_ps(w + n)));
        b = _mm_add_ps(b, _mm_mul_ps(_mm_loadu_ps(x + n + 4), _mm_loadu_ps(w + n + 4)));
    }
    alignas(16) float lanes[4];
    _mm_store_ps(lanes, _mm_add_ps(a, b));
    sum = (lanes[0] + lanes[1]) + (lanes[2] + lanes[3]);
#endif
    for (; n < count; ++n)
        sum += x[n] * w[n];
    return sum;
}

// acc[o] += sum x[n] * w[n * Out + o]
template <int Out>
inline void multiplyAdd(const float* x, const float* w, int count, float* acc)
{
#if HPDG_BASIC_PITCH_SSE
    static_assert(Out % 4 == 0);
    constexpr int kLanes = Out / 4;
    __m128 even[kLanes], odd[kLanes];
    for (int q = 0; q < kLanes; ++q)
    {
        even[q] = _mm_loadu_ps(acc + 4 * q);
        odd[q] = _mm_setzero_ps();
    }
    int n = 0;
    for (; n + 2 <= count; n += 2)
    {
        const __m128 x0 = _mm_set1_ps(x[n]), x1 = _mm_set1_ps(x[n + 1]);
        const float* w0 = w + n * Out;
        for (int q = 0; q < kLanes; ++q)
        {
            even[q] = _mm_add_ps(even[q], _mm_mul_ps(x0, _mm_loadu_ps(w0 + 4 * q)));
            odd[q] = _mm_add_ps(odd[q], _mm_mul_ps(x1, _mm_loadu_ps(w0 + Out + 4 * q)));
        }
    }
    if (n < count)
    {
        const __m128 x0 = _mm_set1_ps(x[n]);
        for (int q = 0; q < kLanes; ++q)
            even[q] = _mm_add_ps(even[q], _mm_mul_ps(x0, _mm_loadu_ps(w + n * Out + 4 * q)));
    }
    for (int q = 0; q < kLanes; ++q)
        _mm_storeu_ps(acc + 4 * q, _mm_add_ps(even[q], odd[q]));
#else
    for (int n = 0; n < count; ++n)
        for (int o = 0; o < Out; ++o)
            acc[o] += x[n] * w[n * Out + o];
#endif
}

// nnAudio downsampling_by_n (match_torch_exactly): zero pad 127 each side, 256-tap filter, stride 2.
std::vector<float> downsample(const std::vector<float>& x)
{
    const int length = static_cast<int>(x.size());
    const int pad = (kKernel - 1) / 2;
    const int out = (length + 2 * pad - kKernel) / 2 + 1;
    std::vector<float> y(static_cast<size_t>(std::max(0, out)));
    for (int n = 0; n < out; ++n)
    {
        const int first = 2 * n - pad;
        const int k0 = std::max(0, -first), k1 = std::min(kKernel, length - first);
        y[static_cast<size_t>(n)] = dot(x.data() + first + k0, bp::kLowpass + k0, k1 - k0);
    }
    return y;
}

// One octave of nnAudio get_cqt_complex: reflect pad 128, the 36 kernels at the given hop.
// Writes |CQT| (before the length scaling) into magnitudes[frame][firstBin + k], skipping bins < 0.
void cqtOctave(const std::vector<float>& x, int hop, int firstBin, std::vector<float>& magnitudes)
{
    const int length = static_cast<int>(x.size());
    const int pad = kKernel / 2;
    std::vector<float> padded(static_cast<size_t>(length + 2 * pad));
    for (int i = 0; i < static_cast<int>(padded.size()); ++i)
    {
        int p = i - pad;
        if (p < 0)
            p = -p;
        else if (p >= length)
            p = 2 * (length - 1) - p;
        padded[static_cast<size_t>(i)] = x[static_cast<size_t>(std::clamp(p, 0, length - 1))];
    }

    const auto& spans = model().spans;
    const int frames = std::min(kWindowFrames, length / hop + 1);
    for (int t = 0; t < frames; ++t)
    {
        const float* in = padded.data() + t * hop;
        for (int k = 0; k < kBinsPerOctave; ++k)
        {
            const int bin = firstBin + k;
            if (bin < 0)
                continue;
            const float* real = bp::kCqtKernelReal + k * kKernel;
            const float* imag = bp::kCqtKernelImag + k * kKernel;
            const auto& span = spans[static_cast<size_t>(k)];
            const float re = dot(in + span.begin, real + span.begin, span.end - span.begin);
            const float im = dot(in + span.begin, imag + span.begin, span.end - span.begin);
            magnitudes[static_cast<size_t>(t * kCqtBins + bin)] = std::sqrt(re * re + im * im);
        }
    }
}

// Conv2D, padding "same", over (time, frequency, channels-last) with a keras kernel
// (kh, kw, in, out); stride only along frequency. Within one kernel row the input taps (frequency x
// channels) and the kernel's are both contiguous, so each row is one flat multiply-add run.
template <int Out>
void convPosition(const float* in, int frames, int bins, int channels, const float* kernel, const float* bias,
                  int kh, int kw, int strideF, int outBins, int padT, int padF, int t, int f, float* out);

template <int Out>
void convRows(const float* in, int frames, int bins, int channels, const float* kernel, const float* bias,
              int kh, int kw, int strideF, int outBins, float* out)
{
    const int padT = (kh - 1) / 2;
    const int padF = std::max(0, (outBins - 1) * strideF + kw - bins) / 2;
    for (int t = 0; t < frames; ++t)
    {
        const int i0 = std::max(0, padT - t), i1 = std::min(kh, frames - t + padT);
        int f = 0;
#if HPDG_BASIC_PITCH_SSE
        if constexpr (Out == 8)
        {
            // 4 neighbouring positions at once share every kernel load (8 independent sums)
            while (f + 4 <= outBins)
            {
                const int origin = f * strideF - padF;
                if (origin < 0) // the left edge: one position at a time
                {
                    convPosition<Out>(in, frames, bins, channels, kernel, bias, kh, kw, strideF, outBins, padT, padF, t, f, out);
                    ++f;
                    continue;
                }
                if (origin + 3 * strideF + kw > bins) // the right edge
                    break;
                __m128 acc[4][2];
                for (int b = 0; b < 4; ++b)
                {
                    acc[b][0] = _mm_loadu_ps(bias);
                    acc[b][1] = _mm_loadu_ps(bias + 4);
                }
                const int taps = kw * channels;
                const int step = strideF * channels;
                for (int i = i0; i < i1; ++i)
                {
                    const float* x = in + static_cast<size_t>(((t + i - padT) * bins + origin) * channels);
                    const float* w = kernel + static_cast<size_t>(i * kw * channels * Out);
                    for (int n = 0; n < taps; ++n)
                    {
                        const __m128 w0 = _mm_loadu_ps(w + n * Out), w1 = _mm_loadu_ps(w + n * Out + 4);
                        for (int b = 0; b < 4; ++b)
                        {
                            const __m128 xv = _mm_set1_ps(x[n + b * step]);
                            acc[b][0] = _mm_add_ps(acc[b][0], _mm_mul_ps(xv, w0));
                            acc[b][1] = _mm_add_ps(acc[b][1], _mm_mul_ps(xv, w1));
                        }
                    }
                }
                for (int b = 0; b < 4; ++b)
                {
                    float* target = out + static_cast<size_t>((t * outBins + f + b) * Out);
                    _mm_storeu_ps(target, acc[b][0]);
                    _mm_storeu_ps(target + 4, acc[b][1]);
                }
                f += 4;
            }
        }
#endif
        for (; f < outBins; ++f)
            convPosition<Out>(in, frames, bins, channels, kernel, bias, kh, kw, strideF, outBins, padT, padF, t, f, out);
    }
}

// one output position (all channels) of convRows
template <int Out>
void convPosition(const float* in, int frames, int bins, int channels, const float* kernel, const float* bias,
                  int kh, int kw, int strideF, int outBins, int padT, int padF, int t, int f, float* out)
{
    const int i0 = std::max(0, padT - t), i1 = std::min(kh, frames - t + padT);
        {
            const int origin = f * strideF - padF;
            const int j0 = std::max(0, -origin), j1 = std::min(kw, bins - origin);
            const int taps = (j1 - j0) * channels;
            float acc[Out];
            for (int o = 0; o < Out; ++o)
                acc[o] = bias[o];
            for (int i = i0; i < i1; ++i)
            {
                const float* x = in + static_cast<size_t>(((t + i - padT) * bins + origin + j0) * channels);
                const float* w = kernel + static_cast<size_t>((i * kw + j0) * channels * Out);
                if constexpr (Out == 1)
                    acc[0] += dot(x, w, taps);
                else
                    multiplyAdd<Out>(x, w, taps, acc);
            }
            float* target = out + static_cast<size_t>((t * outBins + f) * Out);
            for (int o = 0; o < Out; ++o)
                target[o] = acc[o];
        }
}

std::vector<float> conv2d(const std::vector<float>& in, int frames, int bins, int channels,
                          const float* kernel, const float* bias, int kh, int kw, int outChannels,
                          int strideF, int& outBins)
{
    outBins = (bins + strideF - 1) / strideF;
    std::vector<float> out(static_cast<size_t>(frames * outBins * outChannels));
    switch (outChannels)
    {
        case 1: convRows<1>(in.data(), frames, bins, channels, kernel, bias, kh, kw, strideF, outBins, out.data()); break;
        case 8: convRows<8>(in.data(), frames, bins, channels, kernel, bias, kh, kw, strideF, outBins, out.data()); break;
        case 32: convRows<32>(in.data(), frames, bins, channels, kernel, bias, kh, kw, strideF, outBins, out.data()); break;
        default: break; // the model has no other widths
    }
    return out;
}

void normRelu(std::vector<float>& x, const Norm& norm)
{
    const size_t channels = norm.scale.size();
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = std::max(0.0f, x[i] * norm.scale[i % channels] + norm.shift[i % channels]);
}

void sigmoid(std::vector<float>& x)
{
    for (auto& v : x)
        v = 1.0f / (1.0f + std::exp(-v));
}

struct WindowOutput
{
    std::vector<float> note, onset, contour; // 172 frames each
};

// basic_pitch.models.model on one window of kWindowSamples samples.
WindowOutput runWindow(const float* audio)
{
    const auto& m = model();

    // CQT: the top octave on the window, each lower one on the signal downsampled again
    std::vector<float> magnitudes(static_cast<size_t>(kWindowFrames * kCqtBins), 0.0f);
    std::vector<float> x(audio, audio + kWindowSamples);
    const int droppedBins = kOctaves * kBinsPerOctave - kCqtBins; // 15 bottom bins
    for (int octave = 0; octave < kOctaves; ++octave)
    {
        if (octave > 0)
            x = downsample(x);
        const int firstBin = (kOctaves - 1 - octave) * kBinsPerOctave - droppedBins;
        cqtOctave(x, kHop >> octave, firstBin, magnitudes);
    }

    // NormalizedLog: dB of the power, scaled 0..1 over the window; then the first BatchNormalization
    float lowest = 1.0e30f, highest = -1.0e30f;
    for (int t = 0; t < kWindowFrames; ++t)
        for (int b = 0; b < kCqtBins; ++b)
        {
            auto& v = magnitudes[static_cast<size_t>(t * kCqtBins + b)];
            const float magnitude = v * m.lengthScale[static_cast<size_t>(b)];
            v = 10.0f * std::log10(magnitude * magnitude + 1.0e-10f);
            lowest = std::min(lowest, v);
            highest = std::max(highest, v);
        }
    const float range = highest - lowest;
    for (auto& v : magnitudes)
    {
        const float normalized = range > 0.0f ? (v - lowest) / range : 0.0f;
        v = normalized * m.norm0.scale[0] + m.norm0.shift[0];
    }

    // harmonic stacking: harmonics 0.5, 1 .. 7 as bin shifts, the first 264 bins
    static constexpr std::array<int, kHarmonics> kShifts { -36, 0, 36, 57, 72, 84, 93, 101 };
    std::vector<float> stack(static_cast<size_t>(kWindowFrames * kStackBins * kHarmonics), 0.0f);
    for (int t = 0; t < kWindowFrames; ++t)
        for (int f = 0; f < kStackBins; ++f)
            for (int h = 0; h < kHarmonics; ++h)
            {
                const int source = f + kShifts[static_cast<size_t>(h)];
                if (source >= 0 && source < kCqtBins)
                    stack[static_cast<size_t>((t * kStackBins + f) * kHarmonics + h)] = magnitudes[static_cast<size_t>(t * kCqtBins + source)];
            }

    int bins = 0;
    auto features = conv2d(stack, kWindowFrames, kStackBins, kHarmonics, bp::k_conv2d_1_kernel, bp::k_conv2d_1_bias, 3, 39, 8, 1, bins);
    normRelu(features, m.norm2);
    WindowOutput out;
    out.contour = conv2d(features, kWindowFrames, kStackBins, 8, bp::k_contours_reduced_kernel, bp::k_contours_reduced_bias, 5, 5, 1, 1, bins);
    sigmoid(out.contour);

    auto noteFeatures = conv2d(out.contour, kWindowFrames, kStackBins, 1, bp::k_conv2d_2_kernel, bp::k_conv2d_2_bias, 7, 7, 32, 3, bins);
    for (auto& v : noteFeatures)
        v = std::max(0.0f, v);
    out.note = conv2d(noteFeatures, kWindowFrames, BasicPitchFrames::kNotes, 32, bp::k_conv2d_3_kernel, bp::k_conv2d_3_bias, 7, 3, 1, 1, bins);
    sigmoid(out.note);

    auto onsetFeatures = conv2d(stack, kWindowFrames, kStackBins, kHarmonics, bp::k_conv2d_4_kernel, bp::k_conv2d_4_bias, 5, 5, 32, 3, bins);
    normRelu(onsetFeatures, m.norm3);
    std::vector<float> joined(static_cast<size_t>(kWindowFrames * BasicPitchFrames::kNotes * 33));
    for (int i = 0; i < kWindowFrames * BasicPitchFrames::kNotes; ++i)
    {
        joined[static_cast<size_t>(i * 33)] = out.note[static_cast<size_t>(i)];
        std::copy(onsetFeatures.begin() + i * 32, onsetFeatures.begin() + (i + 1) * 32, joined.begin() + i * 33 + 1);
    }
    out.onset = conv2d(joined, kWindowFrames, BasicPitchFrames::kNotes, 33, bp::k_conv2d_5_kernel, bp::k_conv2d_5_bias, 3, 3, 1, 1, bins);
    sigmoid(out.onset);
    return out;
}

// The note decoder's "follow a note while its energy lasts" walk.
constexpr int kEnergyTolerance = 11;
constexpr int kMidiOffset = 21;
constexpr int kMaxPitchIndex = BasicPitchFrames::kNotes - 1;

// basic_pitch.note_creation.model_frames_to_time (its per-window offset included)
double frameTime(int frame)
{
    const double windowOffset = (kHop / BasicPitchTranscriber::kSampleRate) * (kWindowFrames - kWindowSamples / static_cast<double>(kHop)) + 0.0018;
    return frame * kHop / BasicPitchTranscriber::kSampleRate - windowOffset * std::floor(frame / static_cast<double>(kWindowFrames));
}
} // namespace

BasicPitchFrames BasicPitchTranscriber::infer(const float* audio, int numSamples, int threads)
{
    BasicPitchFrames result;
    if (audio == nullptr || numSamples <= 0)
        return result;

    // basic_pitch.inference: half an overlap of silence in front, 2 s windows every kWindowHop samples
    std::vector<float> padded(static_cast<size_t>(kOverlapSamples / 2), 0.0f);
    padded.insert(padded.end(), audio, audio + numSamples);
    const int paddedLength = static_cast<int>(padded.size());
    const int windows = (paddedLength + kWindowHop - 1) / kWindowHop;
    padded.resize(static_cast<size_t>((windows - 1) * kWindowHop + kWindowSamples), 0.0f);

    std::vector<WindowOutput> outputs(static_cast<size_t>(windows));
    const int workers = std::max(1, std::min(threads, windows));
    auto work = [&](int first)
    {
        for (int w = first; w < windows; w += workers)
            outputs[static_cast<size_t>(w)] = runWindow(padded.data() + static_cast<size_t>(w) * kWindowHop);
    };
    if (workers == 1)
        work(0);
    else
    {
        std::vector<std::thread> pool;
        for (int i = 0; i < workers; ++i)
            pool.emplace_back(work, i);
        for (auto& thread : pool)
            thread.join();
    }

    // unwrap_output: drop half the overlap at both ends of every window, keep the original length
    const int keep = kWindowFrames - kOverlapFrames;
    const int total = static_cast<int>(static_cast<long long>(numSamples) * (22050 / kHop) / 22050);
    result.frames = std::min(total, windows * keep);
    auto unwrap = [&](std::vector<float> WindowOutput::*field, int width, std::vector<float>& target)
    {
        target.assign(static_cast<size_t>(result.frames * width), 0.0f);
        for (int frame = 0; frame < result.frames; ++frame)
        {
            const auto& source = outputs[static_cast<size_t>(frame / keep)].*field;
            const int local = frame % keep + kOverlapFrames / 2;
            std::copy(source.begin() + local * width, source.begin() + (local + 1) * width, target.begin() + frame * width);
        }
    };
    unwrap(&WindowOutput::note, BasicPitchFrames::kNotes, result.note);
    unwrap(&WindowOutput::onset, BasicPitchFrames::kNotes, result.onset);
    unwrap(&WindowOutput::contour, BasicPitchFrames::kContours, result.contour);
    return result;
}

std::vector<BasicPitchNote> BasicPitchTranscriber::notes(const BasicPitchFrames& frames, const BasicPitchSettings& settings)
{
    // basic_pitch.note_creation.output_to_notes_polyphonic
    const int count = frames.frames;
    static constexpr int pitches = BasicPitchFrames::kNotes;
    std::vector<BasicPitchNote> result;
    if (count <= 0)
        return result;
    auto at = [](int frame, int pitch) { return static_cast<size_t>(frame * pitches + pitch); };
    const int minimumLength = static_cast<int>(std::lround(settings.minimumNoteMs / 1000.0 * BasicPitchFrames::kFramesPerSecond));

    std::vector<float> onsets = frames.onset;
    if (settings.inferOnsets)
    {
        // get_infered_onsets: a big rise of the note activation over 1 and 2 frames counts as an onset
        std::vector<float> rise(onsets.size(), 0.0f);
        float maxRise = 0.0f, maxOnset = 0.0f;
        for (const float v : onsets)
            maxOnset = std::max(maxOnset, v);
        for (int t = 2; t < count; ++t)
            for (int p = 0; p < pitches; ++p)
            {
                const float now = frames.note[at(t, p)];
                const float d = std::max(0.0f, std::min(now - frames.note[at(t - 1, p)], now - frames.note[at(t - 2, p)]));
                rise[at(t, p)] = d;
                maxRise = std::max(maxRise, d);
            }
        if (maxRise > 0.0f)
            for (size_t i = 0; i < onsets.size(); ++i)
                onsets[i] = std::max(onsets[i], maxOnset * rise[i] / maxRise);
    }

    // onset peaks in time (scipy argrelmax), walked backwards in time, then down in pitch
    std::vector<std::pair<int, int>> starts;
    for (int t = 1; t + 1 < count; ++t)
        for (int p = 0; p < pitches; ++p)
        {
            const float v = onsets[at(t, p)];
            if (v > onsets[at(t - 1, p)] && v > onsets[at(t + 1, p)] && v >= settings.onsetThreshold)
                starts.emplace_back(t, p);
        }
    std::reverse(starts.begin(), starts.end());

    std::vector<float> energy = frames.note;
    auto mean = [&](int begin, int end, int pitch)
    {
        double sum = 0.0;
        for (int t = begin; t < end; ++t)
            sum += frames.note[at(t, pitch)];
        return static_cast<float>(sum / std::max(1, end - begin));
    };
    std::vector<std::array<int, 3>> events; // start frame, end frame, pitch index
    for (const auto& [start, pitch] : starts)
    {
        if (start >= count - 1)
            continue;
        int i = start + 1, k = 0;
        while (i < count - 1 && k < kEnergyTolerance)
        {
            k = energy[at(i, pitch)] < settings.frameThreshold ? k + 1 : 0;
            ++i;
        }
        i -= k;
        if (i - start <= minimumLength)
            continue;
        for (int t = start; t < i; ++t)
        {
            energy[at(t, pitch)] = 0.0f;
            if (pitch < kMaxPitchIndex)
                energy[at(t, pitch + 1)] = 0.0f;
            if (pitch > 0)
                energy[at(t, pitch - 1)] = 0.0f;
        }
        events.push_back({ start, i, pitch });
    }

    if (settings.melodiaTrick)
    {
        // what is left above the frame threshold becomes notes too, loudest first
        auto clearAround = [&](int t, int pitch)
        {
            energy[at(t, pitch)] = 0.0f;
            if (pitch < kMaxPitchIndex)
                energy[at(t, pitch + 1)] = 0.0f;
            if (pitch > 0)
                energy[at(t, pitch - 1)] = 0.0f;
        };
        for (;;)
        {
            const auto peak = std::max_element(energy.begin(), energy.end());
            if (*peak <= settings.frameThreshold)
                break;
            const int index = static_cast<int>(peak - energy.begin());
            const int middle = index / pitches, pitch = index % pitches;
            energy[static_cast<size_t>(index)] = 0.0f;

            int i = middle + 1, k = 0;
            while (i < count - 1 && k < kEnergyTolerance)
            {
                k = energy[at(i, pitch)] < settings.frameThreshold ? k + 1 : 0;
                clearAround(i, pitch);
                ++i;
            }
            const int end = i - 1 - k;
            i = middle - 1;
            k = 0;
            while (i > 0 && k < kEnergyTolerance)
            {
                k = energy[at(i, pitch)] < settings.frameThreshold ? k + 1 : 0;
                clearAround(i, pitch);
                --i;
            }
            const int start = i + 1 + k;
            if (end - start <= minimumLength)
                continue;
            events.push_back({ start, end, pitch });
        }
    }

    for (const auto& [start, end, pitch] : events)
        result.push_back({ frameTime(start), frameTime(end), pitch + kMidiOffset, mean(start, end, pitch) });
    return result;
}

std::vector<float> BasicPitchTranscriber::resampleTo22050(const float* mono, int numSamples, double sampleRate)
{
    if (mono == nullptr || numSamples <= 0 || sampleRate <= 0.0)
        return {};
    if (std::abs(sampleRate - kSampleRate) < 0.5)
        return std::vector<float>(mono, mono + numSamples);

    // windowed sinc (Blackman), cut off below the lower Nyquist; the kernel is tabulated (kSteps points
    // per input sample) and read with linear interpolation: one input sample further is kSteps further
    // in the table, so the interpolation fraction is the same for every tap of an output sample
    const double step = sampleRate / kSampleRate;
    const double cutoff = 0.97 * std::min(1.0, kSampleRate / sampleRate);
    constexpr int kZeroCrossings = 24;
    constexpr int kSteps = 512;
    const double halfWidth = kZeroCrossings / cutoff;
    const int tableSize = static_cast<int>(std::ceil(2.0 * halfWidth * kSteps)) + 2;
    std::vector<float> table(static_cast<size_t>(tableSize), 0.0f);
    constexpr double pi = 3.14159265358979323846;
    for (int k = 0; k < tableSize; ++k)
    {
        const double x = k / static_cast<double>(kSteps) - halfWidth;
        if (std::abs(x) > halfWidth)
            continue;
        const double arg = pi * cutoff * x;
        const double sinc = std::abs(arg) < 1.0e-9 ? 1.0 : std::sin(arg) / arg;
        const double phase = pi * (x / halfWidth + 1.0); // 0 .. 2 pi over the window
        table[static_cast<size_t>(k)] = static_cast<float>(cutoff * sinc * (0.42 - 0.5 * std::cos(phase) + 0.08 * std::cos(2.0 * phase)));
    }

    const int outLength = static_cast<int>(std::floor(numSamples / step));
    std::vector<float> out(static_cast<size_t>(std::max(0, outLength)));
    for (int n = 0; n < outLength; ++n)
    {
        const double centre = n * step;
        const int first = std::max(0, static_cast<int>(std::ceil(centre - halfWidth)));
        const int last = std::min(numSamples - 1, static_cast<int>(std::floor(centre + halfWidth)));
        const double position = (first - centre + halfWidth) * kSteps;
        int index = static_cast<int>(position);
        const float fraction = static_cast<float>(position - index);
        float sum = 0.0f;
        for (int i = first; i <= last && index + 1 < tableSize; ++i, index += kSteps)
        {
            const float a = table[static_cast<size_t>(index)];
            sum += mono[i] * (a + fraction * (table[static_cast<size_t>(index + 1)] - a));
        }
        out[static_cast<size_t>(n)] = sum;
    }
    return out;
}
} // namespace bbg

namespace bbg
{
NoteKey estimateKeyFromNotes(const std::vector<BasicPitchNote>& notes, float bassWeight, float openingWeight)
{
    // SampleHarmonyAnalyzer's Albrecht & Shanahan (2013) profiles
    static constexpr std::array<double, 12> kMajor { 0.238, 0.006, 0.111, 0.006, 0.137, 0.094, 0.016, 0.214, 0.009, 0.080, 0.008, 0.081 };
    static constexpr std::array<double, 12> kMinor { 0.220, 0.006, 0.104, 0.123, 0.019, 0.103, 0.012, 0.214, 0.062, 0.022, 0.061, 0.052 };

    std::array<double, 12> chroma {}, bass {};
    for (const auto& n : notes)
        chroma[static_cast<size_t>(n.midi % 12)] += (n.endSeconds - n.startSeconds) * n.amplitude;

    // the bass: the lowest note sounding where a note starts (20 ms of slack)
    auto sorted = notes;
    std::sort(sorted.begin(), sorted.end(), [](const BasicPitchNote& a, const BasicPitchNote& b)
    {
        if (a.startSeconds != b.startSeconds) return a.startSeconds < b.startSeconds;
        if (a.endSeconds != b.endSeconds) return a.endSeconds < b.endSeconds;
        if (a.midi != b.midi) return a.midi < b.midi;
        return a.amplitude < b.amplitude;
    });
    int opening = -1;
    for (const auto& n : sorted)
    {
        int lowest = 1000;
        for (const auto& other : sorted)
            if (other.startSeconds <= n.startSeconds + 0.02 && other.endSeconds > n.startSeconds)
                lowest = std::min(lowest, other.midi);
        if (lowest != n.midi)
            continue;
        bass[static_cast<size_t>(n.midi % 12)] += (n.endSeconds - n.startSeconds) * n.amplitude;
        if (opening < 0 && n.midi < 60)
            opening = n.midi % 12;
    }

    NoteKey key;
    double total = 0.0, peak = 0.0, bassTotal = 0.0;
    for (int pc = 0; pc < 12; ++pc)
    {
        total += chroma[static_cast<size_t>(pc)];
        peak = std::max(peak, chroma[static_cast<size_t>(pc)]);
        bassTotal += bass[static_cast<size_t>(pc)];
    }
    if (total <= 0.0)
        return key;
    for (auto& c : chroma)
        c /= peak;

    auto correlation = [&](const std::array<double, 12>& profile, int root)
    {
        double mc = 0.0, mp = 0.0;
        for (int pc = 0; pc < 12; ++pc)
        {
            mc += chroma[static_cast<size_t>(pc)] / 12.0;
            mp += profile[static_cast<size_t>(pc)] / 12.0;
        }
        double num = 0.0, cv = 0.0, pv = 0.0;
        for (int pc = 0; pc < 12; ++pc)
        {
            const double c = chroma[static_cast<size_t>(pc)] - mc;
            const double p = profile[static_cast<size_t>((pc - root + 12) % 12)] - mp;
            num += c * p;
            cv += c * c;
            pv += p * p;
        }
        return cv * pv > 1.0e-18 ? num / std::sqrt(cv * pv) : 0.0;
    };

    // ties go to the higher root / major, as the Python reference sorts
    double best = -1.0e30, second = -1.0e30;
    for (int root = 0; root < 12; ++root)
        for (int mode = 0; mode < 2; ++mode)
        {
            double score = correlation(mode == 1 ? kMajor : kMinor, root);
            if (bassTotal > 0.0)
                score += bassWeight * bass[static_cast<size_t>(root)] / bassTotal;
            if (root == opening)
                score += openingWeight;
            if (mode == 0)
                score += 0.10;
            if (score >= best)
            {
                second = best;
                best = score;
                key.root = root;
                key.mode = mode;
            }
            else if (score > second)
                second = score;
        }
    key.valid = true;
    key.score = static_cast<float>(best);
    key.margin = static_cast<float>(best - second);
    // SampleHarmonyAnalyzer's fifth check, on the notes: a key a fifth away in the same mode that
    // fits the notes better means the bass / opening bonuses made the choice
    const auto& profile = key.mode == 1 ? kMajor : kMinor;
    key.fifthFitsBetter = std::max(correlation(profile, (key.root + 7) % 12), correlation(profile, (key.root + 5) % 12))
                          > correlation(profile, key.root);
    return key;
}
} // namespace bbg
