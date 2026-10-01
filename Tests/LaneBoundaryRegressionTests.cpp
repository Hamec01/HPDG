#include <algorithm>
#include <array>
#include <functional>
#include <iostream>
#include <stdexcept>

#include <juce_events/juce_events.h>

#include "../Source/Core/ProjectLaneAccess.h"
#include "../Source/Core/RuntimeLaneLifecycle.h"
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

    file.deleteFile();
}

// The genre list shows only Boom Bap and Trap. Selecting "Trap" must select the Trap choice
// (not the last parameter choice, Drill), and a saved hidden Drill genre must come back as Trap.
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
}

// A 44.1 kHz sample must keep its length and pitch after the bank brings it to 48 kHz, and the
// player must compensate for the device rate (here 44.1 kHz) so it plays at the original speed.
void testBankResamplingKeepsPitchAndLength()
{
    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("hpdg_bank_rate_test");
    root.deleteRecursively();
    writeSineWav(root.getChildFile("BoomBap").getChildFile("Sub808").getChildFile("Test Bass - A.wav"), 44100.0, 110.0, 0.5, 0.5f);
    LaneSampleBank bank;
    loadBankFrom(bank, root);
    const auto buffer = bank.getSelectedBufferShared(TrackType::Sub808);
    expect(buffer != nullptr, "bass sample not loaded");
    expect(std::abs(buffer->getNumSamples() - 24000) <= 2, "resampled length " + juce::String(buffer->getNumSamples()));
    expect(bank.getSelectedRootPitchClass(TrackType::Sub808) == 9, "root A not read from the name");

    // Zero crossings over the first 0.4 s at 48 kHz: 110 Hz -> ~88.
    int crossings = 0;
    for (int i = 1; i < 19200; ++i)
        if ((buffer->getSample(0, i - 1) < 0.0f) != (buffer->getSample(0, i) < 0.0f))
            ++crossings;
    expect(std::abs(crossings - 88) <= 2, "pitch changed by resampling: crossings " + juce::String(crossings));

    PreviewEngine engine;
    engine.prepare(44100.0);
    PreviewEngine::TriggerOptions options;
    engine.noteOnAtSample(TrackType::Sub808, 1.0f, 0, bank, options);
    juce::AudioBuffer<float> out(1, 44100);
    out.clear();
    engine.render(out, 0, out.getNumSamples());
    int lastSound = 0;
    for (int i = 0; i < out.getNumSamples(); ++i)
        if (std::abs(out.getSample(0, i)) > 1.0e-4f)
            lastSound = i;
    expect(std::abs(lastSound - 22050) < 200, "0.5 s sample played for " + juce::String(lastSound) + " samples at 44.1 kHz");
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
    failures += runTest("Trap guide 808 follows sample bass and key", testTrapGuide808FollowsSampleBassAndKey);
    failures += runTest("BoomBap guide bass follows sample bass and key", testBoomBapGuideBassFollowsSampleBassAndKey);
    failures += runTest("Sample root note from name", testSampleRootNoteFromName);
    failures += runTest("Bank resampling keeps pitch and length", testBankResamplingKeepsPitchAndLength);
    failures += runTest("Drum choke and bass note length", testDrumChokeAndBassNoteLength);
    failures += runTest("BoomBap lane actions keep locked lanes", testBoomBapLaneActionsKeepLockedLanes);
    failures += runTest("Trap lane actions keep locked coupled lanes", testTrapLaneActionsKeepLockedCoupledLanes);
    failures += runTest("Trap double-time kicks avoid snares", testTrapDoubleTimeKicksAvoidSnares);
    failures += runTest("BoomBap kicks never land on the backbeat", testBoomBapKicksNeverLandOnBackbeat);
    failures += runTest("BoomBap derived bars are not carbon copies", testBoomBapDerivedBarsAreNotCarbonCopies);

    if (failures == 0)
    {
        std::cout << "All lane boundary tests passed." << std::endl;
        return 0;
    }

    std::cerr << failures << " lane boundary test(s) failed." << std::endl;
    return 1;
}
