#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include "../Source/Core/PatternProject.h"
#include "../Source/Core/PatternProjectSerialization.h"
#include "../Source/Core/ProjectStateController.h"
#include "../Source/Analysis/SampleApplyWeights.h"
#include "../Source/Engine/BoomBapEngine.h"
#include "../Source/Engine/BoomBap/BoomBapClassicAlgebraGenerator.h"
#include "../Source/Engine/DrillEngine.h"
#include "../Source/Engine/Drill/DrillPatternValidator.h"
#include "../Source/Engine/Drill/DrillPhrasePlanner.h"
#include "../Source/Engine/Drill/DrillSnareGenerator.h"
#include "../Source/Engine/ExtractPatternBuilder.h"
#include "../Source/Engine/HiResTiming.h"
#include "../Source/Engine/LaneSampleBank.h"
#include "../Source/Engine/MidiExportEngine.h"
#include "../Source/Engine/PatternPerformanceTransformEngine.h"
#include "../Source/Engine/PatternBlendEngine.h"
#include "../Source/Engine/RapEngine.h"
#include "../Source/Engine/SampleLibraryManager.h"
#include "../Source/Engine/StyleDefaults.h"
#include "../Source/Engine/StyleDefinitionLoader.h"
#include "../Source/Engine/StyleInfluence.h"
#include "../Source/Engine/SubstyleRuleEnforcer.h"

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

juce::ValueTree wrapSerializedProject(const PatternProject& project)
{
    juce::ValueTree root("ROOT");
    root.addChild(PatternProjectSerialization::serialize(project), -1, nullptr);
    return root;
}

TrackState* findTrackByType(PatternProject& project, TrackType type)
{
    for (auto& track : project.tracks)
    {
        if (track.type == type)
            return &track;
    }

    return nullptr;
}

bool hasNoteAt(const TrackState& track, int step, int microOffset, const juce::String& semanticRole)
{
    return std::any_of(track.notes.begin(), track.notes.end(), [&](const NoteEvent& note)
    {
        return note.step == step && note.microOffset == microOffset && note.semanticRole == semanticRole;
    });
}

bool hasNoteAtStepAndMicro(const TrackState& track, int step, int microOffset)
{
    return std::any_of(track.notes.begin(), track.notes.end(), [&](const NoteEvent& note)
    {
        return note.step == step && note.microOffset == microOffset;
    });
}

bool hasHatCarrierCoverageNearTick(const TrackState& track, int tick, int toleranceTicks)
{
    return std::any_of(track.notes.begin(), track.notes.end(), [&](const NoteEvent& note)
    {
        if (note.semanticRole != "drill_hat_backbone" && note.semanticRole != "drill_hat_reference_copy")
            return false;
        return std::abs(HiResTiming::noteTick(note) - tick) <= toleranceTicks;
    });
}

bool isProtectedDrillHatSemantic(const juce::String& semanticRole)
{
    return semanticRole == "drill_hat_backbone" || semanticRole == "drill_hat_reference_copy";
}

bool hasSubStartAt(const TrackState& track, int step, const juce::String& semanticRole = {})
{
    return std::any_of(track.sub808Notes.begin(), track.sub808Notes.end(), [&](const Sub808NoteEvent& note)
    {
        return note.step == step && (semanticRole.isEmpty() || note.semanticRole == semanticRole);
    });
}

bool matchesSnareAnchorStep(const std::array<int, 2>& anchors, int stepInBar)
{
    return std::any_of(anchors.begin(), anchors.end(), [stepInBar](int anchor)
    {
        return anchor >= 0 && anchor == stepInBar;
    });
}

int activeSnareAnchorCount(const std::array<int, 2>& anchors)
{
    return static_cast<int>(std::count_if(anchors.begin(), anchors.end(), [](int anchor)
    {
        return anchor >= 0;
    }));
}

bool noteSequencesEqual(const std::vector<NoteEvent>& lhs, const std::vector<NoteEvent>& rhs)
{
    if (lhs.size() != rhs.size())
        return false;

    for (size_t index = 0; index < lhs.size(); ++index)
    {
        const auto& a = lhs[index];
        const auto& b = rhs[index];
        if (a.pitch != b.pitch
            || a.step != b.step
            || a.length != b.length
            || a.velocity != b.velocity
            || a.microOffset != b.microOffset
            || a.isGhost != b.isGhost
            || a.semanticRole != b.semanticRole
            || a.isSlide != b.isSlide
            || a.isLegato != b.isLegato
            || a.glideToNext != b.glideToNext)
        {
            return false;
        }
    }

    return true;
}

bool sub808NoteSequencesEqual(const std::vector<Sub808NoteEvent>& lhs, const std::vector<Sub808NoteEvent>& rhs)
{
    if (lhs.size() != rhs.size())
        return false;

    for (size_t index = 0; index < lhs.size(); ++index)
    {
        const auto& a = lhs[index];
        const auto& b = rhs[index];
        if (a.pitch != b.pitch
            || a.step != b.step
            || a.length != b.length
            || a.velocity != b.velocity
            || a.microOffset != b.microOffset
            || a.semanticRole != b.semanticRole
            || a.isSlide != b.isSlide
            || a.isLegato != b.isLegato
            || a.glideToNext != b.glideToNext)
        {
            return false;
        }
    }

    return true;
}

bool sub808SequenceIsValidMonophonic(const std::vector<Sub808NoteEvent>& notes)
{
    for (size_t index = 0; index + 1 < notes.size(); ++index)
    {
        const auto& current = notes[index];
        const auto& next = notes[index + 1];
        const int allowedOverlap = (current.glideToNext || current.isLegato) ? 1 : 0;
        if (current.step + current.length > next.step + allowedOverlap)
            return false;
        if (next.isSlide != current.glideToNext)
            return false;
    }

    return true;
}

bool noteMatchesBaseIdentity(const NoteEvent& note, const NoteEvent& baseNote)
{
    return note.pitch == baseNote.pitch
        && note.step == baseNote.step
        && note.length == baseNote.length
        && note.isGhost == baseNote.isGhost
        && note.semanticRole == baseNote.semanticRole
        && note.isSlide == baseNote.isSlide
        && note.isLegato == baseNote.isLegato
        && note.glideToNext == baseNote.glideToNext;
}

bool allVisibleNotesDerivedFromBase(const TrackState& track, const std::vector<NoteEvent>& baseNotes)
{
    return std::all_of(track.notes.begin(), track.notes.end(), [&](const NoteEvent& note)
    {
        return std::any_of(baseNotes.begin(), baseNotes.end(), [&](const NoteEvent& baseNote)
        {
            return noteMatchesBaseIdentity(note, baseNote);
        });
    });
}

struct MidiNoteOnSnapshot
{
    int note = 0;
    int tick = 0;
    int velocity = 0;
};

std::vector<MidiNoteOnSnapshot> collectMidiNoteOns(const juce::MidiMessageSequence& sequence)
{
    std::vector<MidiNoteOnSnapshot> result;
    result.reserve(static_cast<size_t>(sequence.getNumEvents()));

    for (int index = 0; index < sequence.getNumEvents(); ++index)
    {
        const auto* event = sequence.getEventPointer(index);
        if (event == nullptr || !event->message.isNoteOn())
            continue;

        result.push_back({ event->message.getNoteNumber(),
                           static_cast<int>(std::lround(event->message.getTimeStamp())),
                           event->message.getVelocity() });
    }

    return result;
}

bool hasMidiNoteOnAt(const std::vector<MidiNoteOnSnapshot>& noteOns, int note, int tick)
{
    return std::any_of(noteOns.begin(), noteOns.end(), [&](const MidiNoteOnSnapshot& event)
    {
        return event.note == note && event.tick == tick;
    });
}

bool isPitchInScale(int pitch, int keyRoot, int scaleMode)
{
    static const std::array<int, 7> minor { 0, 2, 3, 5, 7, 8, 10 };
    static const std::array<int, 7> major { 0, 2, 4, 5, 7, 9, 11 };
    static const std::array<int, 7> harmonicMinor { 0, 2, 3, 5, 7, 8, 11 };
    const auto& intervals = scaleMode == 1 ? major : (scaleMode == 2 ? harmonicMinor : minor);
    const int pitchClass = ((pitch - keyRoot) % 12 + 12) % 12;
    return std::find(intervals.begin(), intervals.end(), pitchClass) != intervals.end();
}

void testSerializationRoundTripSmoke()
{
    auto project = createDefaultProject();
    project.params.genre = GenreType::Trap;
    project.params.bars = 4;
    project.params.trapSubstyle = 2;

    auto* kick = findTrackByType(project, TrackType::Kick);
    auto* sub = findTrackByType(project, TrackType::Sub808);
    expect(kick != nullptr && sub != nullptr, "Smoke serialization test requires Kick and Sub808 tracks.");

    kick->notes.push_back({ 36, 0, 1, 112, 0, false, "smoke_kick", false, false, false });
    kick->baseNotes.push_back({ 36, 4, 1, 96, 0, false, "smoke_kick_base", false, false, false });
    kick->performanceBaseParams.genre = GenreType::Trap;
    kick->performanceBaseParams.swingPercent = 58.0f;
    kick->performanceBaseParams.velocityAmount = 0.72f;
    kick->performanceBaseParams.timingAmount = 0.48f;
    kick->performanceBaseParams.humanizeAmount = 0.33f;
    kick->performanceBaseParams.densityAmount = 0.61f;
    kick->performanceBaseParams.bars = 4;
    kick->performanceBaseParams.trapSubstyle = 2;
    kick->hasPerformanceBaseParams = true;
    sub->sub808Notes.push_back({ 36, 0, 4, 100, 0, "smoke_sub", false, false, false });
    sub->notes = toLegacyNoteEvents(sub->sub808Notes);
    sub->baseSub808Notes.push_back({ 43, 6, 2, 96, 4, "smoke_sub_base", true, false, true });
    sub->baseNotes = toLegacyNoteEvents(sub->baseSub808Notes);
    sub->performanceBaseParams.genre = GenreType::Trap;
    sub->performanceBaseParams.swingPercent = 57.0f;
    sub->performanceBaseParams.velocityAmount = 0.66f;
    sub->performanceBaseParams.timingAmount = 0.36f;
    sub->performanceBaseParams.humanizeAmount = 0.29f;
    sub->performanceBaseParams.densityAmount = 0.58f;
    sub->performanceBaseParams.bars = 4;
    sub->performanceBaseParams.trapSubstyle = 2;
    sub->hasPerformanceBaseParams = true;

    PatternProject restored;
    expect(PatternProjectSerialization::deserialize(wrapSerializedProject(project), restored),
           "PatternProject deserialize failed during smoke roundtrip.");

    auto* restoredKick = findTrackByType(restored, TrackType::Kick);
    auto* restoredSub = findTrackByType(restored, TrackType::Sub808);
    expect(restoredKick != nullptr && restoredSub != nullptr, "Restored smoke project lost required tracks.");
    expect(restoredKick->notes.size() == 1, "Restored kick notes count should match serialized state.");
        expect(restoredKick->baseNotes.size() == 1, "Restored kick base notes count should match serialized state.");
        expect(restoredKick->baseNotes.front().semanticRole == "smoke_kick_base",
            "Restored kick base notes should preserve their serialized semantic role.");
        expect(restoredKick->hasPerformanceBaseParams,
            "Restored kick should preserve per-track performance baseline params.");
        expect(std::abs(restoredKick->performanceBaseParams.swingPercent - 58.0f) < 0.001f,
            "Restored kick should preserve swing performance baseline.");
    expect(restoredSub->sub808Notes.size() == 1, "Restored sub808 notes count should match serialized state.");
        expect(restoredSub->baseSub808Notes.size() == 1, "Restored sub808 base notes count should match serialized state.");
        expect(restoredSub->baseSub808Notes.front().semanticRole == "smoke_sub_base",
            "Restored sub808 base notes should preserve their serialized semantic role.");
        expect(noteSequencesEqual(restoredSub->baseNotes, toLegacyNoteEvents(restoredSub->baseSub808Notes)),
            "Restored sub808 base legacy mirror should match the canonical baseSub808Notes state.");
        expect(restoredSub->hasPerformanceBaseParams,
            "Restored Sub808 should preserve per-track performance baseline params.");
        expect(std::abs(restoredSub->performanceBaseParams.densityAmount - 0.58f) < 0.001f,
            "Restored Sub808 should preserve density performance baseline.");
    }

    void testEqStateSerializationAndLegacyMigrationSmoke()
    {
        auto project = createDefaultProject();
        project.globalSound.eq.selectedBand = 4;
        auto& attackBand = project.globalSound.eq.bands[4];
        attackBand.enabled = true;
        attackBand.freqHz = 3450.0f;
        attackBand.gainDb = 4.5f;
        attackBand.q = 1.35f;
        attackBand.shape = EqBandShape::Bell;
        project.globalSound.eqTone = legacyEqToneFromEqState(project.globalSound.eq);

        PatternProject restored;
        expect(PatternProjectSerialization::deserialize(wrapSerializedProject(project), restored),
            "EQ smoke deserialize failed on full nested EQ state.");
        expect(restored.globalSound.eq.selectedBand == 4,
            "Restored EQ should preserve selected band.");
        const auto& restoredAttackBand = restored.globalSound.eq.bands[4];
        expect(restoredAttackBand.enabled,
            "Restored EQ should preserve per-band enabled state.");
        expect(std::abs(restoredAttackBand.freqHz - 3450.0f) < 0.001f,
            "Restored EQ should preserve per-band frequency.");
        expect(std::abs(restoredAttackBand.gainDb - 4.5f) < 0.001f,
            "Restored EQ should preserve per-band gain.");
        expect(std::abs(restoredAttackBand.q - 1.35f) < 0.001f,
            "Restored EQ should preserve per-band Q.");
        expect(restoredAttackBand.shape == EqBandShape::Bell,
            "Restored EQ should preserve per-band shape.");

        auto legacyProject = createDefaultProject();
        legacyProject.globalSound.eqTone = 0.5f;
        auto legacyRoot = wrapSerializedProject(legacyProject);
        auto patternNode = legacyRoot.getChild(0);
        patternNode.removeProperty("global_sound_eq_selected_band", nullptr);
        for (int bandIndex = 0; bandIndex < kEqBandCount; ++bandIndex)
        {
         const auto prefix = "global_sound_eq_band_" + juce::String(bandIndex);
         patternNode.removeProperty(prefix + "_enabled", nullptr);
         patternNode.removeProperty(prefix + "_freq_hz", nullptr);
         patternNode.removeProperty(prefix + "_gain_db", nullptr);
         patternNode.removeProperty(prefix + "_q", nullptr);
         patternNode.removeProperty(prefix + "_shape", nullptr);
        }

        PatternProject restoredLegacy;
        expect(PatternProjectSerialization::deserialize(legacyRoot, restoredLegacy),
            "Legacy EQ smoke deserialize failed.");
        expect(restoredLegacy.globalSound.eq.selectedBand == 2,
            "Legacy eqTone migration should seed the body band.");
        const auto& restoredBodyBand = restoredLegacy.globalSound.eq.bands[2];
        expect(restoredBodyBand.enabled,
            "Legacy eqTone migration should enable the seeded body band.");
        expect(std::abs(restoredBodyBand.gainDb - 6.0f) < 0.001f,
            "Legacy eqTone migration should convert tone to body-band gain.");
    }

    void testCompressorStateSerializationAndLegacyMigrationSmoke()
    {
        auto project = createDefaultProject();
        auto& compressor = project.globalSound.compressor;
        compressor = createDefaultCompressorState();
        compressor.enabled = true;
        compressor.order = 1;
        compressor.ratio = 6.4f;
        compressor.thresholdDb = -24.0f;
        compressor.mix = 0.67f;
        compressor.attackMs = 18.0f;
        compressor.releaseMs = 132.0f;
        compressor.saturation = 0.44f;
        compressor.inputTrimDb = 2.5f;
        compressor.outputTrimDb = -1.0f;
        compressor.autoMakeup = true;
        compressor.character = DrumCompressorCharacter::Punch;
        compressor.saturationMode = DrumSaturationMode::Punch;
        syncLegacySoundLayerState(project.globalSound);

        PatternProject restored;
        expect(PatternProjectSerialization::deserialize(wrapSerializedProject(project), restored),
            "Compressor smoke deserialize failed on full nested compressor state.");

        const auto& restoredCompressor = restored.globalSound.compressor;
        expect(restoredCompressor.enabled,
            "Restored compressor should preserve enabled state.");
        expect(restoredCompressor.order == 1,
            "Restored compressor should preserve pre/post order.");
        expect(std::abs(restoredCompressor.ratio - 6.4f) < 0.001f,
            "Restored compressor should preserve ratio.");
        expect(std::abs(restoredCompressor.thresholdDb + 24.0f) < 0.001f,
            "Restored compressor should preserve threshold.");
        expect(std::abs(restoredCompressor.mix - 0.67f) < 0.001f,
            "Restored compressor should preserve wet mix.");
        expect(std::abs(restoredCompressor.attackMs - 18.0f) < 0.001f,
            "Restored compressor should preserve attack.");
        expect(std::abs(restoredCompressor.releaseMs - 132.0f) < 0.001f,
            "Restored compressor should preserve release.");
        expect(std::abs(restoredCompressor.saturation - 0.44f) < 0.001f,
            "Restored compressor should preserve saturation amount.");
        expect(std::abs(restoredCompressor.inputTrimDb - 2.5f) < 0.001f,
            "Restored compressor should preserve input trim.");
        expect(std::abs(restoredCompressor.outputTrimDb + 1.0f) < 0.001f,
            "Restored compressor should preserve output trim.");
        expect(restoredCompressor.autoMakeup,
            "Restored compressor should preserve auto makeup.");
        expect(restoredCompressor.character == DrumCompressorCharacter::Punch,
            "Restored compressor should preserve character mode.");
        expect(restoredCompressor.saturationMode == DrumSaturationMode::Punch,
            "Restored compressor should preserve saturation mode.");
        expect(std::abs(restored.globalSound.compression - 0.67f) < 0.001f,
            "Restored compressor should keep the legacy compression mirror in sync.");

        auto legacyProject = createDefaultProject();
        legacyProject.globalSound.compression = 0.82f;
        auto legacyRoot = wrapSerializedProject(legacyProject);
        auto patternNode = legacyRoot.getChild(0);
        patternNode.removeProperty("global_sound_compressor_enabled", nullptr);
        patternNode.removeProperty("global_sound_compressor_order", nullptr);
        patternNode.removeProperty("global_sound_compressor_ratio", nullptr);
        patternNode.removeProperty("global_sound_compressor_threshold_db", nullptr);
        patternNode.removeProperty("global_sound_compressor_mix", nullptr);
        patternNode.removeProperty("global_sound_compressor_attack_ms", nullptr);
        patternNode.removeProperty("global_sound_compressor_release_ms", nullptr);
        patternNode.removeProperty("global_sound_compressor_saturation", nullptr);
        patternNode.removeProperty("global_sound_compressor_input_trim_db", nullptr);
        patternNode.removeProperty("global_sound_compressor_output_trim_db", nullptr);
        patternNode.removeProperty("global_sound_compressor_auto_makeup", nullptr);
        patternNode.removeProperty("global_sound_compressor_character", nullptr);
        patternNode.removeProperty("global_sound_compressor_saturation_mode", nullptr);

        PatternProject restoredLegacy;
        expect(PatternProjectSerialization::deserialize(legacyRoot, restoredLegacy),
            "Legacy compressor smoke deserialize failed.");

        const auto& migratedCompressor = restoredLegacy.globalSound.compressor;
        expect(migratedCompressor.enabled,
            "Legacy compression migration should enable the nested compressor state.");
        expect(std::abs(migratedCompressor.mix - 0.82f) < 0.001f,
            "Legacy compression migration should map the old flat compression amount to wet mix.");
        expect(migratedCompressor.character == DrumCompressorCharacter::Punch,
            "Legacy compression migration should bias stronger amounts toward punch character.");
        expect(migratedCompressor.saturationMode == DrumSaturationMode::Punch,
            "Legacy compression migration should bias stronger amounts toward punch saturation.");
        expect(restoredLegacy.globalSound.compression > 0.80f,
            "Legacy compression migration should restore the flat compatibility mirror.");
    }

    void testBasePatternCaptureSmoke()
    {
        auto project = createDefaultProject();

        auto* hat = findTrackByType(project, TrackType::HiHat);
        auto* kick = findTrackByType(project, TrackType::Kick);
        auto* sub = findTrackByType(project, TrackType::Sub808);
        expect(hat != nullptr && kick != nullptr && sub != nullptr,
            "Base-pattern capture smoke requires HiHat, Kick, and Sub808 tracks.");

        hat->notes = { { 42, 1, 1, 90, 0, false, "visible_hat", false, false, false } };
        hat->baseNotes = { { 42, 7, 1, 72, 0, false, "old_hat_base", false, false, false } };

        kick->notes = { { 36, 0, 1, 116, 0, false, "visible_kick", false, false, false } };
        kick->baseNotes = { { 36, 8, 1, 88, 0, false, "old_kick_base", false, false, false } };

        sub->sub808Notes = { { 36, 0, 4, 98, 0, "visible_sub", false, false, false } };
        sub->notes = toLegacyNoteEvents(sub->sub808Notes);
        sub->baseSub808Notes = { { 43, 10, 2, 82, 6, "old_sub_base", true, false, true } };
        sub->baseNotes = toLegacyNoteEvents(sub->baseSub808Notes);

        PatternPerformanceTransformEngine::captureBasePatterns(project, { TrackType::Kick, TrackType::Sub808 });

        expect(hat->baseNotes.size() == 1 && hat->baseNotes.front().semanticRole == "old_hat_base",
            "Base-pattern capture should not touch lanes outside the mutable set.");
        expect(noteSequencesEqual(kick->baseNotes, kick->notes),
            "Base-pattern capture should copy visible Kick notes into baseNotes.");
        expect(sub808NoteSequencesEqual(sub->baseSub808Notes, sub->sub808Notes),
            "Base-pattern capture should copy visible Sub808 notes into baseSub808Notes.");
        expect(noteSequencesEqual(sub->baseNotes, sub->notes),
            "Base-pattern capture should refresh the Sub808 legacy baseNotes mirror from baseSub808Notes.");
}

void testPerformanceTransformFromBaseSmoke()
{
    auto project = createDefaultProject();
    project.params.genre = GenreType::Drill;
    project.params.bars = 4;
    project.params.swingPercent = 52.0f;
    project.params.velocityAmount = 0.34f;
    project.params.timingAmount = 0.22f;
    project.params.humanizeAmount = 0.12f;
    project.params.densityAmount = 0.70f;

    auto* hat = findTrackByType(project, TrackType::HiHat);
    auto* perc = findTrackByType(project, TrackType::Perc);
    expect(hat != nullptr && perc != nullptr,
        "Performance transform smoke requires HiHat and Perc tracks.");

    hat->notes = {
        { 42, 0, 1, 92, 0, false, "drill_hat_backbone", false, false, false },
        { 42, 1, 1, 76, 0, false, "drill_hat_support", false, false, false },
        { 42, 3, 1, 72, 0, false, "drill_hat_support", false, false, false }
    };
    perc->notes = {
        { 39, 5, 1, 74, 0, false, "perc_support", false, false, false },
        { 39, 13, 1, 78, 0, false, "perc_fill", false, false, false }
    };

    PatternPerformanceTransformEngine::captureBasePatterns(project, { TrackType::HiHat, TrackType::Perc });

    const auto baseHat = hat->baseNotes;
    const auto basePerc = perc->baseNotes;

    project.params.swingPercent = 57.0f;
    project.params.velocityAmount = 0.80f;
    project.params.timingAmount = 0.68f;
    project.params.humanizeAmount = 0.54f;
    project.params.densityAmount = 0.12f;

    PatternPerformanceTransformEngine::applyPerformanceFromBase(project);

    expect(hat->notes.size() < baseHat.size() || perc->notes.size() < basePerc.size(),
        "Performance transform smoke should allow lower density to deterministically thin support notes from the base pattern.");
    expect(!hasNoteAt(*hat, 1, 0, "drill_hat_support") || !hasNoteAt(*perc, 5, 0, "perc_support"),
        "Performance transform smoke should move or thin non-rigid support notes when live controls diverge from the baseline.");
    expect(hasNoteAt(*hat, 0, 0, "drill_hat_backbone"),
        "Performance transform smoke should preserve rigid Drill backbone notes when applying live controls from base.");

    project.params = hat->performanceBaseParams;
    PatternPerformanceTransformEngine::applyPerformanceFromBase(project);

    expect(noteSequencesEqual(hat->notes, baseHat),
        "Performance transform smoke should restore the HiHat visible pattern when controls return to the captured baseline.");
    expect(noteSequencesEqual(perc->notes, basePerc),
        "Performance transform smoke should restore the Perc visible pattern when controls return to the captured baseline.");
}

void testDensityAuthoringPrioritySmoke()
{
    auto project = createDefaultProject();
    project.params.genre = GenreType::BoomBap;
    project.params.bars = 1;
    project.params.swingPercent = 54.0f;
    project.params.velocityAmount = 0.42f;
    project.params.timingAmount = 0.18f;
    project.params.humanizeAmount = 0.16f;
    project.params.densityAmount = 0.72f;

    auto* hat = findTrackByType(project, TrackType::HiHat);
    expect(hat != nullptr, "Density authoring smoke requires a HiHat track.");

    hat->laneRole = "carrier";
    hat->notes = {
        { 42, 0, 1, 94, 0, false, "hat_backbone", false, false, false },
        { 42, 7, 1, 82, 0, false, "hat_texture", false, false, false },
        { 42, 11, 1, 74, 0, false, "hat_fill", false, false, false }
    };

    PatternPerformanceTransformEngine::captureBasePatterns(project, { TrackType::HiHat });
    const auto baseHat = hat->baseNotes;

    project.authoring.noteMetadataByLane[hat->laneId] = {
        { { 7, 0, 42, 1, false }, true, 100 },
        { { 11, 0, 42, 1, false }, false, 5 }
    };

    project.params.densityAmount = 0.10f;
    PatternPerformanceTransformEngine::applyPerformanceFromBase(project);

    expect(hasNoteAt(*hat, 0, 0, "hat_backbone"),
        "Density authoring smoke should always keep the backbone note visible.");
    expect(hasNoteAt(*hat, 7, 0, "hat_texture"),
        "Density authoring smoke should preserve author-locked notes before semantic fallback thinning.");
    expect(!hasNoteAt(*hat, 11, 0, "hat_fill"),
        "Density authoring smoke should remove low-priority filler notes before protected material.");
    expect(noteSequencesEqual(hat->baseNotes, baseHat),
        "Density authoring smoke should leave the captured base HiHat pattern untouched.");

    project.params = hat->performanceBaseParams;
    PatternPerformanceTransformEngine::applyPerformanceFromBase(project);

    expect(noteSequencesEqual(hat->notes, baseHat),
        "Density authoring smoke should restore the full visible HiHat pattern from base when density returns to baseline.");
}

