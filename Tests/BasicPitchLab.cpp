// The C++ Basic Pitch port against the Python original (tools/basic_pitch_export.py reference).
//
//   HPDG_BasicPitchLab selftest
//       synthetic arpeggio + chord: notes, key, threads (ctest, no files needed)
//   HPDG_BasicPitchLab ref <reference folder> [threads]
//       runs the port on audio.f32 and compares note / onset / contour frames and the notes
//       with Basic Pitch's (exact = same start / end frame and pitch)
//   HPDG_BasicPitchLab keys <files .tsv> <notes folder> <out .tsv>
//       estimateKeyFromNotes on a run's note files (to compare with transcription_key.py arm B2)
//   HPDG_BasicPitchLab corpus <files .tsv> <out folder> [threads]
//       files from `tools/transcription_key.py files`: decode, mix to mono, resample to 22.05 kHz,
//       transcribe -> <out>/notes/<id>/<file>.csv and <out>/timing.csv, the layout of
//       `transcription_key.py run`, so its `report` scores the port and the original alike
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>

#include "Analysis/BasicPitch/BasicPitchTranscriber.h"

namespace
{
std::vector<float> readFloats(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<float> values(bytes.size() / sizeof(float));
    std::copy(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(values.size() * sizeof(float)), reinterpret_cast<char*>(values.data()));
    return values;
}

struct Diff
{
    double maxAbs = 0.0, meanAbs = 0.0;
    size_t count = 0;
};

Diff compare(const std::vector<float>& a, const std::vector<float>& b)
{
    Diff d;
    d.count = std::min(a.size(), b.size());
    double sum = 0.0;
    for (size_t i = 0; i < d.count; ++i)
    {
        const double e = std::abs(static_cast<double>(a[i]) - b[i]);
        d.maxAbs = std::max(d.maxAbs, e);
        sum += e;
    }
    d.meanAbs = d.count > 0 ? sum / static_cast<double>(d.count) : 0.0;
    return d;
}

struct RefNote
{
    double start, end;
    int midi;
    double amplitude;
};

std::vector<RefNote> readNotes(const std::string& path)
{
    std::vector<RefNote> notes;
    std::ifstream in(path);
    std::string line;
    std::getline(in, line);
    while (std::getline(in, line))
    {
        RefNote n {};
        if (std::sscanf(line.c_str(), "%lf,%lf,%d,%lf", &n.start, &n.end, &n.midi, &n.amplitude) == 4)
            notes.push_back(n);
    }
    return notes;
}

int runReference(const std::string& folder, int threads)
{
    const auto audio = readFloats(folder + "/audio.f32");
    if (audio.empty())
    {
        std::printf("no audio.f32 in %s\n", folder.c_str());
        return 2;
    }
    const auto began = std::chrono::steady_clock::now();
    const auto frames = bbg::BasicPitchTranscriber::infer(audio.data(), static_cast<int>(audio.size()), threads);
    const auto inferred = std::chrono::steady_clock::now();
    auto notes = bbg::BasicPitchTranscriber::notes(frames);
    const auto decoded = std::chrono::steady_clock::now();
    const double seconds = static_cast<double>(audio.size()) / bbg::BasicPitchTranscriber::kSampleRate;
    const double inferS = std::chrono::duration<double>(inferred - began).count();
    const double decodeS = std::chrono::duration<double>(decoded - inferred).count();

    const auto refNote = readFloats(folder + "/note.f32");
    const auto refOnset = readFloats(folder + "/onset.f32");
    const auto refContour = readFloats(folder + "/contour.f32");
    std::printf("%s: %.2f s audio, frames %d (python %zu)\n", folder.c_str(), seconds, frames.frames, refNote.size() / 88);
    const char* names[] = { "note", "onset", "contour" };
    const Diff diffs[] = { compare(frames.note, refNote), compare(frames.onset, refOnset), compare(frames.contour, refContour) };
    for (int i = 0; i < 3; ++i)
        std::printf("  %-8s max |diff| %.6f  mean %.7f\n", names[i], diffs[i].maxAbs, diffs[i].meanAbs);

    auto reference = readNotes(folder + "/notes.csv");
    std::sort(notes.begin(), notes.end(), [](const auto& a, const auto& b)
    {
        return std::tie(a.startSeconds, a.endSeconds, a.midi) < std::tie(b.startSeconds, b.endSeconds, b.midi);
    });
    int exact = 0;
    double worstAmplitude = 0.0;
    for (const auto& n : notes)
        for (const auto& r : reference)
            if (r.midi == n.midi && std::abs(r.start - n.startSeconds) < 1.0e-4 && std::abs(r.end - n.endSeconds) < 1.0e-4)
            {
                ++exact;
                worstAmplitude = std::max(worstAmplitude, std::abs(r.amplitude - n.amplitude));
                break;
            }
    std::printf("  notes %zu (python %zu), exact %d, worst amplitude diff %.6f\n", notes.size(), reference.size(), exact, worstAmplitude);
    std::printf("  speed: inference %.3f s + notes %.3f s = %.1fx real time (%d threads)\n", inferS, decodeS, seconds / (inferS + decodeS), threads);
    return 0;
}
std::vector<float> readMono(const juce::File& file, double& sampleRate)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return {};
    sampleRate = reader->sampleRate;
    const int length = static_cast<int>(reader->lengthInSamples);
    const int channels = static_cast<int>(reader->numChannels);
    juce::AudioBuffer<float> buffer(channels, length);
    reader->read(&buffer, 0, length, 0, true, true);
    std::vector<float> mono(static_cast<size_t>(length), 0.0f);
    for (int c = 0; c < channels; ++c)
        for (int i = 0; i < length; ++i)
            mono[static_cast<size_t>(i)] += buffer.getSample(c, i) / static_cast<float>(channels);
    return mono;
}

