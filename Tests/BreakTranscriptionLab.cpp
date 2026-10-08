// HPDG Break Lab - development bench for DrumBreakTranscriber.
//
//   HPDG_BreakLab analyze <file|folder> [--out <folder>] [--host <bpm>] [--hits]
//       Transcribes every audio file, prints a summary line per file and (with --out) writes
//       <name>.mid (Kick 36 / Snare 38 / HiHat 42, tempo = detected bpm) plus <name>.txt with
//       the full report. Drop the MIDI next to the break in a DAW to compare by ear.
//
//   HPDG_BreakLab synth <kitRoot> [--trials N] [--seed S] [--verbose]
//       Renders random breaks from <kitRoot>/Kick, /Snare, /HiHat one-shots with known ground
//       truth (tempo, swing, ghosts, overlaps, loop wrap) and reports tempo accuracy and
//       per-lane precision / recall / F-measure plus timing error.

#include <iostream>
#include <map>
#include <random>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

#include "../Source/Analysis/DrumBreakTranscriber.h"
#include "../Source/Analysis/SampleHarmonyAnalyzer.h"
#include "../Source/Analysis/SampleLineTranscriber.h"
#include "../Source/Core/TimingGrid.h"

using namespace bbg;

namespace
{
bool loadMono(const juce::File& file, std::vector<float>& mono, double& sampleRate)
{
    juce::AudioFormatManager manager;
    manager.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(manager.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return false;

    const int length = static_cast<int>(std::min<juce::int64>(reader->lengthInSamples, static_cast<juce::int64>(reader->sampleRate * 120.0)));
    const int channels = static_cast<int>(reader->numChannels);
    juce::AudioBuffer<float> buffer(channels, length);
    reader->read(&buffer, 0, length, 0, true, true);

    mono.assign(static_cast<size_t>(length), 0.0f);
    for (int channel = 0; channel < channels; ++channel)
        for (int i = 0; i < length; ++i)
            mono[static_cast<size_t>(i)] += buffer.getSample(channel, i) / static_cast<float>(channels);
    sampleRate = reader->sampleRate;
    return true;
}

int laneNote(TrackType lane)
{
    switch (lane)
    {
        case TrackType::Kick: return 36;
        case TrackType::Snare: return 38;
        default: return 42;
    }
}

void writeMidi(const DrumBreakAnalysis& analysis, const juce::File& file)
{
    juce::MidiMessageSequence track;
    track.addEvent(juce::MidiMessage::tempoMetaEvent(static_cast<int>(60000000.0 / analysis.bpm)), 0.0);
    track.addEvent(juce::MidiMessage::timeSignatureMetaEvent(4, 4), 0.0);
    for (const auto& hit : analysis.hits)
    {
        const double start = std::max(0, hit.gridTick + hit.timingOffsetTicks);
        track.addEvent(juce::MidiMessage::noteOn(10, laneNote(hit.lane), static_cast<juce::uint8>(hit.velocity)), start);
        track.addEvent(juce::MidiMessage::noteOff(10, laneNote(hit.lane)), start + 100.0);
    }
    track.addEvent(juce::MidiMessage::endOfTrack(), analysis.bars * TimingGrid::TicksPerBar4_4);
    track.updateMatchedPairs();

    juce::MidiFile midi;
    midi.setTicksPerQuarterNote(TimingGrid::PPQ);
    midi.addTrack(track);
    file.deleteFile();
    juce::FileOutputStream stream(file);
    if (stream.openedOk())
        midi.writeTo(stream);
}

int runAnalyze(const juce::StringArray& args)
{
    if (args.size() < 2)
        return 1;

    juce::File target(args[1]);
    juce::File outDir;
    DrumBreakOptions options;
    bool printHits = false;
    bool printHarmony = false;
    for (int i = 2; i < args.size(); ++i)
    {
        if (args[i] == "--out" && i + 1 < args.size())
            outDir = juce::File(args[++i]);
        else if (args[i] == "--host" && i + 1 < args.size())
            options.hostBpm = args[++i].getDoubleValue();
        else if (args[i] == "--bpm" && i + 1 < args.size())
            options.forcedBpm = args[++i].getDoubleValue();
        else if (args[i] == "--harmony")
            printHarmony = true;
        else if (args[i] == "--hits")
            printHits = true;
    }

    juce::Array<juce::File> files;
    if (target.isDirectory())
        files = target.findChildFiles(juce::File::findFiles, false, "*.wav;*.aif;*.aiff;*.flac;*.mp3");
    else
        files.add(target);
    files.sort();

    if (outDir != juce::File())
        outDir.createDirectory();

    DrumBreakTranscriber transcriber;
    juce::String csv = "file,bpm,bars,tempoConf,swing,kicks,snares,hats,drumLoopConf,duration\n";
    for (const auto& file : files)
    {
        std::vector<float> mono;
        double sampleRate = 0.0;
        if (!loadMono(file, mono, sampleRate))
        {
            std::cout << file.getFileName() << ": cannot read\n";
            continue;
        }

        const auto analysis = transcriber.analyze(mono, sampleRate, options);
        std::cout << file.getFileName().paddedRight(' ', 36)
                  << " bpm " << juce::String(analysis.bpm, 2).paddedLeft(' ', 7)
                  << " bars " << analysis.bars
                  << " conf " << juce::String(analysis.tempoConfidence, 2)
                  << " swing " << juce::String(analysis.swingPercent, 1)
                  << " | K " << analysis.countLane(TrackType::Kick)
                  << " S " << analysis.countLane(TrackType::Snare)
                  << " H " << analysis.countLane(TrackType::HiHat)
                  << " | loop " << juce::String(analysis.drumLoopConfidence, 2) << "\n";
        if (printHarmony)
        {
            const double beat = 60.0 / (analysis.bpm > 20.0 ? analysis.bpm : 90.0);
            // Same call as the plugin (SampleAnalyzer): the sample's tuning first.
            const double tuningCents = SampleLineTranscriber::estimateTuningCents(mono, sampleRate);
            const auto harmony = SampleHarmonyAnalyzer().analyze(mono, sampleRate, beat, analysis.originSeconds, tuningCents);
            std::cout << "    " << harmony.describe() << " | tuning " << juce::String(tuningCents, 1) << " cents\n";
            // The key decision's inputs (offline key-profile experiments, tools/tempo_bench.py).
            std::array<float, 12> bassShare {};
            int openingPc = -1;
            for (const auto& segment : harmony.bass)
            {
                if (segment.midiNote < 0)
                    continue;
                bassShare[static_cast<size_t>(segment.midiNote % 12)] += segment.confidence * segment.lowEnergy;
                if (openingPc < 0 && segment.startSeconds + 1.0e-6 >= analysis.originSeconds && segment.confidence >= 0.35f)
                    openingPc = segment.midiNote % 12;
            }
            std::cout << "    key features | chroma";
            for (const float value : harmony.chroma)
                std::cout << " " << juce::String(value, 4);
            std::cout << " | bass";
            for (const float value : bassShare)
                std::cout << " " << juce::String(value, 4);
            std::cout << " | opening " << openingPc << "\n";
        }
        if (printHits)
        {
            std::cout << analysis.describe(true) << "\n";
            for (size_t onset = 0; onset < analysis.onsetTimes.size(); ++onset)
            {
                const auto& levels = analysis.onsetLaneLevels[onset];
                std::cout << "  onset " << juce::String(analysis.onsetTimes[onset], 4)
                          << " K " << juce::String(levels[0], 3) << " S " << juce::String(levels[1], 3)
                          << " H " << juce::String(levels[2], 3)
                          << " hf " << juce::String(analysis.onsetHighLevels[onset], 3) << "\n";
            }
            std::cout << "\n";
        }

        csv << file.getFileName() << "," << analysis.bpm << "," << analysis.bars << "," << analysis.tempoConfidence
            << "," << analysis.swingPercent << "," << analysis.countLane(TrackType::Kick) << "," << analysis.countLane(TrackType::Snare)
            << "," << analysis.countLane(TrackType::HiHat) << "," << analysis.drumLoopConfidence << "," << analysis.durationSeconds << "\n";

        if (outDir != juce::File() && analysis.valid)
        {
            writeMidi(analysis, outDir.getChildFile(file.getFileNameWithoutExtension() + ".mid"));
            outDir.getChildFile(file.getFileNameWithoutExtension() + ".txt").replaceWithText(analysis.describe(true));
        }
    }

    if (outDir != juce::File())
        outDir.getChildFile("summary.csv").replaceWithText(csv);
    return 0;
}

// ---------------------------------------------------------------------------------------------
struct TruthHit
{
    TrackType lane;
    double time;
    bool ghost;
    int slot;
};

std::vector<std::vector<float>> loadShots(const juce::File& folder, double targetRate)
{
    std::vector<std::vector<float>> shots;
    for (const auto& file : folder.findChildFiles(juce::File::findFiles, false, "*.wav"))
    {
        std::vector<float> mono;
        double rate = 0.0;
        if (!loadMono(file, mono, rate))
            continue;
        if (std::abs(rate - targetRate) > 1.0)
        {
            const double step = rate / targetRate;
            std::vector<float> resampled(static_cast<size_t>(mono.size() / step));
            for (size_t i = 0; i < resampled.size(); ++i)
            {
                const double position = i * step;
                const size_t index = static_cast<size_t>(position);
                const float frac = static_cast<float>(position - index);
                const float next = index + 1 < mono.size() ? mono[index + 1] : 0.0f;
                resampled[i] = mono[index] * (1.0f - frac) + next * frac;
            }
            mono = std::move(resampled);
        }
        float peak = 1.0e-6f;
        for (const float sample : mono)
            peak = std::max(peak, std::abs(sample));
        for (auto& sample : mono)
            sample /= peak;
        shots.push_back(std::move(mono));
    }
    return shots;
}

struct LaneScore
{
    int truePositive = 0;
    int falsePositive = 0;
    int falseNegative = 0;
    int ghostTotal = 0;
    int ghostFound = 0;
    double timingErrorSum = 0.0;

    float precision() const { return truePositive + falsePositive > 0 ? static_cast<float>(truePositive) / static_cast<float>(truePositive + falsePositive) : 1.0f; }
    float recall() const { return truePositive + falseNegative > 0 ? static_cast<float>(truePositive) / static_cast<float>(truePositive + falseNegative) : 1.0f; }
    float fMeasure() const { const float p = precision(), r = recall(); return p + r > 0.0f ? 2.0f * p * r / (p + r) : 0.0f; }
};

int runSynth(const juce::StringArray& args)
{
    if (args.size() < 2)
        return 1;

    const juce::File root(args[1]);
    int trials = 60;
    int seed = 1;
    bool verbose = false;
    bool pickup = false; // loops that start on a beat other than 1 (only tempo / bars are scored)
    int detailTrials = 6;
    for (int i = 2; i < args.size(); ++i)
    {
        if (args[i] == "--detail" && i + 1 < args.size())
            detailTrials = args[++i].getIntValue();
        else if (args[i] == "--trials" && i + 1 < args.size())
            trials = args[++i].getIntValue();
        else if (args[i] == "--seed" && i + 1 < args.size())
            seed = args[++i].getIntValue();
        else if (args[i] == "--verbose")
            verbose = true;
        else if (args[i] == "--pickup")
            pickup = true;
    }

    constexpr double rate = 44100.0;
    const std::array<TrackType, 3> lanes { TrackType::Kick, TrackType::Snare, TrackType::HiHat };
    std::array<std::vector<std::vector<float>>, 3> shots {
        loadShots(root.getChildFile("Kick"), rate),
        loadShots(root.getChildFile("Snare"), rate),
        loadShots(root.getChildFile("HiHat"), rate)
    };
    for (const auto& laneShots : shots)
    {
        if (laneShots.empty())
        {
            std::cout << "missing one-shots under " << root.getFullPathName() << "\n";
            return 1;
        }
    }

    std::mt19937 rng(static_cast<unsigned>(seed));
    auto uniform = [&rng](double low, double high) { return std::uniform_real_distribution<double>(low, high)(rng); };
    auto chance = [&](double probability) { return uniform(0.0, 1.0) < probability; };

    DrumBreakTranscriber transcriber;
    std::array<LaneScore, 3> scores {};
    int tempoCorrect = 0;
    double tempoErrorSum = 0.0, tempoErrorMax = 0.0; // percent, among non-octave results
    int tempoOctave = 0;
    int barsCorrect = 0;
    int gridCorrect = 0;
    int gridTotal = 0;
    std::array<int, 3> onsetMissed {};

    for (int trial = 0; trial < trials; ++trial)
    {
        const double bpm = uniform(72.0, 118.0);
        const int bars = std::array<int, 3> { 1, 2, 4 }[static_cast<size_t>(rng() % 3)];
        const double swing = std::array<double, 4> { 0.0, 0.12, 0.22, 0.33 }[static_cast<size_t>(rng() % 4)];
        const bool sixteenthHats = chance(0.45);
        const double sixteenth = 15.0 / bpm;
        const double loopSeconds = bars * 16 * sixteenth;
        const size_t length = static_cast<size_t>(loopSeconds * rate);

        std::array<const std::vector<float>*, 3> kit {};
        std::array<float, 3> laneGain {};
        for (size_t lane = 0; lane < 3; ++lane)
        {
            kit[lane] = &shots[lane][rng() % shots[lane].size()];
            laneGain[lane] = static_cast<float>(uniform(lane == 2 ? 0.25 : 0.55, 1.0));
        }

        // One-bar pattern, varied per bar.
        std::vector<TruthHit> truth;
        std::vector<float> audio(length, 0.0f);
        std::array<bool, 16> kickBase {};
        kickBase[0] = true;
        for (int slot : { 3, 6, 7, 8, 10, 11, 14, 15 })
            kickBase[static_cast<size_t>(slot)] = chance(slot == 8 || slot == 10 ? 0.5 : 0.18);

        auto place = [&](size_t lane, int slotIndex, double velocity, bool ghost)
        {
            const int slotInBar = slotIndex % 16;
            double time = (slotIndex + ((slotInBar % 2) == 1 ? swing : 0.0)) * sixteenth;
            time += uniform(-0.004, 0.004) + (lane == 1 ? 0.004 : 0.0);
            time = std::max(0.0, time);
            truth.push_back({ lanes[lane], time, ghost, slotIndex });

            const auto& shot = *kit[lane];
            const float gain = laneGain[lane] * static_cast<float>(std::pow(velocity, 1.5));
            const size_t start = static_cast<size_t>(time * rate);
            for (size_t i = 0; i < shot.size(); ++i)
                audio[(start + i) % length] += gain * shot[i]; // wrap tails like a real sampled loop
        };

        for (int bar = 0; bar < bars; ++bar)
        {
            for (int slot = 0; slot < 16; ++slot)
            {
                const int index = bar * 16 + slot;
                bool kick = kickBase[static_cast<size_t>(slot)];
                if (slot != 0 && chance(0.12))
                    kick = !kick;
                if (kick)
                    place(0, index, uniform(0.75, 1.0), false);

                if (slot == 4 || slot == 12)
                    place(1, index, uniform(0.85, 1.0), false);
                else if (!kick && chance(0.07))
                    place(1, index, uniform(0.25, 0.45), true);

                const bool hatSlot = sixteenthHats || slot % 2 == 0;
                if (hatSlot && !chance(0.12))
                    place(2, index, (slot % 2 == 0) ? uniform(0.7, 1.0) : uniform(0.4, 0.7), false);
            }
        }

        if (pickup)
        {
            // Start the file on beat 2, 3 or 4 of the first bar (a loop cut with a pickup).
            const size_t shift = static_cast<size_t>((1 + rng() % 3) * 4 * sixteenth * rate) % length;
            std::rotate(audio.begin(), audio.begin() + static_cast<std::ptrdiff_t>(shift), audio.end());
            truth.clear();
        }

        std::normal_distribution<float> noise(0.0f, 0.002f);
        for (auto& sample : audio)
            sample += noise(rng);

        const auto analysis = transcriber.analyze(audio, rate, {});
        const bool tempoOk = std::abs(analysis.bpm / bpm - 1.0) < 0.01;
        tempoCorrect += tempoOk ? 1 : 0;
        if (tempoOk)
        {
            const double errorPercent = 100.0 * std::abs(analysis.bpm / bpm - 1.0);
            tempoErrorSum += errorPercent;
            tempoErrorMax = std::max(tempoErrorMax, errorPercent);
        }
        if (!tempoOk && (std::abs(analysis.bpm / (2.0 * bpm) - 1.0) < 0.01 || std::abs(analysis.bpm / (0.5 * bpm) - 1.0) < 0.01))
            ++tempoOctave;
        barsCorrect += analysis.bars == bars ? 1 : 0;

        juce::String trialLine = "trial " + juce::String(trial) + " bpm " + juce::String(bpm, 2) + " -> " + juce::String(analysis.bpm, 2)
            + " bars " + juce::String(bars) + "->" + juce::String(analysis.bars)
            + " swing " + juce::String(swing, 2) + " hats16 " + juce::String(sixteenthHats ? 1 : 0)
            + " origin " + juce::String(analysis.originSeconds * 1000.0, 1) + "ms" + (analysis.exactLoop ? " loop" : " free");
        int trialGridErrors = 0;

        for (const auto& want : truth)
        {
            const bool seen = std::any_of(analysis.onsetTimes.begin(), analysis.onsetTimes.end(), [&](double onset)
            {
                double error = std::abs(onset - want.time);
                return std::min(error, std::abs(error - loopSeconds)) < 0.025;
            });
            onsetMissed[static_cast<size_t>(want.lane == TrackType::Kick ? 0 : want.lane == TrackType::Snare ? 1 : 2)] += seen ? 0 : 1;
        }

        for (size_t lane = 0; lane < 3; ++lane)
        {
            std::vector<const TruthHit*> expected;
            for (const auto& hit : truth)
                if (hit.lane == lanes[lane])
                    expected.push_back(&hit);
            std::vector<const BreakDrumHit*> found;
            for (const auto& hit : analysis.hits)
                if (hit.lane == lanes[lane])
                    found.push_back(&hit);

            std::vector<bool> used(found.size(), false);
            int tp = 0;
            for (const auto* want : expected)
            {
                int bestIndex = -1;
                double bestError = 0.025;
                for (size_t index = 0; index < found.size(); ++index)
                {
                    if (used[index])
                        continue;
                    // Compare modulo the loop so wrapped hits still count.
                    double error = std::abs(found[index]->timeSeconds - want->time);
                    error = std::min(error, std::abs(error - loopSeconds));
                    if (error < bestError)
                    {
                        bestError = error;
                        bestIndex = static_cast<int>(index);
                    }
                }

                if (want->ghost)
                    ++scores[lane].ghostTotal;
                if (bestIndex >= 0)
                {
                    used[static_cast<size_t>(bestIndex)] = true;
                    ++tp;
                    scores[lane].timingErrorSum += bestError;
                    if (want->ghost)
                        ++scores[lane].ghostFound;

                    if (tempoOk)
                    {
                        ++gridTotal;
                        if (found[static_cast<size_t>(bestIndex)]->gridTick == want->slot * TimingGrid::Sixteenth)
                            ++gridCorrect;
                        else
                            ++trialGridErrors;
                    }
                }
            }

            if (verbose && trial < detailTrials)
            {
                for (const auto* want : expected)
                {
                    const bool hit = std::any_of(found.begin(), found.end(), [&](const BreakDrumHit* f) { double e = std::abs(f->timeSeconds - want->time); return std::min(e, std::abs(e - loopSeconds)) < 0.025; });
                    for (size_t o = 0; o < analysis.onsetTimes.size(); ++o)
                    {
                        double e = std::abs(analysis.onsetTimes[o] - want->time);
                        if (std::min(e, std::abs(e - loopSeconds)) < 0.025)
                        {
                            const auto& l = analysis.onsetLaneLevels[o];
                            std::cout << "   " << toString(lanes[lane]) << (hit ? " ok  " : " MISS") << " slot " << want->slot
                                      << " lv K " << juce::String(l[0], 3) << " S " << juce::String(l[1], 3) << " H " << juce::String(l[2], 3) << "\n";
                            break;
                        }
                    }
                }
            }

            if (verbose && trial < detailTrials)
            {
                for (size_t index = 0; index < found.size(); ++index)
                {
                    if (used[index])
                        continue;
                    const auto near = std::min_element(truth.begin(), truth.end(), [&](const TruthHit& a, const TruthHit& b)
                    {
                        return std::abs(a.time - found[index]->timeSeconds) < std::abs(b.time - found[index]->timeSeconds);
                    });
                    std::cout << "   " << toString(lanes[lane]) << " FP   t " << juce::String(found[index]->timeSeconds, 3)
                              << " rel " << juce::String(found[index]->strength, 3)
                              << " nearest truth " << toString(near->lane) << " slot " << near->slot
                              << " dt " << juce::String(1000.0 * (found[index]->timeSeconds - near->time), 1) << " ms\n";
                }
            }

            const int fp = static_cast<int>(found.size()) - tp;
            const int fn = static_cast<int>(expected.size()) - tp;
            scores[lane].truePositive += tp;
            scores[lane].falsePositive += fp;
            scores[lane].falseNegative += fn;
            trialLine << " | " << toString(lanes[lane]) << " tp " << tp << " fp " << fp << " fn " << fn;
        }

        if (trialGridErrors > 0 || !tempoOk || analysis.bars != bars)
            trialLine << " | BAD grid errors " << trialGridErrors << "\n" << analysis.describe(false);
        if (verbose)
            std::cout << trialLine << "\n";
    }

    std::cout << "\nSYNTH trials " << trials
              << " | tempo " << tempoCorrect << "/" << trials << " (octave errors " << tempoOctave << ")"
              << " | tempo error avg " << (tempoCorrect > 0 ? tempoErrorSum / tempoCorrect : 0.0) << "% max " << tempoErrorMax << "%"
              << " | bars " << barsCorrect << "/" << trials
              << " | grid slot " << gridCorrect << "/" << gridTotal
              << " | onset misses K/S/H " << onsetMissed[0] << "/" << onsetMissed[1] << "/" << onsetMissed[2] << "\n";
    for (size_t lane = 0; lane < 3; ++lane)
    {
        const auto& score = scores[lane];
        std::cout << "  " << juce::String(toString(lanes[lane])).paddedRight(' ', 6)
                  << " P " << juce::String(score.precision(), 3)
                  << " R " << juce::String(score.recall(), 3)
                  << " F " << juce::String(score.fMeasure(), 3)
                  << " | timing err " << juce::String(score.truePositive > 0 ? 1000.0 * score.timingErrorSum / score.truePositive : 0.0, 2) << " ms";
        if (score.ghostTotal > 0)
            std::cout << " | ghosts " << score.ghostFound << "/" << score.ghostTotal;
        std::cout << "\n";
    }
    return 0;
}
// Root note of one-shot bass samples: YIN on the sustained part (after the attack) plus the
// harmony analyzer's low-note salience as a cross-check.
int runRootNote(const juce::StringArray& args)
{
    static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    for (int i = 1; i < args.size(); ++i)
    {
        const juce::File file(args[i]);
        std::vector<float> mono;
        double rate = 0.0;
        if (!loadMono(file, mono, rate))
        {
            std::cout << file.getFileName() << ": cannot read\n";
            continue;
        }

        // Skip the attack: start 40 ms after the peak, analyse up to 400 ms.
        size_t peak = 0;
        for (size_t s = 0; s < mono.size(); ++s)
            if (std::abs(mono[s]) > std::abs(mono[peak]))
                peak = s;
        const size_t start = std::min(mono.size(), peak + static_cast<size_t>(0.04 * rate));
        const size_t length = std::min(mono.size() - start, static_cast<size_t>(0.4 * rate));

        // YIN, 25-500 Hz.
        double yinHz = 0.0;
        const int maxLag = static_cast<int>(rate / 25.0);
        const int minLag = static_cast<int>(rate / 500.0);
        const int window = static_cast<int>(std::min<size_t>(length > static_cast<size_t>(maxLag) ? length - static_cast<size_t>(maxLag) : 0, static_cast<size_t>(0.1 * rate)));
        if (window > 256)
        {
            std::vector<double> d(static_cast<size_t>(maxLag + 1), 0.0);
            for (int lag = 1; lag <= maxLag; ++lag)
                for (int j = 0; j < window; ++j)
                {
                    const double diff = mono[start + static_cast<size_t>(j)] - mono[start + static_cast<size_t>(j + lag)];
                    d[static_cast<size_t>(lag)] += diff * diff;
                }
            double running = 0.0;
            int best = -1;
            for (int lag = 1; lag <= maxLag; ++lag)
            {
                running += d[static_cast<size_t>(lag)];
                const double cmnd = running > 0.0 ? d[static_cast<size_t>(lag)] * lag / running : 1.0;
                if (lag >= minLag && cmnd < 0.15)
                {
                    while (lag + 1 <= maxLag && d[static_cast<size_t>(lag + 1)] < d[static_cast<size_t>(lag)])
                        ++lag;
                    best = lag;
                    break;
                }
            }
            if (best > 0)
                yinHz = rate / best;
        }

        std::vector<float> body(mono.begin() + static_cast<std::ptrdiff_t>(start), mono.begin() + static_cast<std::ptrdiff_t>(start + length));
        const auto harmony = SampleHarmonyAnalyzer().analyze(body, rate, length / rate + 0.01, 0.0);
        const int harmonyNote = harmony.bass.empty() ? -1 : harmony.bass.front().midiNote;

        const int yinNote = yinHz > 0.0 ? static_cast<int>(std::lround(69.0 + 12.0 * std::log2(yinHz / 440.0))) : -1;
        const double cents = yinHz > 0.0 ? 100.0 * (69.0 + 12.0 * std::log2(yinHz / 440.0) - yinNote) : 0.0;
        std::cout << file.getFileName().paddedRight(' ', 44)
                  << " yin " << juce::String(yinHz, 1).paddedLeft(' ', 7) << " Hz -> "
                  << (yinNote >= 0 ? juce::String(names[yinNote % 12]) + juce::String(yinNote / 12 - 1) : juce::String("?"))
                  << " (" << juce::String(cents, 0) << " c)"
                  << " | harmony " << (harmonyNote >= 0 ? juce::String(names[harmonyNote % 12]) + juce::String(harmonyNote / 12 - 1) : juce::String("?"))
                  << " | " << juce::String(mono.size() / rate, 2) << " s\n";
    }
    return 0;
}

// Random diatonic progressions (one chord per bar) with a bass line on chord roots / fifths and
// a pad on top. Scores key detection and per-beat bass pitch-class accuracy.
// Pitch class sounding most (by duration) inside [from, to), -1 when nothing does.
int dominantPitchClass(const std::vector<SampleLineNote>& line, double from, double to, double minShare = 0.3)
{
    std::array<double, 12> share {};
    for (const auto& n : line)
        share[static_cast<size_t>(n.midiNote % 12)] += std::max(0.0, std::min(to, n.endSeconds) - std::max(from, n.startSeconds));
    const auto best = std::max_element(share.begin(), share.end());
    return *best >= minShare * (to - from) ? static_cast<int>(std::distance(share.begin(), best)) : -1;
}

int noteAt(const std::vector<SampleLineNote>& line, double t)
{
    for (const auto& n : line)
        if (t >= n.startSeconds && t < n.endSeconds)
            return n.midiNote;
    return -1;
}

// lines <mix | stems folder> [--ref <bass stem>] [--print]
//   Transcribes the bass / melody lines. With a stems folder the stems are summed into the
//   mix and the "(Bass)" stem's own transcription is the reference: frame-wise pitch accuracy
//   and per-half-second pitch-class accuracy of the new line vs the old per-beat analyzer.
int runLines(const juce::StringArray& args)
{
    if (args.size() < 2)
        return 1;
    const juce::File input(args[1]);
    juce::File refFile;
    bool print = false;
    juce::StringArray only;
    for (int i = 2; i < args.size(); ++i)
    {
        if (args[i] == "--ref" && i + 1 < args.size())
            refFile = juce::File(args[++i]);
        else if (args[i] == "--print")
            print = true;
        else if (args[i] == "--only" && i + 1 < args.size())
            only = juce::StringArray::fromTokens(args[++i], ",", {});
    }

    std::vector<float> mix;
    double rate = 44100.0;
    if (input.isDirectory())
    {
        for (const auto& stem : input.findChildFiles(juce::File::findFiles, false, "*.wav;*.mp3"))
        {
            std::vector<float> audio;
            double stemRate = 0.0;
            if (!loadMono(stem, audio, stemRate))
                continue;
            if (stem.getFileName().containsIgnoreCase("Bass") && !stem.getFileName().containsIgnoreCase("Backing"))
                refFile = stem;
            if (stem.getFileName().containsIgnoreCase("Vocals"))
                continue; // a beat sample has no lead vocal
            if (!only.isEmpty() && std::none_of(only.begin(), only.end(), [&](const juce::String& part) { return stem.getFileName().containsIgnoreCase(part); }))
                continue;
            double rms = 0.0;
            for (const float v : audio)
                rms += static_cast<double>(v) * v;
            std::cout << "  stem " << stem.getFileName() << " rms " << juce::String(std::sqrt(rms / std::max<size_t>(1, audio.size())), 4) << "\n";
            rate = stemRate;
            if (mix.size() < audio.size())
                mix.resize(audio.size(), 0.0f);
            for (size_t i = 0; i < audio.size(); ++i)
                mix[i] += audio[i];
        }
    }
    else if (!loadMono(input, mix, rate))
    {
        std::cout << "cannot read " << input.getFullPathName() << "\n";
        return 1;
    }

    const double seconds = std::min(40.0, static_cast<double>(mix.size()) / rate);
    mix.resize(static_cast<size_t>(seconds * rate));
    const auto started = juce::Time::getMillisecondCounterHiRes();
    const double tuning = SampleLineTranscriber::estimateTuningCents(mix, rate);
    const auto tTuning = juce::Time::getMillisecondCounterHiRes();
    const auto harmony = SampleHarmonyAnalyzer().analyze(mix, rate, 0.5, 0.0, tuning);
    const auto tHarmony = juce::Time::getMillisecondCounterHiRes();
    const auto lines = SampleLineTranscriber().transcribe(mix, rate, harmony.keyRoot, harmony.scaleMode, harmony.keyConfidence >= 0.4f);
    std::cout << "  timing: tuning " << juce::String(tTuning - started, 0) << " ms | harmony " << juce::String(tHarmony - tTuning, 0)
              << " ms | lines (incl. tuning again) " << juce::String(juce::Time::getMillisecondCounterHiRes() - tHarmony, 0) << " ms\n";
    std::cout << "MIX " << input.getFileName() << " | " << juce::String(seconds, 1) << " s | key " << harmony.keyName()
              << " | transcribe " << juce::String(juce::Time::getMillisecondCounterHiRes() - started, 0) << " ms\n";
    if (print)
        std::cout << lines.describe() << "\n";
    else
        std::cout << "  bass notes " << lines.bass.size() << " | melody notes " << lines.melody.size() << "\n";

    if (!refFile.existsAsFile())
        return 0;

    std::vector<float> refAudio;
    double refRate = 0.0;
    if (!loadMono(refFile, refAudio, refRate))
        return 1;
    refAudio.resize(std::min(refAudio.size(), static_cast<size_t>(seconds * refRate)));
    const auto refLines = SampleLineTranscriber().transcribe(refAudio, refRate);
    if (print)
        std::cout << "REF " << refLines.describe().upToFirstOccurrenceOf("\n", false, false) << "\n";

    // Frame-wise (20 ms).
    int refVoiced = 0, mixVoiced = 0, both = 0, exact = 0, pitchClass = 0;
    for (double t = 0.0; t < seconds; t += 0.02)
    {
        const int r = noteAt(refLines.bass, t);
        const int m = noteAt(lines.bass, t);
        refVoiced += r >= 0 ? 1 : 0;
        mixVoiced += m >= 0 ? 1 : 0;
        if (r >= 0 && m >= 0)
        {
            ++both;
            exact += r == m ? 1 : 0;
            pitchClass += r % 12 == m % 12 ? 1 : 0;
        }
    }
    // Half-second windows: the old per-beat analyzer vs the new line.
    int windows = 0, oldClaimed = 0, oldRight = 0, newClaimed = 0, newRight = 0;
    for (const auto& segment : harmony.bass)
    {
        const int truth = dominantPitchClass(refLines.bass, segment.startSeconds, segment.endSeconds, 0.5);
        if (truth < 0)
            continue;
        ++windows;
        if (segment.midiNote >= 0 && segment.confidence >= 0.35f)
        {
            ++oldClaimed;
            oldRight += segment.midiNote % 12 == truth ? 1 : 0;
        }
        const int mine = dominantPitchClass(lines.bass, segment.startSeconds, segment.endSeconds, 0.3);
        if (mine >= 0)
        {
            ++newClaimed;
            newRight += mine == truth ? 1 : 0;
        }
    }
    auto pct = [](int a, int b) { return b > 0 ? juce::String(100.0 * a / b, 0) + "%" : juce::String("-"); };
    std::cout << "  REF " << refFile.getFileName() << " | ref bass notes " << refLines.bass.size() << "\n"
              << "  frames: ref voiced " << refVoiced << " | mix voiced " << mixVoiced << " | recall " << pct(both, refVoiced)
              << " | precision " << pct(both, mixVoiced) << " | exact " << pct(exact, both) << " | pitch class " << pct(pitchClass, both) << "\n"
              << "  0.5 s windows " << windows << ": OLD claimed " << oldClaimed << " right " << pct(oldRight, oldClaimed)
              << " | NEW claimed " << newClaimed << " right " << pct(newRight, newClaimed) << "\n";
    return 0;
}

// retune <in> <out> <cents>: varispeed re-pitch (Lagrange interpolation), e.g. a C# bass -100 c -> C.
int runRetune(const juce::StringArray& args)
{
    if (args.size() < 4)
        return 1;
    juce::AudioFormatManager manager;
    manager.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(manager.createReaderFor(juce::File(args[1])));
    if (reader == nullptr)
        return 1;
    const int channels = static_cast<int>(reader->numChannels);
    const int length = static_cast<int>(reader->lengthInSamples);
    juce::AudioBuffer<float> input(channels, length);
    reader->read(&input, 0, length, 0, true, true);

    const double ratio = std::pow(2.0, args[3].getDoubleValue() / 1200.0); // > 1: higher and shorter
    const int outLength = static_cast<int>(std::floor((length - 4) / ratio));
    juce::AudioBuffer<float> output(channels, outLength);
    for (int c = 0; c < channels; ++c)
    {
        juce::LagrangeInterpolator interpolator;
        interpolator.process(ratio, input.getReadPointer(c), output.getWritePointer(c), outLength);
    }

    const juce::File out(args[2]);
    out.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(new juce::FileOutputStream(out), reader->sampleRate,
                                                                         static_cast<unsigned int>(channels), 24, {}, 0));
    if (writer == nullptr)
        return 1;
    writer->writeFromAudioSampleBuffer(output, 0, outLength);
    std::cout << out.getFileName() << " | " << args[3] << " c | " << juce::String(outLength / reader->sampleRate, 2) << " s\n";
    return 0;
}

// peaks <file> <fromSeconds> <toSeconds>: averaged long-FFT spectrum, strongest peaks below 1 kHz.
int runPeaks(const juce::StringArray& args)
{
    if (args.size() < 4)
        return 1;
    std::vector<float> audio;
    double rate = 0.0;
    if (!loadMono(juce::File(args[1]), audio, rate))
        return 1;
    constexpr int order = 15;
    constexpr int size = 1 << order;
    juce::dsp::FFT fft(order);
    std::vector<double> sum(size / 2, 0.0);
    std::vector<float> data(size * 2);
    const auto from = static_cast<size_t>(args[2].getDoubleValue() * rate);
    const auto to = std::min(audio.size(), static_cast<size_t>(args[3].getDoubleValue() * rate));
    for (size_t start = from; start + size <= to; start += size / 4)
    {
        std::fill(data.begin(), data.end(), 0.0f);
        for (int i = 0; i < size; ++i)
            data[static_cast<size_t>(i)] = audio[start + static_cast<size_t>(i)] * (0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * i / size));
        fft.performFrequencyOnlyForwardTransform(data.data(), true);
        for (int k = 0; k < size / 2; ++k)
            sum[static_cast<size_t>(k)] += data[static_cast<size_t>(k)];
    }
    const double binHz = rate / size;
    std::vector<std::pair<double, int>> peaks;
    for (int k = 2; k < static_cast<int>(1000.0 / binHz); ++k)
        if (sum[static_cast<size_t>(k)] > sum[static_cast<size_t>(k - 1)] && sum[static_cast<size_t>(k)] >= sum[static_cast<size_t>(k + 1)])
            peaks.push_back({ sum[static_cast<size_t>(k)], k });
    std::sort(peaks.rbegin(), peaks.rend());
    static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    for (size_t i = 0; i < std::min<size_t>(12, peaks.size()); ++i)
    {
        const double hz = peaks[i].second * binHz;
        const double midi = 69.0 + 12.0 * std::log2(hz / 440.0);
        const int nearest = static_cast<int>(std::lround(midi));
        std::cout << "  " << juce::String(hz, 1) << " Hz  " << names[(nearest % 12 + 12) % 12] << (nearest / 12 - 1)
                  << " (" << juce::String((midi - nearest) * 100.0, 0) << "c)  level " << juce::String(20.0 * std::log10(peaks[i].first / peaks[0].first), 1) << " dB\n";
    }
    return 0;
}

