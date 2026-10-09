#include <algorithm>
#include <array>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

#include <juce_events/juce_events.h>

#include "../Source/Analysis/DrumBreakTranscriber.h"
#include "../Source/Core/ProjectLaneAccess.h"
#include "../Source/Core/RuntimeLaneLifecycle.h"
#include "../Source/Core/TrackRegistry.h"
#include "../Source/Engine/BoomBapEngine.h"
#include "../Source/Engine/MidiExportEngine.h"
#include "../Source/Engine/PreviewEngine.h"
#include "../Source/Engine/LaneSampleBank.h"
#include "../Source/Engine/TrapEngine.h"
#include "../Source/Plugin/PluginProcessor.h"
#include "../Source/UI/ComboBoxIdParameterAttachment.h"

namespace bbg
{
namespace
{
[[noreturn]] void fail(const juce::String& message)
{
    throw std::runtime_error(message.toStdString());
}

void expect(bool condition, const juce::String& message)
{
    if (!condition)
        fail(message);
}

float maxAbsSample(const juce::AudioBuffer<float>& buffer)
{
    float peak = 0.0f;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        peak = juce::jmax(peak, buffer.getMagnitude(channel, 0, buffer.getNumSamples()));
    return peak;
}

RuntimeLaneId requireBackedLaneId(const PatternProject& project, TrackType type)
{
    const auto* lane = findRuntimeLaneForTrack(project.runtimeLaneProfile, type);
    expect(lane != nullptr, "Expected backed runtime lane for requested track.");
    return lane->laneId;
}

juce::File makeTestOutputFile(const juce::String& name)
{
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("HPDG_LaneBoundaryTests");
    dir.createDirectory();
    auto file = dir.getChildFile(name);
    if (file.existsAsFile())
        file.deleteFile();
    return file;
}

void cleanupFile(const juce::File& file)
{
    if (file.existsAsFile())
        file.deleteFile();
}

void testLaneAwareExportTrackPath()
{
    BoomBapGeneratorAudioProcessor processor;
    const auto project = processor.getProjectSnapshot();
    const auto kickLaneId = requireBackedLaneId(project, TrackType::Kick);

    const auto trackFile = makeTestOutputFile("kick-tracktype.mid");
    const auto laneFile = makeTestOutputFile("kick-laneid.mid");
    const auto invalidFile = makeTestOutputFile("invalid-lane.mid");

    const bool trackResult = processor.exportTrackToFile(TrackType::Kick, trackFile);
    const bool laneResult = processor.exportTrackToFile(kickLaneId, laneFile);
    const bool invalidLaneResult = processor.exportTrackToFile(RuntimeLaneId("missing:lane"), invalidFile);

    expect(trackResult == laneResult, "Lane-aware export path must mirror TrackType export result.");
    expect(!invalidLaneResult, "Missing lane export must fail safely.");

    if (laneResult)
    {
        expect(trackFile.existsAsFile(), "TrackType export must create a MIDI file when successful.");
        expect(laneFile.existsAsFile(), "Lane-aware export must create a MIDI file when successful.");
        expect(trackFile.getSize() > 0, "TrackType export file must not be empty.");
        expect(laneFile.getSize() > 0, "Lane-aware export file must not be empty.");
    }

    cleanupFile(trackFile);
    cleanupFile(laneFile);
    cleanupFile(invalidFile);
}

void testLaneAwareTemporaryMidiPath()
{
    BoomBapGeneratorAudioProcessor processor;
    const auto project = processor.getProjectSnapshot();
    const auto kickLaneId = requireBackedLaneId(project, TrackType::Kick);

    const auto trackFile = processor.createTemporaryTrackMidiFile(TrackType::Kick);
    const auto laneFile = processor.createTemporaryTrackMidiFile(kickLaneId);
    const auto invalidLaneFile = processor.createTemporaryTrackMidiFile(RuntimeLaneId("missing:lane"));

    expect(trackFile.existsAsFile(), "TrackType temp MIDI path must create a file for a backed lane.");
    expect(laneFile.existsAsFile(), "Lane-aware temp MIDI path must create a file for a backed lane.");
    expect(invalidLaneFile == juce::File(), "Missing lane temp MIDI path must return a safe empty file.");

    cleanupFile(trackFile);
    cleanupFile(laneFile);
}

void testMidiExportKeepsFirstKickWithNegativeMicrotiming()
{
    BoomBapGeneratorAudioProcessor processor;
    auto project = processor.getProjectSnapshot();
    for (auto& track : project.tracks)
    {
        track.enabled = false;
        track.notes.clear();
        track.sub808Notes.clear();
        track.baseNotes.clear();
        track.baseSub808Notes.clear();
    }

    auto* kick = ProjectLaneAccess::findTrackState(project, TrackType::Kick);
    expect(kick != nullptr, "Negative first-kick MIDI export regression requires a Kick track.");
    kick->enabled = true;
    kick->muted = false;
    kick->solo = false;
    {
        NoteEvent note;
        note.pitch = 36;
        note.gridTick = 0;
        note.lengthTicks = TimingGrid::Sixteenth;
        note.velocity = 120;
        note.timingOffsetTicks = -36;
        note.semanticRole = "negative_first_kick";
        kick->notes.push_back(note);
    }

    project.params.bars = 1;
    const auto sequence = MidiExportEngine::patternToSequence(project, TrackType::Kick, 960, false, false);

    bool foundKickAtZero = false;
    for (int index = 0; index < sequence.getNumEvents(); ++index)
    {
        const auto* event = sequence.getEventPointer(index);
        if (event != nullptr
            && event->message.isNoteOn()
            && event->message.getNoteNumber() == 60
            && static_cast<int>(std::lround(event->message.getTimeStamp())) == 0)
        {
            foundKickAtZero = true;
            break;
        }
    }

    expect(foundKickAtZero, "MIDI export must keep a step-0 Kick even when groove timing nudges it before tick 0.");
}

void testLaneAwareSampleCommandPath()
{
    BoomBapGeneratorAudioProcessor processorFromTrack;
    const auto project = processorFromTrack.getProjectSnapshot();
    const auto kickLaneId = requireBackedLaneId(project, TrackType::Kick);

    const auto trackDirectory = processorFromTrack.getLaneSampleDirectory(TrackType::Kick);
    const bool nextTrackResult = processorFromTrack.selectNextLaneSample(TrackType::Kick);
    const bool previousTrackResult = processorFromTrack.selectPreviousLaneSample(TrackType::Kick);

    BoomBapGeneratorAudioProcessor processorFromLane;
    const auto laneDirectory = processorFromLane.getLaneSampleDirectory(kickLaneId);
    const bool nextLaneResult = processorFromLane.selectNextLaneSample(kickLaneId);
    const bool previousLaneResult = processorFromLane.selectPreviousLaneSample(kickLaneId);
    const bool invalidLaneResult = processorFromLane.selectNextLaneSample(RuntimeLaneId("missing:lane"));

    expect(trackDirectory.getFullPathName() == laneDirectory.getFullPathName(),
           "Lane-aware sample directory lookup must match TrackType lookup.");
    expect(nextTrackResult == nextLaneResult,
           "Lane-aware next-sample command must match TrackType behavior.");
    expect(previousTrackResult == previousLaneResult,
           "Lane-aware previous-sample command must match TrackType behavior.");
    expect(!invalidLaneResult, "Missing lane sample command must fail safely.");
}

void testGeneratePatternRotatesLaneSamples()
{
    BoomBapGeneratorAudioProcessor processor;
    const auto beforeProject = processor.getProjectSnapshot();
    const auto* beforeKick = ProjectLaneAccess::findTrackState(beforeProject, TrackType::Kick);
    expect(beforeKick != nullptr, "Sample rotation smoke requires a Kick track.");
    expect(beforeKick->selectedSampleName.isNotEmpty() && beforeKick->selectedSampleName != "(empty)",
           "Sample rotation smoke requires loaded Kick samples.");

    const int beforeIndex = beforeKick->selectedSampleIndex;
    const auto beforeName = beforeKick->selectedSampleName;

    processor.generatePattern();

    const auto afterProject = processor.getProjectSnapshot();
    const auto* afterKick = ProjectLaneAccess::findTrackState(afterProject, TrackType::Kick);
    expect(afterKick != nullptr, "Sample rotation smoke must keep the Kick track.");
    expect(afterKick->selectedSampleIndex != beforeIndex || afterKick->selectedSampleName != beforeName,
           "Generate Pattern must rotate the selected Kick sample.");
}

void testPreviewProcessBlockProducesAudio()
{
    BoomBapGeneratorAudioProcessor processor;
    processor.prepareToPlay(44100.0, 512);

    auto project = processor.getProjectSnapshot();
    for (auto& track : project.tracks)
    {
        track.enabled = false;
        track.notes.clear();
        track.sub808Notes.clear();
        track.baseNotes.clear();
        track.baseSub808Notes.clear();
    }

    auto* kick = ProjectLaneAccess::findTrackState(project, TrackType::Kick);
    expect(kick != nullptr, "Preview audio smoke requires a Kick track.");
    kick->enabled = true;
    kick->muted = false;
    kick->solo = false;
    kick->laneVolume = 1.0f;
    {
        NoteEvent note;
        note.pitch = 36;
        note.gridTick = 0;
        note.lengthTicks = TimingGrid::Sixteenth;
        note.velocity = 120;
        note.timingOffsetTicks = 0;
        note.semanticRole = "preview_smoke";
        kick->notes.push_back(note);
    }

    project.params.bars = 1;
    project.previewStartStep = 0;
    processor.restoreEditorProjectSnapshot(project);
    processor.startPreview();

    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    float peak = 0.0f;

    for (int block = 0; block < 12; ++block)
    {
        buffer.clear();
        midi.clear();
        processor.processBlock(buffer, midi);
        peak = juce::jmax(peak, maxAbsSample(buffer));
    }

    processor.stopPreview();
    expect(peak > 1.0e-4f, "Preview processBlock must produce non-silent audio.");
}

void testPreviewProcessBlockWithNeutralEqProducesAudio()
{
    BoomBapGeneratorAudioProcessor processor;
    processor.prepareToPlay(44100.0, 512);

    auto project = processor.getProjectSnapshot();
    for (auto& track : project.tracks)
    {
        track.enabled = false;
        track.notes.clear();
        track.sub808Notes.clear();
        track.baseNotes.clear();
        track.baseSub808Notes.clear();
    }

    auto* kick = ProjectLaneAccess::findTrackState(project, TrackType::Kick);
    expect(kick != nullptr, "Preview EQ smoke requires a Kick track.");
    kick->enabled = true;
    kick->muted = false;
    kick->solo = false;
    kick->laneVolume = 1.0f;
    {
        NoteEvent note;
        note.pitch = 36;
        note.gridTick = 0;
        note.lengthTicks = TimingGrid::Sixteenth;
        note.velocity = 120;
        note.timingOffsetTicks = 0;
        note.semanticRole = "preview_eq_smoke";
        kick->notes.push_back(note);
    }

    project.params.bars = 1;
    project.previewStartStep = 0;
    project.globalSound.eq.selectedBand = 2;
    project.globalSound.eq.bands[2].enabled = true;
    project.globalSound.eq.bands[2].shape = EqBandShape::Bell;
    project.globalSound.eq.bands[2].freqHz = 180.0f;
    project.globalSound.eq.bands[2].gainDb = 0.0f;
    project.globalSound.eq.bands[2].q = 1.0f;
    project.globalSound.eqTone = legacyEqToneFromEqState(project.globalSound.eq);

    processor.restoreEditorProjectSnapshot(project);
    processor.startPreview();

    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    float peak = 0.0f;

    for (int block = 0; block < 12; ++block)
    {
        buffer.clear();
        midi.clear();
        processor.processBlock(buffer, midi);
        peak = juce::jmax(peak, maxAbsSample(buffer));
    }

    processor.stopPreview();
    expect(peak > 1.0e-4f, "Neutral EQ must not mute preview audio.");
}

void testPreviewProcessBlockKeepsFirstKickWithNegativeMicrotiming()
{
    BoomBapGeneratorAudioProcessor processor;
    processor.prepareToPlay(44100.0, 512);

    auto project = processor.getProjectSnapshot();
    for (auto& track : project.tracks)
    {
        track.enabled = false;
        track.notes.clear();
        track.sub808Notes.clear();
        track.baseNotes.clear();
        track.baseSub808Notes.clear();
    }

    auto* kick = ProjectLaneAccess::findTrackState(project, TrackType::Kick);
    expect(kick != nullptr, "Negative first-kick preview regression requires a Kick track.");
    kick->enabled = true;
    kick->muted = false;
    kick->solo = false;
    kick->laneVolume = 1.0f;
    {
        NoteEvent note;
        note.pitch = 36;
        note.gridTick = 0;
        note.lengthTicks = TimingGrid::Sixteenth;
        note.velocity = 120;
        note.timingOffsetTicks = -36;
        note.semanticRole = "negative_preview_kick";
        kick->notes.push_back(note);
    }

    project.params.bars = 1;
    project.previewStartStep = 0;
    processor.restoreEditorProjectSnapshot(project);
    processor.startPreview();

    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    float peak = 0.0f;

    for (int block = 0; block < 12; ++block)
    {
        buffer.clear();
        midi.clear();
        processor.processBlock(buffer, midi);
        peak = juce::jmax(peak, maxAbsSample(buffer));
    }

    processor.stopPreview();
    expect(peak > 1.0e-4f, "Preview must play a step-0 Kick even when groove timing nudges it before tick 0.");
}

// Renders a 2-bar 90 BPM break (kick / snare / closed hat on eighths) to a WAV and runs it
// through the plugin's Extract / Exact Copy path: only K/S/H lanes, tick positions from the
// break itself (the plugin tempo is left at its own value).
// The bass [1][2][3] amount with a loaded sample, in any sample mode: a click only stores the
// choice (nothing is regenerated), the choice sticks, RG on the bass writes a line in that mode
// and leaves the drums alone, and RG on another lane never wipes the rest.
void checkBassAmountKeepsOtherLanes(BoomBapGeneratorAudioProcessor& processor, const juce::String& mode)
{
    auto ticksOf = [&processor](TrackType lane)
    {
        std::vector<int> ticks;
        const auto snapshot = processor.getProjectSnapshot();
        if (const auto* state = ProjectLaneAccess::findTrackState(snapshot, lane))
            for (const auto& note : state->notes)
                ticks.push_back(note.gridTick + note.timingOffsetTicks);
        std::sort(ticks.begin(), ticks.end());
        return ticks;
    };
    auto settingsOf = [&processor]
    {
        const auto snapshot = processor.getProjectSnapshot();
        const auto* bass = ProjectLaneAccess::findTrackState(snapshot, TrackType::Sub808);
        return bass != nullptr ? bass->sub808Settings : Sub808LaneSettings {};
    };

    processor.setTrackEnabled(TrackType::Sub808, true);
    const juce::String label = mode + ": ";
    const auto kicks = ticksOf(TrackType::Kick);
    const auto snares = ticksOf(TrackType::Snare);
    const auto hats = ticksOf(TrackType::HiHat);
    expect(!kicks.empty() && !snares.empty(), label + "no drums to start from");

    for (const int amount : { 1, 2, 0 })
    {
        const auto bassBefore = ticksOf(TrackType::Sub808);
        auto settings = settingsOf();
        settings.bassAmount = amount;
        processor.setSub808LaneSettings(TrackType::Sub808, settings);
        const juce::String step = label + "amount " + juce::String(amount + 1) + ": ";
        expect(settingsOf().bassAmount == amount, step + "the choice did not stick");
        expect(ticksOf(TrackType::Sub808) == bassBefore && ticksOf(TrackType::Kick) == kicks,
               step + "choosing the amount must not change the pattern");

        processor.regenerateTrack(TrackType::Sub808);
        expect(settingsOf().bassAmount == amount, step + "RG reset the choice");
        expect(!ticksOf(TrackType::Sub808).empty(), step + "RG left the bass empty");
        expect(ticksOf(TrackType::Kick) == kicks && ticksOf(TrackType::Snare) == snares && ticksOf(TrackType::HiHat) == hats,
               step + "RG on the bass changed or wiped the drums");
    }

    processor.regenerateTrack(TrackType::Kick);
    expect(!ticksOf(TrackType::Kick).empty(), label + "RG on the kick left it empty");
    expect(!ticksOf(TrackType::Snare).empty() && !ticksOf(TrackType::Sub808).empty(), label + "RG on the kick wiped other lanes");
    processor.regenerateTrack(TrackType::Snare);
    expect(!ticksOf(TrackType::Snare).empty() && !ticksOf(TrackType::Kick).empty() && !ticksOf(TrackType::Sub808).empty(),
           label + "a second RG wiped lanes");
}

void testDrumBreakExactCopyPath()
{
    constexpr double rate = 44100.0;
    constexpr double bpm = 90.0;
    const double sixteenth = 15.0 / bpm;
    const int length = static_cast<int>(32 * sixteenth * rate);
    juce::AudioBuffer<float> audio(1, length);
    audio.clear();
    juce::Random random(7);

    auto addHit = [&](int slot, int lane)
    {
        const int start = static_cast<int>(slot * sixteenth * rate);
        const int duration = static_cast<int>((lane == 0 ? 0.25 : lane == 1 ? 0.2 : 0.05) * rate);
        float previousNoise = 0.0f;
        double phase = 0.0;
        for (int i = 0; i < duration && start + i < length; ++i)
        {
            const double t = i / rate;
            float value = 0.0f;
            if (lane == 0)
            {
                phase += juce::MathConstants<double>::twoPi * (50.0 + 70.0 * std::exp(-t / 0.03)) / rate;
                value = static_cast<float>(0.9 * std::sin(phase) * std::exp(-t / 0.08));
            }
            else if (lane == 1)
            {
                value = static_cast<float>(0.5 * (random.nextFloat() * 2.0f - 1.0f) * std::exp(-t / 0.05)
                                           + 0.4 * std::sin(juce::MathConstants<double>::twoPi * 190.0 * t) * std::exp(-t / 0.04));
            }
            else
            {
                const float noise = random.nextFloat() * 2.0f - 1.0f;
                value = static_cast<float>(0.35 * (noise - previousNoise) * std::exp(-t / 0.012));
                previousNoise = noise;
            }
            audio.addSample(0, start + i, value);
        }
    };

    const std::vector<int> kicks { 0, 10, 16, 26 };
    const std::vector<int> snares { 4, 12, 20, 28 };
    for (const int slot : kicks)
        addHit(slot, 0);
    for (const int slot : snares)
        addHit(slot, 1);
    for (int slot = 0; slot < 32; slot += 2)
        addHit(slot, 2);

    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("hpdg_break_copy_test.wav");
    file.deleteFile();
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(new juce::FileOutputStream(file), rate, 1, 24, {}, 0));
        expect(writer != nullptr, "could not create test wav");
        writer->writeFromAudioSampleBuffer(audio, 0, length);
    }

