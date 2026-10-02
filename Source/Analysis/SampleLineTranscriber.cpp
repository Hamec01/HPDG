#include "SampleLineTranscriber.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

#include <juce_dsp/juce_dsp.h>

namespace bbg
{
namespace
{
constexpr double kMaxSeconds = 40.0;

struct LineConfig
{
    double binHz = 2.7;             // target FFT resolution (the order follows the working rate)
    double hopSeconds = 0.023;
    int lowNote = 28;
    int highNote = 55;
    int harmonics = 5;
    float harmonicDecay = 0.8f;
    float fundamentalFloor = 0.25f; // salience kept when the fundamental itself is missing
    int timeMedianFrames = 7;       // HPSS harmonic median length (odd)
    int freqMedianBins = 15;        // HPSS percussive median length (odd)
    float unvoicedLevel = 0.2f;     // salience (vs the line's typical peak) below which silence wins
    float jumpCost = 2.2f;
    float jumpCostPerSemitone = 0.06f;
    float voicingCost = 1.4f;
    double minNoteSeconds = 0.09;
    double onsetLowHz = 0.0;        // onset refinement band
    double onsetHighHz = 250.0;
    double onsetSearchBefore = 0.14;
    double onsetSearchAfter = 0.10;
    double maxHz = 20000.0;         // highest harmonic looked at
    int preferBelow = 127;          // notes above this need more evidence (register prior)
    float abovePenalty = 0.0f;      // per semitone above preferBelow
};

LineConfig bassConfig()
{
    LineConfig c;
    c.binHz = 2.7;    // ~0.37 s window: semitones in the low register are 2-3 Hz apart
    c.preferBelow = 47; // a bass lives below C3; above it, keys / guitar take over the low mids
    c.abovePenalty = 0.05f;
    c.maxHz = 1100.0;
    c.hopSeconds = 0.023;
    c.lowNote = 19;   // G0: trap / dub subs sit this low; their harmonics carry the pitch
    c.highNote = 55;  // G3
    c.harmonics = 5;
    c.harmonicDecay = 0.7f; // the bass instrument owns its fundamental; chords above own harmonics
    c.fundamentalFloor = 0.25f;
    c.timeMedianFrames = 7;
    c.freqMedianBins = 15;
    c.unvoicedLevel = 0.22f;
    c.minNoteSeconds = 0.09;
    c.onsetLowHz = 0.0;
    c.onsetHighHz = 260.0;
    c.onsetSearchBefore = 0.16;
    c.onsetSearchAfter = 0.10;
    return c;
}

LineConfig melodyConfig()
{
    LineConfig c;
    c.binHz = 5.4;    // ~0.19 s window, a semitone apart from E3 up
    c.hopSeconds = 0.018;
    c.maxHz = 5000.0;
    c.lowNote = 52;   // E3
    c.highNote = 88;  // E6
    c.harmonics = 6;
    c.harmonicDecay = 0.84f;
    c.fundamentalFloor = 0.4f;
    c.timeMedianFrames = 9;
    c.freqMedianBins = 17;
    c.unvoicedLevel = 0.26f;
    c.jumpCost = 1.8f;
    c.jumpCostPerSemitone = 0.05f;
    c.minNoteSeconds = 0.07;
    c.onsetLowHz = 150.0;
    c.onsetHighHz = 4000.0;
    c.onsetSearchBefore = 0.09;
    c.onsetSearchAfter = 0.08;
    return c;
}

double midiToHz(double note)
{
    return 440.0 * std::pow(2.0, (note - 69.0) / 12.0);
}

bool inScale(int midi, int keyRoot, int scaleMode)
{
    static constexpr std::array<int, 7> minor { 0, 2, 3, 5, 7, 8, 10 };
    static constexpr std::array<int, 7> major { 0, 2, 4, 5, 7, 9, 11 };
    const auto& scale = scaleMode == 1 ? major : minor;
    const int degree = ((midi - keyRoot) % 12 + 12) % 12;
    return std::find(scale.begin(), scale.end(), degree) != scale.end();
}

float medianOf(std::vector<float>& values)
{
    const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::nth_element(values.begin(), middle, values.end());
    return *middle;
}

// Band energy envelope (10 ms frames, log) for onset refinement: a crude band-pass from two
// one-pole filters is enough to see where a note's energy starts.
std::vector<float> bandEnvelope(const std::vector<float>& mono, double sampleRate, double lowHz, double highHz, int frameSamples)
{
    const double aHigh = std::exp(-juce::MathConstants<double>::twoPi * highHz / sampleRate);
    const double aLow = lowHz > 0.0 ? std::exp(-juce::MathConstants<double>::twoPi * lowHz / sampleRate) : 0.0;
    double lp = 0.0;
    double lpOfLow = 0.0;
    std::vector<float> envelope(mono.size() / static_cast<size_t>(frameSamples) + 1, 0.0f);
    double sum = 0.0;
    int count = 0;
    size_t frame = 0;
    for (size_t i = 0; i < mono.size(); ++i)
    {
        lp = (1.0 - aHigh) * mono[i] + aHigh * lp;              // low-pass at highHz
        double band = lp;
        if (lowHz > 0.0)
        {
            lpOfLow = (1.0 - aLow) * lp + aLow * lpOfLow;       // minus its own low-pass at lowHz
            band = lp - lpOfLow;
        }
        sum += band * band;
        if (++count == frameSamples)
        {
            envelope[frame++] = static_cast<float>(std::log10(1.0e-9 + sum / count));
            sum = 0.0;
            count = 0;
        }
    }
    envelope.resize(frame);
    return envelope;
}

// Low-pass (windowed sinc) and keep every factor-th sample: the bass needs nothing above
// ~1 kHz and the melody nothing above 5 kHz, so their long FFTs run on a fraction of the data.
std::vector<float> decimate(const std::vector<float>& input, int factor)
{
    if (factor <= 1)
        return input;
    const int halfTaps = 8 * factor;
    const double cutoff = 0.45 / factor; // of the input rate
    std::vector<float> taps(static_cast<size_t>(2 * halfTaps + 1));
    double sum = 0.0;
    for (int i = -halfTaps; i <= halfTaps; ++i)
    {
        const double x = static_cast<double>(i);
        const double sinc = i == 0 ? 2.0 * cutoff : std::sin(juce::MathConstants<double>::twoPi * cutoff * x) / (juce::MathConstants<double>::pi * x);
        const double window = 0.5 + 0.5 * std::cos(juce::MathConstants<double>::pi * x / (halfTaps + 1));
        taps[static_cast<size_t>(i + halfTaps)] = static_cast<float>(sinc * window);
        sum += sinc * window;
    }
    for (auto& t : taps)
        t = static_cast<float>(t / sum);

    const auto size = static_cast<int>(input.size());
    std::vector<float> output(static_cast<size_t>(size / factor));
    for (size_t o = 0; o < output.size(); ++o)
    {
        const int centre = static_cast<int>(o) * factor;
        float acc = 0.0f;
        const int from = juce::jmax(-halfTaps, -centre);
        const int to = juce::jmin(halfTaps, size - 1 - centre);
        for (int k = from; k <= to; ++k)
            acc += taps[static_cast<size_t>(k + halfTaps)] * input[static_cast<size_t>(centre + k)];
        output[o] = acc;
    }
    return output;
}

std::vector<SampleLineNote> transcribeLine(const std::vector<float>& mono,
                                           double sampleRate,
                                           const LineConfig& config,
                                           int keyRoot,
                                           int scaleMode,
                                           bool keyKnown,
                                           double tuningCents,
                                           const std::vector<SampleLineNote>* suppress = nullptr)
{
    std::vector<SampleLineNote> notes;
    const int fftOrder = juce::jlimit(8, 15, static_cast<int>(std::lround(std::log2(sampleRate / config.binHz))));
    const int fftSize = 1 << fftOrder;
    const int hop = juce::jmax(64, static_cast<int>(std::lround(config.hopSeconds * sampleRate)));
    const auto total = static_cast<int>(std::min<size_t>(mono.size(), static_cast<size_t>(kMaxSeconds * sampleRate)));
    if (total < fftSize / 4)
        return notes;

    const double binHz = sampleRate / fftSize;
    const double topHz = std::min(config.maxHz, midiToHz(config.highNote + 0.5) * config.harmonics);
    const int maxBin = juce::jmin(fftSize / 2 - 2, static_cast<int>(std::ceil(topHz / binHz)) + 2);
    const int bins = maxBin + 1;
    const int frames = total / hop + 1;

    // 1. Magnitude spectrogram.
    juce::dsp::FFT fft(fftOrder);
    std::vector<float> window(static_cast<size_t>(fftSize));
    for (int i = 0; i < fftSize; ++i)
        window[static_cast<size_t>(i)] = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * static_cast<float>(i) / static_cast<float>(fftSize));

