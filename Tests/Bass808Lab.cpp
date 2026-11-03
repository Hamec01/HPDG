// HPDG 808, second pass (2026-11-01): renders every preset and checks the engine with numbers.
//
//   HPDG_Bass808Lab [out]          renders + checks (exit 1 on a failed check)
//   HPDG_Bass808Lab --fit <dir>    one E1 note (4 s) per preset, for tools/bass808_corpus.py
//
// Checks: every preset at C1 / F1 / C2 is finite and its peak sits between -14 and 0 dBFS; the sample
// layer plays a 55 Hz sample at 110 Hz on A2; LOCK TIME keeps the length on every note; Stretch 2x doubles
// the length and keeps the pitch; a loop keeps the level and has no step; a handoff keeps the synth silent
// before it; Phantom raises harmonics 2-5; every saturator model stays finite and a model change mid-note does not click.
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>

#include "Audio/Synth808Voice.h"
#include "Bass808/BassEngine.h"

using namespace bbg;

namespace
{
constexpr double kRate = 48000.0;
constexpr double kPi = 3.14159265358979323846;
int failures = 0;

void check(bool ok, const char* what, const juce::String& detail)
{
    std::printf("  [%s] %s  %s\n", ok ? " ok " : "FAIL", what, detail.toRawUTF8());
    if (!ok)
        ++failures;
}

double rmsDb(const std::vector<float>& x, double from, double to)
{
    const size_t a = static_cast<size_t>(from * kRate), b = std::min(x.size(), static_cast<size_t>(to * kRate));
    double sum = 0.0;
    for (size_t i = a; i < b; ++i)
        sum += static_cast<double>(x[i]) * x[i];
    return 10.0 * std::log10(sum / std::max<size_t>(1, b - a) + 1.0e-20);
}

float peak(const std::vector<float>& x)
{
    float p = 0.0f;
    for (auto v : x)
        p = std::max(p, std::abs(v));
    return p;
}

bool finite(const std::vector<float>& x)
{
    for (auto v : x)
        if (!std::isfinite(v))
            return false;
    return true;
}

// Frequency from rising zero crossings between from and to (s).
double frequency(const std::vector<float>& x, double from, double to)
{
    const size_t a = static_cast<size_t>(from * kRate), b = std::min(x.size() - 1, static_cast<size_t>(to * kRate));
    double first = -1.0, last = -1.0;
    int crossings = 0;
    for (size_t i = a; i < b; ++i)
        if (x[i] <= 0.0f && x[i + 1] > 0.0f)
        {
            const double t = static_cast<double>(i) + x[i] / (x[i] - x[i + 1]);
            if (first < 0.0)
                first = t;
            last = t;
            ++crossings;
        }
    return crossings > 1 ? (crossings - 1) * kRate / (last - first) : 0.0;
}

// Seconds until the 20 ms level is `drop` dB under its peak.
double lengthTo(const std::vector<float>& x, double drop)
{
    const int w = static_cast<int>(0.02 * kRate);
    std::vector<double> level(x.size(), 0.0);
    double sum = 0.0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        sum += static_cast<double>(x[i]) * x[i];
        if (i >= static_cast<size_t>(w))
            sum -= static_cast<double>(x[i - static_cast<size_t>(w)]) * x[i - static_cast<size_t>(w)];
        level[i] = sum;
    }
    const auto top = std::max_element(level.begin(), level.end());
    const double threshold = *top * std::pow(10.0, -drop / 10.0);
    for (auto it = top; it != level.end(); ++it)
        if (*it < threshold)
            return static_cast<double>(std::distance(level.begin(), it)) / kRate;
    return static_cast<double>(x.size()) / kRate;
}

double harmonicDb(const std::vector<float>& x, double f0, int n, double from, double to)
{
    const size_t a = static_cast<size_t>(from * kRate), b = std::min(x.size(), static_cast<size_t>(to * kRate));
    std::complex<double> c1, cn;
    for (size_t i = a; i < b; ++i)
    {
        const double t = static_cast<double>(i - a) / kRate, w = 0.5 - 0.5 * std::cos(2.0 * kPi * (i - a) / (b - a));
        c1 += x[i] * w * std::polar(1.0, -2.0 * kPi * f0 * t);
        cn += x[i] * w * std::polar(1.0, -2.0 * kPi * f0 * n * t);
    }
    return 20.0 * std::log10(std::abs(cn) / (std::abs(c1) + 1.0e-20) + 1.0e-20);
}