    BoomBapGeneratorAudioProcessor processor;
    processor.setAnalysisMode(AnalysisMode::ExtractFromSample);
    processor.setSampleApplyMode(SampleApplyMode::ExactCopy);
    juce::String error;
    expect(processor.analyzeAudioFile(file, &error), "analysis failed: " + error);

    const auto analysis = processor.getDrumBreakAnalysis();
    expect(std::abs(analysis.bpm - bpm) < 0.5, "break tempo " + juce::String(analysis.bpm, 2));
    expect(analysis.bars == 2, "break bars " + juce::String(analysis.bars));

    const auto project = processor.getProjectSnapshot();
    auto gridTicks = [&](TrackType lane)
    {
        std::vector<int> ticks;
        if (const auto* state = ProjectLaneAccess::findTrackState(project, lane))
            for (const auto& note : state->notes)
                ticks.push_back(note.gridTick);
        std::sort(ticks.begin(), ticks.end());
        return ticks;
    };

    auto expectSlots = [&](TrackType lane, const std::vector<int>& slots)
    {
        std::vector<int> expected;
        for (const int slot : slots)
            expected.push_back(slot * TimingGrid::Sixteenth);
        const auto actual = gridTicks(lane);
        juce::String got;
        for (const int tick : actual)
            got << tick / TimingGrid::Sixteenth << " ";
        expect(actual == expected, juce::String(toString(lane)) + " slots: " + got);
    };

    expectSlots(TrackType::Kick, kicks);
    expectSlots(TrackType::Snare, snares);
    std::vector<int> hatSlots;
    for (int slot = 0; slot < 32; slot += 2)
        hatSlots.push_back(slot);
    expectSlots(TrackType::HiHat, hatSlots);
    expect(gridTicks(TrackType::GhostKick).empty() && gridTicks(TrackType::Sub808).empty(),
           "copy must only fill Kick / Snare / HiHat");

    checkBassAmountKeepsOtherLanes(processor, "copy break");

    // Tempo priority: with an analysed sample every generation stays at the sample's tempo
    // (instead of re-rolling the style range); BPM lock still wins over the sample.
    processor.setAnalysisMode(AnalysisMode::GenerateFromSample);
    expect(processor.analyzeAudioFile(file, &error), "guide analysis failed: " + error);
    for (int run = 0; run < 3; ++run)
    {
        processor.generatePattern();
        const float generated = processor.getProjectSnapshot().params.bpm;
        expect(std::abs(generated - static_cast<float>(bpm)) < 0.6f,
               "generation " + juce::String(run + 1) + " left the sample tempo: " + juce::String(generated, 2));
    }

    checkBassAmountKeepsOtherLanes(processor, "guide (drum loop)");

    auto& apvts = processor.getApvts();
    apvts.getParameter(ParamIds::bpm)->setValueNotifyingHost(apvts.getParameter(ParamIds::bpm)->convertTo0to1(100.0f));
    apvts.getParameter(ParamIds::bpmLock)->setValueNotifyingHost(1.0f);
    processor.generatePattern();
    expect(std::abs(processor.getProjectSnapshot().params.bpm - 100.0f) < 0.6f, "BPM lock must win over the sample tempo");

    file.deleteFile();
}

// Guide mode with a musical sample: sustained chords plus low "thumps" (one of them on the
// snare backbeat). Nothing is copied from the sample; kicks may only be added on sample peaks
// that are not backbeat / snare positions.
void testGuideModeAccentKicksAvoidBackbeat()
{
    constexpr double rate = 44100.0;
    BoomBapGeneratorAudioProcessor processor;
    const double bpm = static_cast<double>(processor.getProjectSnapshot().params.bpm);
    const double sixteenth = 15.0 / bpm;
    const int bars = 2;
    const int length = static_cast<int>(bars * 16 * sixteenth * rate);
    juce::AudioBuffer<float> audio(1, length);
    audio.clear();

    for (int i = 0; i < length; ++i)
    {
        const double t = i / rate;
        const double pad = 0.12 * (std::sin(juce::MathConstants<double>::twoPi * 220.0 * t)
                                   + std::sin(juce::MathConstants<double>::twoPi * 277.2 * t)
                                   + std::sin(juce::MathConstants<double>::twoPi * 329.6 * t));
        audio.setSample(0, i, static_cast<float>(pad));
    }

    const std::vector<int> thumps { 0, 4, 10, 16, 20, 26 }; // 4 and 20 are backbeats
    for (const int slot : thumps)
    {
        const int start = static_cast<int>(slot * sixteenth * rate);
        double phase = 0.0;
        for (int i = 0; i < static_cast<int>(0.3 * rate) && start + i < length; ++i)
        {
            const double t = i / rate;
            phase += juce::MathConstants<double>::twoPi * (55.0 + 60.0 * std::exp(-t / 0.03)) / rate;
            audio.addSample(0, start + i, static_cast<float>(0.8 * std::sin(phase) * std::exp(-t / 0.12)));
        }
    }

    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("hpdg_guide_accent_test.wav");
    file.deleteFile();
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(new juce::FileOutputStream(file), rate, 1, 24, {}, 0));
        expect(writer != nullptr, "could not create test wav");
        writer->writeFromAudioSampleBuffer(audio, 0, length);
    }

    processor.setAnalysisMode(AnalysisMode::GenerateFromSample);
    processor.setSampleApplyMode(SampleApplyMode::Blend);
    juce::String error;
    expect(processor.analyzeAudioFile(file, &error), "analysis failed: " + error);
    expect(processor.getDrumBreakAnalysis().drumLoopConfidence < 0.75f, "chord sample must not count as a drum loop");

    processor.generatePattern();
    const auto project = processor.getProjectSnapshot();
    const auto* kick = ProjectLaneAccess::findTrackState(project, TrackType::Kick);
    const auto* sub = ProjectLaneAccess::findTrackState(project, TrackType::Sub808);
    expect(kick != nullptr, "kick lane missing");
    // Not a drum loop, so the tempo is not "trusted" for the key analysis - but generation
    // still follows the tempo shown for the loaded sample (no lock, no DAW sync).
    const auto shownBpm = processor.getDrumBreakAnalysis().bpm;
    expect(shownBpm <= 20.0 || std::abs(project.params.bpm - static_cast<float>(shownBpm)) < 0.6f,
           "generation ignored the sample tempo " + juce::String(shownBpm, 1) + ": " + juce::String(project.params.bpm, 1));
    const auto debug = processor.getGenerationDebugSummary();
    expect(debug.contains("Sample guide accents"), "guide accent path was not taken");
    std::cout << "    " << debug.fromFirstOccurrenceOf("Sample guide accents", true, false).upToFirstOccurrenceOf("\n", false, false) << std::endl;

    for (const auto& note : kick->notes)
    {
        const int step = (note.gridTick / TimingGrid::Sixteenth) % 16;
        if (note.semanticRole == "sample_accent")
            expect(step != 4 && step != 12, "accent kick on backbeat step " + juce::String(step));
    }
    for (const auto& note : sub != nullptr ? sub->notes : std::vector<NoteEvent> {})
        expect(note.semanticRole != "sample_copy", "guide mode must not copy an 808 line out of the sample");

    checkBassAmountKeepsOtherLanes(processor, "guide (musical sample)");

    file.deleteFile();
}