void testSub808DensitySafetySmoke()
{
    auto project = createDefaultProject();
    project.params.genre = GenreType::Trap;
    project.params.bars = 2;
    project.params.swingPercent = 55.0f;
    project.params.velocityAmount = 0.40f;
    project.params.timingAmount = 0.20f;
    project.params.humanizeAmount = 0.14f;
    project.params.densityAmount = 0.84f;

    auto* sub = findTrackByType(project, TrackType::Sub808);
    expect(sub != nullptr, "Sub808 density smoke requires a Sub808 track.");

    sub->laneRole = "trap_sub";
    sub->sub808Notes = {
        { 36, 0, 2, 100, 0, "trap_sub_anchor", false, false, false },
        { 38, 4, 2, 92, 0, "trap_sub_move", false, true, true },
        { 41, 6, 2, 88, 0, "trap_sub_release", true, false, false },
        { 36, 8, 4, 98, 0, "trap_sub_anchor", false, false, false }
    };
    sub->notes = toLegacyNoteEvents(sub->sub808Notes);

    PatternPerformanceTransformEngine::captureBasePatterns(project, { TrackType::Sub808 });
    const auto baseSub = sub->baseSub808Notes;

    project.params.densityAmount = 0.18f;
    PatternPerformanceTransformEngine::applyPerformanceFromBase(project);

    expect(sub->sub808Notes.size() < baseSub.size(),
        "Sub808 density smoke should reduce the number of visible Sub808 starts at lower density.");
    expect(hasSubStartAt(*sub, 0, "trap_sub_anchor") && hasSubStartAt(*sub, 8, "trap_sub_anchor"),
        "Sub808 density smoke should preserve core Trap sub anchors when thinning density.");
    expect(sub->sub808Notes.front().length >= baseSub.front().length,
        "Sub808 density smoke should turn removed intermediate starts into longer held notes.");
    expect(sub808SequenceIsValidMonophonic(sub->sub808Notes),
        "Sub808 density smoke should keep the visible Sub808 lane monophonic with valid glide flags.");
    expect(sub808NoteSequencesEqual(sub->baseSub808Notes, baseSub),
        "Sub808 density smoke should keep the captured baseSub808Notes untouched.");
    expect(noteSequencesEqual(sub->notes, toLegacyNoteEvents(sub->sub808Notes)),
        "Sub808 density smoke should keep the visible legacy mirror aligned with transformed Sub808 notes.");

    project.params = sub->performanceBaseParams;
    PatternPerformanceTransformEngine::applyPerformanceFromBase(project);

    expect(sub808NoteSequencesEqual(sub->sub808Notes, baseSub),
        "Sub808 density smoke should restore the full base Sub808 sequence when density returns to baseline.");
}

void testCombinedLiveControlsGenreRegressionSmoke()
{
    struct GenreInteractionSnapshot
    {
        int hatSupportOffset = 0;
        int kickAnchorOffset = 0;
        int snareAnchorOffset = 0;
        size_t baseHatCount = 0;
        size_t visibleHatCount = 0;
        bool kickAnchorPresent = false;
        bool snareAnchorPresent = false;
        bool notesRemainBaseDerived = false;
    };

    auto renderGenre = [](GenreType genre,
                          const juce::String& hatLaneRole,
                          const juce::String& kickLaneRole,
                          const juce::String& snareLaneRole)
    {
        auto project = createDefaultProject();
        project.params.genre = genre;
        project.params.bars = 1;
        project.params.swingPercent = 52.0f;
        project.params.velocityAmount = 0.34f;
        project.params.timingAmount = 0.18f;
        project.params.humanizeAmount = 0.10f;
        project.params.densityAmount = 0.78f;

        auto* hat = findTrackByType(project, TrackType::HiHat);
        auto* kick = findTrackByType(project, TrackType::Kick);
        auto* snare = findTrackByType(project, TrackType::Snare);
        if (hat == nullptr || kick == nullptr || snare == nullptr)
            fail("Combined-controls smoke requires HiHat, Kick, and Snare tracks.");

        hat->laneRole = hatLaneRole;
        kick->laneRole = kickLaneRole;
        snare->laneRole = snareLaneRole;

        hat->notes = {
            { 42, 0, 1, 92, 0, false, "hat_backbone", false, false, false },
            { 42, 1, 1, 78, 0, false, "hat_support", false, false, false },
            { 42, 3, 1, 72, 0, false, "hat_support", false, false, false },
            { 42, 7, 1, 66, 0, false, "hat_texture", false, false, false }
        };
        kick->notes = {
            { 36, 0, 1, 118, 0, false, "kick_anchor", false, false, false },
            { 36, 6, 1, 92, 0, false, "kick_support", false, false, false }
        };
        snare->notes = {
            { 38, 4, 1, 108, 0, false, "snare_backbone", false, false, false },
            { 38, 12, 1, 104, 0, false, "snare_backbone", false, false, false }
        };

        PatternPerformanceTransformEngine::captureBasePatterns(project, { TrackType::HiHat, TrackType::Kick, TrackType::Snare });

        const auto baseHat = hat->baseNotes;
        const auto baseKick = kick->baseNotes;
        const auto baseSnare = snare->baseNotes;

        project.params.swingPercent = 66.0f;
        project.params.velocityAmount = 0.82f;
        project.params.timingAmount = 0.68f;
        project.params.humanizeAmount = 0.56f;
        project.params.densityAmount = 0.18f;

        PatternPerformanceTransformEngine::applyPerformanceFromBase(project);

        GenreInteractionSnapshot snapshot;
        snapshot.baseHatCount = baseHat.size();
        snapshot.visibleHatCount = hat->notes.size();
        snapshot.notesRemainBaseDerived = allVisibleNotesDerivedFromBase(*hat, baseHat)
            && allVisibleNotesDerivedFromBase(*kick, baseKick)
            && allVisibleNotesDerivedFromBase(*snare, baseSnare);

        for (const auto& note : hat->notes)
        {
            if (note.step == 1 && note.semanticRole == "hat_support")
                snapshot.hatSupportOffset = note.microOffset;
        }

        for (const auto& note : kick->notes)
        {
            if (note.step == 0 && note.semanticRole == "kick_anchor")
            {
                snapshot.kickAnchorPresent = true;
                snapshot.kickAnchorOffset = note.microOffset;
            }
        }

        for (const auto& note : snare->notes)
        {
            if (note.step == 4 && note.semanticRole == "snare_backbone")
            {
                snapshot.snareAnchorPresent = true;
                snapshot.snareAnchorOffset = note.microOffset;
            }
        }

        return snapshot;
    };

    const auto boomBap = renderGenre(GenreType::BoomBap, "carrier", "foundation", "backbeat");
    const auto rap = renderGenre(GenreType::Rap, "carrier", "foundation", "backbeat");
    const auto trap = renderGenre(GenreType::Trap, "trap_hat", "trap_kick", "backbeat");
    const auto drill = renderGenre(GenreType::Drill, "drill_hat", "drill_kick", "drill_backbeat");

    expect(std::abs(boomBap.hatSupportOffset) > std::abs(trap.hatSupportOffset) + 24,
        "Combined-controls smoke should keep BoomBap swing clearly more audible than Trap under the same live control delta.");
    expect(std::abs(rap.hatSupportOffset) > std::abs(trap.hatSupportOffset) + 8,
        "Combined-controls smoke should keep Rap hats looser than Trap instead of collapsing into the same tight feel.");

    expect(trap.kickAnchorPresent && trap.snareAnchorPresent && drill.kickAnchorPresent && drill.snareAnchorPresent,
        "Combined-controls smoke should keep Trap and Drill kick/snare anchors present after density reduction.");
    expect(std::abs(trap.kickAnchorOffset) <= 8 && std::abs(trap.snareAnchorOffset) <= 8,
        "Combined-controls smoke should keep Trap core anchors tight under combined live controls.");
    expect(std::abs(drill.kickAnchorOffset) <= 8 && std::abs(drill.snareAnchorOffset) <= 8,
        "Combined-controls smoke should keep Drill main kick/snare anchors stable under combined live controls.");

    expect(boomBap.visibleHatCount <= boomBap.baseHatCount
            && rap.visibleHatCount <= rap.baseHatCount
            && trap.visibleHatCount <= trap.baseHatCount
            && drill.visibleHatCount <= drill.baseHatCount,
        "Combined-controls smoke should let density only prune from the base pattern instead of inventing new visible hat notes.");
    expect(boomBap.notesRemainBaseDerived && rap.notesRemainBaseDerived && trap.notesRemainBaseDerived && drill.notesRemainBaseDerived,
        "Combined-controls smoke should keep all visible drum notes derived from captured base notes across genres.");
}

void testManualEditLiveControlSafetySmoke()
{
    auto project = createDefaultProject();
    project.params.genre = GenreType::Rap;
    project.params.bars = 2;
    project.params.swingPercent = 54.0f;
    project.params.velocityAmount = 0.40f;
    project.params.timingAmount = 0.24f;
    project.params.humanizeAmount = 0.18f;
    project.params.densityAmount = 0.76f;

    auto* hat = findTrackByType(project, TrackType::HiHat);
    auto* sub = findTrackByType(project, TrackType::Sub808);
    expect(hat != nullptr && sub != nullptr,
        "Manual-edit smoke requires HiHat and Sub808 tracks.");

    hat->laneRole = "carrier";
    sub->laneRole = "foundation";
    hat->notes = {
        { 42, 0, 1, 92, 0, false, "hat_backbone", false, false, false },
        { 42, 2, 1, 78, 0, false, "hat_support", false, false, false },
        { 42, 6, 1, 70, 0, false, "hat_fill", false, false, false }
    };
    sub->sub808Notes = {
        { 36, 0, 4, 100, 0, "sub_anchor", false, false, false },
        { 38, 8, 4, 94, 0, "sub_support", false, false, false }
    };
    sub->notes = toLegacyNoteEvents(sub->sub808Notes);

    PatternPerformanceTransformEngine::captureBasePatterns(project, { TrackType::HiHat, TrackType::Sub808 });

    project.params.swingPercent = 60.0f;
    project.params.velocityAmount = 0.74f;
    project.params.timingAmount = 0.54f;
    project.params.humanizeAmount = 0.42f;
    project.params.densityAmount = 0.34f;
    PatternPerformanceTransformEngine::applyPerformanceFromBase(project);

    hat->notes.push_back({ 42, 10, 1, 86, 0, false, "edited_hat_fill", false, false, false });
    std::sort(hat->notes.begin(), hat->notes.end(), [](const NoteEvent& lhs, const NoteEvent& rhs)
    {
        if (lhs.step != rhs.step)
            return lhs.step < rhs.step;
        return lhs.pitch < rhs.pitch;
    });

    sub->sub808Notes = {
        { 36, 0, 6, 100, 0, "edited_sub_anchor", false, false, false },
        { 43, 8, 4, 95, 0, "edited_sub_support", false, false, false }
    };
    sub->notes = toLegacyNoteEvents(sub->sub808Notes);

    PatternPerformanceTransformEngine::captureBasePattern(*hat, project.params);
    PatternPerformanceTransformEngine::captureBasePattern(*sub, project.params);

    const auto editedHatBase = hat->baseNotes;
    const auto editedSubBase = sub->baseSub808Notes;

    project.params.swingPercent = 64.0f;
    project.params.velocityAmount = 0.82f;
    project.params.timingAmount = 0.66f;
    project.params.humanizeAmount = 0.54f;
    project.params.densityAmount = 0.12f;
    PatternPerformanceTransformEngine::applyPerformanceFromBase(project);

    expect(noteSequencesEqual(hat->baseNotes, editedHatBase),
        "Manual-edit smoke should keep the edited HiHat base pattern intact after later live control changes.");
    expect(sub808NoteSequencesEqual(sub->baseSub808Notes, editedSubBase),
        "Manual-edit smoke should keep the edited Sub808 base pattern intact after later live control changes.");
    expect(!noteSequencesEqual(hat->notes, editedHatBase) || !sub808NoteSequencesEqual(sub->sub808Notes, editedSubBase),
        "Manual-edit smoke should still let live controls affect manually edited tracks after the edit baseline is recaptured.");
    expect(sub808SequenceIsValidMonophonic(sub->sub808Notes),
        "Manual-edit smoke should keep manually edited Sub808 lanes monophonic after later density changes.");

    project.params = hat->performanceBaseParams;
    PatternPerformanceTransformEngine::applyPerformanceFromBase(project);

    expect(noteSequencesEqual(hat->notes, editedHatBase),
        "Manual-edit smoke should restore the edited HiHat pattern exactly when controls return to the edited baseline.");
    expect(sub808NoteSequencesEqual(sub->sub808Notes, editedSubBase),
        "Manual-edit smoke should restore the edited Sub808 pattern exactly when controls return to the edited baseline.");
    expect(hasNoteAt(*hat, 10, 0, "edited_hat_fill"),
        "Manual-edit smoke should not lose manually added visible notes after later live control changes.");
}

void testVisibleTransformExportConsistencySmoke()
{
    auto project = createDefaultProject();
    project.params.genre = GenreType::Drill;
    project.params.bars = 1;
    project.params.swingPercent = 52.0f;
    project.params.velocityAmount = 0.34f;
    project.params.timingAmount = 0.20f;
    project.params.humanizeAmount = 0.12f;
    project.params.densityAmount = 0.80f;

    auto* hat = findTrackByType(project, TrackType::HiHat);
    auto* kick = findTrackByType(project, TrackType::Kick);
    auto* sub = findTrackByType(project, TrackType::Sub808);
    expect(hat != nullptr && kick != nullptr && sub != nullptr,
        "Export consistency smoke requires HiHat, Kick, and Sub808 tracks.");

    hat->laneRole = "drill_hat";
    kick->laneRole = "drill_kick";
    sub->laneRole = "drill_sub";
    hat->enabled = true;
    kick->enabled = true;
    sub->enabled = true;

    hat->notes = {
        { 42, 0, 1, 92, 0, false, "drill_hat_backbone", false, false, false },
        { 42, 1, 1, 78, 0, false, "drill_hat_support", false, false, false },
        { 42, 7, 1, 66, 0, false, "drill_hat_transition", false, false, false }
    };
    kick->notes = {
        { 36, 0, 1, 118, 0, false, "drill_kick_anchor", false, false, false },
        { 36, 6, 1, 94, 0, false, "drill_kick_support", false, false, false }
    };
    sub->sub808Notes = {
        { 36, 0, 4, 100, 0, "drill_sub_anchor", false, false, false },
        { 38, 6, 2, 92, 0, "drill_sub_move", false, true, true },
        { 41, 8, 4, 96, 0, "drill_sub_hold", true, false, false }
    };
    sub->notes = toLegacyNoteEvents(sub->sub808Notes);

    PatternPerformanceTransformEngine::captureBasePatterns(project, { TrackType::HiHat, TrackType::Kick, TrackType::Sub808 });

    project.params.swingPercent = 64.0f;
    project.params.velocityAmount = 0.80f;
    project.params.timingAmount = 0.62f;
    project.params.humanizeAmount = 0.48f;
    project.params.densityAmount = 0.22f;
    PatternPerformanceTransformEngine::applyPerformanceFromBase(project);

    const auto fullSequence = MidiExportEngine::patternToSequence(project, std::nullopt, 960, false, false);
    const auto fullNoteOns = collectMidiNoteOns(fullSequence);
    const int expectedFullCount = static_cast<int>(hat->notes.size() + kick->notes.size() + sub->notes.size());
    expect(static_cast<int>(fullNoteOns.size()) == expectedFullCount,
        "Export consistency smoke should emit one note-on per currently visible transformed note on enabled lanes.");

    for (const auto& note : hat->notes)
        expect(hasMidiNoteOnAt(fullNoteOns, 60, note.step * 240 + note.microOffset),
            "Export consistency smoke should export each visible transformed HiHat note at its visible tick.");

    for (const auto& note : kick->notes)
        expect(hasMidiNoteOnAt(fullNoteOns, 60, note.step * 240 + note.microOffset),
            "Export consistency smoke should export each visible transformed Kick note at its visible tick.");

    for (const auto& note : sub->sub808Notes)
        expect(hasMidiNoteOnAt(fullNoteOns, note.pitch, note.step * 240 + note.microOffset),
            "Export consistency smoke should export each visible transformed Sub808 note at its visible tick and pitch.");

    const auto hatSequence = MidiExportEngine::patternToSequence(project, TrackType::HiHat, 960, false, false);
    const auto hatNoteOns = collectMidiNoteOns(hatSequence);
    expect(static_cast<int>(hatNoteOns.size()) == static_cast<int>(hat->notes.size()),
        "Export consistency smoke should let track-only export/drag follow the visible transformed HiHat notes exactly.");

    const auto subSequence = MidiExportEngine::patternToSequence(project, TrackType::Sub808, 960, false, false);
    const auto subNoteOns = collectMidiNoteOns(subSequence);
    expect(static_cast<int>(subNoteOns.size()) == static_cast<int>(sub->sub808Notes.size()),
        "Export consistency smoke should let track-only export/drag follow the visible transformed Sub808 notes exactly.");
}

void testStyleDefaultsSmoke()
{
    const auto boomBapNames = getBoomBapSubstyleNames();
    expect(boomBapNames.size() == 6, "BoomBap should expose exactly six public substyles after moving Aggressive/LaidBack out.");
    expect(boomBapNames.contains("BoomBapGold"), "BoomBap should expose BoomBapGold.");
    expect(!boomBapNames.contains("Aggressive") && !boomBapNames.contains("LaidBack"),
           "BoomBap should no longer expose Aggressive or LaidBack as public substyles.");
    expect(getRapSubstyleNames().size() > 0, "Rap substyles must remain available.");
    expect(getTrapSubstyleNames().size() > 0, "Trap substyles must remain available.");
    expect(getDrillSubstyleNames().size() == 1, "Drill Phase 2 should expose exactly one Main substyle.");

    const auto& boomBap = getGenreStyleDefaults(GenreType::BoomBap, 0);
    const auto& rap = getGenreStyleDefaults(GenreType::Rap, 0);
    const auto& trap = getGenreStyleDefaults(GenreType::Trap, 0);
    const auto& drill = getGenreStyleDefaults(GenreType::Drill, 0);
    expect(boomBap.genre == GenreType::BoomBap, "BoomBap defaults should resolve BoomBap genre.");
    expect(rap.genre == GenreType::Rap, "Rap defaults should resolve Rap genre.");
    expect(trap.genre == GenreType::Trap, "Trap defaults should resolve Trap genre.");
    expect(drill.genre == GenreType::Drill, "Drill defaults should resolve Drill genre.");
    expect(getGenreStyleDefaults(GenreType::BoomBap, 3).substyleName == "BoomBapGold",
           "BoomBapGold should resolve at public BoomBap substyle index 3.");
    const auto& russianUnderground = getGenreStyleDefaults(GenreType::BoomBap, 4);
    expect(russianUnderground.substyleName == "RussianUnderground",
           "RussianUnderground should resolve at public BoomBap substyle index 4.");
    expect(russianUnderground.bpmMin <= 78 && russianUnderground.bpmMax <= 88,
           "RussianUnderground defaults should live in a slower underground BoomBap BPM range.");
    const auto& lofiRap = getGenreStyleDefaults(GenreType::BoomBap, 5);
    expect(lofiRap.substyleName == "LofiRap",
           "LofiRap should resolve at public BoomBap substyle index 5.");
    expect(lofiRap.bpmMin <= 74 && lofiRap.bpmMax <= 84,
           "LofiRap defaults should stay in a slow, relaxed BoomBap BPM range.");
    expect(drill.substyleName == "Main", "Drill defaults should expose the Main substyle.");
}

void testGenerationBpmSelectionSmoke()
{
    GeneratorParams params;
    params.genre = GenreType::Trap;
    params.trapSubstyle = 3;
    params.seed = 12345;
    params.syncDawTempo = false;

    const auto& rageTrap = getGenreStyleDefaults(GenreType::Trap, params.trapSubstyle);
    const auto deterministicA = resolveGenerationBpm(params, 140.0f, false, std::nullopt);
    const auto deterministicB = resolveGenerationBpm(params, 92.0f, false, std::nullopt);

    expect(deterministicA.source == GenerationBpmSource::DeterministicStyleRange,
        "Generate BPM smoke should use deterministic style range when host sync and BPM lock are off.");
    expect(deterministicA.bpm >= static_cast<float>(rageTrap.bpmMin)
            && deterministicA.bpm <= static_cast<float>(rageTrap.bpmMax),
        "Generate BPM smoke should keep Trap deterministic BPM inside the selected substyle range.");
    expect(std::abs(deterministicA.bpm - deterministicB.bpm) < 0.001f,
        "Generate BPM smoke should stay deterministic for the same seed, genre, and substyle.");

    const auto locked = resolveGenerationBpm(params, 141.0f, true, std::nullopt);
    expect(locked.source == GenerationBpmSource::BpmLock,
        "Generate BPM smoke should report BPM lock when no host tempo overrides it.");
    expect(std::abs(locked.bpm - 141.0f) < 0.001f,
        "Generate BPM smoke should preserve the current BPM when BPM lock is enabled.");

    params.syncDawTempo = true;
    const auto host = resolveGenerationBpm(params, 141.0f, true, 151.5);
    expect(host.source == GenerationBpmSource::HostSync,
        "Generate BPM smoke should prefer host sync over BPM lock when host tempo is available.");
    expect(std::abs(host.bpm - 151.5f) < 0.001f,
        "Generate BPM smoke should adopt the exact host tempo when host sync is enabled.");

    params.genre = GenreType::BoomBap;
    params.boombapSubstyle = 5;
    params.seed = 777;
    params.syncDawTempo = false;

    const auto& lofiRap = getGenreStyleDefaults(GenreType::BoomBap, params.boombapSubstyle);
    const float lofiBpm = chooseDeterministicStyleBpm(params);
    expect(lofiBpm >= static_cast<float>(lofiRap.bpmMin)
            && lofiBpm <= static_cast<float>(lofiRap.bpmMax),
        "Generate BPM smoke should keep BoomBap deterministic BPM inside the selected substyle range.");
}

void testLaneAwareSwingProtectionSmoke()
{
    auto drillProject = createDefaultProject();
    drillProject.params.genre = GenreType::Drill;
    drillProject.params.bars = 1;
    drillProject.params.swingPercent = 52.0f;

    auto* drillHat = findTrackByType(drillProject, TrackType::HiHat);
    auto* drillKick = findTrackByType(drillProject, TrackType::Kick);
    expect(drillHat != nullptr && drillKick != nullptr,
        "Lane-aware swing smoke requires Drill HiHat and Kick tracks.");

    drillHat->laneRole = "drill_hat";
    drillKick->laneRole = "drill_kick";
    drillHat->notes = {
        { 42, 0, 1, 92, 0, false, "drill_hat_backbone", false, false, false },
        { 42, 1, 1, 76, 0, false, "drill_hat_support", false, false, false }
    };
    drillKick->notes = {
        { 36, 0, 1, 118, 0, false, "drill_kick_anchor", false, false, false },
        { 36, 6, 1, 94, 0, false, "drill_kick_support", false, false, false }
    };

    PatternPerformanceTransformEngine::captureBasePatterns(drillProject, { TrackType::HiHat, TrackType::Kick });

    drillProject.params.swingPercent = 64.0f;
    PatternPerformanceTransformEngine::applyPerformanceFromBase(drillProject);

    expect(hasNoteAt(*drillKick, 0, 0, "drill_kick_anchor"),
        "Lane-aware swing smoke should keep Drill kick anchors grid-locked under swing changes.");
    expect(std::any_of(drillHat->notes.begin(), drillHat->notes.end(), [](const NoteEvent& note)
    {
        return note.step == 1 && note.semanticRole == "drill_hat_support" && note.microOffset > 0;
    }), "Lane-aware swing smoke should allow Drill support hats to take a subtle late swing feel.");

    auto transformHatSupportWithSwing = [](GenreType genre, const juce::String& laneRole)
    {
        auto project = createDefaultProject();
        project.params.genre = genre;
        project.params.bars = 1;
        project.params.swingPercent = 52.0f;

        auto* hat = findTrackByType(project, TrackType::HiHat);
        if (hat == nullptr)
            fail("Lane-aware swing smoke requires a HiHat track for cross-genre comparison.");

        hat->laneRole = laneRole;
        hat->notes = {
            { 42, 0, 1, 92, 0, false, "hat_backbone", false, false, false },
            { 42, 1, 1, 76, 0, false, "hat_support", false, false, false }
        };

        PatternPerformanceTransformEngine::captureBasePatterns(project, { TrackType::HiHat });

        project.params.swingPercent = 60.0f;
        PatternPerformanceTransformEngine::applyPerformanceFromBase(project);

        for (const auto& note : hat->notes)
            if (note.step == 1 && note.semanticRole == "hat_support")
                return note.microOffset;

        fail("Lane-aware swing smoke could not find the transformed hat support note.");
    };

    const int boomBapHatOffset = transformHatSupportWithSwing(GenreType::BoomBap, "carrier");
    const int trapHatOffset = transformHatSupportWithSwing(GenreType::Trap, "trap_hat");

    expect(std::abs(boomBapHatOffset) > std::abs(trapHatOffset),
        "Lane-aware swing smoke should give BoomBap hat support more swing motion than Trap hat support for the same control delta.");
}

