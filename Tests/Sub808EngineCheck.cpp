// HPDG's Sub808 lane through the HPDG 808 engine: does a bank sample sound as it did on the plain sample
// player? Every Sub808 sample of every genre plays the same phrase (retriggers, a repeated key, slides,
// legato) both ways; the engine's output, moved back by its latency, is compared with the old one.
//
//   HPDG_Sub808EngineCheck [samplesRoot] [outDir]   (writes old / new WAVs of the first sample per genre)

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>

#include "Bass808/BassEngine.h"
#include "Bass808/Sub808Sound.h"
#include "Core/TrackRegistry.h"
#include "Engine/LaneSampleBank.h"
#include "Engine/PreviewEngine.h"
#include "Engine/SampleLibraryManager.h"

using namespace bbg;

namespace
{
constexpr double kRate = 48000.0;
constexpr double kBpm = 140.0;

struct PhraseNote
{
    double beat;
    double beats;
    int pitch;
    bool glide;
    bool legato;
};

// two bars: a held note, a repeated key, a slide up, a slide down, legato, a long one
const std::vector<PhraseNote> kPhrase {
    { 0.0, 1.0, 36, false, false }, { 1.0, 0.5, 36, false, false }, { 1.5, 0.5, 39, true, false },
    { 2.0, 1.0, 31, false, false }, { 3.0, 0.5, 29, false, true }, { 3.5, 0.5, 36, true, false },
    { 4.0, 0.25, 36, false, false }, { 4.25, 0.25, 36, false, false }, { 4.5, 1.5, 34, false, false },
    { 6.0, 2.0, 27, false, false },
};

int defaultSubPitch()
{
    const auto* info = TrackRegistry::find(TrackType::Sub808);
    return info != nullptr ? info->defaultMidiNote : 36;
}

int basePitch(const LaneSampleBank& bank)
{
    const int def = defaultSubPitch();
    const int rootMidi = bank.getSelectedRootMidi(TrackType::Sub808);
    return rootMidi >= 0 ? rootMidi : def + ((juce::jlimit(0, 11, bank.getSelectedRootPitchClass(TrackType::Sub808)) - def % 12 + 18) % 12 - 6);
}

// the processor's playbackRateForTrackPitch / sub808BankRootHz, for this check
float rateFor(const LaneSampleBank& bank, int pitch)
{
    const double cents = juce::jlimit(-100.0, 100.0, bank.getSelectedRootCents(TrackType::Sub808));
    return static_cast<float>(std::pow(2.0, (static_cast<double>(pitch - basePitch(bank)) - cents / 100.0) / 12.0));
}

double rootHz(const LaneSampleBank& bank)
{
    const double cents = juce::jlimit(-100.0, 100.0, bank.getSelectedRootCents(TrackType::Sub808));
    return 440.0 * std::pow(2.0, (basePitch(bank) + cents / 100.0 - 69.0) / 12.0);
}

// irregular: host blocks of 32 .. 1500 samples, changing every block (FL Studio does this)
std::vector<float> render(const LaneSampleBank& bank, std::shared_ptr<const Sub808Sound> sound, int& lead, bool irregular = false)
{
    PreviewEngine engine;
    engine.prepare(kRate);
    engine.setSub808Sound(std::move(sound), kBpm);
    lead = engine.sub808LeadSamples(bank);
    const double perBeat = 60.0 / kBpm * kRate;
    const int length = static_cast<int>(9.0 * perBeat);
    constexpr int maxBlock = 1500;
    juce::AudioBuffer<float> out(1, length + maxBlock);
    out.clear();
    std::uint32_t random = 12345u;
    for (int position = 0, block = 512; position < length; position += block)
    {
        if (irregular)
        {
            random = random * 1664525u + 1013904223u;
            block = 32 + static_cast<int>((random >> 8) % static_cast<std::uint32_t>(maxBlock - 32));
        }
        for (size_t i = 0; i < kPhrase.size(); ++i)
        {
            const auto& n = kPhrase[i];
            const int at = static_cast<int>(n.beat * perBeat); // old and new sent at the same time: new is compared `lead` later
            if (at < position || at >= position + block)
                continue;
            PreviewEngine::TriggerOptions options;
            options.midiNote = n.pitch;
            options.playbackRate = rateFor(bank, n.pitch);
            options.mono = true;
            options.cutItself = true;
            options.glide = n.glide;
            options.legato = n.legato;
            options.glideDurationSamples = static_cast<int>(0.12 * kRate);
            options.maxDurationSamples = static_cast<int>(n.beats * perBeat);
            engine.noteOnAtSample(TrackType::Sub808, 0.9f, at - position, bank, options);
        }
        engine.render(out, position, block);
    }
    return { out.getReadPointer(0), out.getReadPointer(0) + length };
}

void writeWav(const juce::File& file, const std::vector<float>& data)
{
    juce::AudioBuffer<float> buffer(1, static_cast<int>(data.size()));
    buffer.copyFrom(0, 0, data.data(), static_cast<int>(data.size()));
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(new juce::FileOutputStream(file), kRate, 1, 24, {}, 0));
    if (writer != nullptr)
        writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
}