// The genre list shows only Boom Bap and Trap. Selecting "Trap" must select the Trap choice
// (not the last parameter choice, Drill), and a saved hidden Drill genre must come back as Trap.
// The "DnB" genre entry reaches the DnB engine: genre, DnB tempo range, drum lanes written.
void testDnBGenreFromProcessor()
{
    BoomBapGeneratorAudioProcessor processor;
    auto& apvts = processor.getApvts();
    apvts.getParameter(ParamIds::genre)->setValueNotifyingHost(apvts.getParameter(ParamIds::genre)->convertTo0to1(4.0f));
    apvts.getParameter(ParamIds::dnbSubstyle)->setValueNotifyingHost(apvts.getParameter(ParamIds::dnbSubstyle)->convertTo0to1(1.0f));
    processor.generatePattern();
    const auto project = processor.getProjectSnapshot();
    expect(project.params.genre == GenreType::DnB, "genre choice 4 must be DnB");
    expect(project.params.dnbSubstyle == 1, "DnB substyle not read from the parameter");
    expect(project.params.bpm >= 164.0f && project.params.bpm <= 176.0f, "DnB generated at " + juce::String(project.params.bpm, 1) + " BPM");
    expect(project.generationDebugReport.contains("DNB ALGEBRA"), "the DnB engine did not run");
    const auto* kick = ProjectLaneAccess::findTrackState(project, TrackType::Kick);
    expect(kick != nullptr && !kick->notes.empty(), "DnB kick lane empty");
}

void testGenreComboSelectsTrapNotDrill()
{
    BoomBapGeneratorAudioProcessor processor;
    auto& apvts = processor.getApvts();
    auto* genre = apvts.getParameter(ParamIds::genre);
    expect(genre != nullptr, "genre parameter missing");

    juce::ComboBox combo;
    combo.addItem("Boom Bap", 1);
    if (kShowRapAndDrillGenres)
        combo.addItem("Rap", 2);
    combo.addItem("Trap", 3);
    if (kShowRapAndDrillGenres)
        combo.addItem("Drill", 4);

    {
        ComboBoxIdParameterAttachment attachment(*genre, combo);
        combo.setSelectedId(3, juce::sendNotificationSync);
        expect(static_cast<int>(apvts.getRawParameterValue(ParamIds::genre)->load()) == 2,
               "Trap in the list must set genre choice 2 (Trap)");
        processor.generatePattern();
        expect(processor.getProjectSnapshot().params.genre == GenreType::Trap, "engine genre is not Trap");
    }

    if (!kShowRapAndDrillGenres)
    {
        genre->setValueNotifyingHost(genre->convertTo0to1(3.0f)); // old sessions stored Drill
        juce::MemoryBlock state;
        processor.getStateInformation(state);
        BoomBapGeneratorAudioProcessor restored;
        restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        expect(static_cast<int>(restored.getApvts().getRawParameterValue(ParamIds::genre)->load()) == 2,
               "a saved hidden Drill genre must restore as Trap");
    }
}

// Guide mode + Trap with a tonal sample: A minor, one chord per bar (Am F C G) with the bass on
// the chord root. The key controls must switch to A minor and every 808 note that lands in a
// bar must take that bar's bass pitch class.
// Bass mode [2] over a musical sample with drums: the transcriber hears the sample's bass line
// (A1 / C2 / E2 per bar, plus kicks and snares that must not read as bass) and its melody, and
// the composed line takes its pitches from them: sample-bass notes on the bass's own notes,
// kick notes on what the bass plays there, melody notes from the melody's pitch classes.
void runSampleLineBass(GenreType genre)
{
    constexpr double rate = 44100.0;
    constexpr double bpm = 90.0;
    const double sixteenth = 15.0 / bpm;
    const int bars = 4;
    const int length = static_cast<int>(bars * 16 * sixteenth * rate);
    juce::AudioBuffer<float> audio(1, length);
    audio.clear();
    auto tone = [&](double startStep, double steps, int midi, double level, int harmonics)
    {
        const double hz = 440.0 * std::pow(2.0, (midi - 69) / 12.0);
        const int start = static_cast<int>(startStep * sixteenth * rate);
        const int end = std::min(length, static_cast<int>((startStep + steps) * sixteenth * rate));
        for (int i = start; i < end; ++i)
        {
            const double t = (i - start) / rate;
            const double env = std::min(1.0, t / 0.008) * std::min(1.0, (end - i) / (0.01 * rate)) * std::exp(-t / 1.5);
            double value = 0.0;
            for (int h = 1; h <= harmonics; ++h)
                value += std::sin(juce::MathConstants<double>::twoPi * hz * h * t) / h;
            audio.addSample(0, i, static_cast<float>(level * env * value));
        }
    };
    const std::array<std::pair<int, int>, 3> bassFigure { { { 0, 33 }, { 6, 36 }, { 10, 40 } } };  // A1, C2, E2
    const std::array<int, 8> melody { 64, 67, 69, 67, 64, 62, 60, 62 };                          // E4 G4 A4 G4 E4 D4 C4 D4
    juce::Random noise(7);
    for (int bar = 0; bar < bars; ++bar)
    {
        const int base = bar * 16;
        for (size_t i = 0; i < bassFigure.size(); ++i)
        {
            const int end = i + 1 < bassFigure.size() ? bassFigure[i + 1].first : 16;
            tone(base + bassFigure[i].first, end - bassFigure[i].first - 0.3, bassFigure[i].second, 0.35, 4);
        }
        for (size_t i = 0; i < melody.size(); ++i)
            tone(base + 2.0 * static_cast<double>(i), 1.8, melody[i], 0.12, 3);
        for (const int step : { 0, 10 }) // kick: a 55 Hz thump
        {
            const int start = static_cast<int>((base + step) * sixteenth * rate);
            for (int i = 0; i < static_cast<int>(0.12 * rate) && start + i < length; ++i)
            {
                const double t = i / rate;
                audio.addSample(0, start + i, static_cast<float>(0.6 * std::sin(juce::MathConstants<double>::twoPi * (55.0 + 90.0 * std::exp(-t / 0.02)) * t) * std::exp(-t / 0.05)));
            }
        }
        for (const int step : { 4, 12 }) // snare: noise burst
        {
            const int start = static_cast<int>((base + step) * sixteenth * rate);
            for (int i = 0; i < static_cast<int>(0.1 * rate) && start + i < length; ++i)
                audio.addSample(0, start + i, static_cast<float>(0.35 * (noise.nextFloat() * 2.0f - 1.0f) * std::exp(-i / rate / 0.03)));
        }
    }

    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("hpdg_sample_line_bass.wav");
    file.deleteFile();
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(new juce::FileOutputStream(file), rate, 1, 24, {}, 0));
        expect(writer != nullptr, "could not create test wav");
        writer->writeFromAudioSampleBuffer(audio, 0, length);
    }

    BoomBapGeneratorAudioProcessor processor;
    processor.prepareToPlay(44100.0, 512);
    auto& apvts = processor.getApvts();
    apvts.getParameter(ParamIds::genre)->setValueNotifyingHost(apvts.getParameter(ParamIds::genre)->convertTo0to1(genre == GenreType::Trap ? 2.0f : 0.0f));
    processor.setTrackEnabled(TrackType::Sub808, true);
    auto* barsParam = apvts.getParameter(ParamIds::bars);
    barsParam->setValueNotifyingHost(barsParam->convertTo0to1(2.0f)); // "4" bars
    processor.syncBarsFromState();
    processor.generatePattern();

    processor.setAnalysisMode(AnalysisMode::GenerateFromSample);
    processor.setSampleApplyMode(SampleApplyMode::Blend);
    auto request = processor.getSampleAnalysisRequest();
    request.source = SampleAnalysisRequest::SourceType::AudioFile;
    request.audioFile = file;
    processor.setSampleAnalysisRequest(request);
    request = processor.getSampleAnalysisRequest();
    request.manualBpm = bpm;
    processor.setSampleAnalysisRequest(request);
    juce::String error;
    expect(processor.loadSampleSource(file, &error), "load failed: " + error);
    expect(processor.analyzeCurrentSampleSource(&error), "analysis failed: " + error);

    // The transcription itself: the bass figure, not the kicks; the melody's notes.
    const auto harmony = processor.getSampleHarmony();
    std::cout << "    " << harmony.lines.describe().replace("\n", "\n    ").substring(0, 600) << std::endl;
    int bassRight = 0;
    for (const auto& n : harmony.lines.bass)
        bassRight += (n.midiNote == 33 || n.midiNote == 36 || n.midiNote == 40) ? 1 : 0;
    expect(harmony.lines.bass.size() >= 8 && bassRight >= static_cast<int>(harmony.lines.bass.size()) - 1,
           "bass line not transcribed: " + juce::String(bassRight) + "/" + juce::String(static_cast<int>(harmony.lines.bass.size())));
    int melodyRight = 0;
    for (const auto& n : harmony.lines.melody)
        melodyRight += std::find(melody.begin(), melody.end(), n.midiNote) != melody.end() ? 1 : 0;
    expect(harmony.lines.melody.size() >= 16 && melodyRight >= static_cast<int>(harmony.lines.melody.size() * 0.85),
           "melody not transcribed: " + juce::String(melodyRight) + "/" + juce::String(static_cast<int>(harmony.lines.melody.size())));

    auto settings = ProjectLaneAccess::findTrackState(processor.getProjectSnapshot(), TrackType::Sub808)->sub808Settings;
    settings.bassAmount = 1; // mode [2]
    processor.setSub808LaneSettings(TrackType::Sub808, settings);

    const std::set<int> bassClasses { 9, 0, 4 };       // A C E
    const std::set<int> melodyClasses { 4, 7, 9, 2, 0 }; // E G A D C
    std::set<char> rolesSeen;
    for (int run = 0; run < 8; ++run)
    {
        if (run % 2 == 0)
            processor.generatePattern();
        else
            processor.regenerateTrack(TrackType::Sub808);
        const auto project = processor.getProjectSnapshot();
        const auto debug = processor.getGenerationDebugSummary();
        expect(debug.contains("Sample bass line [2]: bars"), "mode [2] did not compose from the sample (run " + juce::String(run) + ")");
        const auto plan = debug.fromFirstOccurrenceOf("Sample bass line [2]: bars ", false, false).upToFirstOccurrenceOf(" |", false, false);
        for (const auto c : plan)
            if (c != ' ')
                rolesSeen.insert(static_cast<char>(c));
        const auto* sub = ProjectLaneAccess::findTrackState(project, TrackType::Sub808);
        expect(sub != nullptr && sub->notes.size() >= 4, "too few bass notes");
        for (const auto& note : sub->notes)
        {
            const int pc = note.pitch % 12;
            const juce::String where = "run " + juce::String(run) + " note at step " + juce::String(note.gridTick / 240) + " (" + note.semanticRole + ")";
            expect(note.semanticRole.contains("sample_line"), where + ": not composed from the sample");
            if (note.semanticRole.contains("|bass") || note.semanticRole.contains("|kick"))
                expect(bassClasses.count(pc) > 0, where + ": pitch " + juce::String(note.pitch) + " is not the sample's bass");
            if (note.semanticRole.contains("|melody"))
                expect(melodyClasses.count(pc) > 0, where + ": pitch " + juce::String(note.pitch) + " is not from the melody");
            expect(note.pitch >= 19 && note.pitch <= 60, where + ": out of bass register " + juce::String(note.pitch));
        }
        if (run == 0)
            std::cout << "    " << debug.fromFirstOccurrenceOf("Sample bass line [2]", true, false).upToFirstOccurrenceOf("\n", false, false) << std::endl;
    }
    expect(rolesSeen.size() >= 2, "every generation used the same single role");
    file.deleteFile();
}