void testSwingRoundTripGenerationSmoke()
{
    auto verifyRoundTrip = [](PatternProject& project,
                              float baselineSwing,
                              const std::vector<TrackType>& trackedLanes,
                              const juce::String& label)
    {
        struct TrackSnapshot
        {
            TrackType type;
            std::vector<NoteEvent> notes;
        };

        std::vector<TrackSnapshot> baselineTracks;
        for (const auto type : trackedLanes)
        {
            if (auto* track = findTrackByType(project, type); track != nullptr && !track->notes.empty())
                baselineTracks.push_back({ type, track->notes });
        }

        expect(!baselineTracks.empty(),
            label + " swing roundtrip smoke requires generated notes on the tracked lanes.");

        project.params.swingPercent = 75.0f;
        PatternPerformanceTransformEngine::applyPerformanceFromBase(project);

        project.params.swingPercent = baselineSwing;
        PatternPerformanceTransformEngine::applyPerformanceFromBase(project);

        for (const auto& snapshot : baselineTracks)
        {
            const auto* track = findTrackByType(project, snapshot.type);
            expect(track != nullptr && noteSequencesEqual(track->notes, snapshot.notes),
                label + " swing roundtrip smoke should restore tracked lanes exactly after Swing returns to baseline.");
        }
    };

    {
        auto project = createDefaultProject();
        project.params.genre = GenreType::BoomBap;
        project.params.bars = 1;
        project.params.swingPercent = 56.0f;
        if (auto* kick = findTrackByType(project, TrackType::Kick); kick != nullptr)
        {
            kick->laneRole = "boom_bap_kick";
            kick->notes = {
                { 36, 0, 1, 108, 0, false, "boom_bap_kick_anchor", false, false, false },
                { 36, 8, 1, 102, 0, false, "boom_bap_kick_anchor", false, false, false }
            };
        }

        if (auto* hat = findTrackByType(project, TrackType::HiHat); hat != nullptr)
        {
            hat->laneRole = "boom_bap_hat";
            hat->notes = {
                { 42, 0, 1, 88, 0, false, "boom_bap_hat_backbone", false, false, false },
                { 42, 1, 1, 80, 0, false, "boom_bap_hat_support", false, false, false },
                { 42, 3, 1, 82, 0, false, "boom_bap_hat_support", false, false, false },
                { 42, 4, 1, 88, 0, false, "boom_bap_hat_backbone", false, false, false }
            };
        }

        if (auto* openHat = findTrackByType(project, TrackType::OpenHat); openHat != nullptr)
        {
            openHat->laneRole = "boom_bap_open";
            openHat->notes = {
                { 46, 6, 1, 84, 0, false, "boom_bap_hat_support", false, false, false }
            };
        }

        if (auto* perc = findTrackByType(project, TrackType::Perc); perc != nullptr)
        {
            perc->laneRole = "boom_bap_texture";
            perc->notes = {
                { 54, 7, 1, 72, 0, false, "boom_bap_perc_support", false, false, false }
            };
        }

        PatternPerformanceTransformEngine::captureBasePatterns(project,
            { TrackType::HiHat, TrackType::OpenHat, TrackType::Perc, TrackType::Kick });

        verifyRoundTrip(project,
                        56.0f,
                        { TrackType::HiHat, TrackType::OpenHat, TrackType::Perc, TrackType::Kick },
                        "BoomBap");
    }

    {
        DrillEngine engine;
        auto project = createDefaultProject();
        project.params.genre = GenreType::Drill;
        project.params.bars = 2;
        project.params.seed = 4242;
        project.params.swingPercent = 52.0f;
        project.params.velocityAmount = 0.34f;
        project.params.timingAmount = 0.22f;
        project.params.humanizeAmount = 0.16f;
        project.params.densityAmount = 0.64f;
        project.params.drillSubstyle = 0;
        if (auto* hatFx = findTrackByType(project, TrackType::HatFX); hatFx != nullptr)
            hatFx->enabled = true;
        if (auto* sub = findTrackByType(project, TrackType::Sub808); sub != nullptr)
            sub->enabled = true;

        engine.generate(project);

        verifyRoundTrip(project,
                        52.0f,
                        { TrackType::HiHat, TrackType::HatFX, TrackType::Kick },
                        "Drill");
    }
}

void testDrillSwingHatSemanticsSmoke()
{
    auto project = createDefaultProject();
    project.params.genre = GenreType::Drill;
    project.params.bars = 1;
    project.params.swingPercent = 52.0f;

    auto* hat = findTrackByType(project, TrackType::HiHat);
    expect(hat != nullptr, "Drill swing semantics smoke requires a HiHat track.");

    hat->laneRole = "drill_hat";
    hat->notes = {
        { 42, 0, 1, 92, 0, false, "drill_hat_backbone", false, false, false },
        { 42, 1, 1, 84, 0, false, "drill_hat_reference_copy", false, false, false },
        { 42, 3, 1, 76, 0, false, "drill_hat_transition", false, false, false }
    };

    PatternPerformanceTransformEngine::captureBasePatterns(project, { TrackType::HiHat });
    const auto baseHat = hat->baseNotes;

    project.params.swingPercent = 75.0f;
    PatternPerformanceTransformEngine::applyPerformanceFromBase(project);

    int backboneOffset = 0;
    int referenceOffset = 0;
    int supportOffset = 0;

    for (const auto& note : hat->notes)
    {
        if (note.step == 0 && note.semanticRole == "drill_hat_backbone")
            backboneOffset = note.microOffset;
        else if (note.step == 1 && note.semanticRole == "drill_hat_reference_copy")
            referenceOffset = note.microOffset;
        else if (note.step == 3 && note.semanticRole == "drill_hat_transition")
            supportOffset = note.microOffset;
    }

    expect(backboneOffset == 0,
        "Drill swing semantics smoke should keep the main hat backbone grid-locked under Swing.");
    expect(std::abs(referenceOffset) <= 1,
        "Drill swing semantics smoke should keep protected copied-reference hats effectively anchored while Swing changes.");
    expect(supportOffset >= 2 && supportOffset <= 6,
        "Drill swing semantics smoke should allow only a subtle, visible late shift on Drill support hats.");

    project.params.swingPercent = 52.0f;
    PatternPerformanceTransformEngine::applyPerformanceFromBase(project);

    expect(noteSequencesEqual(hat->notes, baseHat),
        "Drill swing semantics smoke should restore the exact visible HiHat pattern when Swing returns to baseline.");
}

void testDrillPhrasePlannerSmoke()
{
    auto project = createDefaultProject();
    project.params.genre = GenreType::Drill;
    project.params.bars = 4;
    project.params.drillSubstyle = 0;
    project.styleInfluence.supportAccentWeight = 1.1f;
    project.styleInfluence.lowEndCouplingWeight = 1.2f;
    project.styleInfluence.drillHatDensityVariationWeight = 1.4f;

    auto plan = DrillPhrasePlanner::buildPlan(project);
    expect(plan.phraseSpanBars == 4, "Drill phrase planner should preserve project bar count.");
    expect(plan.substyleIndex == 0, "Drill phrase planner should clamp to the Main substyle for Phase 2.");
    expect(plan.bars.size() == 4, "Drill phrase planner should return one bar plan per project bar.");
    expect(plan.bars.front().anchorMap.hatCarrierSteps[0] == 0, "Drill phrase planner should seed a stable hat carrier anchor.");
    expect(plan.bars.front().anchorMap.snareAnchorSteps[0] == 8, "First Drill bar should anchor the main snare on beat three.");
    expect(plan.bars[1].anchorMap.snareAnchorSteps[0] == 12, "Second Drill bar should expose the late answer snare anchor.");
    for (const auto& bar : plan.bars)
    {
        int kickTemplateSteps = 0;
        for (const int step : bar.anchorMap.kickAnchorSteps)
            if (step >= 0)
                ++kickTemplateSteps;

        expect(kickTemplateSteps <= (bar.role == DrillPhraseBarRole::Lift || bar.role == DrillPhraseBarRole::Release ? 3 : 2),
               "Drill phrase planner should choose a compact kick template per bar instead of exposing four permissive kick anchors.");
    }
    expect(!plan.summary.isEmpty(), "Drill phrase planner should emit a phrase summary for downstream phases.");
}

void testDrillHatGenerationSmoke()
{
    DrillEngine engine;

    auto project = createDefaultProject();
    project.params.genre = GenreType::Drill;
    project.params.bars = 4;
    project.params.seed = 4242;
    project.params.densityAmount = 0.62f;
    project.params.timingAmount = 0.40f;
    project.params.drillSubstyle = 0;
    project.styleInfluence.hatMotionWeight = 1.3f;
    project.styleInfluence.drillHatTripletWeight = 1.4f;
    project.styleInfluence.drillHatBurstWeight = 1.1f;
    project.styleInfluence.drillHatGapIntentWeight = 0.8f;
    project.styleInfluence.drillHatAccentPatternWeight = 1.2f;
    project.styleInfluence.drillHatDensityVariationWeight = 1.3f;

    const auto baselineProject = project;

    engine.generate(project);

    auto* hat = findTrackByType(project, TrackType::HiHat);
    expect(hat != nullptr, "Drill hat generation smoke requires a HiHat track.");
    const auto expectedPlan = DrillPhrasePlanner::buildPlan(project);

    expect(!hat->notes.empty(), "Drill Phase 3 should generate main hihat notes.");
    expect(hat->laneRole == "drill_hat", "Drill hat generation should mark the HiHat lane role.");

    juce::StringArray missingCarrierCoverage;

    for (const auto& bar : expectedPlan.bars)
    {
        for (const int stepInBar : bar.anchorMap.hatCarrierSteps)
        {
            if (stepInBar < 0)
                continue;

            const int carrierTick = bar.barIndex * HiResTiming::kTicksPerBar4_4 + stepInBar * HiResTiming::kTicks1_16;
            if (!hasHatCarrierCoverageNearTick(*hat,
                                               carrierTick,
                                               HiResTiming::kTicks1_32))
            {
                missingCarrierCoverage.add("bar=" + juce::String(bar.barIndex)
                                           + ",step=" + juce::String(stepInBar));
            }
        }

        int barNoteCount = 0;
        int copiedReferenceCount = 0;
        for (const auto& note : hat->notes)
        {
            if ((note.step / 16) == bar.barIndex)
                ++barNoteCount;
            if ((note.step / 16) == bar.barIndex && note.semanticRole == "drill_hat_reference_copy")
                ++copiedReferenceCount;
        }
        expect(barNoteCount <= std::max(14, copiedReferenceCount + 2),
               "Drill main hihat generation should keep per-bar density controlled unless a copied reference pattern intentionally carries more material.");
    }

    expect(missingCarrierCoverage.isEmpty(),
           "Drill main hihat generation must keep each planned carrier musically covered by either backbone or copied reference hats. Missing: "
               + missingCarrierCoverage.joinIntoString(" | "));

    expect(std::any_of(hat->notes.begin(), hat->notes.end(), [](const NoteEvent& note)
    {
        return note.semanticRole == "drill_hat_transition"
            || note.semanticRole == "drill_hat_triplet"
            || note.semanticRole == "drill_hat_burst"
            || note.semanticRole == "drill_hat_reference_copy";
    }), "Drill main hihat generation should keep either controlled procedural activity or copied reference motion beyond the bare carrier skeleton.");

    expect(std::any_of(hat->notes.begin(), hat->notes.end(), [](const NoteEvent& note)
    {
        return note.microOffset != 0;
    }), "Drill main hihat generation should preserve off-grid subdivision motion.");

    auto secondProject = baselineProject;
    engine.generate(secondProject);

    auto* secondHat = findTrackByType(secondProject, TrackType::HiHat);
    expect(secondHat != nullptr, "Determinism check requires a second HiHat track.");

    expect(noteSequencesEqual(hat->notes, secondHat->notes),
           "Drill main hihat generation must remain deterministic under the same seed and inputs.");
}

void testDrillGenerationCapturesBasePatternsSmoke()
{
    DrillEngine engine;

    auto project = createDefaultProject();
    project.params.genre = GenreType::Drill;
    project.params.bars = 4;
    project.params.seed = 5151;
    project.params.densityAmount = 0.64f;
    project.params.timingAmount = 0.34f;
    project.params.drillSubstyle = 0;

    engine.generate(project);

    auto* hat = findTrackByType(project, TrackType::HiHat);
    auto* kick = findTrackByType(project, TrackType::Kick);
    auto* sub = findTrackByType(project, TrackType::Sub808);
    expect(hat != nullptr && kick != nullptr && sub != nullptr,
        "Drill base capture smoke requires HiHat, Kick, and Sub808 tracks.");
    expect(!hat->notes.empty() && !kick->notes.empty() && !sub->sub808Notes.empty(),
        "Drill base capture smoke requires generated visible notes on the main Drill lanes.");

    expect(noteSequencesEqual(hat->baseNotes, hat->notes),
        "Drill generation should capture generated HiHat notes into baseNotes during Phase 1.");
    expect(noteSequencesEqual(kick->baseNotes, kick->notes),
        "Drill generation should capture generated Kick notes into baseNotes during Phase 1.");
    expect(sub808NoteSequencesEqual(sub->baseSub808Notes, sub->sub808Notes),
        "Drill generation should capture generated Sub808 notes into baseSub808Notes during Phase 1.");
    expect(noteSequencesEqual(sub->baseNotes, sub->notes),
        "Drill generation should keep the Sub808 baseNotes legacy mirror aligned with the visible Sub808 lane during Phase 1.");
}

void testDrillHatGeneratorUsesReferenceCorpusSmoke()
{
    DrillHatGenerator generator;
    DrillPatternValidator validator;

    auto project = createDefaultProject();
    project.params.genre = GenreType::Drill;
    project.params.bars = 1;
    project.params.seed = 7171;
    project.params.densityAmount = 0.58f;
    project.params.timingAmount = 0.18f;
    project.params.drillSubstyle = 0;
    project.styleInfluence.drillHatDensityVariationWeight = 1.2f;
    project.styleInfluence.drillHatAccentPatternWeight = 1.15f;

    ReferenceHatSkeleton skeleton;
    skeleton.available = true;
    skeleton.sourceBars = 1;
    skeleton.sourceId = "drill-hat-reference";

    ReferenceHatBarSkeleton barMap;
    barMap.barIndex = 0;
    barMap.hasBarStartAnchor = true;
    for (int step = 0; step < 12; ++step)
        barMap.notes.push_back({ step * 240 + 60, 92 + (step % 3) * 6 });
    barMap.backboneSteps16 = { 0, 3, 6, 8, 11, 14 };
    barMap.motionSteps32 = { 1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23 };
    barMap.phraseAnchorSteps32 = { 0, 16, 28 };
    barMap.preSnareZoneSteps32 = { 12, 13, 14 };
    skeleton.barMaps.push_back(barMap);

    ReferenceHatCorpus corpus;
    corpus.available = true;
    corpus.sourceReferenceCount = 1;
    corpus.variants.push_back(skeleton);
    project.styleInfluence.referenceHatSkeleton = skeleton;
    project.styleInfluence.referenceHatCorpus = corpus;

    auto plan = DrillPhrasePlanner::buildPlan(project);
    auto* hat = findTrackByType(project, TrackType::HiHat);
    expect(hat != nullptr, "Drill reference hat smoke requires a HiHat track.");

    std::mt19937 rng(7171);
    generator.generate(*hat, project, plan, rng);
    validator.validate(project, plan, { TrackType::HiHat });

    hat = findTrackByType(project, TrackType::HiHat);
    expect(hat != nullptr, "Drill reference hat smoke requires a HiHat track after validation.");

    juce::StringArray missingPositions;
    for (int step = 0; step < 12; ++step)
    {
        if (!hasNoteAtStepAndMicro(*hat, step, 60))
            missingPositions.add("step=" + juce::String(step) + ",micro=60");
    }

    expect(missingPositions.isEmpty(),
           "Drill hats should preserve exact saved reference microtiming across the full engine path. Missing: "
               + missingPositions.joinIntoString(" | "));

    int copiedNotes = 0;
    for (const auto& note : hat->notes)
        if (note.semanticRole == "drill_hat_reference_copy")
            ++copiedNotes;
    expect(copiedNotes >= 12,
           "Drill hats should copy dense reference patterns instead of trimming them back to the old procedural ceiling.");
}

    void testDrillHatValidatorProximitySmoke()
    {
        DrillPatternValidator validator;

        auto project = createDefaultProject();
        project.params.genre = GenreType::Drill;
        project.params.bars = 1;
        project.params.drillSubstyle = 0;

        auto plan = DrillPhrasePlanner::buildPlan(project);
        auto* hat = findTrackByType(project, TrackType::HiHat);
        expect(hat != nullptr, "Drill hat proximity smoke requires a HiHat track.");

        hat->notes.clear();
        hat->notes.push_back({ 42, 0, 1, 92, 60, false, "drill_hat_reference_copy", false, false, false });
        hat->notes.push_back({ 42, 0, 1, 70, 90, false, "drill_hat_subdivision", false, false, false });
        hat->notes.push_back({ 42, 6, 1, 86, 0, false, "drill_hat_backbone", false, false, false });
        hat->notes.push_back({ 42, 6, 1, 68, 60, false, "drill_hat_burst", false, false, false });
        hat->notes.push_back({ 42, 1, 1, 74, 90, false, "drill_hat_transition", false, false, false });

        validator.validate(project, plan, { TrackType::HiHat });

        hat = findTrackByType(project, TrackType::HiHat);
        expect(hat != nullptr, "Drill hat proximity smoke requires a HiHat track after validation.");

        expect(hasNoteAt(*hat, 0, 60, "drill_hat_reference_copy"),
            "Drill hat validator must preserve copied reference hats as protected material.");
        expect(!hasNoteAt(*hat, 0, 90, "drill_hat_subdivision"),
            "Drill hat validator should remove procedural hats that crowd a copied reference hit inside the local window.");
        expect(hasNoteAt(*hat, 6, 0, "drill_hat_backbone"),
            "Drill hat validator must preserve the backbone carrier note.");
        expect(!hasNoteAt(*hat, 6, 60, "drill_hat_burst"),
            "Drill hat validator should remove burst notes that sit too close to a protected backbone carrier.");
        expect(hasNoteAt(*hat, 1, 90, "drill_hat_transition"),
            "Drill hat validator should keep procedural hats that stay outside protected proximity windows.");
    }

    void testDrillHatCopyMostlyStillVariesSmoke()
    {
        DrillHatGenerator generator;
        DrillPatternValidator validator;

        auto project = createDefaultProject();
        project.params.genre = GenreType::Drill;
        project.params.bars = 4;
        project.params.seed = 9191;
        project.params.densityAmount = 0.72f;
        project.params.timingAmount = 0.42f;
        project.params.drillSubstyle = 0;
        project.styleInfluence.hatMotionWeight = 1.3f;
        project.styleInfluence.drillHatTripletWeight = 1.55f;
        project.styleInfluence.drillHatBurstWeight = 1.15f;
        project.styleInfluence.drillHatGapIntentWeight = 0.85f;
        project.styleInfluence.drillHatAccentPatternWeight = 1.2f;
        project.styleInfluence.drillHatDensityVariationWeight = 1.3f;

        ReferenceHatSkeleton skeleton;
        skeleton.available = true;
        skeleton.sourceBars = 1;
        skeleton.sourceId = "drill-dense-reference";

        ReferenceHatBarSkeleton barMap;
        barMap.barIndex = 0;
        barMap.hasBarStartAnchor = true;
        for (int step = 0; step < 12; ++step)
            barMap.notes.push_back({ step * 240 + 60, 92 + (step % 3) * 6 });
        barMap.backboneSteps16 = { 0, 3, 6, 8, 11, 14 };
        barMap.motionSteps32 = { 1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23 };
        barMap.phraseAnchorSteps32 = { 0, 16, 28 };
        barMap.preSnareZoneSteps32 = { 12, 13, 14 };
        skeleton.barMaps.push_back(barMap);

        ReferenceHatCorpus corpus;
        corpus.available = true;
        corpus.sourceReferenceCount = 1;
        corpus.variants.push_back(skeleton);
        project.styleInfluence.referenceHatSkeleton = skeleton;
        project.styleInfluence.referenceHatCorpus = corpus;

        auto plan = DrillPhrasePlanner::buildPlan(project);
        auto* hat = findTrackByType(project, TrackType::HiHat);
        expect(hat != nullptr, "Drill copy-mostly variation smoke requires a HiHat track.");

        std::mt19937 rng(9191);
        generator.generate(*hat, project, plan, rng);
        validator.validate(project, plan, { TrackType::HiHat });

        hat = findTrackByType(project, TrackType::HiHat);
        expect(hat != nullptr, "Drill copy-mostly variation smoke requires a HiHat track after validation.");

        int copiedNotes = 0;
        int dynamicNotes = 0;
        for (const auto& note : hat->notes)
        {
            if (note.semanticRole == "drill_hat_reference_copy")
                ++copiedNotes;
            if (note.semanticRole == "drill_hat_transition" || note.semanticRole == "drill_hat_triplet" || note.semanticRole == "drill_hat_burst")
                ++dynamicNotes;
        }

        expect(copiedNotes >= 40,
               "Drill copy-mostly hats should still keep the dense saved reference foundation across the phrase.");
        expect(dynamicNotes >= 1,
               "Drill copy-mostly hats should still allow controlled phrase-level variation instead of freezing into a pure reference-only clone.");
    }

    void testDrillHatReferenceVariantRotationSmoke()
    {
        DrillHatGenerator generator;
        DrillPatternValidator validator;

        auto project = createDefaultProject();
        project.params.genre = GenreType::Drill;
        project.params.bars = 3;
        project.params.seed = 0;
        project.params.densityAmount = 0.66f;
        project.params.timingAmount = 0.24f;
        project.params.drillSubstyle = 0;

        ReferenceHatCorpus corpus;
        corpus.available = true;
        corpus.sourceReferenceCount = 3;

        for (int variantIndex = 0; variantIndex < 3; ++variantIndex)
        {
            ReferenceHatSkeleton skeleton;
            skeleton.available = true;
            skeleton.sourceBars = 1;
            skeleton.sourceId = "drill-hat-variant-" + juce::String(variantIndex);

            ReferenceHatBarSkeleton barMap;
            barMap.barIndex = 0;
            barMap.hasBarStartAnchor = true;
            const int micro = 20 + variantIndex * 40;
            for (int step = 0; step < 6; ++step)
                barMap.notes.push_back({ step * HiResTiming::kTicks1_16 + micro, 90 + variantIndex * 4 });
            barMap.backboneSteps16 = { 0, 3, 6, 8, 11, 14 };
            skeleton.barMaps.push_back(barMap);
            corpus.variants.push_back(skeleton);
        }

        project.styleInfluence.referenceHatCorpus = corpus;
        project.styleInfluence.referenceHatSkeleton = corpus.variants.front();

        auto plan = DrillPhrasePlanner::buildPlan(project);
        auto* hat = findTrackByType(project, TrackType::HiHat);
        expect(hat != nullptr, "Drill hat reference rotation smoke requires a HiHat track.");

        std::mt19937 rng(0);
        generator.generate(*hat, project, plan, rng);
        validator.validate(project, plan, { TrackType::HiHat });

        hat = findTrackByType(project, TrackType::HiHat);
        expect(hat != nullptr, "Drill hat reference rotation smoke requires a HiHat track after validation.");

        expect(hasNoteAt(*hat, 0, 20, "drill_hat_reference_copy"),
               "Drill hats should use the first saved reference variant on the first bar when rotating through multiple references.");
        expect(hasNoteAt(*hat, 16, 60, "drill_hat_reference_copy"),
               "Drill hats should rotate to the second saved reference variant on the next bar instead of reusing the first one again.");
        expect(hasNoteAt(*hat, 32, 100, "drill_hat_reference_copy"),
               "Drill hats should rotate to the third saved reference variant on the third bar instead of collapsing all references into one pattern.");
    }

void testDrillKickGeneratorUsesReferenceCorpusSmoke()
{
    DrillKickGenerator generator;

    auto project = createDefaultProject();
    project.params.genre = GenreType::Drill;
    project.params.bars = 1;
    project.params.seed = 7272;
    project.params.densityAmount = 0.54f;
    project.params.drillSubstyle = 0;

    ReferenceKickBarPattern barPattern;
    barPattern.barIndex = 0;
    barPattern.notes.push_back({ 0, 118 });
    barPattern.notes.push_back({ 10, 108 });
    barPattern.notes.push_back({ 14, 112 });

    ReferenceKickPattern pattern;
    pattern.available = true;
    pattern.sourceBars = 1;
    pattern.barPatterns.push_back(barPattern);

    ReferenceKickCorpus corpus;
    corpus.available = true;
    corpus.sourceReferenceCount = 1;
    corpus.variants.push_back(pattern);
    project.styleInfluence.referenceKickCorpus = corpus;

    auto plan = DrillPhrasePlanner::buildPlan(project);
    auto* kick = findTrackByType(project, TrackType::Kick);
    expect(kick != nullptr, "Drill reference kick smoke requires a Kick track.");

    std::mt19937 rng(7272);
    generator.generate(*kick, project, plan, nullptr, rng);

    int retainedReferenceSteps = 0;
    for (const int step : { 0, 10, 14 })
    {
        if (std::any_of(kick->notes.begin(), kick->notes.end(), [step](const NoteEvent& note)
        {
            return note.step == step;
        }))
        {
            ++retainedReferenceSteps;
        }
    }

    expect(retainedReferenceSteps >= 2,
           "Drill kick generator should retain roughly 60-70% of the saved reference kick pattern, not just treat it as a weak hint.");
}

void testDrillKickReferenceVariantRotationSmoke()
{
    DrillKickGenerator generator;

    auto project = createDefaultProject();
    project.params.genre = GenreType::Drill;
    project.params.bars = 3;
    project.params.seed = 0;
    project.params.drillSubstyle = 0;

    ReferenceKickCorpus corpus;
    corpus.available = true;
    corpus.sourceReferenceCount = 3;

    for (int variantIndex = 0; variantIndex < 3; ++variantIndex)
    {
        ReferenceKickPattern pattern;
        pattern.available = true;
        pattern.sourceBars = 1;
        const std::array<int, 3> supportSteps { 4, 10, 12 };
        const int supportStep = supportSteps[static_cast<size_t>(variantIndex)];
        pattern.barPatterns.push_back({ 0, { { 0, 116 }, { supportStep, 108 } } });
        corpus.variants.push_back(pattern);
    }

    project.styleInfluence.referenceKickCorpus = corpus;

    DrillPhrasePlan plan;
    plan.phraseSpanBars = 3;
    plan.substyleIndex = 0;
    for (int barIndex = 0; barIndex < 3; ++barIndex)
    {
        DrillPhraseBarPlan bar;
        bar.barIndex = barIndex;
        bar.role = DrillPhraseBarRole::Statement;
        bar.kickDensity = DrillKickDensityIntent::Medium;
        bar.lowEnd = DrillLowEndIntent::Anchor;
        bar.anchorMap.kickAnchorSteps = { { 0, -1, -1, -1 } };
        bar.anchorMap.snareAnchorSteps = { { 8, -1 } };
        bar.anchorMap.lowEndAnchorSteps = { { 0, 6, 10, 14 } };
        plan.bars.push_back(bar);
    }

    auto* kick = findTrackByType(project, TrackType::Kick);
    expect(kick != nullptr, "Drill kick reference rotation smoke requires a Kick track.");

    std::mt19937 rng(0);
    generator.generate(*kick, project, plan, nullptr, rng);

    expect(std::any_of(kick->notes.begin(), kick->notes.end(), [](const NoteEvent& note) { return note.step == 4; }),
           "Drill kick rotation should use the first saved kick reference variant on the first bar.");
        expect(std::any_of(kick->notes.begin(), kick->notes.end(), [](const NoteEvent& note) { return note.step == 26; }),
           "Drill kick rotation should use the second saved kick reference variant on the second bar instead of repeating the first one.");
    expect(std::any_of(kick->notes.begin(), kick->notes.end(), [](const NoteEvent& note) { return note.step == 44; }),
           "Drill kick rotation should use the third saved kick reference variant on the third bar instead of collapsing everything into one kick pattern.");
}