    std::vector<float> magnitude(static_cast<size_t>(frames) * static_cast<size_t>(bins), 0.0f);
    auto mag = [&](int frame, int bin) -> float& { return magnitude[static_cast<size_t>(frame) * static_cast<size_t>(bins) + static_cast<size_t>(bin)]; };
    std::vector<float> data(static_cast<size_t>(fftSize * 2));
    for (int frame = 0; frame < frames; ++frame)
    {
        std::fill(data.begin(), data.end(), 0.0f);
        const int start = frame * hop - fftSize / 2;
        const int from = juce::jmax(0, -start);
        const int to = juce::jmin(fftSize, total - start);
        for (int i = from; i < to; ++i)
            data[static_cast<size_t>(i)] = mono[static_cast<size_t>(start + i)] * window[static_cast<size_t>(i)];
        fft.performFrequencyOnlyForwardTransform(data.data(), true);
        for (int bin = 0; bin < bins; ++bin)
            mag(frame, bin) = data[static_cast<size_t>(bin)];
    }

    // 2. Harmonic / percussive separation: soft (Wiener) mask from the time-median (sustained)
    // vs frequency-median (transient) spectra. Only the harmonic part is kept.
    std::vector<float> harmonic(magnitude.size(), 0.0f);
    {
        const int halfT = config.timeMedianFrames / 2;
        const int halfF = config.freqMedianBins / 2;
        std::vector<float> scratch;
        std::vector<float> timeMedian(magnitude.size(), 0.0f);
        for (int bin = 0; bin < bins; ++bin)
            for (int frame = 0; frame < frames; ++frame)
            {
                scratch.clear();
                for (int t = juce::jmax(0, frame - halfT); t <= juce::jmin(frames - 1, frame + halfT); ++t)
                    scratch.push_back(mag(t, bin));
                timeMedian[static_cast<size_t>(frame) * static_cast<size_t>(bins) + static_cast<size_t>(bin)] = medianOf(scratch);
            }
        for (int frame = 0; frame < frames; ++frame)
            for (int bin = 0; bin < bins; ++bin)
            {
                scratch.clear();
                for (int k = juce::jmax(0, bin - halfF); k <= juce::jmin(bins - 1, bin + halfF); ++k)
                    scratch.push_back(mag(frame, k));
                const float p = medianOf(scratch);
                const size_t index = static_cast<size_t>(frame) * static_cast<size_t>(bins) + static_cast<size_t>(bin);
                const float h = timeMedian[index];
                const float mask = (h * h) / (h * h + p * p + 1.0e-12f);
                // Compressed: a loud note must not hide a quieter line note's harmonics.
                harmonic[index] = std::sqrt(mag(frame, bin) * mask);
            }
    }