// Dev probe (runs only with HPDG_PROBE_SAMPLE=<file>[|startSeconds|endSeconds]): prints the
// transcribed lines and the [2] bass Boom Bap / Trap compose over a real sample.
void probeRealSampleBass()
{
    const auto spec = juce::SystemStats::getEnvironmentVariable("HPDG_PROBE_SAMPLE", {});
    if (spec.isEmpty())
        return;
    const auto parts = juce::StringArray::fromTokens(spec, "|", {});
    const juce::File file(parts[0]);
    for (const auto genre : { GenreType::BoomBap, GenreType::Trap })
    {
        BoomBapGeneratorAudioProcessor processor;
        processor.prepareToPlay(44100.0, 512);
        auto& apvts = processor.getApvts();
        apvts.getParameter(ParamIds::genre)->setValueNotifyingHost(apvts.getParameter(ParamIds::genre)->convertTo0to1(genre == GenreType::Trap ? 2.0f : 0.0f));
        processor.setTrackEnabled(TrackType::Sub808, true);
        processor.generatePattern();
        processor.setAnalysisMode(AnalysisMode::GenerateFromSample);
        processor.setSampleApplyMode(SampleApplyMode::Blend);
        auto request = processor.getSampleAnalysisRequest();
        request.source = SampleAnalysisRequest::SourceType::AudioFile;
        request.audioFile = file;
        processor.setSampleAnalysisRequest(request);
        request = processor.getSampleAnalysisRequest();
        if (parts.size() >= 3)
        {
            request.trimStartSeconds = parts[1].getDoubleValue();
            request.trimEndSeconds = parts[2].getDoubleValue();
        }
        if (parts.size() >= 4)
            request.manualBpm = parts[3].getDoubleValue();
        processor.setSampleAnalysisRequest(request);
        juce::String error;
        processor.loadSampleSource(file, &error);
        const auto started = juce::Time::getMillisecondCounterHiRes();
        processor.analyzeCurrentSampleSource(&error);
        const auto harmony = processor.getSampleHarmony();
        if (genre == GenreType::BoomBap)
            std::cout << "    analysis " << juce::String(juce::Time::getMillisecondCounterHiRes() - started, 0) << " ms | bpm "
                      << juce::String(processor.getDrumBreakAnalysis().bpm, 1) << " | key " << harmony.keyName()
                      << " | tuning " << juce::String(harmony.tuningCents, 0) << "c\n    " << harmony.lines.describe().replace("\n", "\n    ") << std::endl;

        auto settings = ProjectLaneAccess::findTrackState(processor.getProjectSnapshot(), TrackType::Sub808)->sub808Settings;
        settings.bassAmount = 1;
        processor.setSub808LaneSettings(TrackType::Sub808, settings);
        for (int run = 0; run < 3; ++run)
        {
            processor.generatePattern();
            const auto project = processor.getProjectSnapshot();
            std::cout << "    " << (genre == GenreType::Trap ? "TRAP" : "BOOMBAP") << " " << juce::String(project.params.bpm, 1) << " bpm | "
                      << processor.getGenerationDebugSummary().fromFirstOccurrenceOf("Sample bass line [2]", true, false).upToFirstOccurrenceOf("\n", false, false) << "\n      ";
            static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            if (const auto* sub = ProjectLaneAccess::findTrackState(project, TrackType::Sub808))
                for (const auto& n : sub->notes)
                    std::cout << names[n.pitch % 12] << (n.pitch / 12 - 1) << "@" << n.gridTick / 240 << "+" << n.lengthTicks / 240
                              << n.semanticRole.fromLastOccurrenceOf("|", false, false).substring(0, 1) << (n.glideToNext ? "~" : "") << " ";
            std::cout << std::endl;
        }
    }
}

// RG on the bass lane re-applies the Boom Bap style to the project; the lanes' selected sample
// names (what the rows show) must survive it, not turn into "(none)" until the next Generate.
void testLaneRegenerateKeepsSampleNames()
{
    for (const auto genre : { GenreType::BoomBap, GenreType::Trap })
    {
        BoomBapGeneratorAudioProcessor processor;
        auto& apvts = processor.getApvts();
        apvts.getParameter(ParamIds::genre)->setValueNotifyingHost(apvts.getParameter(ParamIds::genre)->convertTo0to1(genre == GenreType::Trap ? 2.0f : 0.0f));
        processor.setTrackEnabled(TrackType::Sub808, true);
        processor.generatePattern();
        std::map<TrackType, juce::String> before;
        for (const auto& track : processor.getProjectSnapshot().tracks)
            if (track.selectedSampleName.isNotEmpty())
                before[track.type] = track.selectedSampleName;
        expect(!before.empty(), "no lane sample names to start from (sample library missing?)");

        for (const auto lane : { TrackType::Sub808, TrackType::HiHat, TrackType::Kick })
        {
            processor.regenerateTrack(lane);
            for (const auto& track : processor.getProjectSnapshot().tracks)
                if (const auto it = before.find(track.type); it != before.end())
                    expect(track.selectedSampleName == it->second,
                           "RG " + juce::String(static_cast<int>(lane)) + " changed / dropped the sample name of lane "
                               + juce::String(static_cast<int>(track.type)) + ": '" + track.selectedSampleName + "'");
        }
    }
}

// The "Techno" genre entry reaches the Techno engine: tempo in the style range, every drum lane
// (Perc included) and the bass written, RG / [1][2][3] bass work, and the grid is printed.
void testTechnoGenreInPlugin()
{
    BoomBapGeneratorAudioProcessor processor;
    processor.prepareToPlay(44100.0, 512);
    auto& apvts = processor.getApvts();
    auto* genre = apvts.getParameter(ParamIds::genre);
    genre->setValueNotifyingHost(genre->convertTo0to1(5.0f));
    auto* bars = apvts.getParameter(ParamIds::bars);
    bars->setValueNotifyingHost(bars->convertTo0to1(2.0f)); // 4 bars
    processor.syncBarsFromState();

    for (int sub = 0; sub < static_cast<int>(TechnoSubstyle::Count); ++sub)
    {
        auto* substyle = apvts.getParameter(ParamIds::technoSubstyle);
        substyle->setValueNotifyingHost(substyle->convertTo0to1(static_cast<float>(sub)));
        processor.applySelectedStylePreset(false); // what the editor does on a genre / substyle change
        processor.generatePattern();
        const auto project = processor.getProjectSnapshot();
        const auto& style = getTechnoStyleProfile(sub);
        const juce::String label = juce::String("Techno ") + style.name + ": ";
        expect(project.params.genre == GenreType::Techno, label + "genre not Techno");
        expect(project.params.bpm >= style.bpmMin - 0.5f && project.params.bpm <= style.bpmMax + 0.5f,
               label + "tempo " + juce::String(project.params.bpm, 1) + " outside " + juce::String(style.bpmMin) + "-" + juce::String(style.bpmMax));
        const auto debug = processor.getGenerationDebugSummary();
        expect(debug.contains("TECHNO ALGEBRA") && debug.contains("TECHNO BASS"), label + "engine report missing: algebra " + juce::String(debug.contains("TECHNO ALGEBRA") ? 1 : 0) + " bass " + juce::String(debug.contains("TECHNO BASS") ? 1 : 0) + " | " + debug.fromFirstOccurrenceOf("TECHNO", true, false).substring(0, 300));
        for (const auto lane : { TrackType::Kick, TrackType::HiHat, TrackType::Sub808 })
        {
            const auto* state = ProjectLaneAccess::findTrackState(project, lane);
            expect(state != nullptr && !state->notes.empty(), label + "lane " + juce::String(static_cast<int>(lane)) + " empty");
        }
        if (sub == 0 || sub == 1 || sub == 5)
        {
            std::cout << "    " << style.name << " " << juce::String(project.params.bpm, 1) << " bpm | "
                      << debug.fromFirstOccurrenceOf("bars: ", false, false).upToFirstOccurrenceOf("\n", false, false) << "\n";
            for (const auto lane : { TrackType::Cymbal, TrackType::OpenHat, TrackType::HiHat, TrackType::HatFX, TrackType::Ride,
                                     TrackType::Perc, TrackType::Snare, TrackType::GhostKick, TrackType::Kick, TrackType::Sub808 })
            {
                const auto* state = ProjectLaneAccess::findTrackState(project, lane);
                juce::String row;
                for (int step = 0; step < 32; ++step)
                {
                    bool hit = false;
                    if (state != nullptr)
                        for (const auto& n : state->notes)
                            hit = hit || n.gridTick / 240 == step;
                    row << (hit ? "x" : (step % 4 == 0 ? "|" : "."));
                }
                const auto* info = TrackRegistry::find(lane);
                std::cout << "      " << (info != nullptr ? info->displayName : juce::String("?")).paddedRight(' ', 11) << row
                          << "  " << (state != nullptr ? state->selectedSampleName : juce::String()) << "\n";
            }
            std::cout << "      " << debug.fromFirstOccurrenceOf("TECHNO BASS\n", false, false).upToFirstOccurrenceOf("\n", false, false) << std::endl;
        }
    }

    // Bass RG with each amount button keeps the drums; amount [3] plays more than [1].
    auto ticksOf = [&processor](TrackType lane)
    {
        std::vector<int> ticks;
        const auto snapshot = processor.getProjectSnapshot(); // keep it alive while reading
        if (const auto* state = ProjectLaneAccess::findTrackState(snapshot, lane))
            for (const auto& n : state->notes)
                ticks.push_back(n.gridTick + n.timingOffsetTicks);
        return ticks;
    };
    const auto kicks = ticksOf(TrackType::Kick);
    const auto hats = ticksOf(TrackType::HiHat);
    std::array<size_t, 3> notesPerAmount {};
    for (const int amount : { 0, 1, 2 })
    {
        auto settings = ProjectLaneAccess::findTrackState(processor.getProjectSnapshot(), TrackType::Sub808)->sub808Settings;
        settings.bassAmount = amount;
        processor.setSub808LaneSettings(TrackType::Sub808, settings);
        size_t total = 0;
        for (int run = 0; run < 6; ++run)
        {
            processor.regenerateTrack(TrackType::Sub808);
            expect(ticksOf(TrackType::Kick) == kicks && ticksOf(TrackType::HiHat) == hats, "techno bass RG changed the drums");
            total += ticksOf(TrackType::Sub808).size();
        }
        notesPerAmount[static_cast<size_t>(amount)] = total;
    }
    expect(notesPerAmount[2] > notesPerAmount[0], "techno bass [3] is not busier than [1]: "
           + juce::String(static_cast<int>(notesPerAmount[0])) + " vs " + juce::String(static_cast<int>(notesPerAmount[2])));
}