void testDrill808GeneratorUsesReferenceCorpusSmoke()
{
    Drill808Generator generator;

    auto project = createDefaultProject();
    project.params.genre = GenreType::Drill;
    project.params.bars = 1;
    project.params.seed = 7373;
    project.params.keyRoot = 0;
    project.params.scaleMode = 0;
    project.params.drillSubstyle = 0;
    project.styleInfluence.lowEndCouplingWeight = 2.0f;
    laneBiasFor(project.styleInfluence, TrackType::Sub808).activityWeight = 1.6f;

    ReferenceKickBarPattern barPattern;
    barPattern.barIndex = 0;
    barPattern.notes.push_back({ 12, 114 });

    ReferenceKickPattern pattern;
    pattern.available = true;
    pattern.sourceBars = 1;
    pattern.barPatterns.push_back(barPattern);

    ReferenceKickCorpus corpus;
    corpus.available = true;
    corpus.sourceReferenceCount = 1;
    corpus.variants.push_back(pattern);
    project.styleInfluence.referenceKickCorpus = corpus;

    auto plan = DrillPhrasePlanner::buildPlan(project);
    expect(!plan.bars.empty() && plan.bars.front().lowEnd == DrillLowEndIntent::Move,
           "Drill 808 reference smoke requires a Move low-end bar to test extra reference starts.");

    auto* kick = findTrackByType(project, TrackType::Kick);
    auto* sub = findTrackByType(project, TrackType::Sub808);
    expect(kick != nullptr && sub != nullptr,
           "Drill 808 reference smoke requires Kick and Sub808 tracks.");

    kick->notes.clear();
    kick->notes.push_back({ 36, 0, 1, 112, 0, false, "drill_kick_anchor", false, false, false });

    std::mt19937 rng(7373);
    generator.generate(*sub, *kick, project, plan, nullptr, rng);

    expect(hasSubStartAt(*sub, 12, "drill_sub_move"),
           "Drill 808 generator should add reference-driven low-end starts even when the kick track stays sparse.");
}

void testDrill808GenerationCompactSmoke()
{
    Drill808Generator generator;

    auto project = createDefaultProject();
    project.params.genre = GenreType::Drill;
    project.params.bars = 4;
    project.params.seed = 8484;
    project.params.keyRoot = 0;
    project.params.scaleMode = 0;
    project.params.drillSubstyle = 0;
    project.styleInfluence.lowEndCouplingWeight = 1.7f;

    ReferenceKickPattern pattern;
    pattern.available = true;
    pattern.sourceBars = 4;
    pattern.barPatterns.push_back({ 0, { { 0, 116 }, { 10, 104 } } });
    pattern.barPatterns.push_back({ 1, { { 0, 112 }, { 9, 108 }, { 12, 110 } } });
    pattern.barPatterns.push_back({ 2, { { 0, 114 }, { 8, 106 }, { 13, 108 } } });
    pattern.barPatterns.push_back({ 3, { { 0, 112 }, { 14, 110 } } });

    ReferenceKickCorpus corpus;
    corpus.available = true;
    corpus.sourceReferenceCount = 1;
    corpus.variants.push_back(pattern);
    project.styleInfluence.referenceKickCorpus = corpus;

    auto* kick = findTrackByType(project, TrackType::Kick);
    auto* sub = findTrackByType(project, TrackType::Sub808);
    expect(kick != nullptr && sub != nullptr,
           "Drill 808 compactness smoke requires Kick and Sub808 tracks.");

    kick->notes.clear();
    kick->notes.push_back({ 36, 0, 1, 112, 0, false, "drill_kick_anchor", false, false, false });
    kick->notes.push_back({ 36, 10, 1, 104, 0, false, "drill_kick_support", false, false, false });
    kick->notes.push_back({ 36, 16, 1, 112, 0, false, "drill_kick_anchor", false, false, false });
    kick->notes.push_back({ 36, 23, 1, 106, 0, false, "drill_kick_support", false, false, false });
    kick->notes.push_back({ 36, 28, 1, 108, 0, false, "drill_kick_support", false, false, false });
    kick->notes.push_back({ 36, 32, 1, 114, 0, false, "drill_kick_anchor", false, false, false });
    kick->notes.push_back({ 36, 39, 1, 106, 0, false, "drill_kick_support", false, false, false });
    kick->notes.push_back({ 36, 46, 1, 108, 0, false, "drill_kick_support", false, false, false });
    kick->notes.push_back({ 36, 48, 1, 112, 0, false, "drill_kick_anchor", false, false, false });
    kick->notes.push_back({ 36, 62, 1, 110, 0, false, "drill_kick_support", false, false, false });

    DrillPhrasePlan plan;
    plan.phraseSpanBars = 4;
    plan.substyleIndex = 0;

    DrillPhraseBarPlan statement;
    statement.barIndex = 0;
    statement.role = DrillPhraseBarRole::Statement;
    statement.anchorMap.snareAnchorSteps = { { 8, -1 } };
    statement.anchorMap.lowEndAnchorSteps = { { 0, 6, 10, 14 } };
    statement.lowEnd = DrillLowEndIntent::Anchor;
    plan.bars.push_back(statement);

    DrillPhraseBarPlan response;
    response.barIndex = 1;
    response.role = DrillPhraseBarRole::Response;
    response.anchorMap.snareAnchorSteps = { { 12, -1 } };
    response.anchorMap.lowEndAnchorSteps = { { 0, 7, 10, 12 } };
    response.lowEnd = DrillLowEndIntent::Move;
    plan.bars.push_back(response);

    DrillPhraseBarPlan lift;
    lift.barIndex = 2;
    lift.role = DrillPhraseBarRole::Lift;
    lift.anchorMap.snareAnchorSteps = { { 12, -1 } };
    lift.anchorMap.lowEndAnchorSteps = { { 0, 5, 9, 12 } };
    lift.lowEnd = DrillLowEndIntent::Move;
    plan.bars.push_back(lift);

    DrillPhraseBarPlan release;
    release.barIndex = 3;
    release.role = DrillPhraseBarRole::Release;
    release.anchorMap.snareAnchorSteps = { { 8, 12 } };
    release.anchorMap.lowEndAnchorSteps = { { 0, 8, 12, 14 } };
    release.lowEnd = DrillLowEndIntent::Release;
    plan.bars.push_back(release);

    std::mt19937 rng(8484);
    generator.generate(*sub, *kick, project, plan, nullptr, rng);

    for (const auto& bar : plan.bars)
    {
        int startsInBar = 0;
        for (const auto& note : sub->sub808Notes)
            if ((note.step / 16) == bar.barIndex)
                ++startsInBar;

        const int maxStarts = (bar.lowEnd == DrillLowEndIntent::Move || bar.lowEnd == DrillLowEndIntent::Release) ? 2 : 1;
        expect(startsInBar <= maxStarts,
               "Drill 808 generation should stay compact per bar instead of inheriting every kick/reference support start.");
    }

    int slideCount = 0;
    for (size_t index = 0; index < sub->sub808Notes.size(); ++index)
    {
        const auto& note = sub->sub808Notes[index];
        if (!note.glideToNext)
            continue;

        ++slideCount;
        expect(index + 1 < sub->sub808Notes.size(),
               "Drill 808 glide markers must always point to a following note.");
        expect((note.step / 16) == (sub->sub808Notes[index + 1].step / 16),
               "Drill 808 slides should stay inside the same bar after the compact low-end pass.");
    }

    expect(slideCount <= 1,
           "Drill 808 generation should keep slide count low after simplifying low-end expansion.");
}

    void testDrillValidatorCompactnessSmoke()
    {
        DrillPatternValidator validator;

        auto project = createDefaultProject();
        project.params.genre = GenreType::Drill;
        project.params.bars = 4;
        project.params.keyRoot = 0;
        project.params.scaleMode = 0;

        auto* kick = findTrackByType(project, TrackType::Kick);
        auto* sub = findTrackByType(project, TrackType::Sub808);
        expect(kick != nullptr && sub != nullptr,
            "Drill validator compactness smoke requires Kick and Sub808 tracks.");

        kick->enabled = true;
        sub->enabled = true;

        DrillPhrasePlan plan;
        plan.phraseSpanBars = 4;
        plan.substyleIndex = 0;

        DrillPhraseBarPlan statement;
        statement.barIndex = 0;
        statement.role = DrillPhraseBarRole::Statement;
        statement.anchorMap.kickAnchorSteps = { { 0, 10, -1, -1 } };
        statement.anchorMap.snareAnchorSteps = { { 8, -1 } };
        statement.anchorMap.lowEndAnchorSteps = { { 0, 6, 10, 14 } };
        statement.lowEnd = DrillLowEndIntent::Anchor;
        plan.bars.push_back(statement);

        DrillPhraseBarPlan response;
        response.barIndex = 1;
        response.role = DrillPhraseBarRole::Response;
        response.anchorMap.kickAnchorSteps = { { 0, 7, -1, -1 } };
        response.anchorMap.snareAnchorSteps = { { 12, -1 } };
        response.anchorMap.lowEndAnchorSteps = { { 0, 7, 10, 12 } };
        response.lowEnd = DrillLowEndIntent::Move;
        plan.bars.push_back(response);

        DrillPhraseBarPlan lift;
        lift.barIndex = 2;
        lift.role = DrillPhraseBarRole::Lift;
        lift.anchorMap.kickAnchorSteps = { { 0, 9, 13, -1 } };
        lift.anchorMap.snareAnchorSteps = { { 12, -1 } };
        lift.anchorMap.lowEndAnchorSteps = { { 0, 5, 9, 12 } };
        lift.lowEnd = DrillLowEndIntent::Move;
        plan.bars.push_back(lift);

        DrillPhraseBarPlan release;
        release.barIndex = 3;
        release.role = DrillPhraseBarRole::Release;
        release.anchorMap.kickAnchorSteps = { { 0, 11, 15, -1 } };
        release.anchorMap.snareAnchorSteps = { { 8, 12 } };
        release.anchorMap.lowEndAnchorSteps = { { 0, 8, 12, 14 } };
        release.lowEnd = DrillLowEndIntent::Release;
        plan.bars.push_back(release);

        kick->notes.clear();
        kick->notes.push_back({ 36, 0, 1, 112, 0, false, "drill_kick_anchor", false, false, false });
        kick->notes.push_back({ 36, 4, 1, 96, 0, false, "drill_kick_support", false, false, false });
        kick->notes.push_back({ 36, 10, 1, 104, 0, false, "drill_kick_support", false, false, false });
        kick->notes.push_back({ 36, 16, 1, 112, 0, false, "drill_kick_anchor", false, false, false });
        kick->notes.push_back({ 36, 20, 1, 96, 0, false, "drill_kick_support", false, false, false });
        kick->notes.push_back({ 36, 23, 1, 102, 0, false, "drill_kick_support", false, false, false });
        kick->notes.push_back({ 36, 28, 1, 106, 0, false, "drill_kick_support", false, false, false });
        kick->notes.push_back({ 36, 32, 1, 114, 0, false, "drill_kick_anchor", false, false, false });
        kick->notes.push_back({ 36, 35, 1, 98, 0, false, "drill_kick_support", false, false, false });
        kick->notes.push_back({ 36, 39, 1, 104, 0, false, "drill_kick_support", false, false, false });
        kick->notes.push_back({ 36, 46, 1, 108, 0, false, "drill_kick_support", false, false, false });
        kick->notes.push_back({ 36, 48, 1, 112, 0, false, "drill_kick_anchor", false, false, false });
        kick->notes.push_back({ 36, 52, 1, 96, 0, false, "drill_kick_support", false, false, false });
        kick->notes.push_back({ 36, 57, 1, 102, 0, false, "drill_kick_support", false, false, false });
        kick->notes.push_back({ 36, 62, 1, 110, 0, false, "drill_kick_support", false, false, false });

        sub->sub808Notes.clear();
        sub->sub808Notes.push_back({ 24, 0, 4, 100, 0, "drill_sub_anchor", false, false, false });
        sub->sub808Notes.push_back({ 27, 6, 2, 94, 0, "drill_sub_anchor", false, false, false });
        sub->sub808Notes.push_back({ 24, 16, 3, 102, 0, "drill_sub_move", false, false, false });
        sub->sub808Notes.push_back({ 31, 20, 2, 92, 0, "drill_sub_move", false, false, false });
        sub->sub808Notes.push_back({ 27, 28, 2, 96, 0, "drill_sub_move", true, true, false });
        sub->sub808Notes.push_back({ 24, 32, 3, 104, 0, "drill_sub_move", false, false, false });
        sub->sub808Notes.push_back({ 29, 36, 2, 90, 0, "drill_sub_move", false, false, false });
        sub->sub808Notes.push_back({ 31, 40, 2, 92, 0, "drill_sub_move", false, false, true });
        sub->sub808Notes.push_back({ 24, 48, 5, 106, 0, "drill_sub_release", false, false, false });
        sub->sub808Notes.push_back({ 27, 56, 3, 94, 0, "drill_sub_release", false, false, false });
        sub->sub808Notes.push_back({ 31, 62, 2, 96, 0, "drill_sub_release", false, false, false });

        validator.validate(project, plan, { TrackType::Kick, TrackType::Sub808 });

        kick = findTrackByType(project, TrackType::Kick);
        sub = findTrackByType(project, TrackType::Sub808);
        expect(kick != nullptr && sub != nullptr,
            "Drill validator compactness smoke requires Kick and Sub808 tracks after validation.");

        for (const auto& bar : plan.bars)
        {
         int kickCount = 0;
         int subCount = 0;
         for (const auto& note : kick->notes)
             if ((note.step / 16) == bar.barIndex)
              ++kickCount;
         for (const auto& note : sub->sub808Notes)
             if ((note.step / 16) == bar.barIndex)
              ++subCount;

         expect(kickCount <= (bar.role == DrillPhraseBarRole::Lift || bar.role == DrillPhraseBarRole::Release ? 3 : 2),
             "Drill validator should clamp kick bars back to the compact Drill template limits.");
         expect(subCount <= ((bar.lowEnd == DrillLowEndIntent::Move || bar.lowEnd == DrillLowEndIntent::Release) ? 2 : 1),
             "Drill validator should clamp low-end starts back to the compact Drill limits.");
        }

        int glideCount = 0;
        for (size_t index = 0; index < sub->sub808Notes.size(); ++index)
        {
         const auto& note = sub->sub808Notes[index];
         if (!note.glideToNext)
             continue;

         ++glideCount;
         expect(index + 1 < sub->sub808Notes.size(),
             "Drill validator glide cleanup must leave each glide attached to a following note.");
         expect((note.step / 16) == (sub->sub808Notes[index + 1].step / 16),
             "Drill validator should remove cross-bar low-end slides.");
        }

        expect(glideCount <= 1,
            "Drill validator should keep the surviving low-end slides restrained.");
    }

void testDrillSnareGenerationSmoke()
{
    DrillSnareGenerator generator;

    auto project = createDefaultProject();
    project.params.genre = GenreType::Drill;
    project.params.bars = 4;

    auto* snare = findTrackByType(project, TrackType::Snare);
    auto* clapGhost = findTrackByType(project, TrackType::ClapGhostSnare);
    expect(snare != nullptr && clapGhost != nullptr,
           "Drill snare generation smoke requires both snare and clap/ghost lanes.");

    DrillPhrasePlan plan;
    plan.phraseSpanBars = 4;
    plan.substyleIndex = 0;

    DrillPhraseBarPlan statement;
    statement.barIndex = 0;
    statement.role = DrillPhraseBarRole::Statement;
    statement.anchorMap.snareAnchorSteps = { { 8, -1 } };
    statement.anchorMap.supportAccentSteps = { { 6, 7, 9, 10 } };
    statement.supportAccent = DrillSupportAccentIntent::Light;
    plan.bars.push_back(statement);

    DrillPhraseBarPlan response;
    response.barIndex = 1;
    response.role = DrillPhraseBarRole::Response;
    response.anchorMap.snareAnchorSteps = { { 12, -1 } };
    response.anchorMap.supportAccentSteps = { { 10, 11, 13, 14 } };
    response.supportAccent = DrillSupportAccentIntent::Drag;
    plan.bars.push_back(response);

    DrillPhraseBarPlan lift;
    lift.barIndex = 2;
    lift.role = DrillPhraseBarRole::Lift;
    lift.anchorMap.snareAnchorSteps = { { 12, -1 } };
    lift.anchorMap.supportAccentSteps = { { 10, 11, 13, 14 } };
    lift.supportAccent = DrillSupportAccentIntent::Push;
    plan.bars.push_back(lift);

    DrillPhraseBarPlan release;
    release.barIndex = 3;
    release.role = DrillPhraseBarRole::Release;
    release.anchorMap.snareAnchorSteps = { { 8, 12 } };
    release.anchorMap.supportAccentSteps = { { 6, 7, 9, 10 } };
    release.supportAccent = DrillSupportAccentIntent::Drag;
    plan.bars.push_back(release);

    std::mt19937 rng(8181);
    generator.generate(*snare, clapGhost, project, plan, rng);

    for (const auto& bar : plan.bars)
    {
        int clapLayersInBar = 0;
        int ghostsInBar = 0;
        const int primarySnare = bar.anchorMap.snareAnchorSteps[0];

        for (const auto& note : clapGhost->notes)
        {
            if ((note.step / 16) != bar.barIndex)
                continue;

            const int stepInBar = note.step % 16;
            if (note.semanticRole == "drill_clap_layer")
            {
                ++clapLayersInBar;
                expect(matchesSnareAnchorStep(bar.anchorMap.snareAnchorSteps, stepInBar),
                       "Drill snare generator must place clap layers exactly on snare backbone anchors.");
            }
            else if (note.semanticRole == "drill_snare_ghost")
            {
                ++ghostsInBar;
                expect(primarySnare >= 0, "Drill snare generator ghost validation requires a primary snare anchor.");
                expect(std::abs(stepInBar - primarySnare) >= 1 && std::abs(stepInBar - primarySnare) <= 2,
                       "Drill snare generator ghosts must stay within a tight one- or two-step support window around the primary snare.");
                if (bar.supportAccent == DrillSupportAccentIntent::Push)
                    expect(stepInBar < primarySnare,
                           "Drill snare generator push ghosts must land before the primary snare.");
                if (bar.supportAccent == DrillSupportAccentIntent::Drag)
                    expect(stepInBar > primarySnare,
                           "Drill snare generator drag ghosts must land after the primary snare.");
            }
        }

        expect(!(clapLayersInBar > 0 && ghostsInBar > 0),
               "Drill snare generator should choose one decoration mode per bar instead of stacking clap layers and ghosts together.");
        expect(ghostsInBar <= 1,
               "Drill snare generator should emit at most one support ghost per bar.");
        expect(clapLayersInBar <= activeSnareAnchorCount(bar.anchorMap.snareAnchorSteps),
               "Drill snare generator should keep clap layers bounded by the bar backbone anchors.");
    }
}

void testDrillFullEngineSmoke()
{
    DrillEngine engine;

    auto project = createDefaultProject();
    project.params.genre = GenreType::Drill;
    project.params.bars = 4;
    project.params.seed = 5151;
    project.params.densityAmount = 0.68f;
    project.params.timingAmount = 0.42f;
    project.params.keyRoot = 2;
    project.params.scaleMode = 0;
    project.params.drillSubstyle = 0;
    project.styleInfluence.hatMotionWeight = 1.25f;
    project.styleInfluence.lowEndCouplingWeight = 1.35f;
    project.styleInfluence.supportAccentWeight = 1.2f;
    project.styleInfluence.drillHatTripletWeight = 1.35f;
    project.styleInfluence.drillHatBurstWeight = 1.1f;
    project.styleInfluence.drillHatGapIntentWeight = 0.7f;
    project.styleInfluence.drillHatAccentPatternWeight = 1.15f;
    project.styleInfluence.drillHatDensityVariationWeight = 1.25f;

    auto* hat = findTrackByType(project, TrackType::HiHat);
    auto* hatFx = findTrackByType(project, TrackType::HatFX);
    auto* snare = findTrackByType(project, TrackType::Snare);
    auto* clapGhost = findTrackByType(project, TrackType::ClapGhostSnare);
    auto* kick = findTrackByType(project, TrackType::Kick);
    auto* sub = findTrackByType(project, TrackType::Sub808);
    expect(hat != nullptr && hatFx != nullptr && snare != nullptr && clapGhost != nullptr && kick != nullptr && sub != nullptr,
           "Drill full engine smoke requires all core Drill lanes.");

        hatFx->enabled = true;
        sub->enabled = true;

    engine.generate(project);

    hat = findTrackByType(project, TrackType::HiHat);
    hatFx = findTrackByType(project, TrackType::HatFX);
    snare = findTrackByType(project, TrackType::Snare);
    clapGhost = findTrackByType(project, TrackType::ClapGhostSnare);
    kick = findTrackByType(project, TrackType::Kick);
    sub = findTrackByType(project, TrackType::Sub808);
    expect(hat != nullptr && hatFx != nullptr && snare != nullptr && clapGhost != nullptr && kick != nullptr && sub != nullptr,
        "Drill full engine smoke requires all core Drill lanes after generation.");

    const auto plan = DrillPhrasePlanner::buildPlan(project);

    expect(!hat->notes.empty(), "Drill full engine should keep main hats active.");
    expect(!hatFx->notes.empty(), "Drill full engine should generate HatFX support material.");
    expect(!snare->notes.empty(), "Drill full engine should generate the main snare lane.");
    expect(!kick->notes.empty(), "Drill full engine should generate kick anchors.");
    expect(!sub->sub808Notes.empty(), "Drill full engine should generate sub808 notes.");
    expect(hatFx->notes.size() < hat->notes.size(), "HatFX must remain sparser than the main hihat lane.");
    expect(snare->laneRole == "drill_snare", "Drill full engine should set the snare lane role.");
    expect(clapGhost->laneRole == "drill_clap_ghost", "Drill full engine should set the clap/ghost lane role.");
    expect(kick->laneRole == "drill_kick", "Drill full engine should set the kick lane role.");
    expect(sub->laneRole == "drill_sub", "Drill full engine should set the sub lane role.");

    for (const auto& bar : plan.bars)
    {
        for (const int snareStep : bar.anchorMap.snareAnchorSteps)
        {
            if (snareStep < 0)
                continue;

            expect(hasNoteAt(*snare,
                             bar.barIndex * 16 + snareStep,
                             0,
                             "drill_snare_backbone"),
                   "Drill full engine must preserve planned snare anchors.");
        }

        int hatFxBarCount = 0;
        int clapGhostBarCount = 0;
        int clapLayersInBar = 0;
        int snareGhostsInBar = 0;
        int kickBarCount = 0;
        int subBarCount = 0;
        for (const auto& note : hatFx->notes)
            if ((note.step / 16) == bar.barIndex)
                ++hatFxBarCount;
        for (const auto& note : clapGhost->notes)
        {
            if ((note.step / 16) != bar.barIndex)
                continue;

                ++clapGhostBarCount;
            const int stepInBar = note.step % 16;
            if (note.semanticRole == "drill_clap_layer")
            {
                ++clapLayersInBar;
                expect(matchesSnareAnchorStep(bar.anchorMap.snareAnchorSteps, stepInBar),
                       "Drill clap layers must stay exactly on planned snare anchors after validation.");
            }
            else if (note.semanticRole == "drill_snare_ghost")
            {
                ++snareGhostsInBar;
                const int primarySnare = bar.anchorMap.snareAnchorSteps[0];
                expect(primarySnare >= 0, "Drill ghost validation requires a primary snare anchor.");
                expect(std::abs(stepInBar - primarySnare) >= 1 && std::abs(stepInBar - primarySnare) <= 2,
                       "Drill ghosts must stay inside the tight support window around the primary snare after validation.");
                if (bar.supportAccent == DrillSupportAccentIntent::Push)
                    expect(stepInBar < primarySnare,
                           "Drill push ghosts must remain before the primary snare after validation.");
                if (bar.supportAccent == DrillSupportAccentIntent::Drag)
                    expect(stepInBar > primarySnare,
                           "Drill drag ghosts must remain after the primary snare after validation.");
            }
            else
            {
                expect(false, "Drill clap/ghost lane should only contain clap layers or snare ghosts.");
            }
        }
        for (const auto& note : kick->notes)
            if ((note.step / 16) == bar.barIndex)
                ++kickBarCount;
         for (const auto& note : sub->sub808Notes)
             if ((note.step / 16) == bar.barIndex)
              ++subBarCount;

        expect(hatFxBarCount <= 3, "Drill HatFX should stay sparse per bar.");
        expect(!(clapLayersInBar > 0 && snareGhostsInBar > 0),
               "Drill clap/ghost support should resolve to either layered backbone reinforcement or a single ghost, not both.");
        expect(snareGhostsInBar <= 1, "Drill clap/ghost support should keep at most one ghost per bar.");
        expect(clapGhostBarCount <= activeSnareAnchorCount(bar.anchorMap.snareAnchorSteps),
               "Drill clap/ghost support should stay bounded by the number of planned snare anchors in the bar.");
        expect(kickBarCount >= 1, "Drill kick lane should keep at least one hit per bar after validation.");
         expect(kickBarCount <= (bar.role == DrillPhraseBarRole::Lift || bar.role == DrillPhraseBarRole::Release ? 3 : 2),
             "Drill kick lane should stay on compact role-based templates instead of drifting into dense permissive bar fills.");
         expect(subBarCount <= ((bar.lowEnd == DrillLowEndIntent::Move || bar.lowEnd == DrillLowEndIntent::Release) ? 2 : 1),
             "Drill sub808 should stay compact per bar instead of echoing every support hit.");
    }

    for (const auto& kickNote : kick->notes)
    {
        const int barIndex = kickNote.step / 16;
        const int stepInBar = kickNote.step % 16;
        expect(barIndex >= 0 && barIndex < static_cast<int>(plan.bars.size()), "Kick note bar index must remain valid.");
        for (const int snareStep : plan.bars[static_cast<size_t>(barIndex)].anchorMap.snareAnchorSteps)
            expect(snareStep < 0 || stepInBar != snareStep, "Kick must not collide with a main Drill snare anchor.");
    }

    expect(sub->sub808Settings.mono, "Drill sub808 should stay mono.");
    expect(sub->sub808Settings.overlapMode == Sub808OverlapMode::Glide, "Drill sub808 should use Glide overlap mode.");
    expect(sub->notes.size() == sub->sub808Notes.size(), "Drill sub808 legacy mirror should stay synchronized.");

    int glideCount = 0;
    for (size_t index = 0; index < sub->sub808Notes.size(); ++index)
    {
        const auto& note = sub->sub808Notes[index];
        if (!note.glideToNext)
            continue;

        ++glideCount;
        expect(index + 1 < sub->sub808Notes.size(),
               "Drill sub808 glide markers must point to a following note.");
        expect((note.step / 16) == (sub->sub808Notes[index + 1].step / 16),
               "Drill sub808 glides should stay inside a single bar after low-end simplification.");
    }

    expect(glideCount <= 1, "Drill sub808 should keep slide usage restrained after low-end simplification.");

    for (size_t index = 0; index < sub->sub808Notes.size(); ++index)
    {
        const auto& note = sub->sub808Notes[index];
        expect(isPitchInScale(note.pitch, project.params.keyRoot, project.params.scaleMode),
               "Drill sub808 pitches must stay inside the selected scale.");
        expect(note.length >= 1, "Drill sub808 note length must remain positive.");
        expect(sub->notes[index].pitch == note.pitch && sub->notes[index].step == note.step,
               "Drill sub808 legacy mirror must match the Sub808 note timeline.");
    }
}

