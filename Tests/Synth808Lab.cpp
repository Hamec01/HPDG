// Synth808Lab: HPDG's 808 synth voice on its own, before it is wired into the Sub808 lane.
//
//   HPDG_Synth808Lab [<out folder>] [<Samples folder>]
//
//   HPDG_Synth808Lab --fit <folder>   (one E1 note per preset, for tools/bass808_corpus.py)
//
// Writes, per preset, one note at C1 / F1 / C2 (3 s each) and a short 808
// phrase at 140 BPM (quick notes, glides, retriggers) to <out folder> (default TempAudio/synth808)
// for listening. Prints, per note, where the energy sits (the bands of tools/synth_808_bands.py,
// dB against the sub) and how long the note holds (-6 dB), for the synth and for the pre-rendered
// WAV played the way the lane plays it (faster / slower). Exit code 1 when a preset does not sound
// the same on every note: hold off by more than 20 % of C2's (the pre-rendered WAVs drift 40-100 %), low-mid spread over 3 dB, or highs
// above -60 dB.

#include <cmath>
#include <cstdio>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

#include "Audio/Synth808Voice.h"

using namespace bbg;

namespace
{
constexpr double kRate = 48000.0;

struct Pitch
{
    const char* name;
    int midi;
};
constexpr Pitch kPitches[] { { "C1", 24 }, { "F1", 29 }, { "C2", 36 } };

double midiHz(double midi)
{
    return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0);
}

struct Measure
{
    double lowMid = 0.0, mid = 0.0, high = 0.0, hold = 0.0;
};

Measure measure(const std::vector<float>& y, double f0)
{
    Measure m;
    constexpr int order = 17;
    juce::dsp::FFT fft(order);
    const int size = 1 << order;
    const int segment = std::min(static_cast<int>(1.5 * kRate), static_cast<int>(y.size()));
    std::vector<float> data(static_cast<size_t>(2 * size), 0.0f);
    for (int i = 0; i < segment; ++i)
        data[static_cast<size_t>(i)] = y[static_cast<size_t>(i)]
            * static_cast<float>(0.5 - 0.5 * std::cos(2.0 * juce::MathConstants<double>::pi * i / (segment - 1)));
    fft.performFrequencyOnlyForwardTransform(data.data());

    const double edges[] { 0.0, 1.6 * f0, 400.0, 1000.0, kRate / 2.0 };
    double band[4] {};
    for (int bin = 0; bin <= size / 2; ++bin)
    {
        const double f = bin * kRate / size;
        const double power = static_cast<double>(data[static_cast<size_t>(bin)]) * data[static_cast<size_t>(bin)];
        for (int b = 0; b < 4; ++b)
            if (f >= edges[b] && f < edges[b + 1])
                band[b] += power;
    }
    const auto db = [&](double v) { return 10.0 * std::log10(v + 1.0e-30) - 10.0 * std::log10(band[0] + 1.0e-30); };
    m.lowMid = db(band[1]);
    m.mid = db(band[2]);
    m.high = db(band[3]);

    const int w = static_cast<int>(0.04 * kRate);
    std::vector<double> power;
    double acc = 0.0;
    for (size_t i = 0; i < y.size(); ++i)
    {
        acc += static_cast<double>(y[i]) * y[i];
        if (i >= static_cast<size_t>(w))
            acc -= static_cast<double>(y[i - w]) * y[i - w];
        if (i + 1 >= static_cast<size_t>(w))
            power.push_back(10.0 * std::log10(acc / w + 1.0e-12));
    }
    size_t peak = 0;
    for (size_t i = 0; i < power.size() && i < static_cast<size_t>(0.2 * kRate); ++i)
        if (power[i] > power[peak])
            peak = i;
    size_t below = peak;
    while (below < power.size() && power[below] >= power[peak] - 6.0)
        ++below;
    m.hold = static_cast<double>(below - peak) / kRate;
    return m;
}