// A loop rendered from a DAW at 80 BPM (file = exactly 4 bars at 80) whose drums are played at
// 79.4, K S K S + eighth hats. Auto: the stated tempo is the whole 80 (slots read on 79.4, the
// drift kept as timing). Typed 80: beat 1 stays on the first hit - a symmetric K S K S groove
// must not be read from beat 3 with half a bar of silence (Copy Break bug, Oct 2026).
void testBreakTempoRenderedAtWholeBpm()
{
    constexpr double rate = 44100.0;
    const double played = 79.4;
    const int length = static_cast<int>(16.0 * 60.0 / 80.0 * rate); // 12.0 s
    std::vector<float> mono(static_cast<size_t>(length), 0.0f);
    juce::Random noise(3);
    const double beat = 60.0 / played;
    auto hit = [&](double t, int kind)
    {
        const int start = static_cast<int>(t * rate);
        for (int i = 0; i < static_cast<int>(0.2 * rate) && start + i < length; ++i)
        {
            const double x = i / rate;
            double v = 0.0;
            if (kind == 0) // kick: pitched-down sine thump
                v = 0.9 * std::sin(juce::MathConstants<double>::twoPi * (50.0 + 110.0 * std::exp(-x / 0.03)) * x) * std::exp(-x / 0.09);
            else if (kind == 1) // snare: noise + body
                v = (0.55 * (noise.nextFloat() * 2.0 - 1.0) + 0.3 * std::sin(juce::MathConstants<double>::twoPi * 190.0 * x)) * std::exp(-x / 0.06);
            else // hat: short bright noise
                v = 0.25 * (noise.nextFloat() * 2.0 - 1.0) * std::exp(-x / 0.012) * (i % 2 == 0 ? 1.0 : -1.0);
            mono[static_cast<size_t>(start + i)] += static_cast<float>(v);
        }
    };
    for (int b = 0; b < 16; ++b)
    {
        const double t = b * beat;
        if (t >= 12.0)
            break;
        hit(t, b % 2 == 0 ? 0 : 1);
        hit(t, 2);
        if (t + 0.5 * beat < 12.0)
            hit(t + 0.5 * beat, 2);
    }

    DrumBreakTranscriber transcriber;
    const auto check = [&](const DrumBreakAnalysis& a, const juce::String& label)
    {
        expect(a.valid, label + ": invalid");
        expect(std::abs(a.bpm - 80.0) < 0.01, label + ": bpm " + juce::String(a.bpm, 2));
        expect(a.originSeconds > -0.03 && a.originSeconds < 0.03, label + ": origin " + juce::String(a.originSeconds, 3) + " s (beat 1 must be the first hit)");
        int snares = 0;
        for (const auto& h : a.hits)
            if (h.lane == TrackType::Snare)
            {
                ++snares;
                const int inBar = h.gridTick % TimingGrid::TicksPerBar4_4;
                expect(inBar == TimingGrid::Quarter || inBar == 3 * TimingGrid::Quarter,
                       label + ": snare read off beat 2 / 4 at tick " + juce::String(h.gridTick));
            }
        expect(snares >= 6, label + ": snares " + juce::String(snares));
    };

    DrumBreakOptions automatic;
    const auto a = transcriber.analyze(mono, rate, automatic);
    std::cout << "    auto: " << a.describe(false).upToFirstOccurrenceOf("\n", false, false) << std::endl;
    check(a, "auto");
    expect(a.gridBpm > 79.0 && a.gridBpm < 79.8, "auto: slots should be read on the played tempo, got " + juce::String(a.gridBpm, 2));

    DrumBreakOptions typed;
    typed.forcedBpm = 80.0;
    const auto t = transcriber.analyze(mono, rate, typed);
    std::cout << "    typed 80: " << t.describe(false).upToFirstOccurrenceOf("\n", false, false) << std::endl;
    check(t, "typed 80");
}

void testBoomBapSampleLineBass() { runSampleLineBass(GenreType::BoomBap); }
void testTrapSampleLineBass() { runSampleLineBass(GenreType::Trap); }

void runGuideBassFollowsSample(GenreType genre)
{
    constexpr double rate = 44100.0;
    BoomBapGeneratorAudioProcessor processor;
    processor.getApvts().getParameter(ParamIds::genre)->setValueNotifyingHost(
        processor.getApvts().getParameter(ParamIds::genre)->convertTo0to1(genre == GenreType::Trap ? 2.0f : 0.0f));
    if (genre == GenreType::BoomBap)
        processor.setTrackEnabled(TrackType::Sub808, true); // Boom Bap bass is opt-in
    const double bpm = static_cast<double>(processor.getProjectSnapshot().params.bpm);
    const double beat = 60.0 / bpm;
    const std::array<int, 4> barRoots { 9, 5, 0, 7 };                 // A F C G
    const std::array<std::array<int, 3>, 4> chords { { { 9, 0, 4 }, { 5, 9, 0 }, { 0, 4, 7 }, { 7, 11, 2 } } };
    const int length = static_cast<int>(16 * beat * rate);
    juce::AudioBuffer<float> audio(1, length);
    audio.clear();

    for (int bar = 0; bar < 4; ++bar)
    {
        const int start = static_cast<int>(bar * 4 * beat * rate);
        const int end = std::min(length, static_cast<int>((bar + 1) * 4 * beat * rate));
        const double bassHz = 440.0 * std::pow(2.0, (36 + barRoots[static_cast<size_t>(bar)] - 69) / 12.0);
        for (int i = start; i < end; ++i)
        {
            const double t = (i - start) / rate;
            double value = 0.45 * std::sin(juce::MathConstants<double>::twoPi * bassHz * t)
                + 0.2 * std::sin(juce::MathConstants<double>::twoPi * 2.0 * bassHz * t);
            for (const int pc : chords[static_cast<size_t>(bar)])
                value += 0.07 * std::sin(juce::MathConstants<double>::twoPi * 440.0 * std::pow(2.0, (60 + pc - 69) / 12.0) * t);
            audio.setSample(0, i, static_cast<float>(value * std::min(1.0, t / 0.01)));
        }
    }

    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("hpdg_trap_bass_test.wav");
    file.deleteFile();
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(new juce::FileOutputStream(file), rate, 1, 24, {}, 0));
        expect(writer != nullptr, "could not create test wav");
        writer->writeFromAudioSampleBuffer(audio, 0, length);
    }

    processor.setAnalysisMode(AnalysisMode::GenerateFromSample);
    processor.setSampleApplyMode(SampleApplyMode::Blend);
    juce::String error;
    expect(processor.analyzeAudioFile(file, &error), "analysis failed: " + error);
    const auto harmony = processor.getSampleHarmony();
    expect(harmony.valid && harmony.keyRoot == 9 && harmony.scaleMode == 0,
           "expected A minor, got " + harmony.keyName() + " conf " + juce::String(harmony.keyConfidence, 2));

    auto* barsParam = processor.getApvts().getParameter(ParamIds::bars);
    barsParam->setValueNotifyingHost(barsParam->convertTo0to1(2.0f)); // "4" bars
    processor.syncBarsFromState();
    processor.generatePattern();
    const auto project = processor.getProjectSnapshot();
    expect(project.params.genre == genre, "wrong genre");
    expect(project.params.keyRoot == 9 && project.params.scaleMode == 0, "808 key was not set to A minor");

    const auto* sub = ProjectLaneAccess::findTrackState(project, TrackType::Sub808);
    expect(sub != nullptr && !sub->notes.empty(), "no bass / 808 notes generated");
    const int ticksPerBar = TimingGrid::TicksPerBar4_4;
    int checked = 0;
    for (const auto& note : sub->notes)
    {
        const int bar = (note.gridTick / ticksPerBar) % 4;
        const int stepInBar = (note.gridTick % ticksPerBar) / TimingGrid::Sixteenth;
        const int pitchClass = note.pitch % 12;
        const bool matchesBar = pitchClass == barRoots[static_cast<size_t>(bar)];
        const bool atBarEdge = stepInBar == 0 || stepInBar == 15; // segment boundary: a neighbour bar is fine
        const bool matchesNeighbour = pitchClass == barRoots[static_cast<size_t>((bar + 3) % 4)]
            || pitchClass == barRoots[static_cast<size_t>((bar + 1) % 4)];
        expect(matchesBar || (atBarEdge && matchesNeighbour),
               "808 note in bar " + juce::String(bar + 1) + " has pitch class " + juce::String(note.pitch % 12)
                   + ", sample bass is " + juce::String(barRoots[static_cast<size_t>(bar)]));
        ++checked;
    }
    expect(checked > 0, "no 808 notes inside bars to check");
    std::cout << "    " << harmony.describe() << " | 808 notes checked " << checked << std::endl;
    file.deleteFile();
}

// DnB over a half-tempo sample runs at double time. Accent kicks taken from the sample's peaks
// must sound at the same real-time moment as the peak (not at the half-tempo tick).
void testDnBGuideAccentsLandOnSamplePeaksAtDoubleTime()
{
    constexpr double rate = 44100.0;
    constexpr double sampleBpm = 86.0;
    BoomBapGeneratorAudioProcessor processor;
    auto& apvts = processor.getApvts();
    auto* genre = apvts.getParameter(ParamIds::genre);
    const int dnbChoice = 4; // genre parameter choice index of Drum & Bass
    genre->setValueNotifyingHost(genre->convertTo0to1(static_cast<float>(dnbChoice)));
    apvts.getParameter(ParamIds::bpm)->setValueNotifyingHost(apvts.getParameter(ParamIds::bpm)->convertTo0to1(static_cast<float>(sampleBpm)));

    const double beat = 60.0 / sampleBpm;
    const int length = static_cast<int>(8 * beat * rate); // 2 sample bars = 4 DnB bars
    juce::AudioBuffer<float> audio(1, length);
    for (int i = 0; i < length; ++i)
    {
        const double t = i / rate;
        audio.setSample(0, i, static_cast<float>(0.1 * (std::sin(juce::MathConstants<double>::twoPi * 220.0 * t)
                                                        + std::sin(juce::MathConstants<double>::twoPi * 329.6 * t))));
    }
    // Low thumps on sample 16ths 0, 5, 11, 16, 21, 27 (off the DnB snares).
    std::vector<double> thumpSeconds;
    for (const int sixteenth : { 0, 5, 11, 16, 21, 27 })
    {
        const double at = sixteenth * beat / 4.0;
        thumpSeconds.push_back(at);
        const int start = static_cast<int>(at * rate);
        double phase = 0.0;
        for (int i = 0; i < static_cast<int>(0.25 * rate) && start + i < length; ++i)
        {
            const double t = i / rate;
            phase += juce::MathConstants<double>::twoPi * (55.0 + 60.0 * std::exp(-t / 0.03)) / rate;
            audio.addSample(0, start + i, static_cast<float>(0.8 * std::sin(phase) * std::exp(-t / 0.1)));
        }
    }

    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("hpdg_dnb_accent_test.wav");
    file.deleteFile();
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(new juce::FileOutputStream(file), rate, 1, 24, {}, 0));
        expect(writer != nullptr, "could not create test wav");
        writer->writeFromAudioSampleBuffer(audio, 0, length);
    }

    processor.setAnalysisMode(AnalysisMode::GenerateFromSample);
    processor.setSampleApplyMode(SampleApplyMode::Blend);
    juce::String error;
    expect(processor.analyzeAudioFile(file, &error), "analysis failed: " + error);

    for (int run = 0; run < 4; ++run)
    {
        processor.generatePattern();
        const auto project = processor.getProjectSnapshot();
        expect(project.params.genre == GenreType::DnB, "genre is not DnB");
        const double patternBpm = project.params.bpm;
        const double loopSeconds = length / rate;
        const auto* kick = ProjectLaneAccess::findTrackState(project, TrackType::Kick);
        expect(kick != nullptr, "no kick lane");
        for (const auto& note : kick->notes)
        {
            if (note.semanticRole != "sample_accent")
                continue;
            double seconds = note.gridTick / static_cast<double>(TimingGrid::PPQ) * 60.0 / patternBpm;
            seconds = std::fmod(seconds, loopSeconds);
            double nearest = 1.0e9;
            for (const double thump : thumpSeconds)
                nearest = std::min(nearest, std::abs(seconds - thump));
            if (nearest >= 60.0 / patternBpm / 8.0)
            {
                const auto analysis = processor.getDrumBreakAnalysis();
                juce::String onsets;
                for (size_t i = 0; i < analysis.onsetTimes.size(); ++i)
                    onsets << juce::String(analysis.onsetTimes[i], 3) << "(K" << juce::String(analysis.onsetLaneLevels[i][0], 2) << ") ";
                const auto debug = processor.getGenerationDebugSummary();
                expect(false, "accent kick at " + juce::String(seconds, 3) + " s is " + juce::String(nearest * 1000.0, 1)
                                  + " ms from any sample peak (pattern " + juce::String(patternBpm, 1) + " BPM)\n onsets: " + onsets
                                  + "\n " + debug.fromFirstOccurrenceOf("Sample guide accents", true, false).upToFirstOccurrenceOf("\n", false, false)
                                  + "\n sample bpm " + juce::String(analysis.bpm, 2) + " conf " + juce::String(analysis.tempoConfidence, 2)
                                  + " origin " + juce::String(analysis.originSeconds, 3));
            }
        }
    }
    file.deleteFile();
}