void testProjectStateBarsClamp()
{
    auto project = createDefaultProject();
    project.params.bars = 8;
    project.phraseLengthBars = 8;

    auto* kick = findTrackByType(project, TrackType::Kick);
    auto* sub = findTrackByType(project, TrackType::Sub808);
    expect(kick != nullptr && sub != nullptr, "Bars clamp test requires Kick and Sub808 tracks.");

    kick->notes.push_back({ 36, 63, 1, 110, 0, false, "tail", false, false, false });
    sub->sub808Notes.push_back({ 36, 62, 2, 100, 0, "tail808", false, false, false });
    sub->notes = toLegacyNoteEvents(sub->sub808Notes);

    ProjectStateController::setBars(project, 2);

    expect(project.params.bars == 2, "Project bars should update to requested size.");
    expect(project.phraseLengthBars == 2, "Phrase length should follow resized project bars.");
    expect(kick->notes.empty(), "Kick notes past the new bar limit should be trimmed.");
    expect(sub->sub808Notes.empty(), "Sub808 notes past the new bar limit should be trimmed.");
}

void testStyleDefinitionFallbackSmoke()
{
    const auto definition = StyleDefinitionLoader::buildFallback(GenreType::Rap, 0);
    expect(definition.genre == GenreType::Rap, "Fallback style definition should preserve requested genre.");
    expect(definition.genreName == "Rap", "Fallback style definition should expose Rap display name.");
    expect(!definition.lanes.empty(), "Fallback style definition should include runtime lanes.");

    const auto drillDefinition = StyleDefinitionLoader::buildFallback(GenreType::Drill, 0);
    expect(drillDefinition.genre == GenreType::Drill, "Fallback style definition should preserve Drill genre.");
    expect(drillDefinition.genreName == "Drill", "Fallback style definition should expose Drill display name.");
    expect(drillDefinition.substyleName == "Main", "Fallback style definition should expose the Drill Main substyle.");
}

void testDrillStyleInfluenceReferenceSmoke()
{
    auto project = createDefaultProject();
    project.params.genre = GenreType::Drill;
    project.params.drillSubstyle = 0;

    auto definition = StyleDefinitionLoader::buildFallback(GenreType::Drill, 0);
    definition.loadedFromReference = true;
    definition.styleHints.set("drill.hat_motion", 0.95f);
    definition.styleHints.set("drill.gap_intent", 0.78f);
    definition.styleHints.set("drill.support_accent", 0.72f);
    definition.styleHints.set("drill.low_end_coupling", 0.88f);
    definition.styleHints.set("drill.ref_hat_roll_length", 0.84f);
    definition.styleHints.set("drill.ref_hat_density_variation", 0.86f);
    definition.styleHints.set("drill.ref_hat_accent_alternation", 0.74f);
    definition.styleHints.set("drill.ref_hat_burst", 0.82f);
    definition.styleHints.set("drill.ref_hat_triplet", 0.90f);

    ReferenceHatSkeleton skeleton;
    skeleton.available = true;
    skeleton.sourceBars = 2;
    skeleton.sourceId = "drill-test-reference";
    skeleton.barMaps.push_back({ 0, true, { { 0, 96 }, { 240, 88 } }, { 0, 6 }, { 0, 4, 8 }, { 0, 8 }, { 6, 7 } });

    ReferenceHatCorpus hatCorpus;
    hatCorpus.available = true;
    hatCorpus.sourceReferenceCount = 1;
    hatCorpus.variants.push_back(skeleton);

    ReferenceKickPattern kickPattern;
    kickPattern.available = true;
    kickPattern.sourceBars = 2;
    kickPattern.barPatterns.push_back({ 0, { { 0, 116 }, { 10, 104 } } });

    ReferenceKickCorpus kickCorpus;
    kickCorpus.available = true;
    kickCorpus.sourceReferenceCount = 1;
    kickCorpus.variants.push_back(kickPattern);

    definition.referenceHatSkeleton = skeleton;
    definition.referenceHatCorpus = hatCorpus;
    definition.referenceKickCorpus = kickCorpus;

    juce::String error;
    expect(DrillStyleInfluence::applyResolvedStyle(definition, project, &error),
        "DrillStyleInfluence reference smoke failed: " + error);

    expect(project.styleInfluence.referenceHatSkeleton.available,
        "DrillStyleInfluence should preserve the resolved reference hat skeleton.");
    expect(project.styleInfluence.referenceHatCorpus.available && project.styleInfluence.referenceHatCorpus.sourceReferenceCount == 1,
        "DrillStyleInfluence should attach the resolved reference hat corpus.");
    expect(project.styleInfluence.referenceKickCorpus.available && project.styleInfluence.referenceKickCorpus.sourceReferenceCount == 1,
        "DrillStyleInfluence should attach the resolved reference kick corpus.");
    expect(project.styleInfluence.hatMotionWeight > 1.2f,
        "DrillStyleInfluence should raise hat motion weight from resolved Drill hints.");
    expect(project.styleInfluence.lowEndCouplingWeight > 1.2f,
        "DrillStyleInfluence should raise low-end coupling from resolved Drill hints.");
    expect(project.styleInfluence.drillHatTripletWeight > 1.2f,
        "DrillStyleInfluence should map Drill reference triplet hints into style weights.");
    expect(project.styleInfluence.drillHatBurstWeight > 1.1f,
        "DrillStyleInfluence should map Drill reference burst hints into style weights.");
}

void testDrillEngineAppliesStyleInfluenceSmoke()
{
    DrillEngine engine;

    auto project = createDefaultProject();
    project.params.genre = GenreType::Drill;
    project.params.bars = 4;
    project.params.seed = 6060;
    project.params.drillSubstyle = 0;

    auto* hatFx = findTrackByType(project, TrackType::HatFX);
    auto* sub = findTrackByType(project, TrackType::Sub808);
    expect(hatFx != nullptr && sub != nullptr,
        "Drill style influence integration smoke requires HatFX and Sub808 tracks.");
    expect(!hatFx->enabled && !sub->enabled,
        "Default project should start with HatFX/Sub808 disabled before Drill style influence runs.");

    engine.generate(project);

        hatFx = findTrackByType(project, TrackType::HatFX);
        sub = findTrackByType(project, TrackType::Sub808);
        expect(hatFx != nullptr && sub != nullptr,
            "Drill style influence integration smoke requires HatFX and Sub808 tracks after generation.");

    expect(hatFx->enabled,
        "Drill engine should apply Drill style influence and enable the HatFX lane from style defaults.");
    expect(sub->enabled,
        "Drill engine should apply Drill style influence and enable the Sub808 lane from style defaults.");
    expect(project.styleInfluence.hatMotionWeight != 1.0f || project.styleInfluence.lowEndCouplingWeight != 1.0f,
        "Drill engine should populate Drill style influence weights before planning.");
}

void testTrapStyleInfluenceSmoke()
{
    auto project = createDefaultProject();
    project.params.genre = GenreType::Trap;
    project.params.trapSubstyle = 0;
    juce::String error;
    expect(TrapStyleInfluence::apply(project, &error), "TrapStyleInfluence smoke application failed: " + error);
}

void testSampleApplyWeightsSmoke()
{
    const auto genreFirst = makeSampleApplyWeights(SampleApplyMode::GenreFirst, AnalysisMode::GenerateFromSample);
    const auto sampleFirst = makeSampleApplyWeights(SampleApplyMode::SampleFirst, AnalysisMode::GenerateFromSample);
    const auto exactCopy = makeSampleApplyWeights(SampleApplyMode::ExactCopy, AnalysisMode::ExtractFromSample);

    expect(genreFirst.generatedDrumsWeight > genreFirst.extractedDrumsWeight,
        "Genre-first apply weights must favor generated drums over extracted drums.");
    expect(sampleFirst.extractedBassWeight > sampleFirst.generatedBassWeight,
        "Sample-first apply weights must favor extracted bass over generated bass.");
    expect(exactCopy.exactCopy,
        "Exact-copy apply weights must mark the mode as exact copy.");
    expect(exactCopy.generatedDrumsWeight == 0.0f && exactCopy.generatedBassWeight == 0.0f,
        "Exact-copy apply weights must disable genre contribution.");
}

void testExtractPatternBlendAndCopySmoke()
{
    SampleAnalysisBundle bundle;
    bundle.summary.analyzedBars = 2;
    bundle.transcription.hasDetectedDrums = true;
    bundle.transcription.hasDetectedBass = true;
    bundle.transcription.drumEvents.push_back({ TrackType::Kick, 3, 1, 121, 36, 0.94f, false });
    bundle.transcription.drumEvents.push_back({ TrackType::Kick, 3, 1, 111, 36, 0.82f, false });
    bundle.transcription.drumEvents.push_back({ TrackType::Snare, 12, 1, 116, 38, 0.90f, false });
    bundle.transcription.bassEvents.push_back({ TrackType::Sub808, 5, 3, 104, 43, 0.88f, false });

    const auto extracted = ExtractPatternBuilder::build(bundle);
    expect(extracted.bars == 2, "ExtractPatternBuilder should preserve the analyzed bar count.");
    expect(extracted.laneNotes[static_cast<size_t>(trackTypeIndex(TrackType::Kick))].size() == 1,
        "ExtractPatternBuilder should dedupe duplicate kick events on the same step.");

    auto project = createDefaultProject();
    project.params.genre = GenreType::BoomBap;
    project.params.bars = 2;

    auto* kick = findTrackByType(project, TrackType::Kick);
    auto* snare = findTrackByType(project, TrackType::Snare);
    auto* hat = findTrackByType(project, TrackType::HiHat);
    auto* sub = findTrackByType(project, TrackType::Sub808);
    expect(kick != nullptr && snare != nullptr && hat != nullptr && sub != nullptr,
        "Pattern blend smoke requires Kick, Snare, HiHat and Sub808 tracks.");

    sub->enabled = true;
    ProjectStateController::setTrackNotes(project,
                        TrackType::Kick,
                        { { 36, 0, 1, 112, 0, false, "genre_kick", false, false, false },
                          { 36, 8, 1, 108, 0, false, "genre_kick", false, false, false } });
    ProjectStateController::setTrackNotes(project,
                        TrackType::Snare,
                        { { 38, 4, 1, 118, 0, false, "genre_snare", false, false, false },
                          { 38, 12, 1, 118, 0, false, "genre_snare", false, false, false } });
    ProjectStateController::setTrackNotes(project,
                        TrackType::HiHat,
                        { { 42, 0, 1, 92, 0, false, "genre_hat", false, false, false },
                          { 42, 2, 1, 88, 0, false, "genre_hat", false, false, false } });
    ProjectStateController::setTrackNotes(project,
                        TrackType::Sub808,
                        { { 36, 0, 4, 100, 0, false, "genre_sub", false, false, false } });

    const auto blendReport = PatternBlendEngine::apply(project,
                                  extracted,
                                  makeSampleApplyWeights(SampleApplyMode::SampleFirst,
                                             AnalysisMode::GenerateFromSample));
    expect(blendReport.changedTracks.count(TrackType::Kick) > 0,
        "Pattern blend smoke should update the kick lane when extracted kicks are present.");
    kick = findTrackByType(project, TrackType::Kick);
    sub = findTrackByType(project, TrackType::Sub808);
    expect(kick != nullptr && sub != nullptr, "Pattern blend smoke lost Kick or Sub808 after apply.");
    expect(hasNoteAt(*kick, 3, 0, "sample_copy"),
        "Pattern blend smoke should inject extracted kick hits into the visible lane.");
    expect(hasSubStartAt(*sub, 5),
        "Pattern blend smoke should inject extracted Sub808 starts into the visible lane.");

    const auto exactReport = PatternBlendEngine::apply(project,
                                  extracted,
                                  makeSampleApplyWeights(SampleApplyMode::ExactCopy,
                                             AnalysisMode::ExtractFromSample));
    expect(exactReport.exactCopy,
        "Pattern blend smoke should report exact-copy mode when exact copy is requested.");
    hat = findTrackByType(project, TrackType::HiHat);
    expect(hat != nullptr && hat->notes.empty(),
        "Exact copy should clear lanes that have no extracted note content.");
}

void testClassicRuleEnforcerSmoke()
{
    auto project = createDefaultProject();
    project.params.genre = GenreType::BoomBap;
    project.params.boombapSubstyle = 0;
    project.params.bars = 1;

    auto* snare = findTrackByType(project, TrackType::Snare);
    auto* clapGhost = findTrackByType(project, TrackType::ClapGhostSnare);
    auto* perc = findTrackByType(project, TrackType::Perc);
    auto* openHat = findTrackByType(project, TrackType::OpenHat);
    expect(snare != nullptr && clapGhost != nullptr && perc != nullptr && openHat != nullptr,
        "Classic rule smoke requires Snare, Clap/Ghost, Perc and OpenHat tracks.");

    openHat->enabled = true;
    ProjectStateController::setTrackNotes(project,
                        TrackType::Snare,
                        { { 38, 4, 1, 118, 0, false, "snare_backbone", false, false, false },
                          { 38, 12, 1, 116, 0, false, "snare_backbone", false, false, false } });
    ProjectStateController::setTrackNotes(project,
                        TrackType::ClapGhostSnare,
                        { { 39, 4, 1, 90, 0, false, "clap_layer", false, false, false },
                          { 39, 10, 1, 88, 0, false, "clap_support", false, false, false },
                          { 39, 12, 1, 89, 0, false, "clap_layer", false, false, false } });
    ProjectStateController::setTrackNotes(project,
                        TrackType::Perc,
                        { { 50, 2, 1, 84, 0, false, "perc_texture", false, false, false },
                          { 50, 10, 1, 90, 0, false, "perc_texture", false, false, false },
                          { 50, 14, 1, 92, 0, false, "perc_texture", false, false, false } });
    ProjectStateController::setTrackNotes(project,
                        TrackType::OpenHat,
                        { { 46, 7, 1, 86, 0, false, "open_hat", false, false, false },
                          { 46, 14, 1, 96, 0, false, "open_hat", false, false, false } });

    const auto report = SubstyleRuleEnforcer::enforce(project);
    expect(report.applied, "Classic rule enforcer should activate for BoomBap Classic.");

    clapGhost = findTrackByType(project, TrackType::ClapGhostSnare);
    perc = findTrackByType(project, TrackType::Perc);
    openHat = findTrackByType(project, TrackType::OpenHat);
    expect(clapGhost != nullptr && perc != nullptr && openHat != nullptr,
        "Classic rule smoke lost one of the decorated lanes after enforcement.");
    expect(std::none_of(clapGhost->notes.begin(), clapGhost->notes.end(), [](const NoteEvent& note)
    {
     return note.step == 4 || note.step == 12;
    }),
        "Classic rule enforcer should remove clap-layer collisions on the main snare backbeat.");
    expect(static_cast<int>(clapGhost->notes.size()) <= 1,
        "Classic rule enforcer should keep the clap/ghost support lane sparse per bar.");
    expect(static_cast<int>(perc->notes.size()) <= 1,
        "Classic rule enforcer should clamp decorative perc density in Classic mode.");
    expect(static_cast<int>(openHat->notes.size()) <= 1,
        "Classic rule enforcer should keep open hats sparse in Classic mode.");
}

void testBoomBapClassicPocketGenerationSmoke()
{
    BoomBapEngine engine;

    for (int seed = 3100; seed < 3112; ++seed)
    {
        auto project = createDefaultProject();
        project.params.genre = GenreType::BoomBap;
        project.params.boombapSubstyle = 0;
        project.params.bars = 4;
        project.params.seed = seed;
        project.params.bpm = 90.0f;
        project.params.swingPercent = 57.0f;
        project.params.densityAmount = 0.50f;
        project.params.timingAmount = 0.38f;
        project.params.humanizeAmount = 0.30f;

        engine.generate(project);

        const auto* snare = findTrackByType(project, TrackType::Snare);
        const auto* clapGhost = findTrackByType(project, TrackType::ClapGhostSnare);
        const auto* kick = findTrackByType(project, TrackType::Kick);
        const auto* hat = findTrackByType(project, TrackType::HiHat);
        expect(snare != nullptr && clapGhost != nullptr && kick != nullptr && hat != nullptr,
               "BoomBap Classic pocket smoke requires Snare, Clap/Ghost, Kick and HiHat tracks.");

        int snareGhosts = 0;
        for (const auto& note : snare->notes)
            if (note.isGhost)
                ++snareGhosts;
        expect(snareGhosts + static_cast<int>(clapGhost->notes.size()) <= 2,
               "BoomBap Classic should keep ghost snare support rare across a four-bar phrase.");

        for (const auto& note : clapGhost->notes)
        {
            const int step = ((note.step % 16) + 16) % 16;
            expect(step != 4 && step != 12,
                   "BoomBap Classic ghost snare lane should not double the main backbeat.");
        }

        for (int bar = 0; bar < project.params.bars; ++bar)
        {
            const int beat2 = bar * 16 + 4;
            const int beat4 = bar * 16 + 12;
            expect(std::any_of(snare->notes.begin(), snare->notes.end(), [beat2](const NoteEvent& note)
            {
                return note.step == beat2 && !note.isGhost;
            }), "BoomBap Classic must keep the main snare on beat 2.");
            expect(std::any_of(snare->notes.begin(), snare->notes.end(), [beat4](const NoteEvent& note)
            {
                return note.step == beat4 && !note.isGhost;
            }), "BoomBap Classic must keep the main snare on beat 4.");

            const int barKickCount = static_cast<int>(std::count_if(kick->notes.begin(), kick->notes.end(), [bar](const NoteEvent& note)
            {
                return note.step / 16 == bar;
            }));
            expect(barKickCount >= 1 && barKickCount <= 5,
                   "BoomBap Classic kick density should stay in a focused head-nod range.");
            expect(std::none_of(kick->notes.begin(), kick->notes.end(), [beat2, beat4](const NoteEvent& note)
            {
                return note.step == beat2 || note.step == beat4;
            }), "BoomBap Classic kick should not collide with the main snare backbeat.");

            const int eighthHatCount = static_cast<int>(std::count_if(hat->notes.begin(), hat->notes.end(), [bar](const NoteEvent& note)
            {
                return note.step / 16 == bar && ((note.step % 16) % 2) == 0;
            }));
            expect(eighthHatCount >= 8,
                   "BoomBap Classic hats should carry a steady eighth-note backbone.");
        }
    }
}

void testBoomBapDustyPocketGenerationSmoke()
{
    BoomBapEngine engine;

    for (int seed = 4200; seed < 4212; ++seed)
    {
        auto project = createDefaultProject();
        project.params.genre = GenreType::BoomBap;
        project.params.boombapSubstyle = 1;
        project.params.bars = 4;
        project.params.seed = seed;
        project.params.bpm = 86.0f;
        project.params.swingPercent = 59.0f;
        project.params.densityAmount = 0.46f;
        project.params.timingAmount = 0.52f;
        project.params.humanizeAmount = 0.36f;

        if (auto* ride = findTrackByType(project, TrackType::Ride); ride != nullptr)
            ride->enabled = true;

        engine.generate(project);

        const auto* snare = findTrackByType(project, TrackType::Snare);
        const auto* kick = findTrackByType(project, TrackType::Kick);
        const auto* hat = findTrackByType(project, TrackType::HiHat);
        const auto* ghostKick = findTrackByType(project, TrackType::GhostKick);
        const auto* clapGhost = findTrackByType(project, TrackType::ClapGhostSnare);
        const auto* openHat = findTrackByType(project, TrackType::OpenHat);
        const auto* perc = findTrackByType(project, TrackType::Perc);
        const auto* ride = findTrackByType(project, TrackType::Ride);

        expect(snare != nullptr && kick != nullptr && hat != nullptr,
               "BoomBap Dusty pocket smoke requires Snare, Kick and HiHat tracks.");

        const auto stepInBar = [](const NoteEvent& note)
        {
            return ((note.step % 16) + 16) % 16;
        };

        const auto countInBar = [](const TrackState* track, int bar)
        {
            if (track == nullptr)
                return 0;

            return static_cast<int>(std::count_if(track->notes.begin(), track->notes.end(), [bar](const NoteEvent& note)
            {
                return note.step / 16 == bar;
            }));
        };

        for (int bar = 0; bar < project.params.bars; ++bar)
        {
            const int beat2 = bar * 16 + 4;
            const int beat4 = bar * 16 + 12;
            const int endingLimit = bar == project.params.bars - 1 ? 2 : 1;

            const auto beat2Snare = std::find_if(snare->notes.begin(), snare->notes.end(), [beat2](const NoteEvent& note)
            {
                return note.step == beat2 && !note.isGhost;
            });
            const auto beat4Snare = std::find_if(snare->notes.begin(), snare->notes.end(), [beat4](const NoteEvent& note)
            {
                return note.step == beat4 && !note.isGhost;
            });
            expect(beat2Snare != snare->notes.end() && beat4Snare != snare->notes.end(),
                   "BoomBap Dusty must keep the main snare on beat 2 and beat 4.");
            expect(beat2Snare->microOffset >= 8 && beat2Snare->microOffset <= 20
                   && beat4Snare->microOffset >= 8 && beat4Snare->microOffset <= 20,
                   "BoomBap Dusty snare anchors should sit slightly late, tied to the hat swing.");

            const int carrierHatCount = static_cast<int>(std::count_if(hat->notes.begin(), hat->notes.end(), [bar, stepInBar](const NoteEvent& note)
            {
                return note.step / 16 == bar && (stepInBar(note) % 2) == 0;
            }));
            const int swungOffbeatHatCount = static_cast<int>(std::count_if(hat->notes.begin(), hat->notes.end(), [bar, stepInBar](const NoteEvent& note)
            {
                return note.step / 16 == bar && (stepInBar(note) % 4) == 2;
            }));
            expect(carrierHatCount >= 6,
                   "BoomBap Dusty hats should keep a slow swung eighth-note carrier. Seed "
                       + juce::String(seed) + " bar " + juce::String(bar)
                       + " carrier " + juce::String(carrierHatCount));
            expect(swungOffbeatHatCount >= 3,
                   "BoomBap Dusty hats should keep delayed offbeats in every bar.");

            for (const auto& note : hat->notes)
            {
                if (note.step / 16 == bar && (stepInBar(note) % 4) == 2)
                {
                    expect(note.microOffset >= 28 && note.microOffset <= 56,
                           "BoomBap Dusty offbeat hats should use the Dusty swing delay range.");
                }
            }

            const int barKickCount = countInBar(kick, bar);
            expect(barKickCount >= 1 && barKickCount <= 5,
                   "BoomBap Dusty kicks should stay slow, grounded and uncluttered.");
            expect(std::none_of(kick->notes.begin(), kick->notes.end(), [beat2, beat4](const NoteEvent& note)
            {
                return note.step == beat2 || note.step == beat4;
            }), "BoomBap Dusty kick should not collide with the main snare backbeat.");

            expect(countInBar(ghostKick, bar) <= endingLimit,
                   "BoomBap Dusty ghost kicks should stay rare and supportive.");
            expect(countInBar(clapGhost, bar) <= 2,
                   "BoomBap Dusty clap/ghost snare lane should not crowd the backbeat.");
            expect(countInBar(openHat, bar) <= endingLimit,
                   "BoomBap Dusty open hats should stay sparse.");
            expect(countInBar(perc, bar) <= endingLimit,
                   "BoomBap Dusty perc should stay sparse and groove-led.");
            expect(countInBar(ride, bar) <= (bar == project.params.bars - 1 ? 7 : 6),
                   "BoomBap Dusty ride should act as a carrier layer, not a busy extra lane.");
        }
    }
}