// estimateKeyFromNotes on note files (either run's) -> id, file, root, mode (0 minor, 1 major)
int runKeys(const std::string& list, const std::string& notesRoot, const std::string& out)
{
    std::ifstream in(list);
    std::string line, result;
    int count = 0;
    while (std::getline(in, line))
    {
        std::stringstream fields(line);
        std::string id, name;
        if (!std::getline(fields, id, '\t') || !std::getline(fields, name, '\t'))
            continue;
        const auto file = juce::File(juce::String::fromUTF8(notesRoot.c_str())).getChildFile("notes")
                              .getChildFile(juce::String::fromUTF8(id.c_str())).getChildFile(juce::String::fromUTF8(name.c_str()) + ".csv");
        if (!file.existsAsFile())
            continue;
        std::vector<bbg::BasicPitchNote> notes;
        juce::StringArray rows;
        rows.addLines(file.loadFileAsString());
        for (int r = 1; r < rows.size(); ++r)
        {
            bbg::BasicPitchNote n;
            double amplitude = 0.0;
            if (std::sscanf(rows[r].toRawUTF8(), "%lf,%lf,%d,%lf", &n.startSeconds, &n.endSeconds, &n.midi, &amplitude) == 4)
            {
                n.amplitude = static_cast<float>(amplitude);
                notes.push_back(n);
            }
        }
        const auto key = bbg::estimateKeyFromNotes(notes);
        result += id + "\t" + name + "\t" + (key.valid ? std::to_string(key.root) + "\t" + std::to_string(key.mode) : std::string("-\t-")) + "\n";
        ++count;
    }
    juce::File(juce::String::fromUTF8(out.c_str())).replaceWithText(juce::String::fromUTF8(result.c_str()), false, false, "\n");
    std::printf("%d keys -> %s\n", count, out.c_str());
    return 0;
}

int runCorpus(const std::string& list, const std::string& out, int threads)
{
    std::ifstream in(list);
    const juce::File root(out);
    root.createDirectory();
    const auto timingFile = root.getChildFile("timing.csv");
    const bool fresh = !timingFile.existsAsFile();
    std::ofstream timing(timingFile.getFullPathName().toStdString(), std::ios::app);
    if (fresh)
        timing << "corpus,file,audio_s,infer_s\n";
    std::string line;
    int done = 0;
    double audioTotal = 0.0, inferTotal = 0.0;
    while (std::getline(in, line))
    {
        std::stringstream fields(line);
        std::string id, name, path;
        if (!std::getline(fields, id, '	') || !std::getline(fields, name, '	') || !std::getline(fields, path))
            continue;
        const auto target = root.getChildFile("notes").getChildFile(juce::String::fromUTF8(id.c_str())).getChildFile(juce::String::fromUTF8(name.c_str()) + ".csv");
        if (target.existsAsFile())
            continue;
        double rate = 0.0;
        const auto mono = readMono(juce::File(juce::String::fromUTF8(path.c_str())), rate);
        if (mono.empty())
        {
            std::printf("  cannot read %s\n", name.c_str());
            continue;
        }
        const auto began = std::chrono::steady_clock::now();
        const auto audio = bbg::BasicPitchTranscriber::resampleTo22050(mono.data(), static_cast<int>(mono.size()), rate);
        const auto frames = bbg::BasicPitchTranscriber::infer(audio.data(), static_cast<int>(audio.size()), threads);
        const auto notes = bbg::BasicPitchTranscriber::notes(frames);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
        const double audioSeconds = frames.frames / bbg::BasicPitchFrames::kFramesPerSecond;

        target.getParentDirectory().createDirectory();
        std::string csv = "start,end,midi,amplitude\n";
        char row[96];
        for (const auto& n : notes)
        {
            std::snprintf(row, sizeof(row), "%.4f,%.4f,%d,%.4f\n", n.startSeconds, n.endSeconds, n.midi, n.amplitude);
            csv += row;
        }
        target.replaceWithText(juce::String::fromUTF8(csv.c_str()), false, false, "\n"); // a UTF-8 path (std::ofstream reads it as ANSI)
        std::snprintf(row, sizeof(row), ",%.3f,%.3f\n", audioSeconds, seconds);
        timing << id << "," << name << row;
        timing.flush();
        audioTotal += audioSeconds;
        inferTotal += seconds;
        ++done;
        std::printf("  %-14s %6.3f s for %5.1f s  %4zu notes  %s\n", id.c_str(), seconds, audioSeconds, notes.size(), name.c_str());
    }
    std::printf("%d files, %.1f s of audio in %.2f s: %.1fx real time (%d threads, resampling included)\n",
                done, audioTotal, inferTotal, inferTotal > 0.0 ? audioTotal / inferTotal : 0.0, threads);
    return 0;
}