int runHarmonySynth(const juce::StringArray& args)
{
    int trials = 40;
    for (int i = 1; i < args.size(); ++i)
        if (args[i] == "--trials" && i + 1 < args.size())
            trials = args[++i].getIntValue();

    constexpr double rate = 44100.0;
    std::mt19937 rng(11);
    auto uniform = [&rng](double low, double high) { return std::uniform_real_distribution<double>(low, high)(rng); };

    const std::array<int, 7> minorScale { 0, 2, 3, 5, 7, 8, 10 };
    const std::array<int, 7> majorScale { 0, 2, 4, 5, 7, 9, 11 };
    int keyExact = 0, keyRelative = 0, bassBeats = 0, bassCorrect = 0, bassOctaveCorrect = 0, bassClaimed = 0;

    for (int trial = 0; trial < trials; ++trial)
    {
        const int root = static_cast<int>(rng() % 12);
        const int mode = static_cast<int>(rng() % 2); // 0 minor, 1 major
        const auto& scale = mode == 1 ? majorScale : minorScale;
        const double bpm = uniform(70.0, 150.0);
        const double beat = 60.0 / bpm;
        const int bars = 4;
        const auto length = static_cast<size_t>(bars * 4 * beat * rate);
        std::vector<float> audio(length, 0.0f);

        // Common hip-hop / trap loop progressions (scale degrees, 0 = tonic).
        static const std::array<std::array<int, 4>, 8> progressions { {
            { 0, 5, 2, 6 },  // i VI III VII / I vi iii vii
            { 0, 3, 4, 0 },  // i iv v i
            { 0, 4, 5, 3 },  // I V vi IV
            { 0, 0, 5, 6 },  // i i VI VII
            { 0, 5, 3, 4 },  // i VI iv v
            { 0, 3, 0, 4 },  // i iv i v
            { 0, 6, 5, 6 },  // i VII VI VII
            { 0, 2, 5, 4 },  // i III VI v
        } };
        const auto degrees = progressions[rng() % progressions.size()];
        std::vector<int> expectedBass;
        for (int bar = 0; bar < bars; ++bar)
        {
            const int degree = degrees[static_cast<size_t>(bar)];
            const int chordRootPc = (root + scale[static_cast<size_t>(degree)]) % 12;
            const int third = (root + scale[static_cast<size_t>((degree + 2) % 7)]) % 12;
            const int fifth = (root + scale[static_cast<size_t>((degree + 4) % 7)]) % 12;

            for (int b = 0; b < 4; ++b)
            {
                const int pc = (b == 3 && (rng() % 3 == 0)) ? fifth : chordRootPc;
                const int midi = 36 + pc; // C2..B2
                expectedBass.push_back(midi);
                const double f0 = 440.0 * std::pow(2.0, (midi - 69) / 12.0);
                const auto start = static_cast<size_t>((bar * 4 + b) * beat * rate);
                const auto end = std::min(length, static_cast<size_t>((bar * 4 + b + 1) * beat * rate));
                for (size_t i = start; i < end; ++i)
                {
                    const double t = static_cast<double>(i - start) / rate;
                    const double env = std::min(1.0, t / 0.01) * std::exp(-t / 0.9);
                    audio[i] += static_cast<float>(env * (0.5 * std::sin(juce::MathConstants<double>::twoPi * f0 * t)
                                                          + 0.25 * std::sin(juce::MathConstants<double>::twoPi * 2 * f0 * t)
                                                          + 0.12 * std::sin(juce::MathConstants<double>::twoPi * 3 * f0 * t)));
                }
            }

            for (const int pc : { chordRootPc, third, fifth })
            {
                const double f = 440.0 * std::pow(2.0, (60 + pc - 69) / 12.0);
                const auto start = static_cast<size_t>(bar * 4 * beat * rate);
                const auto end = std::min(length, static_cast<size_t>((bar + 1) * 4 * beat * rate));
                for (size_t i = start; i < end; ++i)
                {
                    const double t = static_cast<double>(i) / rate;
                    audio[i] += static_cast<float>(0.08 * (std::sin(juce::MathConstants<double>::twoPi * f * t)
                                                           + 0.4 * std::sin(juce::MathConstants<double>::twoPi * 2 * f * t)));
                }
            }
        }

        const auto harmony = SampleHarmonyAnalyzer().analyze(audio, rate, beat, 0.0);
        const bool exact = harmony.keyRoot == root && harmony.scaleMode == mode;
        if (!exact && trial < 12)
            std::cout << "  truth root " << root << (mode ? " maj" : " min") << " degrees " << degrees[1] << degrees[2] << degrees[3] << " -> " << harmony.keyName() << " conf " << harmony.keyConfidence << "\n";
        const int relativeRoot = mode == 0 ? (root + 3) % 12 : (root + 9) % 12;
        const bool relative = !exact && harmony.keyRoot == relativeRoot && harmony.scaleMode != mode;
        keyExact += exact ? 1 : 0;
        keyRelative += relative ? 1 : 0;

        for (size_t b = 0; b < expectedBass.size() && b < harmony.bass.size(); ++b)
        {
            ++bassBeats;
            const auto& segment = harmony.bass[b];
            if (segment.midiNote < 0 || segment.confidence < 0.35f)
                continue;
            ++bassClaimed;
            if (segment.midiNote % 12 == expectedBass[b] % 12)
                ++bassCorrect;
            if (segment.midiNote == expectedBass[b])
                ++bassOctaveCorrect;
        }
    }

    std::cout << "HARMONY trials " << trials
              << " | key exact " << keyExact << " + relative " << keyRelative << " / " << trials
              << " | bass claimed " << bassClaimed << "/" << bassBeats
              << " | bass pitch-class correct " << bassCorrect << "/" << bassClaimed
              << " | exact octave " << bassOctaveCorrect << "/" << bassClaimed << "\n";
    return 0;
}
} // namespace

int main(int argc, char** argv)
{
    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add(juce::String::fromUTF8(argv[i]));

    if (args.isEmpty())
    {
        std::cout << "usage:\n  HPDG_BreakLab analyze <file|folder> [--out <folder>] [--host <bpm>] [--hits]\n"
                     "  HPDG_BreakLab synth <kitRoot> [--trials N] [--seed S] [--verbose]\n";
        return 1;
    }

    if (args[0] == "analyze")
        return runAnalyze(args);
    if (args[0] == "rootnote")
        return runRootNote(args);
    if (args[0] == "retune")
        return runRetune(args);
    if (args[0] == "peaks")
        return runPeaks(args);
    if (args[0] == "lines")
        return runLines(args);
    if (args[0] == "harmony-synth")
        return runHarmonySynth(args);
    if (args[0] == "synth")
        return runSynth(args);
    return 1;
}