void testBoomBapJazzyPocketGenerationSmoke()
{
    BoomBapEngine engine;

    for (int seed = 5200; seed < 5212; ++seed)
    {
        auto project = createDefaultProject();
        project.params.genre = GenreType::BoomBap;
        project.params.boombapSubstyle = 2;
        project.params.bars = 4;
        project.params.seed = seed;
        project.params.bpm = 94.0f;
        project.params.swingPercent = 63.0f;
        project.params.densityAmount = 0.50f;
        project.params.timingAmount = 0.50f;
        project.params.humanizeAmount = 0.34f;

        if (auto* ride = findTrackByType(project, TrackType::Ride); ride != nullptr)
            ride->enabled = true;
        if (auto* cymbal = findTrackByType(project, TrackType::Cymbal); cymbal != nullptr)
            cymbal->enabled = true;

        engine.generate(project);

        const auto* ride = findTrackByType(project, TrackType::Ride);
        const auto* hat = findTrackByType(project, TrackType::HiHat);
        const auto* cymbal = findTrackByType(project, TrackType::Cymbal);
        const auto* kick = findTrackByType(project, TrackType::Kick);
        const auto* snare = findTrackByType(project, TrackType::Snare);
        const auto* clapGhost = findTrackByType(project, TrackType::ClapGhostSnare);
        const auto* ghostKick = findTrackByType(project, TrackType::GhostKick);
        const auto* openHat = findTrackByType(project, TrackType::OpenHat);
        const auto* perc = findTrackByType(project, TrackType::Perc);

        expect(ride != nullptr && hat != nullptr && cymbal != nullptr && kick != nullptr && snare != nullptr,
               "BoomBap Jazzy pocket smoke requires Ride, HiHat, Cymbal, Kick and Snare tracks.");

        const auto hasStep = [](const TrackState* track, int step)
        {
            return track != nullptr && std::any_of(track->notes.begin(), track->notes.end(), [step](const NoteEvent& note)
            {
                return note.step == step;
            });
        };

        const auto findStep = [](const TrackState* track, int step)
        {
            return std::find_if(track->notes.begin(), track->notes.end(), [step](const NoteEvent& note)
            {
                return note.step == step;
            });
        };

        const auto countInBar = [](const TrackState* track, int bar)
        {
            if (track == nullptr)
                return 0;

            return static_cast<int>(std::count_if(track->notes.begin(), track->notes.end(), [bar](const NoteEvent& note)
            {
                return note.step / 16 == bar;
            }));
        };

        int phraseCompingGhosts = 0;
        for (const auto& note : snare->notes)
        {
            const int step = ((note.step % 16) + 16) % 16;
            if (note.isGhost && step != 4 && step != 12)
                ++phraseCompingGhosts;
        }

        int phraseKickCompHits = 0;
        for (const auto& note : kick->notes)
        {
            const int step = ((note.step % 16) + 16) % 16;
            if (step != 0 && step != 4 && step != 8 && step != 12)
                ++phraseKickCompHits;
        }

        for (int bar = 0; bar < project.params.bars; ++bar)
        {
            for (const int stepInBar : { 0, 4, 6, 8, 12, 14 })
            {
                const int absoluteStep = bar * 16 + stepInBar;
                expect(hasStep(hat, absoluteStep),
                       "BoomBap Jazzy hi-hat must inherit the cymbal spang-a-lang carrier. Seed "
                           + juce::String(seed) + " bar " + juce::String(bar));
            }

            const auto hatSkipA = findStep(hat, bar * 16 + 6);
            const auto hatSkipB = findStep(hat, bar * 16 + 14);
            expect(hatSkipA != hat->notes.end() && hatSkipB != hat->notes.end()
                   && hatSkipA->microOffset >= 96 && hatSkipA->microOffset <= 188
                   && hatSkipB->microOffset >= 96 && hatSkipB->microOffset <= 188,
                   "BoomBap Jazzy hi-hat skips should use the cymbal tempo-derived swing delay.");

            for (const int stepInBar : { 4, 12 })
            {
                const int absoluteStep = bar * 16 + stepInBar;
                const auto cymbalFoot = findStep(cymbal, absoluteStep);
                expect(cymbalFoot != cymbal->notes.end(),
                       "BoomBap Jazzy cymbal should inherit the old hi-hat 2 and 4 foot layer.");
                expect(cymbalFoot->velocity <= 44 && cymbalFoot->microOffset >= -1 && cymbalFoot->microOffset <= 12,
                       "BoomBap Jazzy cymbal foot layer should be much quieter than the carrier.");
            }

            for (const int stepInBar : { 0, 4, 8, 12 })
            {
                const int absoluteStep = bar * 16 + stepInBar;
                const auto kickFeather = findStep(kick, absoluteStep);
                expect(kickFeather != kick->notes.end(),
                       "BoomBap Jazzy kick should feather quiet quarter notes.");
                expect(kickFeather->velocity <= 66 && kickFeather->microOffset >= -3 && kickFeather->microOffset <= 8,
                       "BoomBap Jazzy feathered kick should stay quiet and close to the walking pulse.");
            }

            expect(countInBar(kick, bar) >= 4 && countInBar(kick, bar) <= (bar == project.params.bars - 1 ? 6 : 5),
                   "BoomBap Jazzy kick should feather without becoming a busy boom-bap kick lane.");
            expect(countInBar(snare, bar) >= 1 && countInBar(snare, bar) <= (bar == project.params.bars - 1 ? 6 : 5),
                   "BoomBap Jazzy snare should comp, not hard-loop a dense backbeat lane.");
            expect(countInBar(hat, bar) == 6,
                   "BoomBap Jazzy hi-hat should now carry the transferred cymbal swing pattern.");
            expect(countInBar(cymbal, bar) >= 2 && countInBar(cymbal, bar) <= (bar == project.params.bars - 1 ? 4 : 3),
                   "BoomBap Jazzy cymbal should keep the transferred hat layer sparse and quiet.");
            expect(countInBar(ride, bar) == 0,
                   "BoomBap Jazzy ride should not duplicate the carrier after moving it into hi-hat.");
            expect(countInBar(clapGhost, bar) <= (bar == project.params.bars - 1 ? 3 : 2),
                   "BoomBap Jazzy clap/ghost snare support should stay sparse.");
            expect(countInBar(ghostKick, bar) <= (bar == project.params.bars - 1 ? 2 : 1),
                   "BoomBap Jazzy ghost kicks should not crowd the feathered kick.");
            expect(countInBar(openHat, bar) <= (bar == project.params.bars - 1 ? 2 : 1),
                   "BoomBap Jazzy open hats should be occasional color.");
            expect(countInBar(perc, bar) <= (bar == project.params.bars - 1 ? 3 : 2),
                   "BoomBap Jazzy percussion should be comping color, not the carrier.");
        }

        expect(phraseCompingGhosts > 0,
               "BoomBap Jazzy should add at least one snare comping ghost across a phrase.");
        expect(phraseKickCompHits > 0,
               "BoomBap Jazzy kick should add a small low comping gesture on top of feathering.");
    }
}

void testBoomBapGoldPocketGenerationSmoke()
{
    BoomBapEngine engine;

    for (int seed = 6300; seed < 6312; ++seed)
    {
        auto project = createDefaultProject();
        project.params.genre = GenreType::BoomBap;
        project.params.boombapSubstyle = 3;
        project.params.bars = 4;
        project.params.seed = seed;
        project.params.bpm = 92.0f;
        project.params.swingPercent = 58.5f;
        project.params.densityAmount = 0.52f;
        project.params.timingAmount = 0.38f;
        project.params.humanizeAmount = 0.28f;

        if (auto* ride = findTrackByType(project, TrackType::Ride); ride != nullptr)
            ride->enabled = true;
        if (auto* cymbal = findTrackByType(project, TrackType::Cymbal); cymbal != nullptr)
            cymbal->enabled = true;

        engine.generate(project);

        const auto* hat = findTrackByType(project, TrackType::HiHat);
        const auto* kick = findTrackByType(project, TrackType::Kick);
        const auto* snare = findTrackByType(project, TrackType::Snare);
        const auto* clapGhost = findTrackByType(project, TrackType::ClapGhostSnare);
        const auto* ghostKick = findTrackByType(project, TrackType::GhostKick);
        const auto* openHat = findTrackByType(project, TrackType::OpenHat);
        const auto* perc = findTrackByType(project, TrackType::Perc);
        const auto* ride = findTrackByType(project, TrackType::Ride);
        const auto* cymbal = findTrackByType(project, TrackType::Cymbal);

        expect(hat != nullptr && kick != nullptr && snare != nullptr,
               "BoomBapGold smoke requires HiHat, Kick and Snare tracks.");

        const auto hasStep = [](const TrackState* track, int step)
        {
            return track != nullptr && std::any_of(track->notes.begin(), track->notes.end(), [step](const NoteEvent& note)
            {
                return note.step == step;
            });
        };

        const auto findStep = [](const TrackState* track, int step)
        {
            return std::find_if(track->notes.begin(), track->notes.end(), [step](const NoteEvent& note)
            {
                return note.step == step;
            });
        };

        const auto countInBar = [](const TrackState* track, int bar)
        {
            if (track == nullptr)
                return 0;

            return static_cast<int>(std::count_if(track->notes.begin(), track->notes.end(), [bar](const NoteEvent& note)
            {
                return note.step / 16 == bar;
            }));
        };

        int phraseKickPickups = 0;
        for (const auto& note : kick->notes)
        {
            const int step = ((note.step % 16) + 16) % 16;
            if (step == 10 || step == 11 || step == 14)
                ++phraseKickPickups;
        }

        int phraseSnareGhosts = 0;
        for (const auto& note : snare->notes)
        {
            const int step = ((note.step % 16) + 16) % 16;
            if (note.isGhost && step != 4 && step != 12)
                ++phraseSnareGhosts;
        }

        for (int bar = 0; bar < project.params.bars; ++bar)
        {
            const int beat2 = bar * 16 + 4;
            const int beat4 = bar * 16 + 12;

            const auto beat2Snare = findStep(snare, beat2);
            const auto beat4Snare = findStep(snare, beat4);
            expect(beat2Snare != snare->notes.end() && beat4Snare != snare->notes.end(),
                   "BoomBapGold should keep hard golden-era snares on beat 2 and beat 4.");
            expect(!beat2Snare->isGhost && !beat4Snare->isGhost
                   && beat2Snare->velocity >= 98 && beat4Snare->velocity >= 98,
                   "BoomBapGold snare anchors should be strong, not ghosted.");
            expect(beat2Snare->microOffset >= 6 && beat2Snare->microOffset <= 16
                   && beat4Snare->microOffset >= 6 && beat4Snare->microOffset <= 16,
                   "BoomBapGold snare anchors should sit slightly late in the pocket.");

            for (const int stepInBar : { 0, 2, 4, 6, 8, 10, 12, 14 })
            {
                const int absoluteStep = bar * 16 + stepInBar;
                expect(hasStep(hat, absoluteStep),
                       "BoomBapGold should keep a swung eighth-note hat carrier.");

                const auto hatHit = findStep(hat, absoluteStep);
                if ((stepInBar % 4) == 2)
                {
                    expect(hatHit != hat->notes.end() && hatHit->microOffset >= 28 && hatHit->microOffset <= 58,
                           "BoomBapGold offbeat hats should carry a clear delayed swing.");
                }
            }

            expect(countInBar(hat, bar) >= 8 && countInBar(hat, bar) <= 9,
                   "BoomBapGold hats should be present but not modern 16th-note clutter.");
            expect(hasStep(kick, bar * 16),
                   "BoomBapGold should ground each bar with a kick on the one.");
            expect(countInBar(kick, bar) >= 3 && countInBar(kick, bar) <= 5,
                   "BoomBapGold kicks should be active and syncopated without turning into a dense modern lane.");
            expect(!hasStep(kick, beat2) && !hasStep(kick, beat4),
                   "BoomBapGold kick should not collide with the main snare backbeat.");

            expect(countInBar(clapGhost, bar) <= 1,
                   "BoomBapGold clap layer should stay as occasional backbeat color.");
            expect(countInBar(ghostKick, bar) <= 1,
                   "BoomBapGold ghost kicks should stay rare.");
            expect(countInBar(openHat, bar) <= (bar == project.params.bars - 1 ? 1 : 0),
                   "BoomBapGold open hats should only appear as rare phrase-end color.");
            expect(countInBar(perc, bar) <= (bar == project.params.bars - 1 ? 2 : 1),
                   "BoomBapGold percussion should stay sparse and era-appropriate.");
            expect(countInBar(ride, bar) == 0 && countInBar(cymbal, bar) == 0,
                   "BoomBapGold should not add ride/cymbal clutter over the core drum break language.");
        }

        expect(phraseKickPickups >= project.params.bars,
               "BoomBapGold should use signature pickup kicks around the back half of each bar.");
        expect(phraseSnareGhosts >= 1 && phraseSnareGhosts <= project.params.bars + 1,
               "BoomBapGold should keep snare ghosts rare but present enough to add push.");
    }
}

void testBoomBapRussianUndergroundPocketGenerationSmoke()
{
    BoomBapEngine engine;

    for (int seed = 7100; seed < 7112; ++seed)
    {
        auto project = createDefaultProject();
        project.params.genre = GenreType::BoomBap;
        project.params.boombapSubstyle = 4;
        project.params.bars = 4;
        project.params.seed = seed;
        project.params.bpm = 82.0f;
        project.params.swingPercent = 56.5f;
        project.params.densityAmount = 0.42f;
        project.params.timingAmount = 0.34f;
        project.params.humanizeAmount = 0.28f;

        if (auto* ride = findTrackByType(project, TrackType::Ride); ride != nullptr)
            ride->enabled = true;
        if (auto* cymbal = findTrackByType(project, TrackType::Cymbal); cymbal != nullptr)
            cymbal->enabled = true;
        if (auto* openHat = findTrackByType(project, TrackType::OpenHat); openHat != nullptr)
            openHat->enabled = true;

        engine.generate(project);

        const auto* hat = findTrackByType(project, TrackType::HiHat);
        const auto* kick = findTrackByType(project, TrackType::Kick);
        const auto* snare = findTrackByType(project, TrackType::Snare);
        const auto* clapGhost = findTrackByType(project, TrackType::ClapGhostSnare);
        const auto* ghostKick = findTrackByType(project, TrackType::GhostKick);
        const auto* openHat = findTrackByType(project, TrackType::OpenHat);
        const auto* perc = findTrackByType(project, TrackType::Perc);
        const auto* ride = findTrackByType(project, TrackType::Ride);
        const auto* cymbal = findTrackByType(project, TrackType::Cymbal);

        expect(hat != nullptr && kick != nullptr && snare != nullptr,
               "BoomBap RussianUnderground smoke requires HiHat, Kick and Snare tracks.");

        const auto hasStep = [](const TrackState* track, int step)
        {
            return track != nullptr && std::any_of(track->notes.begin(), track->notes.end(), [step](const NoteEvent& note)
            {
                return note.step == step;
            });
        };

        const auto findStep = [](const TrackState* track, int step)
        {
            return std::find_if(track->notes.begin(), track->notes.end(), [step](const NoteEvent& note)
            {
                return note.step == step;
            });
        };

        const auto countInBar = [](const TrackState* track, int bar)
        {
            if (track == nullptr)
                return 0;

            return static_cast<int>(std::count_if(track->notes.begin(), track->notes.end(), [bar](const NoteEvent& note)
            {
                return note.step / 16 == bar;
            }));
        };

        int phraseLateKicks = 0;
        for (const auto& note : kick->notes)
        {
            const int step = ((note.step % 16) + 16) % 16;
            if (step == 10 || step == 11 || step == 14 || step == 15)
                ++phraseLateKicks;
        }

        int phraseSnareGhosts = 0;
        for (const auto& note : snare->notes)
        {
            const int step = ((note.step % 16) + 16) % 16;
            if (note.isGhost && step != 4 && step != 12)
                ++phraseSnareGhosts;
        }

        for (int bar = 0; bar < project.params.bars; ++bar)
        {
            const int beat2 = bar * 16 + 4;
            const int beat4 = bar * 16 + 12;

            const auto beat2Snare = findStep(snare, beat2);
            const auto beat4Snare = findStep(snare, beat4);
            expect(beat2Snare != snare->notes.end() && beat4Snare != snare->notes.end(),
                   "RussianUnderground should keep the dry main snare on beat 2 and beat 4.");
            expect(!beat2Snare->isGhost && !beat4Snare->isGhost
                   && beat2Snare->velocity >= 101 && beat4Snare->velocity >= 101,
                   "RussianUnderground snare anchors should stay hard and dry.");
            expect(beat2Snare->microOffset >= 10 && beat2Snare->microOffset <= 24
                   && beat4Snare->microOffset >= 10 && beat4Snare->microOffset <= 24,
                   "RussianUnderground snare anchors should sit late in a slow pocket.");

            expect(countInBar(hat, bar) >= 6 && countInBar(hat, bar) <= (bar == project.params.bars - 1 ? 9 : 8),
                   "RussianUnderground hats should stay sparse, not full modern 16ths.");

            int swungOffbeats = 0;
            for (const int stepInBar : { 2, 6, 10, 14 })
            {
                const auto hatHit = findStep(hat, bar * 16 + stepInBar);
                if (hatHit != hat->notes.end())
                {
                    ++swungOffbeats;
                    expect(hatHit->microOffset >= 20 && hatHit->microOffset <= 50,
                           "RussianUnderground offbeat hats should have a modest late head-nod delay.");
                }
            }
            expect(swungOffbeats >= 2,
                   "RussianUnderground hats should keep enough delayed offbeats to carry the head-nod.");

            expect(hasStep(kick, bar * 16),
                   "RussianUnderground should ground each bar with a heavy kick on the one.");
            expect(countInBar(kick, bar) >= 3 && countInBar(kick, bar) <= 5,
                   "RussianUnderground kicks should be heavy and sparse, with a few syncopated answers.");
            expect(!hasStep(kick, beat2) && !hasStep(kick, beat4),
                   "RussianUnderground kick should not collide with the main backbeat.");

            expect(countInBar(clapGhost, bar) <= (bar == project.params.bars - 1 ? 1 : 0),
                   "RussianUnderground clap/ghost support should be nearly absent.");
            expect(countInBar(ghostKick, bar) <= (bar == project.params.bars - 1 ? 1 : 0),
                   "RussianUnderground ghost kicks should be almost empty.");
            expect(countInBar(openHat, bar) == 0,
                   "RussianUnderground should avoid open-hat shine.");
            expect(countInBar(perc, bar) <= (bar == project.params.bars - 1 ? 2 : 1),
                   "RussianUnderground percussion should stay as rare basement texture.");
            expect(countInBar(ride, bar) == 0 && countInBar(cymbal, bar) == 0,
                   "RussianUnderground should not use ride/cymbal gloss.");
        }

        expect(phraseLateKicks >= project.params.bars,
               "RussianUnderground should answer the one with late-bar kick weight.");
        expect(phraseSnareGhosts >= 1 && phraseSnareGhosts <= project.params.bars,
               "RussianUnderground should keep ghost snares rare but not sterile.");
    }
}

void testBoomBapLofiRapPocketGenerationSmoke()
{
    BoomBapEngine engine;

    for (int seed = 7200; seed < 7212; ++seed)
    {
        auto project = createDefaultProject();
        project.params.genre = GenreType::BoomBap;
        project.params.boombapSubstyle = 5;
        project.params.bars = 4;
        project.params.seed = seed;
        project.params.bpm = 78.0f;
        project.params.swingPercent = 57.0f;
        project.params.densityAmount = 0.36f;
        project.params.timingAmount = 0.40f;
        project.params.humanizeAmount = 0.50f;

        if (auto* ride = findTrackByType(project, TrackType::Ride); ride != nullptr)
            ride->enabled = true;
        if (auto* cymbal = findTrackByType(project, TrackType::Cymbal); cymbal != nullptr)
            cymbal->enabled = true;
        if (auto* openHat = findTrackByType(project, TrackType::OpenHat); openHat != nullptr)
            openHat->enabled = true;

        engine.generate(project);

        const auto* hat = findTrackByType(project, TrackType::HiHat);
        const auto* kick = findTrackByType(project, TrackType::Kick);
        const auto* snare = findTrackByType(project, TrackType::Snare);
        const auto* clapGhost = findTrackByType(project, TrackType::ClapGhostSnare);
        const auto* ghostKick = findTrackByType(project, TrackType::GhostKick);
        const auto* openHat = findTrackByType(project, TrackType::OpenHat);
        const auto* perc = findTrackByType(project, TrackType::Perc);
        const auto* ride = findTrackByType(project, TrackType::Ride);
        const auto* cymbal = findTrackByType(project, TrackType::Cymbal);

        expect(hat != nullptr && kick != nullptr && snare != nullptr,
               "BoomBap LofiRap smoke requires HiHat, Kick and Snare tracks.");

        const auto hasStep = [](const TrackState* track, int step)
        {
            return track != nullptr && std::any_of(track->notes.begin(), track->notes.end(), [step](const NoteEvent& note)
            {
                return note.step == step;
            });
        };

        const auto findStep = [](const TrackState* track, int step)
        {
            return std::find_if(track->notes.begin(), track->notes.end(), [step](const NoteEvent& note)
            {
                return note.step == step;
            });
        };

        const auto countInBar = [](const TrackState* track, int bar)
        {
            if (track == nullptr)
                return 0;

            return static_cast<int>(std::count_if(track->notes.begin(), track->notes.end(), [bar](const NoteEvent& note)
            {
                return note.step / 16 == bar;
            }));
        };

        int phraseLateKicks = 0;
        for (const auto& note : kick->notes)
        {
            const int step = ((note.step % 16) + 16) % 16;
            if (step == 10 || step == 11 || step == 14 || step == 15)
                ++phraseLateKicks;
        }

        int phraseSnareGhosts = 0;
        for (const auto& note : snare->notes)
        {
            const int step = ((note.step % 16) + 16) % 16;
            if (note.isGhost && step != 4 && step != 12)
            {
                ++phraseSnareGhosts;
                expect(note.velocity <= 44,
                       "LofiRap ghost snares should stay very quiet.");
            }
        }

        for (int bar = 0; bar < project.params.bars; ++bar)
        {
            const int beat2 = bar * 16 + 4;
            const int beat4 = bar * 16 + 12;

            const auto beat2Snare = findStep(snare, beat2);
            const auto beat4Snare = findStep(snare, beat4);
            expect(beat2Snare != snare->notes.end() && beat4Snare != snare->notes.end(),
                   "LofiRap should keep the mellow main snare on beat 2 and beat 4.");
            expect(!beat2Snare->isGhost && !beat4Snare->isGhost
                   && beat2Snare->velocity >= 78 && beat4Snare->velocity <= 108,
                   "LofiRap snare anchors should be present but not hard/bright.");
            expect(beat2Snare->microOffset >= 10 && beat2Snare->microOffset <= 26
                   && beat4Snare->microOffset >= 10 && beat4Snare->microOffset <= 26,
                   "LofiRap snare anchors should sit a little late in the pocket.");

            expect(countInBar(hat, bar) >= 5 && countInBar(hat, bar) <= (bar == project.params.bars - 1 ? 8 : 7),
                   "LofiRap hats should be dusty, sparse eighths with holes.");

            int swungOffbeats = 0;
            for (const int stepInBar : { 2, 6, 10, 14 })
            {
                const auto hatHit = findStep(hat, bar * 16 + stepInBar);
                if (hatHit != hat->notes.end())
                {
                    ++swungOffbeats;
                    expect(hatHit->microOffset >= 22 && hatHit->microOffset <= 58,
                           "LofiRap offbeat hats should have a soft late swing.");
                    expect(hatHit->velocity <= 78,
                           "LofiRap hats should stay soft.");
                }
            }
            expect(swungOffbeats >= 2,
                   "LofiRap needs enough delayed offbeat hats to carry the relaxed head-nod.");

            expect(hasStep(kick, bar * 16),
                   "LofiRap should keep a soft kick on the one.");
            expect(countInBar(kick, bar) >= 2 && countInBar(kick, bar) <= (bar == project.params.bars - 1 ? 5 : 4),
                   "LofiRap kicks should be sparse with a few lazy answers.");
            expect(!hasStep(kick, beat2) && !hasStep(kick, beat4),
                   "LofiRap kick should not collide with the main snare backbeat.");

            expect(countInBar(clapGhost, bar) <= (bar == project.params.bars - 1 ? 1 : 0),
                   "LofiRap clap support should be phrase-end dust only.");
            expect(countInBar(ghostKick, bar) <= (bar == project.params.bars - 1 ? 1 : 0),
                   "LofiRap ghost kicks should stay almost empty.");
            expect(countInBar(openHat, bar) <= (bar == project.params.bars - 1 ? 1 : 0),
                   "LofiRap open hats should be rare phrase-end color.");
            expect(countInBar(perc, bar) <= (bar == project.params.bars - 1 ? 2 : 1),
                   "LofiRap percussion should stay as small tape texture.");
            expect(countInBar(ride, bar) == 0 && countInBar(cymbal, bar) == 0,
                   "LofiRap should not use ride/cymbal gloss.");
        }

        expect(phraseLateKicks >= project.params.bars,
               "LofiRap should answer the one with late, lazy kick movement.");
        expect(phraseSnareGhosts >= 1 && phraseSnareGhosts <= project.params.bars + 1,
               "LofiRap should keep ghost snares quiet, rare, and musical.");
    }
}

void testRapEastCoastPocketGenerationSmoke()
{
    RapEngine engine;

    for (int seed = 7300; seed < 7312; ++seed)
    {
        auto project = createDefaultProject();
        project.params.genre = GenreType::Rap;
        project.params.rapSubstyle = 0;
        project.params.bars = 4;
        project.params.seed = seed;
        project.params.bpm = 92.0f;
        project.params.swingPercent = 54.0f;
        project.params.densityAmount = 0.46f;
        project.params.timingAmount = 0.30f;
        project.params.humanizeAmount = 0.24f;
        project.params.velocityAmount = 0.54f;

        if (auto* openHat = findTrackByType(project, TrackType::OpenHat); openHat != nullptr)
            openHat->enabled = true;
        if (auto* ride = findTrackByType(project, TrackType::Ride); ride != nullptr)
            ride->enabled = true;
        if (auto* cymbal = findTrackByType(project, TrackType::Cymbal); cymbal != nullptr)
            cymbal->enabled = true;
        if (auto* sub = findTrackByType(project, TrackType::Sub808); sub != nullptr)
            sub->enabled = true;

        engine.generate(project);

        const auto* hat = findTrackByType(project, TrackType::HiHat);
        const auto* kick = findTrackByType(project, TrackType::Kick);
        const auto* snare = findTrackByType(project, TrackType::Snare);
        const auto* clapGhost = findTrackByType(project, TrackType::ClapGhostSnare);
        const auto* ghostKick = findTrackByType(project, TrackType::GhostKick);
        const auto* openHat = findTrackByType(project, TrackType::OpenHat);
        const auto* perc = findTrackByType(project, TrackType::Perc);
        const auto* ride = findTrackByType(project, TrackType::Ride);
        const auto* cymbal = findTrackByType(project, TrackType::Cymbal);
        const auto* sub = findTrackByType(project, TrackType::Sub808);

        expect(hat != nullptr && kick != nullptr && snare != nullptr,
               "Rap EastCoast smoke requires HiHat, Kick and Snare tracks.");

        const auto hasStep = [](const TrackState* track, int step)
        {
            return track != nullptr && std::any_of(track->notes.begin(), track->notes.end(), [step](const NoteEvent& note)
            {
                return note.step == step;
            });
        };

        const auto findStep = [](const TrackState* track, int step)
        {
            return std::find_if(track->notes.begin(), track->notes.end(), [step](const NoteEvent& note)
            {
                return note.step == step;
            });
        };

        const auto countInBar = [](const TrackState* track, int bar)
        {
            if (track == nullptr)
                return 0;

            return static_cast<int>(std::count_if(track->notes.begin(), track->notes.end(), [bar](const NoteEvent& note)
            {
                return note.step / 16 == bar;
            }));
        };

        int phraseLateKicks = 0;
        for (const auto& note : kick->notes)
        {
            const int step = ((note.step % 16) + 16) % 16;
            if (step == 10 || step == 14 || step == 15)
                ++phraseLateKicks;
        }

        int phraseSnareGhosts = 0;
        for (const auto& note : snare->notes)
        {
            const int step = ((note.step % 16) + 16) % 16;
            if (note.isGhost && step != 4 && step != 12)
            {
                ++phraseSnareGhosts;
                expect(note.velocity <= 48,
                       "Rap EastCoast ghost snares should stay quiet and rare.");
            }
        }

        for (int bar = 0; bar < project.params.bars; ++bar)
        {
            const int beat2 = bar * 16 + 4;
            const int beat4 = bar * 16 + 12;

            const auto beat2Snare = findStep(snare, beat2);
            const auto beat4Snare = findStep(snare, beat4);
            expect(beat2Snare != snare->notes.end() && beat4Snare != snare->notes.end(),
                   "Rap EastCoast should keep the main snare on beat 2 and beat 4.");
            expect(!beat2Snare->isGhost && !beat4Snare->isGhost
                   && beat2Snare->velocity >= 98 && beat4Snare->velocity >= 98,
                   "Rap EastCoast snare anchors should be firm and upfront.");
            expect(beat2Snare->microOffset >= 2 && beat2Snare->microOffset <= 15
                   && beat4Snare->microOffset >= 2 && beat4Snare->microOffset <= 15,
                   "Rap EastCoast snare anchors should sit slightly late but tight.");

            expect(countInBar(hat, bar) >= 7 && countInBar(hat, bar) <= 8,
                   "Rap EastCoast hats should be tight eighth-note carriers with limited extra 16ths.");

            int swungOffbeats = 0;
            for (const int stepInBar : { 2, 6, 10, 14 })
            {
                const auto hatHit = findStep(hat, bar * 16 + stepInBar);
                if (hatHit != hat->notes.end())
                {
                    ++swungOffbeats;
                    expect(hatHit->microOffset >= 8 && hatHit->microOffset <= 42,
                           "Rap EastCoast offbeat hats should carry a moderate MPC-style swing.");
                    expect(hatHit->velocity <= 86,
                           "Rap EastCoast hats should not get modern-bright.");
                }
            }
            expect(swungOffbeats >= 3,
                   "Rap EastCoast hats should keep enough swung offbeats for the head-nod.");

            int oddHatHits = 0;
            for (const auto& note : hat->notes)
            {
                if (note.step / 16 == bar && (((note.step % 16) + 16) % 16) % 2 == 1)
                    ++oddHatHits;
            }
            expect(oddHatHits <= 1,
                   "Rap EastCoast should avoid busy modern 16th-hat chatter.");

            expect(hasStep(kick, bar * 16),
                   "Rap EastCoast should ground each bar with a kick on the one.");
            expect(countInBar(kick, bar) >= 3 && countInBar(kick, bar) <= 4,
                   "Rap EastCoast kicks should be sparse but assertive.");
            expect(!hasStep(kick, beat2) && !hasStep(kick, beat4),
                   "Rap EastCoast kick should not collide with the main snare backbeat.");

            expect(countInBar(clapGhost, bar) <= (bar == project.params.bars - 1 ? 1 : 0),
                   "Rap EastCoast clap support should stay phrase-end color only.");
            expect(countInBar(ghostKick, bar) <= (bar == project.params.bars - 1 ? 1 : 0),
                   "Rap EastCoast ghost kicks should stay rare.");
            expect(countInBar(openHat, bar) == 0,
                   "Rap EastCoast should avoid open-hat shine.");
            expect(countInBar(perc, bar) <= (bar == project.params.bars - 1 ? 2 : 1),
                   "Rap EastCoast percussion should stay as small sampled texture.");
            expect(countInBar(ride, bar) == 0 && countInBar(cymbal, bar) == 0,
                   "Rap EastCoast should not add ride/cymbal gloss.");
            expect(countInBar(sub, bar) == 0,
                   "Rap EastCoast should not default to modern Sub808 movement.");
        }

        expect(phraseLateKicks >= project.params.bars,
               "Rap EastCoast should answer the one with late-bar pickup kicks.");
        expect(phraseSnareGhosts >= 1 && phraseSnareGhosts <= project.params.bars,
               "Rap EastCoast should keep ghost snares quiet, rare, and phrase-aware.");
    }
}