// No reference files needed (ctest): a synthetic C major arpeggio and chord at 44.1 kHz through
// resampling, the network and the decoder; each played note is found, nothing far off is added,
// the key is C major and 1 and 4 threads give the same frames.
int runSelfTest()
{
    constexpr double rate = 44100.0;
    struct Played { double start, length; int midi; };
    const std::vector<Played> played { { 0.0, 0.5, 48 }, { 0.5, 0.5, 52 }, { 1.0, 0.5, 55 }, { 1.5, 0.5, 60 },
                                       { 2.0, 0.5, 55 }, { 2.5, 0.5, 52 }, { 3.0, 1.0, 48 }, { 3.0, 1.0, 52 }, { 3.0, 1.0, 55 } };
    std::vector<float> audio(static_cast<size_t>(4.5 * rate), 0.0f);
    for (const auto& p : played)
    {
        const double hz = 440.0 * std::pow(2.0, (p.midi - 69) / 12.0);
        const int first = static_cast<int>(p.start * rate), count = static_cast<int>(p.length * rate);
        for (int i = 0; i < count; ++i)
        {
            const double t = i / rate;
            const double envelope = std::min(1.0, t / 0.005) * std::exp(-2.0 * t) * std::min(1.0, (p.length - t) / 0.01);
            double v = 0.0;
            for (int k = 1; k <= 8; ++k)
                v += std::sin(2.0 * 3.14159265358979323846 * hz * k * t) / k;
            audio[static_cast<size_t>(first + i)] += static_cast<float>(0.15 * envelope * v);
        }
    }

    const auto resampled = bbg::BasicPitchTranscriber::resampleTo22050(audio.data(), static_cast<int>(audio.size()), rate);
    const auto frames = bbg::BasicPitchTranscriber::infer(resampled.data(), static_cast<int>(resampled.size()), 1);
    const auto threaded = bbg::BasicPitchTranscriber::infer(resampled.data(), static_cast<int>(resampled.size()), 4);
    const auto notes = bbg::BasicPitchTranscriber::notes(frames);
    const auto key = bbg::estimateKeyFromNotes(notes);

    int failures = 0;
    auto check = [&](bool ok, const std::string& what)
    {
        std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what.c_str());
        failures += ok ? 0 : 1;
    };
    for (const auto& n : notes)
        std::printf("  note %.3f-%.3f s  midi %d  amplitude %.2f\n", n.startSeconds, n.endSeconds, n.midi, n.amplitude);
    for (const auto& p : played)
    {
        const bool found = std::any_of(notes.begin(), notes.end(), [&](const bbg::BasicPitchNote& n)
        {
            return n.midi == p.midi && std::abs(n.startSeconds - p.start) < 0.06;
        });
        check(found, "midi " + std::to_string(p.midi) + " at " + std::to_string(p.start).substr(0, 4) + " s found");
    }
    double inTriad = 0.0, total = 0.0;
    for (const auto& n : notes)
    {
        const double length = n.endSeconds - n.startSeconds;
        total += length;
        const int pc = n.midi % 12;
        inTriad += (pc == 0 || pc == 4 || pc == 7) ? length : 0.0;
    }
    check(total > 0.0 && inTriad / total > 0.95, "notes on C / E / G: " + std::to_string(total > 0.0 ? inTriad / total : 0.0));
    check(key.valid && key.root == 0 && key.mode == 1, "key C major (root " + std::to_string(key.root) + ", mode " + std::to_string(key.mode) + ")");
    check(frames.note == threaded.note && frames.onset == threaded.onset && frames.contour == threaded.contour, "4 threads give the same frames");
    std::printf("%s\n", failures == 0 ? "Basic Pitch self-test passed." : "Basic Pitch self-test FAILED.");
    return failures == 0 ? 0 : 1;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc >= 2 && std::string(argv[1]) == "selftest")
        return runSelfTest();
    if (argc >= 3 && std::string(argv[1]) == "ref")
        return runReference(argv[2], argc >= 4 ? std::atoi(argv[3]) : 1);
    if (argc >= 5 && std::string(argv[1]) == "keys")
        return runKeys(argv[2], argv[3], argv[4]);
    if (argc >= 4 && std::string(argv[1]) == "corpus")
        return runCorpus(argv[2], argv[3], argc >= 5 ? std::atoi(argv[4]) : 1);
    std::printf("usage: HPDG_BasicPitchLab ref <reference folder> [threads] | corpus <files .tsv> <out folder> [threads]\n");
    return 1;
}