    // 2b. Iterative multi-pitch estimation (Klapuri): the line found first (the bass) is taken
    // out of the spectrum - its harmonics would otherwise be "fundamentals" here (A1's 4th
    // harmonic is A3, C2's 3rd is G3) and pull the melody an octave down.
    if (suppress != nullptr && !suppress->empty())
    {
        const double frameSeconds = static_cast<double>(hop) / sampleRate;
        size_t cursor = 0;
        for (int frame = 0; frame < frames; ++frame)
        {
            const double time = frame * frameSeconds;
            while (cursor < suppress->size() && (*suppress)[cursor].endSeconds <= time)
                ++cursor;
            if (cursor >= suppress->size() || (*suppress)[cursor].startSeconds > time)
                continue;
            const double f0 = midiToHz((*suppress)[cursor].midiNote + tuningCents / 100.0);
            float* row = harmonic.data() + static_cast<size_t>(frame) * static_cast<size_t>(bins);
            for (int h = 1; h <= 16; ++h)
            {
                const double hz = f0 * h;
                if (hz / binHz >= bins - 1)
                    break;
                const double halfWidth = std::max(1.5, hz * 0.03 / binHz);
                const int lo = juce::jmax(0, static_cast<int>(std::floor(hz / binHz - halfWidth)));
                const int hi = juce::jmin(bins - 1, static_cast<int>(std::ceil(hz / binHz + halfWidth)));
                for (int bin = lo; bin <= hi; ++bin)
                    row[bin] *= 0.2f;
            }
        }
    }