// Export Loop WAV and the per-lane WAV drag render the pattern offline: a file of exactly one
// pattern length with real audio in it.
// A preset stores the state plus only the path (fragment, tempo, mode) of the loaded sample.
// Loading it restores parameters and pattern, hands the sample reference to the editor, and
// re-analysing that sample leaves the restored pattern alone. A missing file is reported.
void testPresetKeepsSampleReference()
{
    constexpr double rate = 44100.0;
    const int length = static_cast<int>(4.0 * rate);
    juce::AudioBuffer<float> audio(1, length);
    audio.clear();
    for (int beat = 0; beat < 8; ++beat)
    {
        const int start = static_cast<int>(beat * 0.5 * rate);
        for (int i = 0; i < static_cast<int>(0.2 * rate) && start + i < length; ++i)
        {
            const double t = i / rate;
            audio.addSample(0, start + i, static_cast<float>(0.8 * std::sin(juce::MathConstants<double>::twoPi * 60.0 * t) * std::exp(-t / 0.08)));
        }
    }

    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("hpdg_preset_test");
    dir.deleteRecursively();
    dir.createDirectory();
    const auto sampleFile = dir.getChildFile("loop.wav");
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(new juce::FileOutputStream(sampleFile), rate, 1, 24, {}, 0));
        expect(writer != nullptr, "could not create test wav");
        writer->writeFromAudioSampleBuffer(audio, 0, length);
    }

    BoomBapGeneratorAudioProcessor processor;
    processor.prepareToPlay(48000.0, 512);
    processor.setAnalysisMode(AnalysisMode::GenerateFromSample);
    processor.setSampleApplyMode(SampleApplyMode::Blend);
    auto request = processor.getSampleAnalysisRequest();
    request.source = SampleAnalysisRequest::SourceType::AudioFile;
    request.audioFile = sampleFile;
    processor.setSampleAnalysisRequest(request);
    request = processor.getSampleAnalysisRequest();
    request.trimStartSeconds = 0.0;
    request.trimEndSeconds = 2.0;
    request.manualBpm = 120.0;
    processor.setSampleAnalysisRequest(request);
    juce::String error;
    expect(processor.loadSampleSource(sampleFile, &error), "load failed: " + error);
    expect(processor.analyzeCurrentSampleSource(&error), "analysis failed: " + error);
    processor.setPlaySampleWithPattern(true);
    processor.generatePattern();
    const auto saved = processor.getProjectSnapshot();

    const auto presetFile = dir.getChildFile("test.hpdgpreset");
    expect(processor.savePresetToFile(presetFile), "preset not written");
    const auto presetText = presetFile.loadFileAsString();
    expect(presetText.contains(sampleFile.getFullPathName()), "preset must reference the sample path");
    expect(!presetText.contains("vst3_editor_width"), "preset must not carry the window size");
    expect(presetFile.getSize() < 2 * 1024 * 1024, "preset must not embed audio");

    BoomBapGeneratorAudioProcessor other;
    other.prepareToPlay(48000.0, 512);
    auto& apvts = other.getApvts();
    apvts.getParameter(ParamIds::bpm)->setValueNotifyingHost(apvts.getParameter(ParamIds::bpm)->convertTo0to1(150.0f));
    other.generatePattern();
    expect(other.loadPresetFromFile(presetFile, &error), "preset load failed: " + error);

    const auto countNotes = [](const PatternProject& p)
    {
        size_t n = 0;
        for (const auto& track : p.tracks)
            n += track.notes.size();
        return n;
    };
    const auto loaded = other.getProjectSnapshot();
    expect(std::abs(loaded.params.bpm - saved.params.bpm) < 0.01f,
           "bpm not restored: " + juce::String(loaded.params.bpm) + " vs " + juce::String(saved.params.bpm));
    expect(countNotes(loaded) == countNotes(saved), "pattern not restored");

    const auto reference = other.takePendingSampleRestore();
    expect(reference.has_value(), "loaded preset must hand over the sample reference");
    expect(!other.takePendingSampleRestore().has_value(), "the reference is taken once");
    expect(reference->file == sampleFile, "wrong sample path");
    expect(std::abs(reference->trimEndSeconds - 2.0) < 1.0e-6 && std::abs(reference->manualBpm - 120.0) < 1.0e-6, "fragment / tempo lost");
    expect(reference->mode == AnalysisMode::GenerateFromSample && reference->playWithPattern, "mode lost");

    expect(other.restoreSampleSource(*reference, {}, &error), "restore failed: " + error);
    const auto restored = other.getSampleAnalysisRequest();
    expect(restored.audioFile == sampleFile && std::abs(restored.trimEndSeconds - 2.0) < 1.0e-6
               && std::abs(restored.manualBpm - 120.0) < 1.0e-6, "sample request not restored");
    expect(other.getSampleSource() != nullptr && other.isSampleAnalysisReady(), "sample not reloaded");
    expect(other.isPlaySampleWithPattern(), "play-with-pattern not restored");
    expect(countNotes(other.getProjectSnapshot()) == countNotes(saved), "re-analysis changed the restored pattern");

    // Missing sample: still a reference (the editor asks: load another / skip), restore fails.
    sampleFile.deleteFile();
    BoomBapGeneratorAudioProcessor third;
    expect(third.loadPresetFromFile(presetFile, &error), "preset load failed: " + error);
    const auto missing = third.takePendingSampleRestore();
    expect(missing.has_value() && !missing->file.existsAsFile(), "missing sample must still be referenced");
    expect(!third.restoreSampleSource(*missing, {}, &error), "restoring a missing file must fail");

    dir.deleteRecursively();
}

// DnB follows the sample tempo (octave up into the DnB range): 94 -> 188, 87 -> 174, 172 -> 172.
void testDnBFollowsSampleTempo()
{
    constexpr double rate = 44100.0;
    const int length = static_cast<int>(6.0 * rate);
    juce::AudioBuffer<float> audio(1, length);
    audio.clear();
    for (int i = 0; i < length; ++i)
        audio.setSample(0, i, static_cast<float>(0.2 * std::sin(juce::MathConstants<double>::twoPi * 220.0 * i / rate)));

    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("hpdg_dnb_tempo_test.wav");
    file.deleteFile();
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(new juce::FileOutputStream(file), rate, 1, 24, {}, 0));
        expect(writer != nullptr, "could not create test wav");
        writer->writeFromAudioSampleBuffer(audio, 0, length);
    }

    for (const auto& [typed, expected] : std::vector<std::pair<double, float>> { { 94.0, 188.0f }, { 87.0, 174.0f }, { 172.0, 172.0f } })
    {
        for (const auto mode : { AnalysisMode::GenerateFromSample, AnalysisMode::ExtractFromSample })
        {
            BoomBapGeneratorAudioProcessor processor;
            processor.prepareToPlay(48000.0, 512);
            auto& apvts = processor.getApvts();
            apvts.getParameter(ParamIds::genre)->setValueNotifyingHost(apvts.getParameter(ParamIds::genre)->convertTo0to1(4.0f));
            processor.generatePattern();
            processor.setAnalysisMode(mode);
            processor.setSampleApplyMode(mode == AnalysisMode::ExtractFromSample ? SampleApplyMode::ExactCopy : SampleApplyMode::Blend);
            auto request = processor.getSampleAnalysisRequest();
            request.source = SampleAnalysisRequest::SourceType::AudioFile;
            request.audioFile = file;
            processor.setSampleAnalysisRequest(request);
            request = processor.getSampleAnalysisRequest();
            request.manualBpm = typed;
            processor.setSampleAnalysisRequest(request);
            juce::String error;
            expect(processor.loadSampleSource(file, &error), "load failed: " + error);
            processor.analyzeCurrentSampleSource(&error);
            processor.generatePattern();
            const auto bpm = processor.getProjectSnapshot().params.bpm;
            std::cout << "    typed " << typed << " mode " << static_cast<int>(mode) << " -> " << bpm << std::endl;
            expect(std::abs(bpm - expected) < 0.6f,
                   "DnB ignored the sample tempo " + juce::String(typed) + ": " + juce::String(bpm, 1));
        }
    }
    file.deleteFile();
}

void testLoopAndLaneWavExport()
{
    BoomBapGeneratorAudioProcessor processor;
    processor.prepareToPlay(48000.0, 512);
    processor.generatePattern();
    const auto project = processor.getProjectSnapshot();

    auto readWav = [](const juce::File& file, juce::AudioBuffer<float>& audio, double& rate)
    {
        juce::AudioFormatManager manager;
        manager.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(manager.createReaderFor(file));
        if (reader == nullptr)
            return false;
        audio.setSize(static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
        reader->read(&audio, 0, audio.getNumSamples(), 0, true, true);
        rate = reader->sampleRate;
        return true;
    };

    const auto loopFile = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("hpdg_loop_export_test.wav");
    loopFile.deleteFile();
    expect(processor.exportLoopWavToFile(loopFile), "Export Loop WAV returned false");
    juce::AudioBuffer<float> audio;
    double rate = 0.0;
    expect(readWav(loopFile, audio, rate), "exported WAV cannot be read");
    const double expectedSeconds = project.params.bars * 4 * 60.0 / project.params.bpm;
    expect(std::abs(audio.getNumSamples() / rate - expectedSeconds) < 0.01,
           "loop length " + juce::String(audio.getNumSamples() / rate, 3) + " s, pattern " + juce::String(expectedSeconds, 3) + " s");
    expect(audio.getMagnitude(0, audio.getNumSamples()) > 0.01f, "exported loop is silent");
    loopFile.deleteFile();

    const auto kickFile = processor.createTemporaryWavFile(TrackType::Kick);
    expect(kickFile.existsAsFile(), "kick lane WAV was not created");
    expect(readWav(kickFile, audio, rate) && audio.getMagnitude(0, audio.getNumSamples()) > 0.01f, "kick lane WAV is silent");
    expect(kickFile.getFileName().contains("Kick"), "lane WAV should be named after the lane: " + kickFile.getFileName());
    kickFile.deleteFile();
}

// --- Sample playback correctness -------------------------------------------------------------
juce::File writeSineWav(const juce::File& file, double rate, double hz, double seconds, float amplitude)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    const int length = static_cast<int>(seconds * rate);
    juce::AudioBuffer<float> audio(1, length);
    for (int i = 0; i < length; ++i)
        audio.setSample(0, i, hz > 0.0 ? amplitude * static_cast<float>(std::sin(juce::MathConstants<double>::twoPi * hz * i / rate)) : amplitude);
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(new juce::FileOutputStream(file), rate, 1, 24, {}, 0));
    expect(writer != nullptr, "could not create " + file.getFileName());
    writer->writeFromAudioSampleBuffer(audio, 0, length);
    return file;
}