double db(double ratio) { return 20.0 * std::log10(std::max(1.0e-12, ratio)); }
} // namespace

int main(int argc, char* argv[])
{
    juce::File root { argc > 1 ? juce::String(argv[1]) : juce::File::getCurrentWorkingDirectory().getChildFile("Samples").getFullPathName() };
    juce::File outDir { argc > 2 ? juce::String(argv[2]) : juce::String() };
    if (outDir != juce::File())
        outDir.createDirectory();

    const std::array<GenreType, 5> genres { GenreType::BoomBap, GenreType::Trap, GenreType::Drill, GenreType::DnB, GenreType::Techno };
    int samples = 0, failed = 0;
    double worstResidual = -200.0, worstLevel = 0.0, worstBlockDiff = 0.0;
    std::printf("%-10s %-28s %8s %9s %9s %8s\n", "genre", "sample", "lead", "residual", "level", "corr");
    for (const auto genre : genres)
    {
        SampleLibraryManager library;
        library.setRootDirectory(root);
        library.setGenre(genre);
        library.scan();
        LaneSampleBank bank;
        bank.applyLibrary(library);
        if (!bank.hasSamples(TrackType::Sub808))
            continue;
        for (int index = 0; index < 64; ++index)
        {
            bank.selectIndex(TrackType::Sub808, index);
            if (bank.getSelectedIndex(TrackType::Sub808) != index)
                break;
            const auto name = bank.getSelectedName(TrackType::Sub808);
            int oldLead = 0, newLead = 0;
            const auto before = render(bank, nullptr, oldLead);
            const auto sound = buildSub808Sound({}, bank.getSelectedBufferShared(TrackType::Sub808),
                                                bank.getSelectedSampleRate(TrackType::Sub808), rootHz(bank), name);
            const auto after = render(bank, sound, newLead);
            int irregularLead = 0;
            const auto irregularBlocks = render(bank, sound, irregularLead, true);
            double blockDiff = 0.0;
            for (size_t i = 0; i < after.size(); ++i)
                blockDiff = std::max(blockDiff, static_cast<double>(std::abs(after[i] - irregularBlocks[i])));
            worstBlockDiff = std::max(worstBlockDiff, blockDiff);
            if (std::getenv("SUB808_BLOCKS") != nullptr && index == 0)
            {
                int l0 = 0, l1 = 0;
                const auto oldIrregular = render(bank, nullptr, l1, true);
                double oldDiff = 0.0;
                for (size_t i = 0; i < before.size(); ++i)
                    oldDiff = std::max(oldDiff, static_cast<double>(std::abs(before[i] - oldIrregular[i])));
                size_t first = 0;
                while (first < after.size() && std::abs(after[first] - irregularBlocks[first]) < 1.0e-4f)
                    ++first;
                std::printf("    old player irregular diff %.2e; engine first difference at sample %zu (%.3f s)\n", oldDiff, first, first / kRate);
                for (size_t k = first; k < first + 6 && k < after.size(); ++k)
                    std::printf("      %zu: 512 %+.5f  irregular %+.5f  old %+.5f\n", k, after[k], irregularBlocks[k], k >= 242 ? before[k - 242] : 0.0f);
                (void) l0;
            }

            double energy = 0.0, residual = 0.0, cross = 0.0, energyNew = 0.0;
            for (size_t i = 0; i + static_cast<size_t>(newLead) < after.size(); ++i)
            {
                const double a = before[i], b = after[i + static_cast<size_t>(newLead)];
                energy += a * a;
                energyNew += b * b;
                residual += (a - b) * (a - b);
                cross += a * b;
            }
            if (const char* only = std::getenv("SUB808_NOTES"); only != nullptr && name == juce::String(only))
            {
                const double perBeat = 60.0 / kBpm * kRate;
                for (const auto& n : kPhrase)
                {
                    const size_t s0 = static_cast<size_t>(n.beat * perBeat), s1 = static_cast<size_t>((n.beat + n.beats) * perBeat);
                    double e = 0, en = 0, c = 0;
                    for (size_t i = s0; i < s1 && i + static_cast<size_t>(newLead) < after.size(); ++i)
                    {
                        const double a = before[i], b = after[i + static_cast<size_t>(newLead)];
                        e += a * a; en += b * b; c += a * b;
                    }
                    std::printf("    beat %.2f pitch %d %s%s: corr %.4f level %.2f dB\n", n.beat, n.pitch, n.glide ? "glide " : "", n.legato ? "legato " : "",
                                c / std::sqrt(std::max(1.0e-12, e * en)), db(std::sqrt(en / std::max(1.0e-12, e))));
                }
            }
            const double residualDb = db(std::sqrt(residual / std::max(1.0e-12, energy)));
            const double levelDb = db(std::sqrt(energyNew / std::max(1.0e-12, energy)));
            const double corr = cross / std::sqrt(std::max(1.0e-12, energy * energyNew));
            const bool ok = corr > 0.998 && std::abs(levelDb) < 0.1;
            ++samples;
            failed += ok ? 0 : 1;
            worstResidual = std::max(worstResidual, residualDb);
            worstLevel = std::max(worstLevel, std::abs(levelDb));
            std::printf("%-10s %-28s %8d %8.1f %8.2f %8.4f %s\n", SampleLibraryManager::folderNameForGenre(genre).toRawUTF8(),
                        name.substring(0, 28).toRawUTF8(), newLead, residualDb, levelDb, corr, ok ? "" : "<-- FAIL");
            if (outDir != juce::File() && index == 0)
            {
                const auto stem = SampleLibraryManager::folderNameForGenre(genre) + "_" + juce::File::createLegalFileName(name);
                writeWav(outDir.getChildFile(stem + "_old.wav"), before);
                std::vector<float> aligned(after.begin() + newLead, after.end());
                writeWav(outDir.getChildFile(stem + "_new.wav"), aligned);
            }
        }
    }
    std::printf("\n%d samples, %d failed; worst residual %.1f dB, worst level %.2f dB\n", samples, failed, worstResidual, worstLevel);
    // the strip's DRIVE: level of a held C2 (RMS 50-500 ms) against DRIVE 0, per saturator model, for the
    // lane's bank sample and two HPDG 808 presets (one without, one with a saturator of its own)
    double worstDriveDb = 0.0;
    {
        SampleLibraryManager library;
        library.setRootDirectory(root);
        library.setGenre(GenreType::Trap);
        library.scan();
        LaneSampleBank bank;
        bank.applyLibrary(library);
        const auto bankSound = buildSub808Sound({}, bank.getSelectedBufferShared(TrackType::Sub808),
                                                bank.getSelectedSampleRate(TrackType::Sub808), rootHz(bank), "bank");
        struct Source { const char* name; BassSettings settings; std::shared_ptr<const BassSample> sample; };
        std::vector<Source> sources { { "bank sample", sub808SampleOnlySettings(), bankSound->sample } };
        for (const auto& preset : bassPresets())
            if (juce::String(preset.name) == bassPresets().front().name || juce::String(preset.name) == "Dusty")
                sources.push_back({ preset.name, preset.settings, nullptr });
        const auto level = [](const BassSettings& s, std::shared_ptr<const BassSample> sample)
        {
            const auto audio = BassEngine::renderNote(s, sample, 36, 0.6, kRate);
            double e = 0.0;
            const size_t from = static_cast<size_t>(0.05 * kRate), to = static_cast<size_t>(0.5 * kRate);
            for (size_t i = from; i < to; ++i)
                e += audio[i] * audio[i];
            return std::sqrt(e / static_cast<double>(to - from));
        };
        std::printf("\nstrip DRIVE: level against DRIVE 0 (dB), held C2\n");
        for (const auto& source : sources)
        {
            Sub808SoundState strip;
            const double dry = level(applySub808Strip(source.settings, strip), source.sample);
            for (int model = 1; model <= 6; ++model)
            {
                std::printf("  %-12s model %d:", source.name, model);
                for (const float drive : { 0.25f, 0.5f, 0.75f, 1.0f })
                {
                    strip.drive = drive;
                    strip.driveModel = model;
                    const double changeDb = db(level(applySub808Strip(source.settings, strip), source.sample) / dry);
                    worstDriveDb = std::max(worstDriveDb, std::abs(changeDb));
                    std::printf(" %+6.1f", changeDb);
                }
                std::printf("\n");
                if (source.settings.satModel != 0)
                    break; // a sound with its own saturator: DRIVE adds to it, the model is its own
            }
        }
        std::printf("strip DRIVE: largest level change %.1f dB\n", worstDriveDb);
    }

    // the kick's sidechain: one held bass note, kicks every beat; level just after a kick vs between kicks
    double sidechainDipDb = 0.0, sidechainOffDipDb = 0.0;
    {
        SampleLibraryManager library;
        library.setRootDirectory(root);
        library.setGenre(GenreType::Trap);
        library.scan();
        LaneSampleBank bank;
        bank.applyLibrary(library);
        for (const float amount : { 0.0f, 1.0f })
        {
            Sub808SoundState state;
            state.sidechain = amount;
            PreviewEngine engine;
            engine.prepare(kRate);
            engine.setSub808Sound(buildSub808Sound(state, bank.getSelectedBufferShared(TrackType::Sub808),
                                                   bank.getSelectedSampleRate(TrackType::Sub808), rootHz(bank), "sc"), kBpm);
            const int perBeat = static_cast<int>(60.0 / kBpm * kRate);
            juce::AudioBuffer<float> out(1, 4 * perBeat); // the bass lane alone (the kicks sound on their own lane)
            out.clear();
            std::array<juce::AudioBuffer<float>, kTrackTypeCount> lanes;
            for (int position = 0; position < out.getNumSamples(); position += 512)
            {
                const int block = std::min(512, out.getNumSamples() - position);
                if (position == 0)
                {
                    PreviewEngine::TriggerOptions options;
                    options.midiNote = 36;
                    options.maxDurationSamples = 4 * perBeat;
                    engine.noteOnAtSample(TrackType::Sub808, 0.9f, 0, bank, options);
                }
                for (int beat = 1; beat < 4; ++beat)
                    if (beat * perBeat >= position && beat * perBeat < position + block)
                        engine.noteOnAtSample(TrackType::Kick, 0.9f, beat * perBeat - position, bank, {});
                for (auto& laneBuffer : lanes)
                {
                    laneBuffer.setSize(1, block, false, false, true);
                    laneBuffer.clear();
                }
                engine.renderSeparated(lanes, 0, block);
                out.copyFrom(0, position, lanes[static_cast<size_t>(trackTypeIndex(TrackType::Sub808))], 0, 0, block);
            }
            const auto rms = [&](int from, int count)
            {
                double e = 0.0;
                for (int i = from; i < from + count; ++i)
                    e += out.getSample(0, i) * out.getSample(0, i);
                return std::sqrt(e / count);
            };
            const int window = static_cast<int>(0.02 * kRate);
            const double dip = db(rms(2 * perBeat + window / 2, window) / rms(2 * perBeat - 2 * window, window));
            (amount > 0.5f ? sidechainDipDb : sidechainOffDipDb) = dip;
        }
        std::printf("sidechain: level 10-30 ms after a kick vs before it: off %.1f dB, full %.1f dB\n", sidechainOffDipDb, sidechainDipDb);
    }
    std::printf("irregular host blocks vs 512: largest sample difference %.2e\n", worstBlockDiff);
    std::printf("%s\n", failed == 0 && samples > 0 && worstBlockDiff < 1.0e-6 && sidechainDipDb < -10.0 && std::abs(sidechainOffDipDb) < 3.0 && worstDriveDb < 3.0 ? "OK" : "FAILED");
    return failed == 0 && samples > 0 && worstBlockDiff < 1.0e-6 && sidechainDipDb < -10.0 && std::abs(sidechainOffDipDb) < 3.0 && worstDriveDb < 3.0 ? 0 : 1;
}