    // 3. Salience per semitone (harmonic summation, own fundamental required).
    const int noteCount = config.highNote - config.lowNote + 1;
    std::vector<float> salience(static_cast<size_t>(frames) * static_cast<size_t>(noteCount), 0.0f);
    float weightSum = 0.0f;
    std::vector<float> weights;
    for (int h = 0; h < config.harmonics; ++h)
    {
        weights.push_back(std::pow(config.harmonicDecay, static_cast<float>(h)));
        weightSum += weights.back();
    }
    std::vector<float> frameMax(static_cast<size_t>(frames), 0.0f);
    for (int frame = 0; frame < frames; ++frame)
    {
        const float* row = harmonic.data() + static_cast<size_t>(frame) * static_cast<size_t>(bins);
        // Strongest spectral peak near hz, counted only as far as its interpolated frequency is
        // really this semitone: in the low register neighbouring semitones are ~1 bin apart,
        // and a bare "max bin within +-1" handed F#1 the energy of an F1 bass.
        auto peakNear = [&](double hz)
        {
            const double centre = hz / binHz;
            const double halfWidth = std::max(1.0, centre * (std::pow(2.0, 1.0 / 24.0) - 1.0));
            const int lo = juce::jmax(2, static_cast<int>(std::floor(centre - halfWidth - 1.0)));
            const int hi = juce::jmin(bins - 2, static_cast<int>(std::ceil(centre + halfWidth + 1.0)));
            float best = 0.0f;
            for (int bin = lo; bin <= hi; ++bin)
            {
                const float b = row[bin];
                if (b <= 0.0f || b < row[bin - 1] || b < row[bin + 1])
                    continue;
                const float a = std::log(row[bin - 1] + 1.0e-12f);
                const float g = std::log(row[bin + 1] + 1.0e-12f);
                const float m = std::log(b + 1.0e-12f);
                const float denominator = a - 2.0f * m + g;
                const float offset = std::abs(denominator) > 1.0e-9f ? juce::jlimit(-0.5f, 0.5f, 0.5f * (a - g) / denominator) : 0.0f;
                const double peakHz = (bin + offset) * binHz;
                const double cents = 1200.0 * std::log2(peakHz / hz);
                const double off = std::abs(cents) / 50.0;
                if (off >= 1.0)
                    continue;
                best = std::max(best, b * static_cast<float>(1.0 - off * off));
            }
            return best;
        };
        for (int n = 0; n < noteCount; ++n)
        {
            const double f0 = midiToHz(config.lowNote + n + tuningCents / 100.0);
            float value = 0.0f;
            float fundamental = 0.0f;
            for (int h = 0; h < config.harmonics; ++h)
            {
                const float peak = peakNear(f0 * (h + 1));
                if (h == 0)
                    fundamental = peak;
                value += weights[static_cast<size_t>(h)] * peak;
            }
            const float average = value / weightSum;
            const float own = juce::jmin(1.0f, fundamental / (0.5f * average + 1.0e-9f));
            const float s = value * (config.fundamentalFloor + (1.0f - config.fundamentalFloor) * own);
            salience[static_cast<size_t>(frame) * static_cast<size_t>(noteCount) + static_cast<size_t>(n)] = s;
            frameMax[static_cast<size_t>(frame)] = std::max(frameMax[static_cast<size_t>(frame)], s);
        }
    }