std::vector<float> renderNote(int preset, double hz, double seconds)
{
    Synth808Voice voice;
    voice.prepare(kRate);
    voice.setParams(Synth808Voice::presets()[static_cast<size_t>(preset)], Synth808Voice::presetGain(preset));
    voice.noteOn(hz, 1.0f);
    std::vector<float> out(static_cast<size_t>(seconds * kRate), 0.0f);
    voice.renderAdd(out.data(), static_cast<int>(out.size()));
    return out;
}

// The pre-rendered WAV (tuned to C2) as the lane plays it: read faster / slower.
std::vector<float> playWav(const juce::File& file, double hz, double seconds)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr)
        return {};
    juce::AudioBuffer<float> buffer(1, static_cast<int>(reader->lengthInSamples));
    reader->read(&buffer, 0, buffer.getNumSamples(), 0, true, false);
    const double step = hz / Synth808Voice::kC2Hz * reader->sampleRate / kRate;
    std::vector<float> out(static_cast<size_t>(seconds * kRate), 0.0f);
    for (size_t i = 0; i < out.size(); ++i)
    {
        const double pos = static_cast<double>(i) * step;
        const int base = static_cast<int>(pos);
        if (base + 1 >= buffer.getNumSamples())
            break;
        const float frac = static_cast<float>(pos - base);
        out[i] = buffer.getSample(0, base) + (buffer.getSample(0, base + 1) - buffer.getSample(0, base)) * frac;
    }
    return out;
}

// A dark-trap-like phrase at 140 BPM: (start beat, length beats, MIDI note, glide into it).
std::vector<float> renderPhrase(int preset)
{
    struct Step
    {
        double start, length;
        int midi;
        bool glide;
    };
    static const Step steps[] {
        { 0.0, 1.5, 28, false }, { 1.75, 0.25, 28, false }, { 2.0, 1.0, 28, false }, { 3.0, 1.0, 31, true },
        { 4.0, 2.5, 28, false }, { 6.5, 0.25, 28, false }, { 7.0, 1.0, 33, false },
        { 8.0, 1.5, 28, false }, { 9.75, 0.25, 28, false }, { 10.0, 2.0, 24, true }, { 12.0, 3.5, 28, false },
    };
    const double beat = 60.0 / 140.0 * kRate;
    std::vector<float> out(static_cast<size_t>(16.5 * beat), 0.0f);
    std::array<Synth808Voice, 2> voices;
    for (auto& v : voices)
    {
        v.prepare(kRate);
        v.setParams(Synth808Voice::presets()[static_cast<size_t>(preset)], Synth808Voice::presetGain(preset));
    }
    int current = 0;
    size_t stepIndex = 0;
    int64_t noteEnd = -1;
    for (size_t n = 0; n < out.size(); ++n)
    {
        if (stepIndex < std::size(steps) && n == static_cast<size_t>(steps[stepIndex].start * beat))
        {
            const auto& s = steps[stepIndex++];
            if (s.glide && voices[static_cast<size_t>(current)].isActive())
            {
                voices[static_cast<size_t>(current)].glideTo(midiHz(s.midi), static_cast<int>(0.12 * kRate));
            }
            else
            {
                voices[static_cast<size_t>(current)].noteOff(static_cast<int>(0.003 * kRate)); // mono: 3 ms choke
                current = 1 - current;
                voices[static_cast<size_t>(current)].noteOn(midiHz(s.midi), 0.9f);
            }
            noteEnd = static_cast<int64_t>((s.start + s.length) * beat);
        }
        if (static_cast<int64_t>(n) == noteEnd)
            voices[static_cast<size_t>(current)].noteOff(static_cast<int>(0.006 * kRate)); // 6 ms release
        for (auto& v : voices)
            out[n] += v.renderSample();
    }
    return out;
}