void writeWav(const juce::File& file, const std::vector<float>& x)
{
    file.deleteFile();
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(new juce::FileOutputStream(file), kRate, 1, 24, {}, 0));
    if (writer == nullptr)
        return;
    const float* channels[] { x.data() };
    writer->writeFromFloatArrays(channels, 1, static_cast<int>(x.size()));
}

juce::String fileName(const BassPreset& p)
{
    return (juce::String(p.genre) + " " + p.name).replaceCharacters(" /", "__");
}

// A decaying sine at `hz` (optionally with a kick's pitch drop at the start), 44.1 kHz.
std::shared_ptr<const BassSample> testSample(double hz, double seconds, double decaySeconds, double dropHz)
{
    const double rate = 44100.0;
    std::vector<float> x(static_cast<size_t>(seconds * rate));
    double phase = 0.0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double t = static_cast<double>(i) / rate;
        phase += (hz + dropHz * std::exp(-t / 0.015)) / rate;
        x[i] = static_cast<float>(std::sin(2.0 * kPi * phase) * std::exp(-t / decaySeconds));
    }
    return BassSample::fromBuffer(std::move(x), rate, "test");
}

BassSettings sampleOnly()
{
    BassSettings s;
    s.synthLevelDb = -60.0f;
    s.sampleLevelDb = 0.0f;
    s.clickDb = -60.0f;
    s.attackMs = 0.5f;
    s.holdSeconds = 3.0f;
    s.decaySeconds = 4.0f;
    s.sustain = 1.0f;
    return s;
}

std::vector<float> phrase(const BassSettings& s)
{
    // 140 BPM: notes, a glide, a short repeat
    BassEngine engine;
    auto settings = s;
    engine.prepare(kRate, 512);
    engine.setSettings(settings);
    const double beat = 60.0 / 140.0;
    struct Event { double at; int note; bool on; };
    const std::vector<Event> events { { 0.0, 33, true }, { 1.5, 33, false }, { 2.0, 33, true }, { 2.5, 33, false }, { 3.0, 36, true },
                                      { 3.5, 38, true }, { 4.0, 36, false }, { 4.5, 38, false }, { 5.0, 31, true }, { 7.5, 31, false } };
    const int latency = engine.latencySamples();
    std::vector<float> out(static_cast<size_t>(8.5 * beat * kRate) + static_cast<size_t>(latency), 0.0f);
    size_t position = 0;
    for (const auto& e : events)
    {
        const size_t at = static_cast<size_t>(e.at * beat * kRate);
        engine.render(out.data() + position, static_cast<int>(at - position));
        position = at;
        if (e.on)
            engine.noteOn(e.note, 0.9f);
        else
            engine.noteOff(e.note);
    }
    engine.render(out.data() + position, static_cast<int>(out.size() - position));
    out.erase(out.begin(), out.begin() + latency);
    return out;
}
} // namespace