    // Absolute floor: frames ~40 dB below the loudest are silence, not quiet notes (salience is
    // on square-rooted magnitudes, so 0.1 here is -40 dB). Without it a stem / sample that is
    // silent for a while gets its noise floor transcribed.
    const float loudest = *std::max_element(frameMax.begin(), frameMax.end());
    // ...and an absolute one at about -66 dBFS (a Hann-windowed sine of amplitude A peaks at
    // A * N / 4 in this FFT): a sample that is all near-silence has no line at all.
    const float absoluteGate = std::sqrt(0.0005f * static_cast<float>(fftSize) / 4.0f);
    const float gate = std::max(0.1f * loudest, absoluteGate);
    std::vector<float> sorted;
    for (const float value : frameMax)
        if (value > gate)
            sorted.push_back(value);
    if (sorted.size() < 4)
        return notes;
    std::sort(sorted.begin(), sorted.end());
    const float reference = sorted[static_cast<size_t>(0.9 * static_cast<double>(sorted.size() - 1))];
    if (reference <= 1.0e-9f)
        return notes;

    // 4. Viterbi over notes + silence (state noteCount).
    const int states = noteCount + 1;
    const int silent = noteCount;
    const float floorProb = 0.03f;
    const float silentEmission = std::log(floorProb + config.unvoicedLevel);
    std::vector<float> score(static_cast<size_t>(states), 0.0f);
    std::vector<float> next(static_cast<size_t>(states), 0.0f);
    std::vector<int> back(static_cast<size_t>(frames) * static_cast<size_t>(states), silent);
    auto emission = [&](int frame, int state)
    {
        if (state == silent)
            return silentEmission;
        if (frameMax[static_cast<size_t>(frame)] <= gate)
            return std::log(floorProb) - 3.0f;
        const float s = salience[static_cast<size_t>(frame) * static_cast<size_t>(noteCount) + static_cast<size_t>(state)] / reference;
        float e = std::log(floorProb + juce::jmin(1.5f, s));
        if (keyKnown && !inScale(config.lowNote + state, keyRoot, scaleMode))
            e -= 0.25f;
        if (config.lowNote + state > config.preferBelow)
            e -= config.abovePenalty * static_cast<float>(config.lowNote + state - config.preferBelow);
        return e;
    };
    for (int s = 0; s < states; ++s)
        score[static_cast<size_t>(s)] = emission(0, s) - (s == silent ? 0.0f : config.voicingCost);
    for (int frame = 1; frame < frames; ++frame)
    {
        for (int to = 0; to < states; ++to)
        {
            float best = -1.0e30f;
            int bestFrom = silent;
            for (int from = 0; from < states; ++from)
            {
                float transition = 0.0f;
                if (from != to)
                {
                    if (from == silent || to == silent)
                        transition = -config.voicingCost;
                    else
                        transition = -(config.jumpCost + config.jumpCostPerSemitone * static_cast<float>(std::abs(to - from)));
                }
                const float candidate = score[static_cast<size_t>(from)] + transition;
                if (candidate > best)
                {
                    best = candidate;
                    bestFrom = from;
                }
            }
            next[static_cast<size_t>(to)] = best + emission(frame, to);
            back[static_cast<size_t>(frame) * static_cast<size_t>(states) + static_cast<size_t>(to)] = bestFrom;
        }
        std::swap(score, next);
    }
    std::vector<int> path(static_cast<size_t>(frames), silent);
    path.back() = static_cast<int>(std::distance(score.begin(), std::max_element(score.begin(), score.end())));
    for (int frame = frames - 1; frame > 0; --frame)
        path[static_cast<size_t>(frame - 1)] = back[static_cast<size_t>(frame) * static_cast<size_t>(states) + static_cast<size_t>(path[static_cast<size_t>(frame)])];