void writeWav(const juce::File& file, const std::vector<float>& samples)
{
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
    if (stream == nullptr)
        return;
    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream.get(), kRate, 1, 24, {}, 0));
    if (writer == nullptr)
        return;
    stream.release();
    const float* channels[] { samples.data() };
    writer->writeFromFloatArrays(channels, 1, static_cast<int>(samples.size()));
}
} // namespace

int main(int argc, char* argv[])
{
    const auto cwd = juce::File::getCurrentWorkingDirectory();
    if (argc > 2 && juce::String(argv[1]) == "--fit")
    {
        // One E1 note (4 s) per preset, for tools/bass808_corpus.py (the same measurement as the
        // pro one-shots the presets are fitted to).
        const auto fitDir = cwd.getChildFile(argv[2]);
        fitDir.createDirectory();
        for (int p = 0; p < Synth808Voice::kPresetCount; ++p)
            writeWav(fitDir.getChildFile(juce::String(Synth808Voice::presets()[static_cast<size_t>(p)].name).removeCharacters(" ") + ".wav"),
                     renderNote(p, midiHz(28), 4.0));
        return 0;
    }
    const juce::File outDir = argc > 1 ? cwd.getChildFile(argv[1]) : cwd.getChildFile("TempAudio/synth808");
    const juce::File samples = argc > 2 ? cwd.getChildFile(argv[2]) : cwd.getChildFile("Samples");
    outDir.createDirectory();

    bool ok = true;
    std::printf("| preset | pitch | synth low-mid | mid | high | hold s | WAV low-mid | mid | high | hold s |\n");
    std::printf("|---|---|---|---|---|---|---|---|---|---|\n");
    for (int p = 0; p < Synth808Voice::kPresetCount; ++p)
    {
        const juce::String name = Synth808Voice::presets()[static_cast<size_t>(p)].name;
        const auto stem = name.removeCharacters(" ");
        const auto wavFile = samples.getChildFile("Synth/Sub808/Sub808" + stem + ".wav");
        Measure atC2;
        double lowMidMin = 1.0e9, lowMidMax = -1.0e9;
        std::vector<Measure> perPitch;
        for (const auto& pitch : kPitches)
        {
            const double hz = midiHz(pitch.midi);
            const auto note = renderNote(p, hz, 3.0);
            writeWav(outDir.getChildFile(stem + "_" + pitch.name + ".wav"), note);
            const auto s = measure(note, hz);
            const auto wav = playWav(wavFile, hz, 3.0);
            const auto w = wav.empty() ? Measure {} : measure(wav, hz);
            std::printf("| %s | %s | %.1f | %.1f | %.1f | %.2f | %.1f | %.1f | %.1f | %.2f |\n",
                        name.toRawUTF8(), pitch.name, s.lowMid, s.mid, s.high, s.hold, w.lowMid, w.mid, w.high, w.hold);
            perPitch.push_back(s);
            lowMidMin = std::min(lowMidMin, s.lowMid);
            lowMidMax = std::max(lowMidMax, s.lowMid);
            if (pitch.midi == 36)
                atC2 = s;
        }
        for (size_t i = 0; i < perPitch.size(); ++i)
        {
            const auto& s = perPitch[i];
            if (std::abs(s.hold - atC2.hold) > 0.20 * atC2.hold || s.high > -60.0)
            {
                std::printf("FAIL %s %s: hold %.2f s (C2 %.2f s), high %.1f dB\n", name.toRawUTF8(), kPitches[i].name, s.hold, atC2.hold, s.high);
                ok = false;
            }
        }
        if (lowMidMax - lowMidMin > 3.0)
        {
            std::printf("FAIL %s: low-mid spread %.1f dB over C1-C2\n", name.toRawUTF8(), lowMidMax - lowMidMin);
            ok = false;
        }
        writeWav(outDir.getChildFile(stem + "_phrase.wav"), renderPhrase(p));
    }
    std::printf("%s, WAVs in %s\n", ok ? "OK" : "FAILED", outDir.getFullPathName().toRawUTF8());
    return ok ? 0 : 1;
}