void testIndependentSampleAndRackVolume()
{
    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("HPDG_Volume_" + juce::Uuid().toString() + ".wav");
    writeSineWav(file, 44100.0, 220.0, 1.0, 0.4f);
    BoomBapGeneratorAudioProcessor processor;
    processor.prepareToPlay(44100.0, 1024);
    juce::String error;
    expect(processor.loadSampleSource(file, &error), "Load volume test sample: " + error);
    const auto setRack = [&processor](float gain)
    {
        auto* parameter = processor.getApvts().getParameter(ParamIds::masterVolume);
        parameter->setValueNotifyingHost(parameter->convertTo0to1(gain));
    };
    const auto samplePeak = [&processor, &setRack](float sampleGain, float rackGain)
    {
        processor.setSamplePlaybackGain(sampleGain);
        setRack(rackGain);
        processor.auditionSampleRegion(0.0, 1.0, false);
        juce::AudioBuffer<float> buffer(2, 1024);
        juce::MidiBuffer midi;
        processor.processBlock(buffer, midi);
        processor.stopSampleAudition();
        return maxAbsSample(buffer);
    };
    const float full = samplePeak(1.0f, 1.0f);
    expect(full > 0.1f, "Sample audition must have audible output.");
    expect(std::abs(samplePeak(0.5f, 1.0f) / full - 0.5f) < 0.01f, "Sample knob scales sample playback.");
    expect(samplePeak(0.0f, 1.0f) < 1.0e-6f, "Zero sample volume mutes sample audition.");
    expect(std::abs(samplePeak(1.0f, 0.0f) - full) < 0.001f, "Rack volume does not mute the separate sample bus.");
    processor.setSamplePlaybackGain(0.37f);
    setRack(0.61f);
    juce::MemoryBlock saved;
    processor.getStateInformation(saved);
    processor.setSamplePlaybackGain(1.0f);
    setRack(1.0f);
    processor.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    expect(std::abs(processor.getApvts().getRawParameterValue(ParamIds::sampleVolume)->load() - 0.37f) < 0.001f
           && std::abs(processor.getApvts().getRawParameterValue(ParamIds::masterVolume)->load() - 0.61f) < 0.001f,
           "Both volume controls survive session state restoration.");
    cleanupFile(file);
}

void testVst3VolumeControls()
{
    juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_VST3);
    BoomBapGeneratorAudioProcessor processor;
    juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Undefined);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    std::function<juce::Slider*(juce::Component&, const juce::String&)> findSlider;
    findSlider = [&findSlider](juce::Component& component, const juce::String& name) -> juce::Slider*
    {
        if (component.getName() == name)
            if (auto* slider = dynamic_cast<juce::Slider*>(&component))
                return slider;
        for (auto* child : component.getChildren())
            if (auto* found = findSlider(*child, name))
                return found;
        return nullptr;
    };
    auto* sample = findSlider(*editor, "Sample Volume");
    auto* rack = findSlider(*editor, "Drums and Bass Volume");
    expect(sample != nullptr && rack != nullptr, "VST3 editor exposes both volume knobs.");
    sample->setValue(0.55, juce::sendNotificationSync);
    rack->setValue(0.65, juce::sendNotificationSync);
    expect(std::abs(processor.getApvts().getRawParameterValue(ParamIds::sampleVolume)->load() - 0.55f) < 0.001f
           && std::abs(processor.getApvts().getRawParameterValue(ParamIds::masterVolume)->load() - 0.65f) < 0.001f,
           "VST3 volume knobs are attached to their independent host parameters.");
    for (const int width : { 820, 1100, 1460 })
    {
        editor->setSize(width, 800);
        expect(!sample->getBounds().isEmpty() && !rack->getBounds().isEmpty(), "Volume knobs retain usable bounds.");
    }
    const auto snapshotPath = juce::SystemStats::getEnvironmentVariable("HPDG_VOLUME_UI_SNAPSHOT", {});
    if (snapshotPath.isNotEmpty())
    {
        const auto snapshot = editor->createComponentSnapshot(editor->getLocalBounds());
        juce::FileOutputStream output { juce::File(snapshotPath) };
        expect(output.openedOk() && juce::PNGImageFormat().writeImageToStream(snapshot, output), "Write VST3 UI snapshot.");
    }
}

void loadBankFrom(LaneSampleBank& bank, const juce::File& root)
{
    SampleLibraryManager library;
    library.setRootDirectory(root);
    library.setGenre(GenreType::BoomBap);
    library.scan();
    bank.applyLibrary(library);
}

void testSampleRootNoteFromName()
{
    expect(LaneSampleBank::rootPitchClassFromName("BoomBap Bass - Puma - C.wav") == 0, "C");
    expect(LaneSampleBank::rootPitchClassFromName("Kit 808 - F#.wav") == 6, "F#");
    expect(LaneSampleBank::rootPitchClassFromName("Kit 808 - Bb1.wav") == 10, "Bb1");
    expect(LaneSampleBank::rootPitchClassFromName("Kit 808 - G2.wav") == 7, "G2");
    expect(LaneSampleBank::rootPitchClassFromName("Kit 808 - B.wav") == 11, "B");
    expect(LaneSampleBank::rootPitchClassFromName("Kit 808 - Bass.wav") == 0, "word that starts with a note letter");
    expect(LaneSampleBank::rootPitchClassFromName("Deep Sub.wav") == 0, "no note");
    expect(LaneSampleBank::rootPitchClassFromNameOrNone("Deep Sub.wav") == -1, "no note: -1");
}

// The note a bass one-shot is tuned to: name and sound together (SAMPLE_ANALYSIS_STAGE step 9).
void testSampleRootFromNameAndSound()
{
    auto sound = [](double midi, float confidence)
    {
        SampleRoot root;
        root.valid = true;
        root.midi = midi;
        root.confidence = confidence;
        return root;
    };
    // name C, sound C1 40 cents flat: the name's note, tuned by the sound
    auto r = LaneSampleBank::resolveRoot(0, sound(24.0 - 0.40, 0.9f));
    expect(r.pitchClass == 0 && std::abs(r.cents + 40.0) < 0.5 && r.midi == 24, "detuned C1: C, -40 cents");
    // name C, sound clearly C#2 (a mislabelled sample): the sound wins
    r = LaneSampleBank::resolveRoot(0, sound(37.0, 1.0f));
    expect(r.pitchClass == 1 && r.midi == 37, "mislabelled C#2 must read C#");
    // name E, sound unclear and far: the name stays
    r = LaneSampleBank::resolveRoot(4, sound(29.0, 0.3f));
    expect(r.pitchClass == 4 && r.cents == 0.0, "unclear sound keeps the name");
    // no note in the name: the sound
    r = LaneSampleBank::resolveRoot(-1, sound(29.1, 0.8f));
    expect(r.pitchClass == 5 && std::abs(r.cents - 10.0) < 0.5, "unnamed F1");

    // Bank: the roots are read from the samples' sound when the bank loads.
    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("hpdg_bank_root_test");
    root.deleteRecursively();
    const double cSharp2 = 440.0 * std::pow(2.0, (37 - 69) / 12.0);
    writeSineWav(root.getChildFile("BoomBap").getChildFile("Sub808").getChildFile("Bass A - C.wav"), 44100.0, cSharp2, 1.0, 0.5f);
    LaneSampleBank bank;
    loadBankFrom(bank, root);
    expect(bank.getSelectedRootPitchClass(TrackType::Sub808) == 1, "a C#2 sample named C must play as C#: "
           + juce::String(bank.getSelectedRootPitchClass(TrackType::Sub808)));
    expect(bank.getSelectedRootMidi(TrackType::Sub808) == 37, "octave from the sound: " + juce::String(bank.getSelectedRootMidi(TrackType::Sub808)));
    root.deleteRecursively();
}

// A 44.1 kHz sample played on a 48 kHz device: the bank keeps the file as it is (no slow
// resampling at load) and the player reads it at fileRate / deviceRate, so it lasts its real
// 0.5 s and keeps its 110 Hz pitch.
void testBankResamplingKeepsPitchAndLength()
{
    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("hpdg_bank_rate_test");
    root.deleteRecursively();
    writeSineWav(root.getChildFile("BoomBap").getChildFile("Sub808").getChildFile("Test Bass - A.wav"), 44100.0, 110.0, 0.5, 0.5f);
    LaneSampleBank bank;
    loadBankFrom(bank, root);
    const auto buffer = bank.getSelectedBufferShared(TrackType::Sub808);
    expect(buffer != nullptr, "bass sample not loaded");
    expect(buffer->getNumSamples() == 22050, "the file must be kept at its own rate: " + juce::String(buffer->getNumSamples()));
    expect(std::abs(bank.getSelectedSampleRate(TrackType::Sub808) - 44100.0) < 1.0, "file rate not recorded");
    expect(bank.getSelectedRootPitchClass(TrackType::Sub808) == 9, "root A not read from the name");

    PreviewEngine engine;
    engine.prepare(48000.0);
    PreviewEngine::TriggerOptions options;
    engine.noteOnAtSample(TrackType::Sub808, 1.0f, 0, bank, options);
    juce::AudioBuffer<float> out(1, 48000);
    out.clear();
    engine.render(out, 0, out.getNumSamples());
    int lastSound = 0;
    int crossings = 0;
    for (int i = 0; i < out.getNumSamples(); ++i)
    {
        if (std::abs(out.getSample(0, i)) > 1.0e-4f)
            lastSound = i;
        if (i > 0 && i < 19200 && (out.getSample(0, i - 1) < 0.0f) != (out.getSample(0, i) < 0.0f))
            ++crossings;
    }
    expect(std::abs(lastSound - 24000) < 200, "0.5 s sample played for " + juce::String(lastSound) + " samples at 48 kHz");
    // 0.4 s of 110 Hz: ~88 zero crossings.
    expect(std::abs(crossings - 88) <= 2, "pitch changed: crossings " + juce::String(crossings));
    root.deleteRecursively();
}

// Drums: a new hit on the same lane cuts the one still ringing (FL "cut itself"), with a short
// fade. Bass: sounds exactly for the note length, then a short release.
void testDrumChokeAndBassNoteLength()
{
    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("hpdg_choke_test");
    root.deleteRecursively();
    writeSineWav(root.getChildFile("BoomBap").getChildFile("Kick").getChildFile("Long Kick.wav"), 48000.0, 0.0, 1.0, 0.4f);
    writeSineWav(root.getChildFile("BoomBap").getChildFile("Sub808").getChildFile("Long Bass - C.wav"), 48000.0, 0.0, 2.0, 0.4f);
    LaneSampleBank bank;
    loadBankFrom(bank, root);

    PreviewEngine engine;
    engine.prepare(48000.0);
    engine.noteOnAtSample(TrackType::Kick, 1.0f, 0, bank, {});
    engine.noteOnAtSample(TrackType::Kick, 1.0f, 4800, bank, {}); // second hit at 100 ms
    juce::AudioBuffer<float> out(1, 9600);
    out.clear();
    engine.render(out, 0, out.getNumSamples());
    const float single = out.getSample(0, 2400);  // only the first hit sounding
    const float after = out.getSample(0, 7200);   // 50 ms after the second hit
    expect(std::abs(after - single) < 1.0e-3f,
           "second kick must choke the first (single " + juce::String(single, 4) + ", after " + juce::String(after, 4) + ")");

    PreviewEngine bass;
    bass.prepare(48000.0);
    PreviewEngine::TriggerOptions options;
    options.maxDurationSamples = 12000; // a 250 ms note
    bass.noteOnAtSample(TrackType::Sub808, 1.0f, 0, bank, options);
    juce::AudioBuffer<float> bassOut(1, 48000);
    bassOut.clear();
    bass.render(bassOut, 0, bassOut.getNumSamples());
    int lastSound = 0;
    for (int i = 0; i < bassOut.getNumSamples(); ++i)
        if (std::abs(bassOut.getSample(0, i)) > 1.0e-4f)
            lastSound = i;
    expect(lastSound >= 12000 && lastSound <= 12000 + 400,
           "2 s bass sample must stop with its 250 ms note, sounded " + juce::String(lastSound) + " samples");
    root.deleteRecursively();
}

void testTrapGuide808FollowsSampleBassAndKey() { runGuideBassFollowsSample(GenreType::Trap); }
void testBoomBapGuideBassFollowsSampleBassAndKey() { runGuideBassFollowsSample(GenreType::BoomBap); }

int runTest(const char* name, const std::function<void()>& test)
{
    try
    {
        test();
        std::cout << "[PASS] " << name << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << name << ": " << error.what() << std::endl;
        return 1;
    }
}

// Generation engine regressions -------------------------------------------------------------

// Grid step a note belongs to: an early push of up to a 1/64 still counts as its own step.
int perceivedStep(const NoteEvent& note)
{
    return (note.gridTick + TimingGrid::Sixteenth / 4) / TimingGrid::Sixteenth;
}

PatternProject makeEngineProject(GenreType genre, int bars, float bpm, int seed)
{
    auto project = createDefaultProject();
    project.params.genre = genre;
    project.params.bars = bars;
    project.params.bpm = bpm;
    project.params.seed = seed;
    return project;
}