int main(int argc, char** argv)
{
    const auto& presets = bassPresets();

    if (argc >= 3 && juce::String(argv[1]) == "--fit")
    {
        const juce::File dir { juce::String(argv[2]) };
        dir.createDirectory();
        for (const auto& p : presets)
            writeWav(dir.getChildFile(fileName(p) + ".wav"), BassEngine::renderNote(p.settings, nullptr, 28, 4.0, kRate));
        std::printf("wrote %d E1 notes to %s\n", static_cast<int>(presets.size()), dir.getFullPathName().toRawUTF8());
        return 0;
    }
    const juce::File out(argc >= 2 ? juce::String(argv[1]) : juce::File::getCurrentWorkingDirectory().getChildFile("bass808").getFullPathName());
    out.createDirectory();

    std::printf("presets (peak dBFS at C1 / F1 / C2;  -6 / -24 dB length at C2)\n");
    for (const auto& p : presets)
    {
        juce::String line;
        bool ok = true;
        for (int note : { 24, 29, 36 })
        {
            const auto x = BassEngine::renderNote(p.settings, nullptr, note, 2.5, kRate);
            const double db = 20.0 * std::log10(peak(x) + 1.0e-9);
            ok = ok && finite(x) && (p.settings.synthLevelDb <= -59.5f || (db > -14.0 && db <= 0.0));
            line += juce::String(db, 1) + "  ";
            if (note == 36)
            {
                line += "|  " + juce::String(lengthTo(x, 6.0), 2) + " / " + juce::String(lengthTo(x, 24.0), 2) + " s";
                writeWav(out.getChildFile(fileName(p) + "_C2.wav"), x);
            }
            if (note == 24)
                writeWav(out.getChildFile(fileName(p) + "_C1.wav"), x);
        }
        check(ok, (juce::String(p.genre) + " " + p.name).toRawUTF8(), line);
        writeWav(out.getChildFile(fileName(p) + "_phrase.wav"), phrase(p.settings));
    }

    std::printf("sample layer\n");
    {
        const auto kick = testSample(55.0, 1.5, 0.35, 150.0);
        const auto sine = testSample(55.0, 3.0, 2.0, 0.0);
        check(std::abs(sine->rootHz - 55.0) < 0.55, "pitch of a 55 Hz sine", juce::String(sine->rootHz, 2) + " Hz");
        check(std::abs(kick->rootHz - 55.0) < 1.65, "pitch of a kick (55 Hz body, +150 Hz drop)", juce::String(kick->rootHz, 2) + " Hz");

        auto s = sampleOnly();
        const auto a2 = BassEngine::renderNote(s, sine, 45, 1.0, kRate);
        const double f = frequency(a2, 0.2, 0.8);
        check(std::abs(f / 110.0 - 1.0) < 0.005, "55 Hz sample on A2", juce::String(f, 2) + " Hz (110)");

        auto noFollow = s;
        noFollow.sampleFollow = false;
        const double fixedHz = frequency(BassEngine::renderNote(noFollow, sine, 45, 1.0, kRate), 0.2, 0.8);
        check(std::abs(fixedHz / 55.0 - 1.0) < 0.005, "FOLLOW KEY off: A2 plays the sample's own pitch", juce::String(fixedHz, 2) + " Hz (55)");

        auto plain = s;
        plain.sustain = 1.0f;
        const double lengthA1 = lengthTo(BassEngine::renderNote(plain, kick, 33, 3.0, kRate), 30.0);
        const double lengthA2 = lengthTo(BassEngine::renderNote(plain, kick, 45, 3.0, kRate), 30.0);
        auto locked = plain;
        locked.sampleLockTime = true;
        const auto lockedA2 = BassEngine::renderNote(locked, kick, 45, 3.0, kRate);
        const double lockA1 = lengthTo(BassEngine::renderNote(locked, kick, 33, 3.0, kRate), 30.0);
        const double lockA2 = lengthTo(lockedA2, 30.0);
        check(lengthA2 < 0.6 * lengthA1, "without LOCK TIME an octave up is shorter",
              juce::String(lengthA1, 2) + " s -> " + juce::String(lengthA2, 2) + " s");
        check(std::abs(lockA2 / lockA1 - 1.0) < 0.10, "LOCK TIME: the same length an octave up",
              juce::String(lockA1, 2) + " s -> " + juce::String(lockA2, 2) + " s");
        const double lockedHz = frequency(lockedA2, 0.15, 0.6);
        check(std::abs(lockedHz / 110.0 - 1.0) < 0.02, "LOCK TIME keeps the pitch", juce::String(lockedHz, 2) + " Hz (110)");

        auto stretched = plain;
        stretched.sampleStretch = 2.0f;
        const auto stretchedA1 = BassEngine::renderNote(stretched, kick, 33, 3.0, kRate);
        const double stretchLength = lengthTo(stretchedA1, 30.0);
        check(std::abs(stretchLength / lengthA1 - 2.0) < 0.2, "Stretch 2x doubles the length",
              juce::String(lengthA1, 2) + " s -> " + juce::String(stretchLength, 2) + " s");
        const double stretchHz = frequency(stretchedA1, 0.2, 0.8);
        check(std::abs(stretchHz / 55.0 - 1.0) < 0.02, "Stretch keeps the pitch", juce::String(stretchHz, 2) + " Hz (55)");

        auto looped = plain;
        looped.sampleLoop = true;
        looped.sampleLoopStart = 0.12f;
        looped.sampleEnd = 0.2f;
        const auto loopA1 = BassEngine::renderNote(looped, kick, 33, 2.5, kRate);
        const double drop = rmsDb(loopA1, 1.8, 2.2) - rmsDb(loopA1, 0.2, 0.3);
        float step = 0.0f;
        for (size_t i = static_cast<size_t>(0.35 * kRate); i < static_cast<size_t>(2.4 * kRate); ++i)
            step = std::max(step, std::abs(loopA1[i] - loopA1[i - 1]));
        const float slope = static_cast<float>(2.0 * kPi * 55.0 / kRate) * peak(loopA1);
        check(drop > -9.0, "LOOP keeps the body going", juce::String(drop, 1) + " dB at 2 s against 0.25 s");
        check(step < 2.0f * slope, "LOOP has no step", "largest sample step " + juce::String(step / slope, 2) + " x a sine's slope");
        writeWav(out.getChildFile("check_kick_loop.wav"), loopA1);

        auto handoff = bassPresets()[static_cast<size_t>(Synth808Voice::kPresetCount + 9)].settings; // Kick + 808
        handoff.sampleLevelDb = -59.0f;
        const auto synthPart = BassEngine::renderNote(handoff, kick, 33, 1.0, kRate);
        const double before = rmsDb(synthPart, 0.0, 0.06), after = rmsDb(synthPart, 0.15, 0.3);
        check(before < after - 30.0, "handoff: the synth waits for it", juce::String(before - after, 1) + " dB before 60 ms");
        auto both = bassPresets()[static_cast<size_t>(Synth808Voice::kPresetCount + 9)].settings;
        writeWav(out.getChildFile("check_kick_plus_808.wav"), BassEngine::renderNote(both, kick, 33, 2.0, kRate));
    }

    std::printf("phantom and saturators\n");
    {
        BassSettings sub;
        sub.punchSemitones = 0.0f;
        sub.drive = 0.5f;
        sub.saturation = 0.0f;
        sub.clickDb = -60.0f;
        sub.sustain = 1.0f;
        const auto dry = BassEngine::renderNote(sub, nullptr, 28, 1.0, kRate);
        sub.phantom = 0.5f;
        const auto wet = BassEngine::renderNote(sub, nullptr, 28, 1.0, kRate);
        const double f0 = BassEngine::noteHz(28, 0.0f);
        juce::String text;
        bool ok = true;
        for (int n = 2; n <= 5; ++n)
        {
            const double a = harmonicDb(dry, f0, n, 0.3, 0.9), b = harmonicDb(wet, f0, n, 0.3, 0.9);
            ok = ok && b > a + 10.0 && b > -30.0;
            text += "h" + juce::String(n) + " " + juce::String(a, 0) + " -> " + juce::String(b, 0) + "  ";
        }
        check(ok, "Phantom adds harmonics 2-5 (dB re the fundamental)", text);

        for (int model = 1; model <= 6; ++model)
        {
            BassSettings s;
            s.satModel = model;
            s.satDriveDb = 24.0f;
            const auto x = BassEngine::renderNote(s, nullptr, 29, 1.5, kRate);
            check(finite(x) && peak(x) < 2.0f, ("saturator model " + juce::String(model)).toRawUTF8(),
                  "peak " + juce::String(20.0 * std::log10(peak(x) + 1.0e-9), 1) + " dBFS at 24 dB drive");
        }

        // a held note while the model changes (Tape -> Vinyl, on -> off -> on): the largest sample-to-sample
        // step after each change against the largest before it (a click shows as a step far above the wave's own)
        {
            BassEngine engine;
            BassSettings s;
            s.satModel = 2;
            s.satDriveDb = 18.0f;
            s.holdSeconds = 4.0f;
            s.decaySeconds = 4.0f;
            engine.setSettings(s);
            engine.prepare(kRate, 512);
            engine.noteOn(36, 1.0f);
            std::vector<float> x(static_cast<size_t>(2.0 * kRate), 0.0f);
            const auto stepAround = [&x](size_t from, size_t to)
            {
                float most = 0.0f;
                for (size_t i = std::max<size_t>(from, 1); i < std::min(to, x.size()); ++i)
                    most = std::max(most, std::abs(x[i] - x[i - 1]));
                return most;
            };
            const int quarter = static_cast<int>(0.5 * kRate);
            float worst = 0.0f;
            for (int part = 0; part < 4; ++part)
            {
                if (part > 0)
                {
                    s.satModel = part == 1 ? 6 : part == 2 ? 0 : 3;
                    engine.setSettings(s);
                }
                engine.render(x.data() + part * quarter, quarter);
                if (part > 0)
                {
                    const auto at = static_cast<size_t>(part * quarter);
                    const float before = stepAround(at - static_cast<size_t>(0.1 * kRate), at);
                    worst = std::max(worst, stepAround(at, at + static_cast<size_t>(0.03 * kRate)) / std::max(1.0e-6f, before));
                }
            }
            check(finite(x) && worst < 1.5f, "saturator model change mid-note: no click",
                  "largest step after a change " + juce::String(worst, 2) + " x the one before");
        }
    }

    std::printf("%s  (%d failed)  renders: %s\n", failures == 0 ? "OK" : "FAILED", failures, out.getFullPathName().toRawUTF8());
    return failures == 0 ? 0 : 1;
}
