#include "SampleHarmonyAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include <juce_dsp/juce_dsp.h>

namespace bbg
{
namespace
{
constexpr int kFftOrder = 14;               // 16384: ~2.7 Hz bins at 44.1 kHz, needed below 100 Hz
constexpr int kFrameSize = 1 << kFftOrder;
constexpr int kHopSize = 2048;
constexpr int kLowestBassNote = 26;         // D1, 36.7 Hz
constexpr int kHighestBassNote = 55;        // G3, 196 Hz
constexpr std::array<float, 4> kHarmonicWeights { 1.0f, 0.7f, 0.5f, 0.35f };

// Krumhansl-Kessler key profiles (C major / C minor).
constexpr std::array<float, 12> kMajorProfile { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
constexpr std::array<float, 12> kMinorProfile { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };
// Albrecht & Shanahan (2013) corpus profiles: the key decision uses these (docs/audit/
// SAMPLE_ANALYSIS_STAGE.md, key step: per-pack MIREX on 9 held-out packs beat Krumhansl-Kessler
// and Temperley). The KK profiles stay for reference.
// A key the sample's file name states is kept unless its scale clashes with the notes heard:
// correlation of the chroma with the stated key's profile below this rejects it (step 6:
// true labels kept 94.7 %, a tritone-off label 15 %, a semitone-off label 24 %).
constexpr float kKeyLabelMinCorrelation = -0.1f;
constexpr std::array<float, 12> kMajorProfileAS { 0.238f, 0.006f, 0.111f, 0.006f, 0.137f, 0.094f, 0.016f, 0.214f, 0.009f, 0.080f, 0.008f, 0.081f };
constexpr std::array<float, 12> kMinorProfileAS { 0.220f, 0.006f, 0.104f, 0.123f, 0.019f, 0.103f, 0.012f, 0.214f, 0.062f, 0.022f, 0.061f, 0.052f };

const char* const kNoteNames[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

double midiToHz(double note)
{
    return 440.0 * std::pow(2.0, (note - 69.0) / 12.0);
}

float correlation(const std::array<float, 12>& chroma, const std::array<float, 12>& profile, int rotation)
{
    float meanChroma = 0.0f;
    float meanProfile = 0.0f;
    for (int i = 0; i < 12; ++i)
    {
        meanChroma += chroma[static_cast<size_t>(i)];
        meanProfile += profile[static_cast<size_t>(i)];
    }
    meanChroma /= 12.0f;
    meanProfile /= 12.0f;

    float numerator = 0.0f;
    float chromaVar = 0.0f;
    float profileVar = 0.0f;
    for (int pc = 0; pc < 12; ++pc)
    {
        const float c = chroma[static_cast<size_t>(pc)] - meanChroma;
        const float p = profile[static_cast<size_t>((pc - rotation + 12) % 12)] - meanProfile;
        numerator += c * p;
        chromaVar += c * c;
        profileVar += p * p;
    }
    const float denominator = std::sqrt(chromaVar * profileVar);
    return denominator > 1.0e-9f ? numerator / denominator : 0.0f;
}
}

int SampleHarmony::confidentBassNotes(float minConfidence) const
{
    return static_cast<int>(std::count_if(bass.begin(), bass.end(), [minConfidence](const SampleBassSegment& segment)
    {
        return segment.midiNote >= 0 && segment.confidence >= minConfidence;
    }));
}

const SampleBassSegment* SampleHarmony::segmentAt(double seconds) const
{
    for (const auto& segment : bass)
        if (seconds >= segment.startSeconds && seconds < segment.endSeconds)
            return &segment;
    return nullptr;
}

void SampleHarmony::refineBassFromLines()
{
    for (auto& segment : bass)
    {
        const double length = segment.endSeconds - segment.startSeconds;
        if (length <= 0.0)
            continue;
        std::array<double, 128> heard {};
        std::array<float, 128> confidence {};
        for (const auto& note : lines.bass)
        {
            const double overlap = std::min(segment.endSeconds, note.endSeconds) - std::max(segment.startSeconds, note.startSeconds);
            if (overlap <= 0.0 || note.midiNote < 0 || note.midiNote > 127)
                continue;
            heard[static_cast<size_t>(note.midiNote)] += overlap;
            confidence[static_cast<size_t>(note.midiNote)] = std::max(confidence[static_cast<size_t>(note.midiNote)], note.confidence);
        }
        const auto best = std::distance(heard.begin(), std::max_element(heard.begin(), heard.end()));
        const double share = heard[static_cast<size_t>(best)] / length;
        if (share < 0.35)
            continue;
        segment.midiNote = static_cast<int>(best);
        segment.confidence = std::max(segment.confidence,
                                      juce::jlimit(0.0f, 1.0f, 0.45f + 0.55f * static_cast<float>(std::min(1.0, share)) * confidence[static_cast<size_t>(best)]));
        segment.lowEnergy = std::max(segment.lowEnergy, 0.2f);
    }
}

juce::String SampleHarmony::keyName() const
{
    if (!valid)
        return "-";
    return juce::String(kNoteNames[keyRoot % 12]) + (scaleMode == 1 ? " major" : " minor");
}

juce::String SampleHarmony::describe() const
{
    if (!valid)
        return "Sample harmony: none";

    juce::String line = "Sample harmony: key " + keyName() + " (" + juce::String(keyConfidence, 2) + ")"
        + " | bass notes " + juce::String(confidentBassNotes()) + "/" + juce::String(static_cast<int>(bass.size())) + ":";
    for (const auto& segment : bass)
    {
        line << " ";
        if (segment.midiNote < 0 || segment.confidence < 0.35f)
            line << "-";
        else
            line << kNoteNames[segment.midiNote % 12] << (segment.midiNote / 12 - 1);
    }
    return line;
}

SampleHarmony SampleHarmonyAnalyzer::analyze(const std::vector<float>& mono,
                                             double sampleRate,
                                             double segmentSeconds,
                                             double originSeconds,
                                             double tuningCents,
                                             int labelKeyRoot,
                                             int labelKeyMode) const
{
    const double tuning = juce::jlimit(-50.0, 50.0, tuningCents) / 100.0;
    SampleHarmony harmony;
    if (mono.size() < static_cast<size_t>(kFrameSize / 2) || sampleRate < 8000.0 || segmentSeconds <= 0.05)
        return harmony;

    const double binHz = sampleRate / kFrameSize;
    const int frameCount = static_cast<int>(mono.size() / kHopSize) + 1;
    const double durationSeconds = static_cast<double>(mono.size()) / sampleRate;

    juce::dsp::FFT fft(kFftOrder);
    std::vector<float> window(static_cast<size_t>(kFrameSize));
    for (int i = 0; i < kFrameSize; ++i)
        window[static_cast<size_t>(i)] = 0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi * static_cast<float>(i) / static_cast<float>(kFrameSize));

    const int bassNoteCount = kHighestBassNote - kLowestBassNote + 1;
    std::vector<std::vector<float>> frameSalience(static_cast<size_t>(frameCount), std::vector<float>(static_cast<size_t>(bassNoteCount), 0.0f));
    std::vector<float> frameLowEnergy(static_cast<size_t>(frameCount), 0.0f);
    std::array<float, 12> chroma {};

    std::vector<float> data(static_cast<size_t>(kFrameSize * 2));
    auto magnitudeNear = [&](double hz)
    {
        // Strongest bin within +-half a semitone (at least +-1 bin).
        const double centre = hz / binHz;
        const double halfSemitone = std::max(1.0, centre * (std::pow(2.0, 1.0 / 24.0) - 1.0));
        const int lo = std::max(1, static_cast<int>(std::floor(centre - halfSemitone)));
        const int hi = std::min(kFrameSize / 2 - 1, static_cast<int>(std::ceil(centre + halfSemitone)));
        float best = 0.0f;
        for (int bin = lo; bin <= hi; ++bin)
            best = std::max(best, data[static_cast<size_t>(bin)]);
        return best;
    };

    for (int frame = 0; frame < frameCount; ++frame)
    {
        std::fill(data.begin(), data.end(), 0.0f);
        const int start = frame * kHopSize - kFrameSize / 2;
        for (int i = 0; i < kFrameSize; ++i)
        {
            const int index = start + i;
            if (index >= 0 && index < static_cast<int>(mono.size()))
                data[static_cast<size_t>(i)] = mono[static_cast<size_t>(index)] * window[static_cast<size_t>(i)];
        }
        fft.performFrequencyOnlyForwardTransform(data.data());

        auto& salience = frameSalience[static_cast<size_t>(frame)];
        for (int note = kLowestBassNote; note <= kHighestBassNote; ++note)
        {
            const double f0 = midiToHz(note + tuning);
            float value = 0.0f;
            for (size_t harmonic = 0; harmonic < kHarmonicWeights.size(); ++harmonic)
                value += kHarmonicWeights[harmonic] * magnitudeNear(f0 * static_cast<double>(harmonic + 1));
            // A note needs its own fundamental: otherwise the octave below (whose 2nd
            // harmonic is this fundamental) would win on harmonics alone.
            const float fundamental = magnitudeNear(f0);
            salience[static_cast<size_t>(note - kLowestBassNote)] = value * (0.25f + 0.75f * std::min(1.0f, fundamental / (value * 0.5f + 1.0e-9f)));
        }

        float low = 0.0f;
        for (int bin = static_cast<int>(30.0 / binHz); bin <= static_cast<int>(260.0 / binHz); ++bin)
            low += data[static_cast<size_t>(bin)];
        frameLowEnergy[static_cast<size_t>(frame)] = low;

        // Spectral peaks only: noise, attacks and the skirts of loud partials smear a summed
        // spectrum over neighbouring pitch classes (peak chroma: per-pack key MIREX 0.49 -> 0.61).
        for (int bin = static_cast<int>(65.0 / binHz); bin <= std::min(kFrameSize / 2 - 2, static_cast<int>(2100.0 / binHz)); ++bin)
        {
            const float magnitude = data[static_cast<size_t>(bin)];
            if (magnitude <= data[static_cast<size_t>(bin - 1)] || magnitude < data[static_cast<size_t>(bin + 1)])
                continue;
            const double hz = bin * binHz;
            const int pc = (static_cast<int>(std::lround(12.0 * std::log2(hz / 440.0) + 69.0 - tuning)) % 12 + 12) % 12;
            chroma[static_cast<size_t>(pc)] += data[static_cast<size_t>(bin)];
        }
    }

    const float maxLow = *std::max_element(frameLowEnergy.begin(), frameLowEnergy.end());
    const float chromaTotal = std::accumulate(chroma.begin(), chroma.end(), 0.0f);
    if (maxLow <= 1.0e-6f && chromaTotal <= 1.0e-6f)
        return harmony;

    // Bass per segment, aligned to beat 1.
    std::array<float, 12> bassPitchClassShare {};
    double segmentStart = originSeconds - std::floor(originSeconds / segmentSeconds) * segmentSeconds;
    if (segmentStart > 0.0)
        segmentStart -= segmentSeconds; // cover the lead-in too
    std::vector<float> segmentLow;
    for (; segmentStart < durationSeconds; segmentStart += segmentSeconds)
    {
        SampleBassSegment segment;
        segment.startSeconds = std::max(0.0, segmentStart);
        segment.endSeconds = std::min(durationSeconds, segmentStart + segmentSeconds);

        std::vector<float> salience(static_cast<size_t>(bassNoteCount), 0.0f);
        float low = 0.0f;
        int frames = 0;
        for (int frame = 0; frame < frameCount; ++frame)
        {
            const double centre = static_cast<double>(frame * kHopSize) / sampleRate;
            if (centre < segment.startSeconds || centre >= segment.endSeconds)
                continue;
            for (int n = 0; n < bassNoteCount; ++n)
                salience[static_cast<size_t>(n)] += frameSalience[static_cast<size_t>(frame)][static_cast<size_t>(n)];
            low += frameLowEnergy[static_cast<size_t>(frame)];
            ++frames;
        }
        if (frames == 0)
            continue;

        std::array<float, 12> pcScore {};
        for (int n = 0; n < bassNoteCount; ++n)
            pcScore[static_cast<size_t>((kLowestBassNote + n) % 12)] += salience[static_cast<size_t>(n)];
        const float pcTotal = std::accumulate(pcScore.begin(), pcScore.end(), 0.0f);
        const auto bestNote = std::distance(salience.begin(), std::max_element(salience.begin(), salience.end()));
        const int midi = kLowestBassNote + static_cast<int>(bestNote);
        const float share = pcTotal > 0.0f ? pcScore[static_cast<size_t>(midi % 12)] / pcTotal : 0.0f;

        segment.midiNote = midi;
        segment.confidence = juce::jlimit(0.0f, 1.0f, (share - 0.12f) / 0.30f);
        segment.lowEnergy = low / static_cast<float>(frames);
        segmentLow.push_back(segment.lowEnergy);
        harmony.bass.push_back(segment);
    }

    const float loudestSegment = segmentLow.empty() ? 0.0f : *std::max_element(segmentLow.begin(), segmentLow.end());
    for (auto& segment : harmony.bass)
    {
        segment.lowEnergy = loudestSegment > 0.0f ? segment.lowEnergy / loudestSegment : 0.0f;
        if (segment.lowEnergy < 0.15f)
        {
            segment.midiNote = -1; // bass is silent here
            segment.confidence = 0.0f;
        }
        if (segment.midiNote >= 0)
            bassPitchClassShare[static_cast<size_t>(segment.midiNote % 12)] += segment.confidence * segment.lowEnergy;
    }

    // Key: chroma correlation, nudged towards roots the bass actually sits on, and towards the
    // bass note the loop starts on (loops overwhelmingly open on the tonic).
    const float bassTotal = std::accumulate(bassPitchClassShare.begin(), bassPitchClassShare.end(), 0.0f);
    int openingBassPc = -1;
    for (const auto& segment : harmony.bass)
    {
        if (segment.startSeconds + 1.0e-6 >= originSeconds && segment.midiNote >= 0 && segment.confidence >= 0.35f)
        {
            openingBassPc = segment.midiNote % 12;
            break;
        }
    }
    for (auto& value : chroma)
        value /= std::max(1.0e-9f, chromaTotal); // peak chroma needs no compression (tested: sqrt is worse)
    const float chromaPeak = std::max(1.0e-9f, *std::max_element(chroma.begin(), chroma.end()));
    for (auto& value : chroma)
        value /= chromaPeak;
    harmony.chroma = chroma;

    float bestScore = -10.0f;
    float secondScore = -10.0f;
    for (int root = 0; root < 12; ++root)
    {
        for (int mode = 0; mode < 2; ++mode)
        {
            const float corr = correlation(chroma, mode == 1 ? kMajorProfileAS : kMinorProfileAS, root);
            // Relative keys share every note: the tonic is told by where the bass lives and
            // where the loop opens (bass loops open on the tonic: 75-85 % in the bass packs).
            // Minor prior: the genres served here are minor-led (255 of 283 labelled loops).
            // Weights from leave-one-pack-out over 9 packs; the opening note is weighted 0.5 (not
            // the 0.35 the packs alone favour) so an i-VI-III-VII loop whose loud bass roots make
            // the chroma look like VI major still reads as the minor key it opens on, and major
            // loops gain (7 -> 9 / 28 right).
            const float bassBonus = bassTotal > 0.0f ? 0.20f * bassPitchClassShare[static_cast<size_t>(root)] / bassTotal : 0.0f;
            const float openingBonus = root == openingBassPc ? 0.50f : 0.0f;
            const float minorPrior = mode == 0 ? 0.10f : 0.0f;
            const float score = corr + bassBonus + openingBonus + minorPrior;
            if (score > bestScore)
            {
                secondScore = bestScore;
                bestScore = score;
                harmony.keyRoot = root;
                harmony.scaleMode = mode;
            }
            else if (score > secondScore)
            {
                secondScore = score;
            }
        }
    }

    harmony.keyConfidence = juce::jlimit(0.0f, 1.0f, (bestScore - 0.35f) / 0.45f) * juce::jlimit(0.3f, 1.0f, (bestScore - secondScore) / 0.08f);
    // A key a fifth away (same mode) that fits the notes heard better than the chosen one: the
    // choice came from the bass / opening bonuses, and it is right far less often (step 16: 38 %
    // exact vs 64 %, lower in each of the 9 tonal packs, yet 86 % of these said >= 0.6). The cap
    // stays above the 0.4 the key is applied / the lines are read in key with: a 38 % key is still
    // better than none (a fifth away shares six notes), so only the shown confidence changes.
    {
        const auto& profile = harmony.scaleMode == 1 ? kMajorProfileAS : kMinorProfileAS;
        const float chosen = correlation(chroma, profile, harmony.keyRoot);
        const float neighbour = std::max(correlation(chroma, profile, (harmony.keyRoot + 7) % 12),
                                         correlation(chroma, profile, (harmony.keyRoot + 5) % 12));
        if (neighbour > chosen)
            harmony.keyConfidence = std::min(harmony.keyConfidence, 0.45f);
    }
    // The key the file name states (step 6). Fifth / relative / parallel keys share most notes
    // and are exactly where the audio is least sure, so the label wins there; a label whose scale
    // clashes with what is heard (wrong label, "Kit A" read as a key) is rejected.
    if (labelKeyRoot >= 0 && labelKeyRoot < 12)
    {
        int labelMode = labelKeyMode;
        if (labelMode < 0) // root only: the mode the audio prefers on that root
            labelMode = correlation(chroma, kMinorProfileAS, labelKeyRoot) + 0.10f
                            >= correlation(chroma, kMajorProfileAS, labelKeyRoot) ? 0 : 1;
        const float labelCorrelation = correlation(chroma, labelMode == 1 ? kMajorProfileAS : kMinorProfileAS, labelKeyRoot);
        if (labelCorrelation >= kKeyLabelMinCorrelation)
        {
            harmony.keyRoot = labelKeyRoot;
            harmony.scaleMode = labelMode;
            harmony.keyConfidence = 0.95f; // stated, and the notes fit
        }
        else
        {
            harmony.keyConfidence = std::min(harmony.keyConfidence, 0.5f); // the file says otherwise
        }
    }
    harmony.valid = true;
    return harmony;
}
} // namespace bbg