bool sameNotes(const std::vector<NoteEvent>& a, const std::vector<NoteEvent>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (a[i].gridTick != b[i].gridTick || a[i].timingOffsetTicks != b[i].timingOffsetTicks
            || a[i].velocity != b[i].velocity || a[i].pitch != b[i].pitch)
            return false;
    }
    return true;
}

// Per-lane actions must never touch a locked lane, including ones that are musically coupled
// to the lane being regenerated.
void testBoomBapLaneActionsKeepLockedLanes()
{
    BoomBapEngine engine;
    auto project = makeEngineProject(GenreType::BoomBap, 4, 90.0f, 4242);
    engine.generate(project);

    auto* snare = ProjectLaneAccess::findTrackState(project, TrackType::Snare);
    auto* openHat = ProjectLaneAccess::findTrackState(project, TrackType::OpenHat);
    auto* ghostKick = ProjectLaneAccess::findTrackState(project, TrackType::GhostKick);
    auto* hatFx = ProjectLaneAccess::findTrackState(project, TrackType::HatFX);
    expect(snare != nullptr && openHat != nullptr && ghostKick != nullptr && hatFx != nullptr, "BoomBap lanes missing");

    // A user edit: drop the bar-1 beat-4 snare, hand-place an open hat, then lock them.
    snare->notes.erase(std::remove_if(snare->notes.begin(), snare->notes.end(),
                                      [](const NoteEvent& n) { return perceivedStep(n) == 12; }),
                       snare->notes.end());
    NoteEvent edit;
    edit.pitch = 46;
    edit.gridTick = 3 * TimingGrid::Sixteenth;
    edit.velocity = 99;
    openHat->notes.push_back(edit);
    for (auto* lane : { snare, openHat, ghostKick, hatFx })
        lane->locked = true;

    const auto lockedSnare = snare->notes;
    const auto lockedOpenHat = openHat->notes;
    const auto lockedGhostKick = ghostKick->notes;
    const auto lockedHatFx = hatFx->notes;

    for (int round = 0; round < 3; ++round)
    {
        for (const auto type : { TrackType::Kick, TrackType::HiHat, TrackType::Cymbal, TrackType::Snare })
        {
            engine.generateTrackNew(project, type);
            engine.regenerateTrackVariation(project, type);
            engine.mutateTrack(project, type);
        }
        engine.mutatePattern(project);
        ++project.generationCounter;
    }

    snare = ProjectLaneAccess::findTrackState(project, TrackType::Snare);
    openHat = ProjectLaneAccess::findTrackState(project, TrackType::OpenHat);
    ghostKick = ProjectLaneAccess::findTrackState(project, TrackType::GhostKick);
    hatFx = ProjectLaneAccess::findTrackState(project, TrackType::HatFX);
    expect(sameNotes(snare->notes, lockedSnare), "locked Snare changed by a per-lane action");
    expect(sameNotes(openHat->notes, lockedOpenHat), "locked OpenHat changed by a per-lane action");
    expect(sameNotes(ghostKick->notes, lockedGhostKick), "locked GhostKick changed by a Kick action");
    expect(sameNotes(hatFx->notes, lockedHatFx), "locked HatFX changed by a HiHat action");
}

void testTrapLaneActionsKeepLockedCoupledLanes()
{
    TrapEngine engine;
    auto project = makeEngineProject(GenreType::Trap, 4, 140.0f, 777);
    engine.generate(project);

    auto* sub = ProjectLaneAccess::findTrackState(project, TrackType::Sub808);
    auto* hatFx = ProjectLaneAccess::findTrackState(project, TrackType::HatFX);
    expect(sub != nullptr && hatFx != nullptr, "Trap lanes missing");
    // A user-placed 808 on the normal-time snare (step 8); validation used to shove it off.
    NoteEvent onSnare;
    onSnare.pitch = 36;
    onSnare.gridTick = 8 * TimingGrid::Sixteenth;
    onSnare.velocity = 100;
    sub->notes.push_back(onSnare);
    std::sort(sub->notes.begin(), sub->notes.end(), [](const NoteEvent& a, const NoteEvent& b) { return a.gridTick < b.gridTick; });
    sub->sub808Notes = toSub808NoteEvents(sub->notes); // the 808 editor's own storage
    sub->locked = true;
    hatFx->locked = true;
    const auto lockedSub = sub->notes;
    const auto lockedHatFx = hatFx->notes;

    for (int round = 0; round < 3; ++round)
    {
        engine.generateTrackNew(project, TrackType::Kick);
        engine.mutateTrack(project, TrackType::HiHat);
        ++project.generationCounter;
    }

    sub = ProjectLaneAccess::findTrackState(project, TrackType::Sub808);
    hatFx = ProjectLaneAccess::findTrackState(project, TrackType::HatFX);
    expect(sameNotes(sub->notes, lockedSub), "locked Sub808 changed by a Kick action");
    expect(sameNotes(hatFx->notes, lockedHatFx), "locked HatFX changed by a HiHat action");
}

// Host 70 BPM puts Trap in double-time: snares land on steps 4 and 12, so no main kick may
// share a step with them.
void testTrapDoubleTimeKicksAvoidSnares()
{
    TrapEngine engine;
    for (int seed = 1; seed <= 40; ++seed)
    {
        auto project = makeEngineProject(GenreType::Trap, 4, 70.0f, seed);
        project.params.trapSubstyle = seed % 6;
        engine.generate(project);
        const auto* kick = ProjectLaneAccess::findTrackState(project, TrackType::Kick);
        const auto* snare = ProjectLaneAccess::findTrackState(project, TrackType::Snare);
        expect(kick != nullptr && snare != nullptr, "Trap lanes missing");

        bool doubleTimeSnare = false;
        for (const auto& s : snare->notes)
            doubleTimeSnare = doubleTimeSnare || perceivedStep(s) % 16 == 4;
        expect(doubleTimeSnare, "70 BPM Trap should place double-time snares on step 4");

        for (const auto& k : kick->notes)
            for (const auto& s : snare->notes)
                expect(perceivedStep(k) != perceivedStep(s),
                       "seed " + juce::String(seed) + ": kick on snare step " + juce::String(perceivedStep(k)));
    }
}

void testBoomBapKicksNeverLandOnBackbeat()
{
    BoomBapEngine engine;
    for (int seed = 1; seed <= 60; ++seed)
    {
        auto project = makeEngineProject(GenreType::BoomBap, seed % 2 == 0 ? 8 : 4, 90.0f, seed);
        project.params.boombapSubstyle = seed % 6;
        engine.generate(project);
        for (int i = 0; i < 4; ++i)
            engine.mutateTrack(project, TrackType::Kick);

        const auto* kick = ProjectLaneAccess::findTrackState(project, TrackType::Kick);
        for (const auto& k : kick->notes)
        {
            const int step = perceivedStep(k) % 16;
            expect(step != 4 && step != 12, "seed " + juce::String(seed) + ": BoomBap kick on backbeat step " + juce::String(step));
        }
    }
}

// Derived bars used to copy bar 1's exact micro-timing and velocity; they must now breathe.
void testBoomBapDerivedBarsAreNotCarbonCopies()
{
    BoomBapEngine engine;
    int identical = 0;
    int compared = 0;
    for (int seed = 1; seed <= 12; ++seed)
    {
        auto project = makeEngineProject(GenreType::BoomBap, 4, 90.0f, seed);
        engine.generate(project);
        const auto* hat = ProjectLaneAccess::findTrackState(project, TrackType::HiHat);
        for (const auto& a : hat->notes)
        {
            if (perceivedStep(a) / 16 != 0)
                continue;
            for (const auto& b : hat->notes)
            {
                if (perceivedStep(b) != perceivedStep(a) + 32)
                    continue;
                ++compared;
                identical += (b.gridTick - 2 * TimingGrid::TicksPerBar4_4 == a.gridTick && b.velocity == a.velocity) ? 1 : 0;
            }
        }
    }
    expect(compared > 0, "no bar-1 / bar-3 hat pairs to compare");
    expect(identical * 2 < compared,
           "bar 3 hats repeat bar 1 exactly: " + juce::String(identical) + " of " + juce::String(compared));
}
} // namespace
} // namespace bbg

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    using namespace bbg;

    int failures = 0;
    failures += runTest("Independent sample and rack volume", testIndependentSampleAndRackVolume);
    failures += runTest("VST3 volume controls", testVst3VolumeControls);
    failures += runTest("Lane-aware export path", testLaneAwareExportTrackPath);
    failures += runTest("Lane-aware temporary MIDI path", testLaneAwareTemporaryMidiPath);
    failures += runTest("MIDI export keeps negative first Kick", testMidiExportKeepsFirstKickWithNegativeMicrotiming);
    failures += runTest("Lane-aware sample command path", testLaneAwareSampleCommandPath);
    failures += runTest("Generate Pattern rotates lane samples", testGeneratePatternRotatesLaneSamples);
    failures += runTest("Preview processBlock audio smoke", testPreviewProcessBlockProducesAudio);
    failures += runTest("Preview processBlock neutral EQ smoke", testPreviewProcessBlockWithNeutralEqProducesAudio);
    failures += runTest("Preview keeps negative first Kick", testPreviewProcessBlockKeepsFirstKickWithNegativeMicrotiming);
    failures += runTest("Drum break exact copy path", testDrumBreakExactCopyPath);
    failures += runTest("Guide mode accent kicks avoid backbeat", testGuideModeAccentKicksAvoidBackbeat);
    failures += runTest("Genre combo selects Trap, not Drill", testGenreComboSelectsTrapNotDrill);
    failures += runTest("DnB genre from the processor", testDnBGenreFromProcessor);
    failures += runTest("Trap guide 808 follows sample bass and key", testTrapGuide808FollowsSampleBassAndKey);
    failures += runTest("BoomBap guide bass follows sample bass and key", testBoomBapGuideBassFollowsSampleBassAndKey);
    failures += runTest("Sample root note from name", testSampleRootNoteFromName);
    failures += runTest("Sample root from name and sound", testSampleRootFromNameAndSound);
    failures += runTest("Bank resampling keeps pitch and length", testBankResamplingKeepsPitchAndLength);
    failures += runTest("Drum choke and bass note length", testDrumChokeAndBassNoteLength);
    failures += runTest("Loop and lane WAV export", testLoopAndLaneWavExport);
    failures += runTest("DnB guide accents land on sample peaks at double time", testDnBGuideAccentsLandOnSamplePeaksAtDoubleTime);
    failures += runTest("BoomBap lane actions keep locked lanes", testBoomBapLaneActionsKeepLockedLanes);
    failures += runTest("Trap lane actions keep locked coupled lanes", testTrapLaneActionsKeepLockedCoupledLanes);
    failures += runTest("Trap double-time kicks avoid snares", testTrapDoubleTimeKicksAvoidSnares);
    failures += runTest("BoomBap kicks never land on the backbeat", testBoomBapKicksNeverLandOnBackbeat);
    failures += runTest("BoomBap derived bars are not carbon copies", testBoomBapDerivedBarsAreNotCarbonCopies);
    failures += runTest("Preset keeps the sample reference", testPresetKeepsSampleReference);
    failures += runTest("DnB follows the sample tempo", testDnBFollowsSampleTempo);
    failures += runTest("Lane RG keeps the lanes' sample names", testLaneRegenerateKeepsSampleNames);
    failures += runTest("Techno genre in the plugin", testTechnoGenreInPlugin);
    failures += runTest("Break rendered at a whole BPM (auto / typed)", testBreakTempoRenderedAtWholeBpm);
    failures += runTest("Boom Bap bass [2] composed from the sample", testBoomBapSampleLineBass);
    failures += runTest("Trap 808 [2] composed from the sample", testTrapSampleLineBass);
    if (juce::SystemStats::getEnvironmentVariable("HPDG_PROBE_SAMPLE", {}).isNotEmpty())
        failures += runTest("Probe real sample bass", probeRealSampleBass);

    if (failures == 0)
    {
        std::cout << "All lane boundary tests passed." << std::endl;
        return 0;
    }

    std::cerr << failures << " lane boundary test(s) failed." << std::endl;
    return 1;
}