    // 5. Runs -> notes (re-attacks split, fragments dropped).
    const double frameSeconds = static_cast<double>(hop) / sampleRate;
    const int minFrames = juce::jmax(2, static_cast<int>(std::ceil(config.minNoteSeconds / frameSeconds)));
    auto salAt = [&](int frame, int state)
    {
        return salience[static_cast<size_t>(frame) * static_cast<size_t>(noteCount) + static_cast<size_t>(state)];
    };
    struct Run
    {
        int first = 0;
        int last = 0;
        int state = 0;
    };
    std::vector<Run> runs;
    for (int frame = 0; frame < frames;)
    {
        const int state = path[static_cast<size_t>(frame)];
        int end = frame;
        while (end + 1 < frames && path[static_cast<size_t>(end + 1)] == state)
            ++end;
        if (state != silent)
        {
            // Re-attack: the note's salience dips and comes back up.
            int segmentStart = frame;
            float runningPeak = salAt(frame, state);
            for (int t = frame + 1; t <= end; ++t)
            {
                const float now = salAt(t, state);
                const float before = salAt(t - 1, state);
                if (t - segmentStart >= minFrames && end - t + 1 >= minFrames
                    && before < 0.7f * runningPeak && now > 1.45f * before)
                {
                    runs.push_back({ segmentStart, t - 1, state });
                    segmentStart = t;
                    runningPeak = now;
                }
                runningPeak = std::max(runningPeak, now);
            }
            runs.push_back({ segmentStart, end, state });
        }
        frame = end + 1;
    }

    // Octave flips inside one sound: a run an octave above (or below) its neighbour, while the
    // lower note's salience stays close by, is the same bass whose fundamental faded against
    // its 2nd harmonic (or came in late) - one note, in the lower octave.
    for (size_t i = 0; i + 1 < runs.size();)
    {
        auto& a = runs[i];
        const auto& b = runs[i + 1];
        if (b.first == a.last + 1 && std::abs(a.state - b.state) == 12)
        {
            const int lower = std::min(a.state, b.state);
            const int upper = lower + 12;
            const auto& upperRun = a.state == upper ? a : b;
            float ratio = 0.0f;
            for (int t = upperRun.first; t <= upperRun.last; ++t)
                ratio += salAt(t, lower) / (salAt(t, upper) + 1.0e-9f);
            ratio /= static_cast<float>(upperRun.last - upperRun.first + 1);
            if (ratio >= 0.45f)
            {
                a.state = lower;
                a.last = b.last;
                runs.erase(runs.begin() + static_cast<std::ptrdiff_t>(i + 1));
                continue;
            }
        }
        ++i;
    }

    // Band envelope for onset refinement (long windows smear a note's start backwards).
    const int envelopeFrame = juce::jmax(32, static_cast<int>(sampleRate * 0.005));
    const double envelopeSeconds = static_cast<double>(envelopeFrame) / sampleRate;
    const auto envelope = bandEnvelope(std::vector<float>(mono.begin(), mono.begin() + total), sampleRate,
                                       config.onsetLowHz, config.onsetHighHz, envelopeFrame);

    float strongest = 0.0f;
    for (const auto& run : runs)
    {
        if (run.last - run.first + 1 < minFrames)
            continue;
        float sum = 0.0f;
        float dominance = 0.0f;
        for (int t = run.first; t <= run.last; ++t)
        {
            const float s = salAt(t, run.state);
            sum += s;
            dominance += frameMax[static_cast<size_t>(t)] > 0.0f ? s / frameMax[static_cast<size_t>(t)] : 0.0f;
        }
        const int length = run.last - run.first + 1;

        SampleLineNote note;
        note.midiNote = config.lowNote + run.state;
        note.startSeconds = juce::jmax(0.0, (run.first - 0.5) * frameSeconds);
        note.endSeconds = (run.last + 0.5) * frameSeconds;
        note.strength = sum / static_cast<float>(length);
        note.confidence = juce::jlimit(0.0f, 1.0f, dominance / static_cast<float>(length));
        strongest = std::max(strongest, note.strength);

        // Onset: the steepest rise of the band energy near the detected start.
        if (!envelope.empty())
        {
            const int lo = juce::jmax(1, static_cast<int>((note.startSeconds - config.onsetSearchBefore) / envelopeSeconds));
            const int hi = juce::jmin(static_cast<int>(envelope.size()) - 2,
                                      static_cast<int>((note.startSeconds + config.onsetSearchAfter) / envelopeSeconds));
            float bestRise = 0.25f; // ~ +2.5 dB over 5-10 ms: a real attack, not a swell
            int bestIndex = -1;
            for (int i = lo; i <= hi; ++i)
            {
                const float rise = envelope[static_cast<size_t>(i + 1)] - envelope[static_cast<size_t>(i - 1)];
                if (rise > bestRise)
                {
                    bestRise = rise;
                    bestIndex = i;
                }
            }
            if (bestIndex >= 0)
            {
                note.startSeconds = juce::jmax(0.0, bestIndex * envelopeSeconds);
                note.attacked = true;
            }
        }
        if (note.endSeconds - note.startSeconds < config.minNoteSeconds * 0.6)
            continue;
        notes.push_back(note);
    }