void testRapEastCoastControlsInfluenceSmoke()
{
    RapEngine engine;

    auto low = createDefaultProject();
    low.params.genre = GenreType::Rap;
    low.params.rapSubstyle = 0;
    low.params.bars = 4;
    low.params.seed = 7401;
    low.params.bpm = 92.0f;
    low.params.swingPercent = 51.0f;
    low.params.densityAmount = 0.18f;
    low.params.timingAmount = 0.10f;
    low.params.humanizeAmount = 0.08f;
    low.params.velocityAmount = 0.18f;

    auto high = low;
    high.params.swingPercent = 58.0f;
    high.params.densityAmount = 0.82f;
    high.params.timingAmount = 0.82f;
    high.params.humanizeAmount = 0.82f;
    high.params.velocityAmount = 0.82f;

    engine.generate(low);
    engine.generate(high);

    const auto* lowHat = findTrackByType(low, TrackType::HiHat);
    const auto* highHat = findTrackByType(high, TrackType::HiHat);
    const auto* lowKick = findTrackByType(low, TrackType::Kick);
    const auto* highKick = findTrackByType(high, TrackType::Kick);
    const auto* lowSnare = findTrackByType(low, TrackType::Snare);
    const auto* highSnare = findTrackByType(high, TrackType::Snare);

    expect(lowHat != nullptr && highHat != nullptr && lowKick != nullptr && highKick != nullptr && lowSnare != nullptr && highSnare != nullptr,
           "Rap EastCoast controls smoke requires core tracks.");

    expect(highHat->notes.size() >= lowHat->notes.size(),
           "Rap EastCoast Density should not make hats thinner when raised.");
    expect(highKick->notes.size() >= lowKick->notes.size(),
           "Rap EastCoast Density should not make kicks thinner when raised.");

    const auto averageOffbeatHatMicro = [](const TrackState& track)
    {
        int sum = 0;
        int count = 0;
        for (const auto& note : track.notes)
        {
            const int step = ((note.step % 16) + 16) % 16;
            if (step == 2 || step == 6 || step == 10 || step == 14)
            {
                sum += note.microOffset;
                ++count;
            }
        }

        return count > 0 ? static_cast<float>(sum) / static_cast<float>(count) : 0.0f;
    };

    expect(averageOffbeatHatMicro(*highHat) > averageOffbeatHatMicro(*lowHat) + 6.0f,
           "Rap EastCoast Swing should push offbeat hats later when raised.");

    const auto maxAbsMicro = [](const TrackState& track)
    {
        int out = 0;
        for (const auto& note : track.notes)
            out = std::max(out, std::abs(note.microOffset));
        return out;
    };

    expect(maxAbsMicro(*highKick) >= maxAbsMicro(*lowKick),
           "Rap EastCoast Timing/Humanize should allow wider kick microtiming when raised.");

    const auto averageMainSnareVelocity = [](const TrackState& track)
    {
        int sum = 0;
        int count = 0;
        for (const auto& note : track.notes)
        {
            const int step = ((note.step % 16) + 16) % 16;
            if (!note.isGhost && (step == 4 || step == 12))
            {
                sum += note.velocity;
                ++count;
            }
        }
        return count > 0 ? static_cast<float>(sum) / static_cast<float>(count) : 0.0f;
    };

    expect(averageMainSnareVelocity(*highSnare) > averageMainSnareVelocity(*lowSnare),
           "Rap EastCoast Velocity should lift main snare accents when raised.");
}

void testRapWestCoastPocketGenerationSmoke()
{
    RapEngine engine;

    for (int seed = 7500; seed < 7512; ++seed)
    {
        auto project = createDefaultProject();
        project.params.genre = GenreType::Rap;
        project.params.rapSubstyle = 1;
        project.params.bars = 4;
        project.params.seed = seed;
        project.params.bpm = 94.0f;
        project.params.swingPercent = 55.5f;
        project.params.densityAmount = 0.50f;
        project.params.timingAmount = 0.38f;
        project.params.humanizeAmount = 0.34f;
        project.params.velocityAmount = 0.44f;

        if (auto* openHat = findTrackByType(project, TrackType::OpenHat); openHat != nullptr)
            openHat->enabled = true;
        if (auto* clap = findTrackByType(project, TrackType::ClapGhostSnare); clap != nullptr)
            clap->enabled = true;
        if (auto* sub = findTrackByType(project, TrackType::Sub808); sub != nullptr)
            sub->enabled = true;
        if (auto* ride = findTrackByType(project, TrackType::Ride); ride != nullptr)
            ride->enabled = true;
        if (auto* cymbal = findTrackByType(project, TrackType::Cymbal); cymbal != nullptr)
            cymbal->enabled = true;

        engine.generate(project);

        const auto* hat = findTrackByType(project, TrackType::HiHat);
        const auto* kick = findTrackByType(project, TrackType::Kick);
        const auto* snare = findTrackByType(project, TrackType::Snare);
        const auto* clap = findTrackByType(project, TrackType::ClapGhostSnare);
        const auto* ghostKick = findTrackByType(project, TrackType::GhostKick);
        const auto* openHat = findTrackByType(project, TrackType::OpenHat);
        const auto* perc = findTrackByType(project, TrackType::Perc);
        const auto* ride = findTrackByType(project, TrackType::Ride);
        const auto* cymbal = findTrackByType(project, TrackType::Cymbal);
        const auto* sub = findTrackByType(project, TrackType::Sub808);

        expect(hat != nullptr && kick != nullptr && snare != nullptr && clap != nullptr && sub != nullptr,
               "Rap WestCoast smoke requires HiHat, Kick, Snare, Clap and Sub tracks.");

        const auto hasStep = [](const TrackState* track, int step)
        {
            return track != nullptr && std::any_of(track->notes.begin(), track->notes.end(), [step](const NoteEvent& note)
            {
                return note.step == step;
            });
        };

        const auto findStep = [](const TrackState* track, int step)
        {
            return std::find_if(track->notes.begin(), track->notes.end(), [step](const NoteEvent& note)
            {
                return note.step == step;
            });
        };

        const auto countInBar = [](const TrackState* track, int bar)
        {
            if (track == nullptr)
                return 0;

            return static_cast<int>(std::count_if(track->notes.begin(), track->notes.end(), [bar](const NoteEvent& note)
            {
                return note.step / 16 == bar;
            }));
        };

        int phraseFunkKicks = 0;
        for (const auto& note : kick->notes)
        {
            const int step = ((note.step % 16) + 16) % 16;
            if (step == 5 || step == 7 || step == 10 || step == 13 || step == 15)
                ++phraseFunkKicks;
        }

        int phraseOpenHats = 0;
        for (const auto& note : openHat->notes)
        {
            if (((note.step % 16) + 16) % 16 == 6 || ((note.step % 16) + 16) % 16 == 14 || ((note.step % 16) + 16) % 16 == 15)
                ++phraseOpenHats;
        }

        for (int bar = 0; bar < project.params.bars; ++bar)
        {
            const int beat2 = bar * 16 + 4;
            const int beat4 = bar * 16 + 12;

            const auto beat2Snare = findStep(snare, beat2);
            const auto beat4Snare = findStep(snare, beat4);
            expect(beat2Snare != snare->notes.end() && beat4Snare != snare->notes.end(),
                   "Rap WestCoast should keep the snare on beat 2 and beat 4.");
            expect(!beat2Snare->isGhost && !beat4Snare->isGhost
                   && beat2Snare->velocity >= 92 && beat4Snare->velocity >= 92,
                   "Rap WestCoast snare anchors should be firm but smoother than EastCoast.");
            expect(beat2Snare->microOffset >= 7 && beat2Snare->microOffset <= 26
                   && beat4Snare->microOffset >= 7 && beat4Snare->microOffset <= 26,
                   "Rap WestCoast snare anchors should lean later for laid-back bounce.");

            expect(countInBar(clap, bar) >= 1 && countInBar(clap, bar) <= 2,
                   "Rap WestCoast should layer snare with clap color.");

            expect(countInBar(hat, bar) >= 8 && countInBar(hat, bar) <= (bar == project.params.bars - 1 ? 10 : 9),
                   "Rap WestCoast hats should be smooth eighth carriers with light 16th bounce.");

            int swungOffbeats = 0;
            for (const int stepInBar : { 2, 6, 10, 14 })
            {
                const auto hatHit = findStep(hat, bar * 16 + stepInBar);
                if (hatHit != hat->notes.end())
                {
                    ++swungOffbeats;
                    expect(hatHit->microOffset >= 18 && hatHit->microOffset <= 66,
                           "Rap WestCoast offbeat hats should have a wider laid-back swing.");
                    expect(hatHit->velocity <= 88,
                           "Rap WestCoast hats should stay smooth, not modern-bright.");
                }
            }
            expect(swungOffbeats >= 3,
                   "Rap WestCoast hats need swung offbeats to carry the bounce.");

            int oddHatHits = 0;
            for (const auto& note : hat->notes)
            {
                if (note.step / 16 == bar && (((note.step % 16) + 16) % 16) % 2 == 1)
                    ++oddHatHits;
            }
            expect(oddHatHits <= 2,
                   "Rap WestCoast should use 16th hats as bounce, not trap chatter.");

            expect(hasStep(kick, bar * 16),
                   "Rap WestCoast should ground each bar with a kick on the one.");
            expect(countInBar(kick, bar) >= 3 && countInBar(kick, bar) <= (bar == project.params.bars - 1 ? 5 : 4),
                   "Rap WestCoast kicks should be bouncy but not overcrowded.");
            expect(!hasStep(kick, beat2) && !hasStep(kick, beat4),
                   "Rap WestCoast kick should not collide with the snare/clap backbeat.");

            expect(countInBar(ghostKick, bar) <= 1,
                   "Rap WestCoast ghost kicks should stay as small pickup color.");
            expect(countInBar(openHat, bar) <= (bar == project.params.bars - 1 ? 2 : 1),
                   "Rap WestCoast open hats should be controlled phrase lift.");
            expect(countInBar(perc, bar) <= (bar == project.params.bars - 1 ? 2 : 1),
                   "Rap WestCoast percussion should be light G-funk texture.");
            expect(countInBar(sub, bar) >= 1 && countInBar(sub, bar) <= 2,
                   "Rap WestCoast should keep a simple low-end bounce tied to kick anchors.");
            expect(countInBar(ride, bar) == 0 && countInBar(cymbal, bar) == 0,
                   "Rap WestCoast should avoid ride/cymbal gloss.");
        }

        expect(phraseFunkKicks >= project.params.bars,
               "Rap WestCoast should answer the one with syncopated funk kicks.");
        expect(phraseOpenHats >= 1,
               "Rap WestCoast should include occasional open-hat lift.");
    }
}

void testRapWestCoastControlsInfluenceSmoke()
{
    RapEngine engine;

    auto low = createDefaultProject();
    low.params.genre = GenreType::Rap;
    low.params.rapSubstyle = 1;
    low.params.bars = 4;
    low.params.seed = 7601;
    low.params.bpm = 94.0f;
    low.params.swingPercent = 52.0f;
    low.params.densityAmount = 0.18f;
    low.params.timingAmount = 0.12f;
    low.params.humanizeAmount = 0.10f;
    low.params.velocityAmount = 0.18f;

    if (auto* sub = findTrackByType(low, TrackType::Sub808); sub != nullptr)
        sub->enabled = true;
    if (auto* openHat = findTrackByType(low, TrackType::OpenHat); openHat != nullptr)
        openHat->enabled = true;

    auto high = low;
    high.params.swingPercent = 60.0f;
    high.params.densityAmount = 0.84f;
    high.params.timingAmount = 0.84f;
    high.params.humanizeAmount = 0.84f;
    high.params.velocityAmount = 0.84f;

    engine.generate(low);
    engine.generate(high);

    const auto* lowHat = findTrackByType(low, TrackType::HiHat);
    const auto* highHat = findTrackByType(high, TrackType::HiHat);
    const auto* lowKick = findTrackByType(low, TrackType::Kick);
    const auto* highKick = findTrackByType(high, TrackType::Kick);
    const auto* lowSnare = findTrackByType(low, TrackType::Snare);
    const auto* highSnare = findTrackByType(high, TrackType::Snare);
    const auto* lowOpen = findTrackByType(low, TrackType::OpenHat);
    const auto* highOpen = findTrackByType(high, TrackType::OpenHat);

    expect(lowHat != nullptr && highHat != nullptr && lowKick != nullptr && highKick != nullptr
           && lowSnare != nullptr && highSnare != nullptr && lowOpen != nullptr && highOpen != nullptr,
           "Rap WestCoast controls smoke requires core and open-hat tracks.");

    expect(highHat->notes.size() >= lowHat->notes.size(),
           "Rap WestCoast Density should not make hats thinner when raised.");
    expect(highKick->notes.size() >= lowKick->notes.size(),
           "Rap WestCoast Density should not make kicks thinner when raised.");
    expect(highOpen->notes.size() >= lowOpen->notes.size(),
           "Rap WestCoast Density should not make open-hat lift thinner when raised.");

    const auto averageOffbeatHatMicro = [](const TrackState& track)
    {
        int sum = 0;
        int count = 0;
        for (const auto& note : track.notes)
        {
            const int step = ((note.step % 16) + 16) % 16;
            if (step == 2 || step == 6 || step == 10 || step == 14)
            {
                sum += note.microOffset;
                ++count;
            }
        }

        return count > 0 ? static_cast<float>(sum) / static_cast<float>(count) : 0.0f;
    };

    expect(averageOffbeatHatMicro(*highHat) > averageOffbeatHatMicro(*lowHat) + 8.0f,
           "Rap WestCoast Swing should push offbeat hats later when raised.");

    const auto maxAbsMicro = [](const TrackState& track)
    {
        int out = 0;
        for (const auto& note : track.notes)
            out = std::max(out, std::abs(note.microOffset));
        return out;
    };

    expect(maxAbsMicro(*highKick) >= maxAbsMicro(*lowKick),
           "Rap WestCoast Timing/Humanize should allow wider kick microtiming when raised.");

    const auto averageMainSnareVelocity = [](const TrackState& track)
    {
        int sum = 0;
        int count = 0;
        for (const auto& note : track.notes)
        {
            const int step = ((note.step % 16) + 16) % 16;
            if (!note.isGhost && (step == 4 || step == 12))
            {
                sum += note.velocity;
                ++count;
            }
        }
        return count > 0 ? static_cast<float>(sum) / static_cast<float>(count) : 0.0f;
    };

    expect(averageMainSnareVelocity(*highSnare) > averageMainSnareVelocity(*lowSnare),
           "Rap WestCoast Velocity should lift snare/clap accents when raised.");
}

void testRapDirtySouthPocketGenerationSmoke()
{
    RapEngine engine;

    for (int seed = 7700; seed < 7712; ++seed)
    {
        auto project = createDefaultProject();
        project.params.genre = GenreType::Rap;
        project.params.rapSubstyle = 2;
        project.params.bars = 4;
        project.params.seed = seed;
        project.params.bpm = 80.0f;
        project.params.swingPercent = 54.5f;
        project.params.densityAmount = 0.56f;
        project.params.timingAmount = 0.36f;
        project.params.humanizeAmount = 0.30f;
        project.params.velocityAmount = 0.56f;

        for (const auto type : { TrackType::ClapGhostSnare, TrackType::OpenHat, TrackType::Sub808, TrackType::Perc, TrackType::Cymbal, TrackType::Ride, TrackType::HatFX })
        {
            if (auto* track = findTrackByType(project, type); track != nullptr)
                track->enabled = true;
        }

        engine.generate(project);

        const auto* hat = findTrackByType(project, TrackType::HiHat);
        const auto* kick = findTrackByType(project, TrackType::Kick);
        const auto* snare = findTrackByType(project, TrackType::Snare);
        const auto* clap = findTrackByType(project, TrackType::ClapGhostSnare);
        const auto* ghostKick = findTrackByType(project, TrackType::GhostKick);
        const auto* openHat = findTrackByType(project, TrackType::OpenHat);
        const auto* perc = findTrackByType(project, TrackType::Perc);
        const auto* ride = findTrackByType(project, TrackType::Ride);
        const auto* cymbal = findTrackByType(project, TrackType::Cymbal);
        const auto* hatFx = findTrackByType(project, TrackType::HatFX);
        const auto* sub = findTrackByType(project, TrackType::Sub808);

        expect(hat != nullptr && kick != nullptr && snare != nullptr && clap != nullptr && sub != nullptr,
               "Rap DirtySouth smoke requires HiHat, Kick, Snare, Clap and Sub tracks.");

        const auto hasStep = [](const TrackState* track, int step)
        {
            return track != nullptr && std::any_of(track->notes.begin(), track->notes.end(), [step](const NoteEvent& note)
            {
                return note.step == step;
            });
        };

        const auto findStep = [](const TrackState* track, int step)
        {
            return std::find_if(track->notes.begin(), track->notes.end(), [step](const NoteEvent& note)
            {
                return note.step == step;
            });
        };

        const auto countInBar = [](const TrackState* track, int bar)
        {
            if (track == nullptr)
                return 0;

            return static_cast<int>(std::count_if(track->notes.begin(), track->notes.end(), [bar](const NoteEvent& note)
            {
                return note.step / 16 == bar;
            }));
        };

        int phrasePickupKicks = 0;
        for (const auto& note : kick->notes)
        {
            const int step = ((note.step % 16) + 16) % 16;
            if (step == 3 || step == 6 || step == 10 || step == 14 || step == 15)
                ++phrasePickupKicks;
        }

        int phraseOpenHats = 0;
        for (const auto& note : openHat->notes)
        {
            const int step = ((note.step % 16) + 16) % 16;
            if (step == 6 || step == 14 || step == 15)
                ++phraseOpenHats;
        }

        for (int bar = 0; bar < project.params.bars; ++bar)
        {
            const int beat2 = bar * 16 + 4;
            const int beat4 = bar * 16 + 12;

            const auto beat2Snare = findStep(snare, beat2);
            const auto beat4Snare = findStep(snare, beat4);
            expect(beat2Snare != snare->notes.end() && beat4Snare != snare->notes.end(),
                   "Rap DirtySouth should keep the snare on beat 2 and beat 4.");
            expect(!beat2Snare->isGhost && !beat4Snare->isGhost
                   && beat2Snare->velocity >= 94 && beat4Snare->velocity >= 94,
                   "Rap DirtySouth snares should stay strong enough for clap stacking.");
            expect(beat2Snare->microOffset >= 6 && beat2Snare->microOffset <= 26
                   && beat4Snare->microOffset >= 6 && beat4Snare->microOffset <= 26,
                   "Rap DirtySouth snares should lean late but stay locked.");

            expect(countInBar(clap, bar) == 2,
                   "Rap DirtySouth should stack both backbeats with claps.");

            expect(countInBar(hat, bar) >= 8 && countInBar(hat, bar) <= (bar == project.params.bars - 1 ? 10 : 10),
                   "Rap DirtySouth hats should be eighth-led with selected 16th bounce.");

            int swungOffbeats = 0;
            for (const int stepInBar : { 2, 6, 10, 14 })
            {
                const auto hatHit = findStep(hat, bar * 16 + stepInBar);
                if (hatHit != hat->notes.end())
                {
                    ++swungOffbeats;
                    expect(hatHit->microOffset >= 14 && hatHit->microOffset <= 58,
                           "Rap DirtySouth offbeat hats should carry a southern swing pocket.");
                    expect(hatHit->velocity <= 94,
                           "Rap DirtySouth hats should be crisp but not trap-bright.");
                }
            }
            expect(swungOffbeats >= 3,
                   "Rap DirtySouth hats need enough swung offbeats to carry the bounce.");

            int oddHatHits = 0;
            for (const auto& note : hat->notes)
            {
                if (note.step / 16 == bar && (((note.step % 16) + 16) % 16) % 2 == 1)
                    ++oddHatHits;
            }
            expect(oddHatHits <= (bar == project.params.bars - 1 ? 3 : 2),
                   "Rap DirtySouth should use 16ths as bounce, not modern hat rolls.");

            expect(hasStep(kick, bar * 16),
                   "Rap DirtySouth should ground every bar with a kick on the one.");
            expect(countInBar(kick, bar) >= 3 && countInBar(kick, bar) <= (bar == project.params.bars - 1 ? 6 : 5),
                   "Rap DirtySouth kicks should be heavy, syncopated, and not overcrowded.");
            expect(!hasStep(kick, beat2) && !hasStep(kick, beat4),
                   "Rap DirtySouth kick should not collide with the snare/clap backbeat.");

            expect(countInBar(ghostKick, bar) <= 1,
                   "Rap DirtySouth ghost kicks should stay as small pickup color.");
            expect(countInBar(openHat, bar) <= (bar == project.params.bars - 1 ? 2 : 1),
                   "Rap DirtySouth open hats should be controlled southern lift.");
            expect(countInBar(perc, bar) <= (bar == project.params.bars - 1 ? 2 : 1),
                   "Rap DirtySouth percussion should be small bounce texture.");
            expect(countInBar(sub, bar) == 0,
                   "Rap DirtySouth should keep Sub808 disabled in this substyle.");
            expect(countInBar(cymbal, bar) <= 1,
                   "Rap DirtySouth cymbals should be occasional markers only.");
            expect(countInBar(ride, bar) == 0 && countInBar(hatFx, bar) == 0,
                   "Rap DirtySouth should avoid ride gloss and modern hat-fx chatter.");
        }

        expect(phrasePickupKicks >= project.params.bars,
               "Rap DirtySouth should answer the one with southern pickup kicks.");
        expect(phraseOpenHats >= 1,
               "Rap DirtySouth should include occasional open-hat lift.");
    }
}

void testRapDirtySouthControlsInfluenceSmoke()
{
    RapEngine engine;

    auto low = createDefaultProject();
    low.params.genre = GenreType::Rap;
    low.params.rapSubstyle = 2;
    low.params.bars = 4;
    low.params.seed = 7801;
    low.params.bpm = 80.0f;
    low.params.swingPercent = 51.0f;
    low.params.densityAmount = 0.18f;
    low.params.timingAmount = 0.12f;
    low.params.humanizeAmount = 0.10f;
    low.params.velocityAmount = 0.18f;

    for (const auto type : { TrackType::ClapGhostSnare, TrackType::OpenHat, TrackType::Sub808, TrackType::Perc, TrackType::Cymbal })
    {
        if (auto* track = findTrackByType(low, type); track != nullptr)
            track->enabled = true;
    }

    auto high = low;
    high.params.swingPercent = 59.0f;
    high.params.densityAmount = 0.84f;
    high.params.timingAmount = 0.84f;
    high.params.humanizeAmount = 0.84f;
    high.params.velocityAmount = 0.84f;

    engine.generate(low);
    engine.generate(high);

    const auto* lowHat = findTrackByType(low, TrackType::HiHat);
    const auto* highHat = findTrackByType(high, TrackType::HiHat);
    const auto* lowKick = findTrackByType(low, TrackType::Kick);
    const auto* highKick = findTrackByType(high, TrackType::Kick);
    const auto* lowSnare = findTrackByType(low, TrackType::Snare);
    const auto* highSnare = findTrackByType(high, TrackType::Snare);
    const auto* lowOpen = findTrackByType(low, TrackType::OpenHat);
    const auto* highOpen = findTrackByType(high, TrackType::OpenHat);
    const auto* lowSub = findTrackByType(low, TrackType::Sub808);
    const auto* highSub = findTrackByType(high, TrackType::Sub808);

    expect(lowHat != nullptr && highHat != nullptr && lowKick != nullptr && highKick != nullptr
           && lowSnare != nullptr && highSnare != nullptr && lowOpen != nullptr && highOpen != nullptr
           && lowSub != nullptr && highSub != nullptr,
           "Rap DirtySouth controls smoke requires core, open-hat and sub tracks.");

    expect(highHat->notes.size() >= lowHat->notes.size(),
           "Rap DirtySouth Density should not make hats thinner when raised.");
    expect(highKick->notes.size() >= lowKick->notes.size(),
           "Rap DirtySouth Density should not make kicks thinner when raised.");
    expect(highOpen->notes.size() >= lowOpen->notes.size(),
           "Rap DirtySouth Density should not make open hats thinner when raised.");
    expect(lowSub->notes.empty() && highSub->notes.empty()
           && !lowSub->enabled && !highSub->enabled,
           "Rap DirtySouth controls should keep Sub808 disabled even when density is raised.");

    const auto averageOffbeatHatMicro = [](const TrackState& track)
    {
        int sum = 0;
        int count = 0;
        for (const auto& note : track.notes)
        {
            const int step = ((note.step % 16) + 16) % 16;
            if (step == 2 || step == 6 || step == 10 || step == 14)
            {
                sum += note.microOffset;
                ++count;
            }
        }

        return count > 0 ? static_cast<float>(sum) / static_cast<float>(count) : 0.0f;
    };

    expect(averageOffbeatHatMicro(*highHat) > averageOffbeatHatMicro(*lowHat) + 8.0f,
           "Rap DirtySouth Swing should push offbeat hats later when raised.");

    const auto maxAbsMicro = [](const TrackState& track)
    {
        int out = 0;
        for (const auto& note : track.notes)
            out = std::max(out, std::abs(note.microOffset));
        return out;
    };

    expect(maxAbsMicro(*highKick) >= maxAbsMicro(*lowKick),
           "Rap DirtySouth Timing/Humanize should allow wider kick microtiming when raised.");

    const auto averageMainSnareVelocity = [](const TrackState& track)
    {
        int sum = 0;
        int count = 0;
        for (const auto& note : track.notes)
        {
            const int step = ((note.step % 16) + 16) % 16;
            if (!note.isGhost && (step == 4 || step == 12))
            {
                sum += note.velocity;
                ++count;
            }
        }
        return count > 0 ? static_cast<float>(sum) / static_cast<float>(count) : 0.0f;
    };

    expect(averageMainSnareVelocity(*highSnare) > averageMainSnareVelocity(*lowSnare),
           "Rap DirtySouth Velocity should lift backbeat accents when raised.");
}

