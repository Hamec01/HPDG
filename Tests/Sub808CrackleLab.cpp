// The Sub808 lane's crackle (maintainer 2026-11-03: "a small crackle while the bass is on and the pattern plays,
// and when I turn the knobs; with the bass off there is none; Boom Bap the most"). The processor plays a
// generated pattern through processBlock; the Sub808 lane alone (solo) is searched for clicks: samples whose
// second difference stands far above what the bass's own waveform allows.
//
//   HPDG_Sub808CrackleLab [genre 0..] [seeds] [outDir]   (writes the worst seed's solo bass as a WAV)

#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <map>
#include <thread>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>

#include "Core/ProjectLaneAccess.h"
#include "Plugin/PluginProcessor.h"
#include "Bass808/BassSettingsIO.h"
#include "Bass808/Sub808Sound.h"

using namespace bbg;

namespace
{
constexpr double kRate = 48000.0;

struct Clicks
{
    int count = 0;           // samples whose jump is above the limit
    double worst = 0.0;      // the largest jump / the limit
    int onBlockEdge = 0;     // of them, on a host block's first sample
    int nearNoteStart = 0;   // within 3 ms after a Sub808 note starts
    std::vector<int> where;
};

// A jump: |x[n] - 2 x[n-1] + x[n-2]| against the local peak. A bass at f Hz moves at most
// (2 pi f / rate)^2 * peak per sample; even its 8th harmonic at 120 Hz (~ 1 kHz) stays under 0.02 * peak.
Clicks findClicks(const std::vector<float>& x, int block, const std::vector<int>& noteStarts)
{
    Clicks c;
    const int window = static_cast<int>(0.01 * kRate);
    for (size_t n = 2; n < x.size(); ++n)
    {
        const double jump = std::abs(x[n] - 2.0 * x[n - 1] + x[n - 2]);
        double peak = 0.0;
        for (int k = -window; k <= window; k += 8)
        {
            const auto i = static_cast<long long>(n) + k;
            if (i >= 0 && i < static_cast<long long>(x.size()))
                peak = std::max(peak, static_cast<double>(std::abs(x[static_cast<size_t>(i)])));
        }
        const double limit = std::max(0.02 * peak, 1.0e-4);
        if (jump <= limit)
            continue;
        ++c.count;
        c.worst = std::max(c.worst, jump / limit);
        if (block > 0 && static_cast<int>(n) % block == 0)
            ++c.onBlockEdge;
        for (const int s : noteStarts)
            if (static_cast<int>(n) >= s && static_cast<int>(n) < s + static_cast<int>(0.003 * kRate))
            {
                ++c.nearNoteStart;
                break;
            }
        if (c.where.size() < 12)
            c.where.push_back(static_cast<int>(n));
    }
    return c;
}

struct Run
{
    std::vector<float> audio;
    std::vector<int> noteStarts;
};

// knobs: turn the strip's DRIVE / FILTER every few blocks while it plays
Run play(int genre, int seed, int block, bool irregular, bool soloBass, bool knobs, int bars = 4, bool bassOff = false, float drive = 0.0f, float filter = 0.0f)
{
    BoomBapGeneratorAudioProcessor p;
    p.prepareToPlay(kRate, 2048);
    auto set = [&](const char* id, float v) { auto* a = p.getApvts().getParameter(id); a->setValueNotifyingHost(a->convertTo0to1(v)); };
    set(ParamIds::genre, static_cast<float>(genre));
    p.applySelectedStylePreset(false);
    set(ParamIds::seedLock, 1);
    set(ParamIds::seed, static_cast<float>(seed));
    set(ParamIds::bars, static_cast<float>(bars));
    p.syncBarsFromState();
    p.generatePattern();

    auto project = p.getProjectSnapshot();
    Run run;
    const double bpm = project.params.bpm > 0.0f ? project.params.bpm : 90.0;
    if (auto* sub = ProjectLaneAccess::findTrackState(project, TrackType::Sub808); sub != nullptr)
    {
        sub->enabled = !bassOff;
        sub->muted = false;
        if (seed == 1 && !bassOff && soloBass && !knobs && block == 512)
            std::printf("  Sub808 sound: custom %d '%s', drive %.2f filter %.2f gain %.1f dB sidechain %.2f, sample %d bytes\n",
                        sub->sub808Sound.custom ? 1 : 0, sub->sub808Sound.name.toRawUTF8(), sub->sub808Sound.drive,
                        sub->sub808Sound.filter, sub->sub808Sound.gainDb, sub->sub808Sound.sidechain,
                        static_cast<int>(sub->sub808Sound.sampleWav.getSize()));
        sub->solo = soloBass;
        for (const auto& n : sub->sub808Notes)
            run.noteStarts.push_back(static_cast<int>(std::lround((n.gridTick + n.timingOffsetTicks) / 960.0 * 60.0 / bpm * kRate)));
    }
    project.previewStartStep = 0;
    p.restoreEditorProjectSnapshot(project);
    if (drive != 0.0f || filter != 0.0f)
    {
        // the strip set once (DRIVE / FILTER), nothing turned while it plays
        auto sound = p.getSub808Sound();
        sound.drive = drive;
        sound.filter = filter;
        p.setSub808Sound(sound);
    }
    p.startPreview();

    const int length = static_cast<int>(bars * 4 * 60.0 / bpm * kRate);
    run.audio.reserve(static_cast<size_t>(length));
    juce::MidiBuffer midi;
    juce::Random random(seed);
    int blocks = 0;
    while (static_cast<int>(run.audio.size()) < length)
    {
        const int n = irregular ? 32 + random.nextInt(1469) : block;
        juce::AudioBuffer<float> buffer(2, n);
        buffer.clear();
        midi.clear();
        if (knobs && blocks % 6 == 0)
        {
            auto sound = p.getSub808Sound();
            const float t = static_cast<float>(blocks) * 0.05f;
            sound.drive = 0.5f + 0.5f * std::sin(t);
            sound.filter = 0.6f * std::sin(0.7f * t);
            p.setSub808Sound(sound);
        }
        const int skippedBefore = p.getBlocksSkippedWhileProjectBusy();
        p.processBlock(buffer, midi);
        if (std::getenv("HPDG_CRACKLE_TRACE") != nullptr && knobs && p.getBlocksSkippedWhileProjectBusy() != skippedBefore)
            std::printf("    block %d (sample %d) skipped: project lock busy\n", blocks, static_cast<int>(run.audio.size()));
        if (std::getenv("HPDG_CRACKLE_TRACE") != nullptr && knobs && !p.isPreviewPlaying())
            std::printf("    block %d: preview not playing\n", blocks);
        for (int i = 0; i < n; ++i)
            run.audio.push_back(buffer.getSample(0, i));
        ++blocks;
    }
    p.stopPreview();
    return run;
}


// Like a host: the audio on its own thread in real time (512-sample blocks), the message thread doing what the
// editor does while it plays (a project snapshot 15 times a second, the strip's knobs when `knobs`). A block the
// processor skips because the project lock is busy comes out silent: counted as a dropout.
// the message loop for `ms` (the processor's AsyncUpdater rebuilds run here, as in a host)
void pumpMessages(int ms)
{
    const auto end = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(ms);
    while (juce::Time::getMillisecondCounter() < end)
    {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        juce::Thread::sleep(1);
    }
}

struct HostResult
{
    int blocks = 0, dropouts = 0;
    int settledDropouts = 0;
    std::uint64_t settledComparisons = 0;
    double snapshotMs = 0.0, blockMs = 0.0;
};

HostResult hostRun(int genre, int seed, bool bassOn, bool knobs, double seconds)
{
    BoomBapGeneratorAudioProcessor p;
    p.prepareToPlay(kRate, 512);
    auto set = [&](const char* id, float v) { auto* a = p.getApvts().getParameter(id); a->setValueNotifyingHost(a->convertTo0to1(v)); };
    set(ParamIds::genre, static_cast<float>(genre));
    p.applySelectedStylePreset(false);
    set(ParamIds::seedLock, 1);
    set(ParamIds::seed, static_cast<float>(seed));
    set(ParamIds::bars, 4);
    p.syncBarsFromState();
    p.generatePattern();
    auto project = p.getProjectSnapshot();
    if (auto* sub = ProjectLaneAccess::findTrackState(project, TrackType::Sub808); sub != nullptr)
        sub->enabled = bassOn;
    project.previewStartStep = 0;
    p.restoreEditorProjectSnapshot(project);
    if (std::getenv("HPDG_CRACKLE_EDITOR") != nullptr)
    {
        auto sound = p.getSub808Sound();
        if (const auto sample = p.getSub808BankSample()) sound.sampleWav = sample->toWavMemory();
        sound.custom = true; sound.name = "Editor sample playback";
        sound.settings = bassSettingsToTree(sub808SampleOnlySettings());
        p.setSub808Sound(sound);
    }
    p.startPreview();

    // HPDG_CRACKLE_PRECISE: the audio thread waits for its deadline by yielding, never sleeping, as a driver
    // callback starts on the audio clock. Without it both threads sleep: the Windows timer tick (up to
    // 15.6 ms) makes the audio thread late, it plays the missed blocks back to back at the very tick the
    // message thread wakes, and even a lock of a few microseconds taken right after waking silences one.
    const bool precise = std::getenv("HPDG_CRACKLE_PRECISE") != nullptr;
    // HPDG_CRACKLE_STRESS: the audio thread plays blocks back to back for the same wall time. A block finds the
    // lock busy exactly as often as the message thread holds it, whatever the timer or the machine's load.
    const bool stress = std::getenv("HPDG_CRACKLE_STRESS") != nullptr;
    std::atomic<bool> done { false };
    HostResult result;
    std::thread audio([&]
    {
        juce::AudioBuffer<float> buffer(2, 512);
        juce::MidiBuffer midi;
        const auto period = 512.0 / kRate * 1000.0;
        auto next = juce::Time::getMillisecondCounterHiRes();
        const int total = static_cast<int>(seconds * kRate / 512.0);
        const auto start = juce::Time::getMillisecondCounterHiRes();
        for (int b = 0; stress ? juce::Time::getMillisecondCounterHiRes() - start < seconds * 1000.0 : b < total; ++b)
        {
            buffer.clear();
            midi.clear();
            const auto b0 = juce::Time::getMillisecondCounterHiRes();
            p.processBlock(buffer, midi);
            result.blockMs = std::max(result.blockMs, juce::Time::getMillisecondCounterHiRes() - b0);
            ++result.blocks;
            next += period;
            const auto wait = next - juce::Time::getMillisecondCounterHiRes();
            if (stress)
                continue;
            if (!precise)
            {
                if (wait > 0.0)
                    juce::Thread::sleep(static_cast<int>(wait));
                continue;
            }
            while (juce::Time::getMillisecondCounterHiRes() < next)
                std::this_thread::yield();
        }
        done = true;
    });
    // The message thread does what the editor does while the beat plays, 15 times a second: its timer
    // reads the play state, the host transport and the playhead (HPDG_CRACKLE_NO_TIMER leaves them out);
    // with knobs, the 808 editor sends the new sound and the strip refreshes from a project snapshot.
    int tick = 0;
    int dropsAtFreeze = 0;
    std::uint64_t comparisonsAtFreeze = 0;
    const bool settle = std::getenv("HPDG_CRACKLE_SETTLE") != nullptr;
    const bool editorTimer = std::getenv("HPDG_CRACKLE_NO_TIMER") == nullptr;
    while (!done)
    {
        pumpMessages(66);
        if (editorTimer)
        {
            const bool playing = p.isPreviewPlaying();
            const auto transport = p.getLastTransportSnapshot();
            const float step = p.getPreviewPlayheadStep();
            juce::ignoreUnused(playing, transport, step);
        }
        if (settle && tick == 45)
        {
            dropsAtFreeze = p.getBlocksSkippedWhileProjectBusy();
            comparisonsAtFreeze = p.getSub808SoundComparisonCount();
        }
        if (knobs && (!settle || tick < 45))
        {
            auto sound = p.getSub808Sound();
            sound.drive = 0.5f + 0.5f * std::sin(0.3f * static_cast<float>(tick));
            p.setSub808Sound(sound);
        }
        if (std::getenv("HPDG_CRACKLE_NO_SNAPSHOT") == nullptr)
        {
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            const auto snapshot = p.getProjectSnapshot();
            result.snapshotMs = std::max(result.snapshotMs, juce::Time::getMillisecondCounterHiRes() - t0);
            juce::ignoreUnused(snapshot);
        }
        ++tick;
    }
    audio.join();
    result.dropouts = p.getBlocksSkippedWhileProjectBusy();
    result.settledDropouts = result.dropouts - dropsAtFreeze;
    result.settledComparisons = p.getSub808SoundComparisonCount() - comparisonsAtFreeze;
    p.stopPreview();
    return result;
}

void writeWav(const juce::File& file, const std::vector<float>& x)
{
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(new juce::FileOutputStream(file), kRate, 1, 24, {}, 0));
    if (writer == nullptr)
        return;
    juce::AudioBuffer<float> b(1, static_cast<int>(x.size()));
    std::copy(x.begin(), x.end(), b.getWritePointer(0));
    writer->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
}