    // Monophonic, in order, no overlaps (a refined onset may now precede the previous end).
    std::sort(notes.begin(), notes.end(), [](const SampleLineNote& a, const SampleLineNote& b) { return a.startSeconds < b.startSeconds; });

    // An octave change with no new attack is one sound whose fundamental faded (or came in
    // late) against its 2nd harmonic: keep it as one note, in the lower octave.
    for (size_t i = 0; i + 1 < notes.size();)
    {
        auto& a = notes[i];
        const auto& b = notes[i + 1];
        if (std::abs(a.midiNote - b.midiNote) == 12 && !b.attacked && b.startSeconds - a.endSeconds < 0.06)
        {
            a.midiNote = std::min(a.midiNote, b.midiNote);
            a.endSeconds = std::max(a.endSeconds, b.endSeconds);
            a.strength = std::max(a.strength, b.strength);
            a.confidence = 0.5f * (a.confidence + b.confidence);
            notes.erase(notes.begin() + static_cast<std::ptrdiff_t>(i + 1));
            continue;
        }
        ++i;
    }
    for (size_t i = 0; i + 1 < notes.size(); ++i)
        notes[i].endSeconds = std::min(notes[i].endSeconds, notes[i + 1].startSeconds);
    notes.erase(std::remove_if(notes.begin(), notes.end(), [&](const SampleLineNote& n) { return n.durationSeconds() < config.minNoteSeconds * 0.5; }), notes.end());
    for (auto& note : notes)
        note.strength = strongest > 0.0f ? juce::jlimit(0.0f, 1.0f, note.strength / strongest) : 0.0f;
    return notes;
}

float coverage(const std::vector<SampleLineNote>& line, double from, double to)
{
    if (to <= from)
        return 0.0f;
    double covered = 0.0;
    for (const auto& note : line)
        covered += std::max(0.0, std::min(to, note.endSeconds) - std::max(from, note.startSeconds));
    return static_cast<float>(covered / (to - from));
}

const char* const kNames[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
} // namespace

float SampleLines::bassCoverage(double fromSeconds, double toSeconds) const { return coverage(bass, fromSeconds, toSeconds); }
float SampleLines::melodyCoverage(double fromSeconds, double toSeconds) const { return coverage(melody, fromSeconds, toSeconds); }

juce::String SampleLines::describe() const
{
    auto line = [](const char* title, const std::vector<SampleLineNote>& notes)
    {
        juce::String text = juce::String(title) + " (" + juce::String(static_cast<int>(notes.size())) + "):";
        for (const auto& n : notes)
            text << " " << kNames[n.midiNote % 12] << (n.midiNote / 12 - 1) << "@" << juce::String(n.startSeconds, 2)
                 << "+" << juce::String(n.durationSeconds(), 2);
        return text;
    };
    return line("bass line", bass) + "\n" + line("melody line", melody);
}

double SampleLineTranscriber::estimateTuningCents(const std::vector<float>& mono, double sampleRate)
{
    // Every clear spectral peak (80 Hz - 2 kHz) votes with its deviation from the equal-tempered
    // grid; deviations wrap at +-50 cents, so they are averaged on a circle, weighted by level.
    constexpr int order = 13;
    constexpr int size = 1 << order;
    const auto total = std::min<size_t>(mono.size(), static_cast<size_t>(kMaxSeconds * sampleRate));
    if (total < static_cast<size_t>(size) || sampleRate < 8000.0)
        return 0.0;

    juce::dsp::FFT fft(order);
    std::vector<float> data(static_cast<size_t>(size * 2));
    const double binHz = sampleRate / size;
    const int lo = juce::jmax(2, static_cast<int>(80.0 / binHz));
    const int hi = juce::jmin(size / 2 - 2, static_cast<int>(2000.0 / binHz));
    double sumCos = 0.0;
    double sumSin = 0.0;
    double weightTotal = 0.0;
    for (size_t start = 0; start + size <= total; start += size / 2)
    {
        std::fill(data.begin(), data.end(), 0.0f);
        for (int i = 0; i < size; ++i)
            data[static_cast<size_t>(i)] = mono[start + static_cast<size_t>(i)]
                * (0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * static_cast<float>(i) / static_cast<float>(size)));
        fft.performFrequencyOnlyForwardTransform(data.data(), true);
        float frameMax = 0.0f;
        for (int k = lo; k <= hi; ++k)
            frameMax = std::max(frameMax, data[static_cast<size_t>(k)]);
        if (frameMax <= 1.0e-6f)
            continue;
        for (int k = lo; k <= hi; ++k)
        {
            const float m = data[static_cast<size_t>(k)];
            if (m < 0.1f * frameMax || m < data[static_cast<size_t>(k - 1)] || m < data[static_cast<size_t>(k + 1)])
                continue;
            const float a = std::log(data[static_cast<size_t>(k - 1)] + 1.0e-12f);
            const float g = std::log(data[static_cast<size_t>(k + 1)] + 1.0e-12f);
            const float b = std::log(m + 1.0e-12f);
            const float denominator = a - 2.0f * b + g;
            const float offset = std::abs(denominator) > 1.0e-9f ? juce::jlimit(-0.5f, 0.5f, 0.5f * (a - g) / denominator) : 0.0f;
            const double hz = (k + offset) * binHz;
            const double midi = 69.0 + 12.0 * std::log2(hz / 440.0);
            const double angle = juce::MathConstants<double>::twoPi * (midi - std::round(midi));
            sumCos += m * std::cos(angle);
            sumSin += m * std::sin(angle);
            weightTotal += m;
        }
    }
    if (weightTotal <= 0.0)
        return 0.0;
    // A weak vote (peaks spread all over: noise, drums) means "in tune".
    const double resultant = std::sqrt(sumCos * sumCos + sumSin * sumSin) / weightTotal;
    if (resultant < 0.2)
        return 0.0;
    return 100.0 * std::atan2(sumSin, sumCos) / juce::MathConstants<double>::twoPi;
}

SampleLines SampleLineTranscriber::transcribe(const std::vector<float>& mono,
                                              double sampleRate,
                                              int keyRoot,
                                              int scaleMode,
                                              bool keyKnown) const
{
    SampleLines lines;
    if (mono.empty() || sampleRate < 8000.0)
        return lines;
    lines.tuningCents = estimateTuningCents(mono, sampleRate);
    const auto run = [&](const LineConfig& config, const std::vector<SampleLineNote>* suppress)
    {
        int factor = 1;
        while (sampleRate / (factor * 2) >= 2.3 * config.maxHz && factor < 32)
            factor *= 2;
        const auto limit = std::min(mono.size(), static_cast<size_t>(kMaxSeconds * sampleRate));
        const auto input = decimate(std::vector<float>(mono.begin(), mono.begin() + static_cast<std::ptrdiff_t>(limit)), factor);
        return transcribeLine(input, sampleRate / factor, config, keyRoot, scaleMode, keyKnown, lines.tuningCents, suppress);
    };
    lines.bass = run(bassConfig(), nullptr);
    lines.melody = run(melodyConfig(), &lines.bass);
    return lines;
}
} // namespace bbg