void testRapGermanStreetPocketGenerationSmoke()
{
    RapEngine engine;

    for (int seed = 7900; seed < 7912; ++seed)
    {
        auto project = createDefaultProject();
        project.params.genre = GenreType::Rap;
        project.params.rapSubstyle = 3;
        project.params.bars = 4;
        project.params.seed = seed;
        project.params.bpm = 88.0f;
        project.params.swingPercent = 51.5f;
        project.params.densityAmount = 0.48f;
        project.params.timingAmount = 0.22f;
        project.params.humanizeAmount = 0.16f;
        project.params.velocityAmount = 0.54f;

        for (const auto type : { TrackType::ClapGhostSnare, TrackType::OpenHat, TrackType::Sub808, TrackType::Perc, TrackType::Cymbal, TrackType::Ride, TrackType::HatFX })
        {
            if (auto* track = findTrackByType(project, type); track != nullptr)
                track->enabled = true;
        }

        engine.generate(project);

        const auto* hat = findTrackByType(project, TrackType::HiHat);
        const auto* kick = findTrackByType(project, TrackType::Kick);
        const auto* snare = findTrackByType(project, TrackType::Snare);
        const auto* clap = findTrackByType(project, TrackType::ClapGhostSnare);
        const auto* ghostKick = findTrackByType(project, TrackType::GhostKick);
        const auto* openHat = findTrackByType(project, TrackType::OpenHat);
        const auto* perc = findTrackByType(project, TrackType::Perc);
        const auto* ride = findTrackByType(project, TrackType::Ride);
        const auto* cymbal = findTrackByType(project, TrackType::Cymbal);
        const auto* hatFx = findTrackByType(project, TrackType::HatFX);
        const auto* sub = findTrackByType(project, TrackType::Sub808);

        expect(hat != nullptr && kick != nullptr && snare != nullptr && clap != nullptr && sub != nullptr,
               "Rap GermanStreet smoke requires HiHat, Kick, Snare, Clap and Sub tracks.");

        const auto hasStep = [](const TrackState* track, int step)
        {
            return track != nullptr && std::any_of(track->notes.begin(), track->notes.end(), [step](const NoteEvent& note)
            {
                return note.step == step;
            });
        };

        const auto findStep = [](const TrackState* track, int step)
        {
            return std::find_if(track->notes.begin(), track->notes.end(), [step](const NoteEvent& note)
            {
                return note.step == step;
            });
        };

        const auto countInBar = [](const TrackState* track, int bar)
        {
            if (track == nullptr)
                return 0;

            return static_cast<int>(std::count_if(track->notes.begin(), track->notes.end(), [bar](const NoteEvent& note)
            {
                return note.step / 16 == bar;
            }));
        };

        int phraseHardKicks = 0;
        for (const auto& note : kick->notes)
        {
            const int step = ((note.step % 16) + 16) % 16;
            if (step == 6 || step == 8 || step == 10 || step == 14 || step == 15)
                ++phraseHardKicks;
        }

        int ghostSnares = 0;
        for (const auto& note : snare->notes)
        {
            const int step = ((note.step % 16) + 16) % 16;
            if (note.isGhost)
            {
                ++ghostSnares;
                expect((step == 3 || step == 11) && note.velocity <= 56,
                       "Rap GermanStreet ghost snares should be rare, quiet, and only pre-backbeat color.");
            }
        }
        expect(ghostSnares <= project.params.bars,
               "Rap GermanStreet should not flood the pattern with ghost snares.");

        for (int bar = 0; bar < project.params.bars; ++bar)
        {
            const int beat2 = bar * 16 + 4;
            const int beat4 = bar * 16 + 12;

            const auto beat2Snare = findStep(snare, beat2);
            const auto beat4Snare = findStep(snare, beat4);
            expect(beat2Snare != snare->notes.end() && beat4Snare != snare->notes.end(),
                   "Rap GermanStreet should keep the dry snare on beat 2 and beat 4.");
            expect(!beat2Snare->isGhost && !beat4Snare->isGhost
                   && beat2Snare->velocity >= 102 && beat4Snare->velocity >= 102,
                   "Rap GermanStreet snares should hit hard and upfront.");
            expect(beat2Snare->microOffset >= -2 && beat2Snare->microOffset <= 12
                   && beat4Snare->microOffset >= -2 && beat4Snare->microOffset <= 12,
                   "Rap GermanStreet snares should stay tight with only a small late lean.");

            expect(countInBar(hat, bar) >= 7 && countInBar(hat, bar) <= 8,
                   "Rap GermanStreet hats should be restrained eighth-led carriers.");

            int swungOffbeats = 0;
            for (const int stepInBar : { 2, 6, 10, 14 })
            {
                const auto hatHit = findStep(hat, bar * 16 + stepInBar);
                if (hatHit != hat->notes.end())
                {
                    ++swungOffbeats;
                    expect(hatHit->microOffset >= 3 && hatHit->microOffset <= 32,
                           "Rap GermanStreet offbeat hats should swing subtly, not slump.");
                    expect(hatHit->velocity <= 86,
                           "Rap GermanStreet hats should stay cold and controlled.");
                }
            }
            expect(swungOffbeats >= 3,
                   "Rap GermanStreet hats need enough offbeats for the marching street pocket.");

            int oddHatHits = 0;
            for (const auto& note : hat->notes)
            {
                if (note.step / 16 == bar && (((note.step % 16) + 16) % 16) % 2 == 1)
                    ++oddHatHits;
            }
            expect(oddHatHits <= 1,
                   "Rap GermanStreet should avoid modern 16th-hat chatter.");

            expect(hasStep(kick, bar * 16),
                   "Rap GermanStreet should ground every bar with a kick on the one.");
            expect(countInBar(kick, bar) >= 3 && countInBar(kick, bar) <= (bar == project.params.bars - 1 ? 5 : 4),
                   "Rap GermanStreet kicks should be hard, short, and not overcrowded.");
            expect(!hasStep(kick, beat2) && !hasStep(kick, beat4),
                   "Rap GermanStreet kick should not collide with the main snare backbeat.");

            expect(countInBar(clap, bar) <= 1,
                   "Rap GermanStreet clap support should be a dry single layer only.");
            if (countInBar(clap, bar) == 1)
            {
                const auto clapHit = findStep(clap, beat4);
                expect(clapHit != clap->notes.end() && clapHit->velocity <= 98,
                       "Rap GermanStreet clap layer should sit quietly on beat 4.");
            }

            expect(countInBar(ghostKick, bar) <= 1,
                   "Rap GermanStreet ghost kicks should stay as rare pickups.");
            expect(countInBar(openHat, bar) <= 1,
                   "Rap GermanStreet open hats should be phrase markers only.");
            expect(countInBar(perc, bar) <= 1,
                   "Rap GermanStreet percussion should stay almost absent.");
            expect(countInBar(ride, bar) == 0 && countInBar(cymbal, bar) == 0 && countInBar(hatFx, bar) == 0,
                   "Rap GermanStreet should avoid ride/cymbal gloss and hat-fx chatter.");
            expect(countInBar(sub, bar) == 0,
                   "Rap GermanStreet should keep Sub808 disabled.");
        }

        expect(sub->notes.empty() && !sub->enabled,
               "Rap GermanStreet should clear and disable Sub808 even when the lane is forced on.");
        expect(phraseHardKicks >= project.params.bars,
               "Rap GermanStreet should answer the one with hard syncopated kick punctuation.");
    }
}

void testRapGermanStreetControlsInfluenceSmoke()
{
    RapEngine engine;

    auto low = createDefaultProject();
    low.params.genre = GenreType::Rap;
    low.params.rapSubstyle = 3;
    low.params.bars = 4;
    low.params.seed = 8001;
    low.params.bpm = 88.0f;
    low.params.swingPercent = 50.5f;
    low.params.densityAmount = 0.18f;
    low.params.timingAmount = 0.10f;
    low.params.humanizeAmount = 0.08f;
    low.params.velocityAmount = 0.18f;

    for (const auto type : { TrackType::ClapGhostSnare, TrackType::OpenHat, TrackType::Sub808, TrackType::Perc, TrackType::Cymbal, TrackType::Ride, TrackType::HatFX })
    {
        if (auto* track = findTrackByType(low, type); track != nullptr)
            track->enabled = true;
    }

    auto high = low;
    high.params.swingPercent = 55.5f;
    high.params.densityAmount = 0.84f;
    high.params.timingAmount = 0.84f;
    high.params.humanizeAmount = 0.84f;
    high.params.velocityAmount = 0.84f;

    engine.generate(low);
    engine.generate(high);

    const auto* lowHat = findTrackByType(low, TrackType::HiHat);
    const auto* highHat = findTrackByType(high, TrackType::HiHat);
    const auto* lowKick = findTrackByType(low, TrackType::Kick);
    const auto* highKick = findTrackByType(high, TrackType::Kick);
    const auto* lowSnare = findTrackByType(low, TrackType::Snare);
    const auto* highSnare = findTrackByType(high, TrackType::Snare);
    const auto* lowOpen = findTrackByType(low, TrackType::OpenHat);
    const auto* highOpen = findTrackByType(high, TrackType::OpenHat);
    const auto* lowSub = findTrackByType(low, TrackType::Sub808);
    const auto* highSub = findTrackByType(high, TrackType::Sub808);

    expect(lowHat != nullptr && highHat != nullptr && lowKick != nullptr && highKick != nullptr
           && lowSnare != nullptr && highSnare != nullptr && lowOpen != nullptr && highOpen != nullptr
           && lowSub != nullptr && highSub != nullptr,
           "Rap GermanStreet controls smoke requires core, open-hat and sub tracks.");

    expect(highHat->notes.size() >= lowHat->notes.size(),
           "Rap GermanStreet Density should not make hats thinner when raised.");
    expect(highKick->notes.size() >= lowKick->notes.size(),
           "Rap GermanStreet Density should not make kicks thinner when raised.");
    expect(highOpen->notes.size() >= lowOpen->notes.size(),
           "Rap GermanStreet Density should not make phrase open hats thinner when raised.");
    expect(lowSub->notes.empty() && highSub->notes.empty()
           && !lowSub->enabled && !highSub->enabled,
           "Rap GermanStreet controls should keep Sub808 disabled even when density is raised.");

    const auto averageOffbeatHatMicro = [](const TrackState& track)
    {
        int sum = 0;
        int count = 0;
        for (const auto& note : track.notes)
        {
            const int step = ((note.step % 16) + 16) % 16;
            if (step == 2 || step == 6 || step == 10 || step == 14)
            {
                sum += note.microOffset;
                ++count;
            }
        }

        return count > 0 ? static_cast<float>(sum) / static_cast<float>(count) : 0.0f;
    };

    expect(averageOffbeatHatMicro(*highHat) > averageOffbeatHatMicro(*lowHat) + 5.0f,
           "Rap GermanStreet Swing should push offbeat hats later while staying tight.");

    const auto maxAbsMicro = [](const TrackState& track)
    {
        int out = 0;
        for (const auto& note : track.notes)
            out = std::max(out, std::abs(note.microOffset));
        return out;
    };

    expect(maxAbsMicro(*highKick) >= maxAbsMicro(*lowKick),
           "Rap GermanStreet Timing/Humanize should widen kick microtiming carefully when raised.");

    const auto averageMainSnareVelocity = [](const TrackState& track)
    {
        int sum = 0;
        int count = 0;
        for (const auto& note : track.notes)
        {
            const int step = ((note.step % 16) + 16) % 16;
            if (!note.isGhost && (step == 4 || step == 12))
            {
                sum += note.velocity;
                ++count;
            }
        }
        return count > 0 ? static_cast<float>(sum) / static_cast<float>(count) : 0.0f;
    };

    expect(averageMainSnareVelocity(*highSnare) > averageMainSnareVelocity(*lowSnare),
           "Rap GermanStreet Velocity should lift dry backbeat accents when raised.");
}

void testLaneSampleBankPreferredTagRotationSmoke()
{
    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("HPDG_CoreSampleTagTests");
    if (root.exists())
        root.deleteRecursively();

    const auto kickDir = root.getChildFile("Rap").getChildFile("Kick");
    const auto snareDir = root.getChildFile("Rap").getChildFile("Snare");
    expect(kickDir.createDirectory() && snareDir.createDirectory(),
           "Sample tag test must create temporary lane folders.");

    const auto writePlaceholder = [](const juce::File& file)
    {
        return file.replaceWithText("placeholder");
    };

    expect(writePlaceholder(kickDir.getChildFile("A_Generic_Kick.wav"))
           && writePlaceholder(kickDir.getChildFile("Dirty_South_Kick_A.wav"))
           && writePlaceholder(kickDir.getChildFile("Dirty_South_Kick_B.wav"))
           && writePlaceholder(snareDir.getChildFile("A_Generic_Snare.wav"))
           && writePlaceholder(snareDir.getChildFile("B_Generic_Snare.wav")),
           "Sample tag test must create placeholder wav entries.");

    SampleLibraryManager library;
    library.setRootDirectory(root);
    library.setGenre(GenreType::Rap);
    library.scan();

    LaneSampleBank bank;
    bank.applyLibrary(library);

    const std::vector<juce::String> dirtyTags { "Dirty_South", "Dirty South", "DirtySouth" };
    expect(bank.hasSamplesMatchingAnyTag(TrackType::Kick, dirtyTags),
           "Sample bank should detect Dirty_South-tagged kick samples.");
    expect(!bank.hasSamplesMatchingAnyTag(TrackType::Snare, dirtyTags),
           "Sample bank should report no tagged snare samples when only generic files exist.");

    expect(bank.selectIndex(TrackType::Kick, 0), "Sample bank should select the first kick.");
    expect(bank.selectNextMatchingAnyTag(TrackType::Kick, dirtyTags),
           "Sample bank should select a preferred tagged kick when one exists.");
    expect(bank.getSelectedName(TrackType::Kick).containsIgnoreCase("Dirty_South"),
           "DirtySouth generation should prefer Dirty_South-tagged lane samples.");

    const auto firstDirty = bank.getSelectedName(TrackType::Kick);
    expect(bank.selectNextMatchingAnyTag(TrackType::Kick, dirtyTags),
           "Sample bank should keep rotating inside the tagged sample group.");
    expect(bank.getSelectedName(TrackType::Kick) != firstDirty,
           "Tagged sample rotation should move to the next Dirty_South sample when available.");

    expect(bank.selectIndex(TrackType::Snare, 0), "Sample bank should select the first generic snare.");
    const auto firstSnare = bank.getSelectedName(TrackType::Snare);
    expect(bank.selectNextMatchingAnyTag(TrackType::Snare, dirtyTags),
           "Sample bank should fall back to normal rotation when no tagged sample exists.");
    expect(bank.getSelectedName(TrackType::Snare) != firstSnare,
           "Fallback sample rotation should still change the selected sample.");

    root.deleteRecursively();
}

void testBoomBapClassicAlgebraGeneratorSmoke()
{
    BoomBapClassicAlgebraParams params;
    params.seed = 4242;
    params.bars = 4;
    params.bpm = 88.0f;
    params.density = 0.58f;
    params.swing = 0.60f;
    params.humanize = 0.48f;
    params.variation = 0.42f;

    BoomBapClassicAlgebraGenerator generator;
    const auto first = generator.generate(params);
    const auto second = generator.generate(params);

    const auto allNotesEqual = [](const BoomBapClassicAlgebraPattern& a, const BoomBapClassicAlgebraPattern& b)
    {
        const auto lhs = a.allNotes();
        const auto rhs = b.allNotes();
        if (lhs.size() != rhs.size())
            return false;

        for (size_t index = 0; index < lhs.size(); ++index)
        {
            const auto& left = lhs[index];
            const auto& right = rhs[index];
            if (left.laneIndex != right.laneIndex
                || left.barIndex != right.barIndex
                || left.tick64 != right.tick64
                || left.length != right.length
                || left.velocity != right.velocity
                || left.microTimingTicks != right.microTimingTicks
                || left.role != right.role
                || left.roleString != right.roleString)
            {
                return false;
            }
        }

        return true;
    };

    expect(allNotesEqual(first, second),
           "BoomBap Classic Algebra generator should be deterministic for the same seed and params.");

    const auto hasLaneTick = [](const BoomBapClassicAlgebraPattern& pattern, int lane, int bar, int tickInBar)
    {
        const auto& notes = pattern.notesByLane[static_cast<size_t>(lane)];
        return std::any_of(notes.begin(), notes.end(), [bar, tickInBar](const auto& note)
        {
            return note.barIndex == bar && (note.tick64 % 64) == tickInBar;
        });
    };

    for (int bar = 0; bar < params.bars; ++bar)
    {
        expect(hasLaneTick(first, BoomBapClassicLanes::Snare, bar, 16),
               "BoomBap Classic Algebra should keep mandatory snare on beat 2.");
        expect(hasLaneTick(first, BoomBapClassicLanes::Snare, bar, 48),
               "BoomBap Classic Algebra should keep mandatory snare on beat 4.");
    }

    expect(hasLaneTick(first, BoomBapClassicLanes::Kick, 0, 0),
           "BoomBap Classic Algebra should anchor the first bar with kick on tick 0.");
    expect(first.notesByLane[BoomBapClassicLanes::Sub808].empty(),
           "BoomBap Classic Algebra should leave Sub808 empty by default.");
    expect(static_cast<int>(first.notesByLane[BoomBapClassicLanes::OpenHat].size()) <= 2,
           "BoomBap Classic Algebra should keep open hats rare.");
    expect(static_cast<int>(first.notesByLane[BoomBapClassicLanes::Cymbal].size()) <= 2,
           "BoomBap Classic Algebra should keep cymbals rare.");

    const auto& hats = first.notesByLane[BoomBapClassicLanes::HiHat];
    expect(hats.size() >= static_cast<size_t>(params.bars * 8),
           "BoomBap Classic Algebra should create the eighth-note hat pulse.");
    expect(!std::all_of(hats.begin() + 1, hats.end(), [&](const auto& note) { return note.velocity == hats.front().velocity; }),
           "BoomBap Classic Algebra hats should not have flat velocity.");

    bool hasSwungHat = false;
    for (const auto& note : hats)
    {
        const int tick = note.tick64 % 64;
        if ((tick == 8 || tick == 24 || tick == 40 || tick == 56) && note.microTimingTicks > 0)
            hasSwungHat = true;
    }
    expect(hasSwungHat,
           "BoomBap Classic Algebra should delay offbeat hats for swing.");

    int maxSnareVelocity = 0;
    for (const auto& note : first.notesByLane[BoomBapClassicLanes::Snare])
        maxSnareVelocity = std::max(maxSnareVelocity, note.velocity);
    for (const auto& note : first.notesByLane[BoomBapClassicLanes::ClapGhost])
        expect(note.velocity < maxSnareVelocity,
               "BoomBap Classic Algebra clap ghosts should be lower velocity than main snare.");

    expect(first.score.quality > 0.0f,
           "BoomBap Classic Algebra scorer should produce a positive selected quality.");
    expect(first.debugSummary.contains("style: Boom Bap Classic Algebra")
               && first.debugSummary.contains("selected candidate index")
               && first.debugSummary.contains("repairs applied"),
           "BoomBap Classic Algebra debug summary should include compact generation diagnostics.");
}

int runTest(const char* name, const std::function<void()>& test)
{
    try
    {
        test();
        std::cout << "[PASS] " << name << std::endl;
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "[FAIL] " << name << ": " << ex.what() << std::endl;
        return 1;
    }
}
} // namespace
} // namespace bbg

int main()
{
    using namespace bbg;

    int failures = 0;
    failures += runTest("Serialization roundtrip smoke", testSerializationRoundTripSmoke);
    failures += runTest("EQ state serialization and legacy migration smoke", testEqStateSerializationAndLegacyMigrationSmoke);
    failures += runTest("Compressor state serialization and legacy migration smoke", testCompressorStateSerializationAndLegacyMigrationSmoke);
    failures += runTest("Base pattern capture smoke", testBasePatternCaptureSmoke);
    failures += runTest("Performance transform from base smoke", testPerformanceTransformFromBaseSmoke);
    failures += runTest("Density authoring priority smoke", testDensityAuthoringPrioritySmoke);
    failures += runTest("Sub808 density safety smoke", testSub808DensitySafetySmoke);
    failures += runTest("Combined live controls genre regression smoke", testCombinedLiveControlsGenreRegressionSmoke);
    failures += runTest("Manual edit live control safety smoke", testManualEditLiveControlSafetySmoke);
    failures += runTest("Visible transform export consistency smoke", testVisibleTransformExportConsistencySmoke);
    failures += runTest("Style defaults smoke", testStyleDefaultsSmoke);
    failures += runTest("Generation BPM selection smoke", testGenerationBpmSelectionSmoke);
    failures += runTest("Lane-aware swing protection smoke", testLaneAwareSwingProtectionSmoke);
    failures += runTest("Swing roundtrip generation smoke", testSwingRoundTripGenerationSmoke);
    failures += runTest("Drill swing hat semantics smoke", testDrillSwingHatSemanticsSmoke);
    failures += runTest("ProjectStateController bars clamp", testProjectStateBarsClamp);
    failures += runTest("Style definition fallback smoke", testStyleDefinitionFallbackSmoke);
    failures += runTest("Drill style influence reference smoke", testDrillStyleInfluenceReferenceSmoke);
    failures += runTest("Drill engine applies style influence smoke", testDrillEngineAppliesStyleInfluenceSmoke);
    failures += runTest("Drill phrase planner smoke", testDrillPhrasePlannerSmoke);
    failures += runTest("Drill hat generation smoke", testDrillHatGenerationSmoke);
    failures += runTest("Drill generation captures base patterns smoke", testDrillGenerationCapturesBasePatternsSmoke);
    failures += runTest("Drill hat generator uses reference corpus smoke", testDrillHatGeneratorUsesReferenceCorpusSmoke);
    failures += runTest("Drill hat reference variant rotation smoke", testDrillHatReferenceVariantRotationSmoke);
    failures += runTest("Drill kick generator uses reference corpus smoke", testDrillKickGeneratorUsesReferenceCorpusSmoke);
    failures += runTest("Drill kick reference variant rotation smoke", testDrillKickReferenceVariantRotationSmoke);
    failures += runTest("Drill 808 generator uses reference corpus smoke", testDrill808GeneratorUsesReferenceCorpusSmoke);
    failures += runTest("Drill 808 generation compact smoke", testDrill808GenerationCompactSmoke);
    failures += runTest("Drill validator compactness smoke", testDrillValidatorCompactnessSmoke);
    failures += runTest("Drill snare generation smoke", testDrillSnareGenerationSmoke);
    failures += runTest("Drill hat validator proximity smoke", testDrillHatValidatorProximitySmoke);
    failures += runTest("Drill hat copy mostly still varies smoke", testDrillHatCopyMostlyStillVariesSmoke);
    failures += runTest("Drill full engine smoke", testDrillFullEngineSmoke);
    failures += runTest("Trap style influence smoke", testTrapStyleInfluenceSmoke);
    failures += runTest("Sample apply weights smoke", testSampleApplyWeightsSmoke);
    failures += runTest("Extract pattern blend and copy smoke", testExtractPatternBlendAndCopySmoke);
    failures += runTest("Lane sample preferred tag rotation smoke", testLaneSampleBankPreferredTagRotationSmoke);
    failures += runTest("Rap EastCoast pocket generation smoke", testRapEastCoastPocketGenerationSmoke);
    failures += runTest("Rap EastCoast controls influence smoke", testRapEastCoastControlsInfluenceSmoke);
    failures += runTest("Rap WestCoast pocket generation smoke", testRapWestCoastPocketGenerationSmoke);
    failures += runTest("Rap WestCoast controls influence smoke", testRapWestCoastControlsInfluenceSmoke);
    failures += runTest("Rap DirtySouth pocket generation smoke", testRapDirtySouthPocketGenerationSmoke);
    failures += runTest("Rap DirtySouth controls influence smoke", testRapDirtySouthControlsInfluenceSmoke);
    failures += runTest("Rap GermanStreet pocket generation smoke", testRapGermanStreetPocketGenerationSmoke);
    failures += runTest("Rap GermanStreet controls influence smoke", testRapGermanStreetControlsInfluenceSmoke);
    failures += runTest("Classic rule enforcer smoke", testClassicRuleEnforcerSmoke);
    failures += runTest("BoomBap Classic pocket generation smoke", testBoomBapClassicPocketGenerationSmoke);
    failures += runTest("BoomBap Classic Algebra generator smoke", testBoomBapClassicAlgebraGeneratorSmoke);
    failures += runTest("BoomBap Dusty pocket generation smoke", testBoomBapDustyPocketGenerationSmoke);
    failures += runTest("BoomBap Jazzy pocket generation smoke", testBoomBapJazzyPocketGenerationSmoke);
    failures += runTest("BoomBap Gold pocket generation smoke", testBoomBapGoldPocketGenerationSmoke);
    failures += runTest("BoomBap Russian Underground pocket generation smoke", testBoomBapRussianUndergroundPocketGenerationSmoke);
    failures += runTest("BoomBap LofiRap pocket generation smoke", testBoomBapLofiRapPocketGenerationSmoke);
    return failures == 0 ? 0 : 1;
}