double peakOf(const std::vector<float>& x)
{
    double p = 0.0;
    for (const float v : x)
        p = std::max(p, static_cast<double>(std::abs(v)));
    return p;
}
} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc > 1 && juce::String(argv[1]) == "--editor-settle")
    {
        _putenv_s("HPDG_CRACKLE_EDITOR","1");
        _putenv_s("HPDG_CRACKLE_SETTLE","1");
        _putenv_s("HPDG_CRACKLE_PRECISE","1");
        const auto r = hostRun(2,1,true,true,8.0);
        std::printf("editor settle: blocks %d total lock dropouts %d; after editing: dropouts %d comparisons %llu; longest block %.3f ms\n",
            r.blocks,r.dropouts,r.settledDropouts,static_cast<unsigned long long>(r.settledComparisons),r.blockMs);
        return r.settledDropouts == 0 && r.settledComparisons == 0 ? 0 : 1;
    }
    const int genre = argc > 1 ? std::atoi(argv[1]) : 0;
    const int seeds = argc > 2 ? std::atoi(argv[2]) : 4;
    const juce::File out = argc > 3 && juce::String(argv[3]) != "-" ? juce::File(juce::String(argv[3])) : juce::File();
    const int firstSeed = argc > 4 ? std::atoi(argv[4]) : 1;

    struct Case { const char* name; int block; bool irregular; bool knobs; float drive, filter; };
    const Case cases[] = { { "block 512", 512, false, false, 0.0f, 0.0f }, { "irregular 32..1500", 0, true, false, 0.0f, 0.0f },
                           { "drive 0.6", 512, false, false, 0.6f, 0.0f }, { "filter +0.4 (HP)", 512, false, false, 0.0f, 0.4f },
                           { "filter -0.4 (LP)", 512, false, false, 0.0f, -0.4f }, { "512 + knobs", 512, false, true, 0.0f, 0.0f } };
    int total = 0;
    for (const auto& k : cases)
    {
        int clicks = 0, edge = 0, onset = 0, notes = 0;
        double worst = 0.0, peak = 0.0;
        for (int seed = firstSeed; seed < firstSeed + seeds; ++seed)
        {
            const auto run = play(genre, seed, k.block, k.irregular, true, k.knobs, 4, false, k.drive, k.filter);
            const auto c = findClicks(run.audio, k.block, run.noteStarts);
            clicks += c.count;
            edge += c.onBlockEdge;
            onset += c.nearNoteStart;
            notes += static_cast<int>(run.noteStarts.size());
            worst = std::max(worst, c.worst);
            peak = std::max(peak, peakOf(run.audio));
            if (seed == firstSeed)
            {
                std::printf("  %s seed %d first clicks at:", k.name, seed);
                for (const int w : c.where)
                    std::printf(" %d", w);
                std::printf("\n");
                if (out != juce::File())
                {
                    out.createDirectory();
                    writeWav(out.getChildFile(juce::String("sub808_genre") + juce::String(genre) + "_" + juce::String(k.name).replaceCharacters(" .+", "___") + ".wav"), run.audio);
                }
            }
        }
        std::printf("%-20s genre %d, %d seeds: %d notes, peak %.3f, click samples %d (on a block edge %d, in a note's first 3 ms %d), worst %.1f x the limit\n",
                    k.name, genre, seeds, notes, peak, clicks, edge, onset, worst);
        total += clicks;
    }
    // the whole mix with the bass minus the same mix without it: the bass and whatever it does to the mix
    // (the master's tanh, the duck); clicks in that difference are the bass's
    for (int seed = 1; seed <= seeds; ++seed)
    {
        const auto with = play(genre, seed, 512, false, false, false);
        auto without = play(genre, seed, 512, false, false, false, 4, true);
        const auto solo = play(genre, seed, 512, false, true, false);
        std::vector<float> diff(with.audio.size());
        int hot = 0;
        double mixPeak = 0.0;
        for (size_t i = 0; i < diff.size(); ++i)
        {
            diff[i] = with.audio[i] - without.audio[i];
            mixPeak = std::max(mixPeak, static_cast<double>(std::abs(with.audio[i])));
            hot += std::abs(with.audio[i]) > 0.6f; // tanh(0.85 x) > 0.6: x > 0.82, the master's knee
        }
        // what the mix does to the bass: the difference against the bass alone
        double err = 0.0, ref = 0.0;
        for (size_t i = 0; i < diff.size(); ++i)
        {
            err += (diff[i] - solo.audio[i]) * (diff[i] - solo.audio[i]);
            ref += solo.audio[i] * solo.audio[i];
        }
        const auto c = findClicks(diff, 512, with.noteStarts);
        std::printf("mix seed %d: mix peak %.3f, %d samples over the master's knee; with - without vs the bass alone %.1f dB; clicks in the difference %d (worst %.1f x)\n",
                    seed, mixPeak, hot, 10.0 * std::log10(std::max(1.0e-12, err) / std::max(1.0e-12, ref)), c.count, c.worst);
        if (seed == 1 && out != juce::File())
        {
            writeWav(out.getChildFile("mix_with_bass.wav"), with.audio);
            writeWav(out.getChildFile("mix_without_bass.wav"), without.audio);
            writeWav(out.getChildFile("mix_difference.wav"), diff);
        }
    }
    // HPDG_CRACKLE_HOST=n: n runs of 8 s per case (1 when not a number)
    if (const char* host = std::getenv("HPDG_CRACKLE_HOST"); host != nullptr)
        for (int run = 0; run < std::max(1, std::atoi(host)); ++run)
            for (const bool bassOn : { false, true })
                for (const bool knobs : { false, true })
                {
                    if (knobs && !bassOn)
                        continue;
                    const auto r = hostRun(genre, 1, bassOn, knobs, 8.0);
                    std::printf("host: bass %s%s: %d blocks, %d dropouts (a busy project lock: silent block); longest snapshot %.2f ms, longest block %.2f ms\n",
                                bassOn ? "on" : "off", knobs ? " + knobs" : "", r.blocks, r.dropouts, r.snapshotMs, r.blockMs);
                }
    return total > 0 ? 1 : 0;
}
