#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <stdexcept>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include "../Source/Core/PatternProject.h"
#include "../Source/Core/PatternProjectSerialization.h"
#include "../Source/Core/ProjectStateController.h"
#include "../Source/Analysis/SampleApplyWeights.h"
#include "../Source/Engine/BoomBapEngine.h"
#include "../Source/Engine/BoomBap/BoomBapBassGenerator.h"
#include "../Source/Engine/BoomBap/BoomBapClassicAlgebraGenerator.h"
#include "../Source/Engine/DnBEngine.h"
#include "../Source/Engine/DnB/DnBScorer.h"
#include "../Source/Engine/DnB/DnBBass.h"
#include "../Source/Engine/TechnoEngine.h"
#include "../Source/Engine/DrillEngine.h"
#include "../Source/Engine/Drill/DrillPatternValidator.h"
#include "../Source/Engine/Drill/DrillPhrasePlanner.h"
#include "../Source/Engine/Drill/DrillSnareGenerator.h"
#include "../Source/Engine/ExtractPatternBuilder.h"
#include "../Source/Engine/HiResTiming.h"
#include "../Source/Engine/GenerationModel/CandidateSelectionEngine.h"
#include "../Source/Engine/GenerationModel/PatternFeatureVector.h"
#include "../Source/Engine/GenerationModel/StyleTargetModel.h"
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
#include "../Source/Engine/TempoInterpretation.h"
#include "../Source/Engine/TrapEngine.h"
#include "../Source/Engine/Trap/TrapAlgebraEngine.h"
#include "../Source/Engine/Trap/TrapTempoContext.h"

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

int stepIndexOf(const NoteEvent& note) noexcept
{
    return note.gridTick / HiResTiming::kTicks1_16;
}

int stepIndexOf(const Sub808NoteEvent& note) noexcept
{
    return note.gridTick / HiResTiming::kTicks1_16;
}

int tickForStep(int step) noexcept
{
    return step * HiResTiming::kTicks1_16;
}

void testTempoInterpretationHalfTimeBandSelection()
{
    GeneratorParams params;

    params.tempoInterpretationMode = 2; // Half-time aware
    expect(selectTempoBand(90.0f, params, 120.0f, 140.0f, 98.0f, 126.0f) == TempoBand::Base,
           "Half-time aware should keep base tempo band for typical BoomBap tempos.");

    expect(selectTempoBand(140.0f, params, 120.0f, 140.0f, 98.0f, 126.0f) == TempoBand::Elevated,
           "Half-time aware should treat 140 BPM as half-time (Elevated) instead of Fast.");
}

void testTempoInterpretationAutoGenreFolding()
{
    GeneratorParams params;
    params.tempoInterpretationMode = 0; // Auto

    params.genre = GenreType::Trap;
    expect(selectTempoBand(70.0f, params, 120.0f, 145.0f, 100.0f, 132.0f) == TempoBand::Elevated,
           "Auto should interpret Trap 70 BPM as double-time (Elevated).");

    params.genre = GenreType::Drill;
    expect(selectTempoBand(70.0f, params, 120.0f, 145.0f, 100.0f, 132.0f) == TempoBand::Elevated,
           "Auto should interpret Drill 70 BPM as double-time (Elevated).");

    params.genre = GenreType::BoomBap;
    expect(selectTempoBand(140.0f, params, 120.0f, 140.0f, 98.0f, 126.0f) == TempoBand::Base,
           "Auto should interpret BoomBap 140 BPM as half-time (Base band after folding).");
}

void testTrapTempoContextScientificMapping()
{
    const auto at86 = resolveTrapTempo(86.0, 4);
    expect(at86.doubleTime && at86.clockMultiplier == 2.0,
           "Trap 86 BPM must select the double-time micro clock.");
    expect(std::abs(at86.styleBpm - 172.0) < 0.001,
           "Trap 86 BPM must resolve to 172 style BPM.");
    expect(at86.hostBars == 4 && at86.styleBars == 8,
           "Double-time must preserve four host bars while exposing eight style bars.");
    expect(at86.styleTick64ToHostPpq(1) == 30,
           "One style 1/64 tick at double-time must map to 30 host PPQ.");

    const auto at85 = resolveTrapTempo(85.0, 4);
    expect(std::abs(at85.styleBpm - 170.0) < 0.001,
           "Trap 85 BPM must resolve to 170 style BPM.");

    const auto highTempo = resolveTrapTempo(150.0, 4);
    expect(!highTempo.doubleTime && highTempo.styleBpm == 150.0,
           "Trap 150 BPM must retain the normal rhythmic clock.");

    expect(highTempo.straightSubdivisionPpq(16) == 240
               && highTempo.straightSubdivisionPpq(32) == 120
               && highTempo.straightSubdivisionPpq(64) == 60,
           "Straight Trap subdivisions must use exact PPQ values.");
    expect(highTempo.tripletSubdivisionPpq(8) == 320
               && highTempo.tripletSubdivisionPpq(16) == 160
               && highTempo.tripletSubdivisionPpq(32) == 80,
           "Trap triplets must use exact PPQ values, never tick64 approximations.");
}

void testSharedGenerationModelCore()
{
    PatternFeatureInput input;
    input.bars = 2;
    input.ticksPerBar = 64;
    input.events = {
        { GenerationLaneFamily::Kick, MusicalRole::Anchor, 0, 112, 0 },
        { GenerationLaneFamily::Hat, MusicalRole::Pulse, 0, 84, 0 },
        { GenerationLaneFamily::Snare, MusicalRole::Backbeat, 32, 118, 4 },
        { GenerationLaneFamily::Kick, MusicalRole::Response, 40, 98, 0 },
        { GenerationLaneFamily::Kick, MusicalRole::Anchor, 64, 110, 0 },
        { GenerationLaneFamily::Hat, MusicalRole::Pulse, 64, 78, 0 },
        { GenerationLaneFamily::Snare, MusicalRole::Backbeat, 96, 120, 5 },
        { GenerationLaneFamily::Kick, MusicalRole::Pickup, 120, 92, -2 }
    };
    const auto features = PatternFeatureExtractor::extract(input);
    expect(features.roleClarity > 0.99f, "Shared feature extractor must preserve typed musical intent.");
    expect(features.interlock > 0.0f && features.negativeSpace > 0.0f,
           "Shared feature extractor must measure cross-lane relations and negative space.");

    const std::vector<CandidateSelectionEntry> candidates {
        { 0.90f, 0.10f, true },
        { 0.88f, 0.90f, true },
        { 0.99f, 1.00f, false },
        { 0.40f, 1.00f, true }
    };
    const CandidateSelectionConfig config { 0.80f, 0.05f, 0.12f, 0.20f, 7781u };
    const auto first = CandidateSelectionEngine::select(candidates, config);
    const auto second = CandidateSelectionEngine::select(candidates, config);
    expect(first.selectedIndex == second.selectedIndex,
           "Shared near-best selection must remain deterministic for a fixed seed.");
    expect(first.selectedIndex != 2 && first.selectedIndex != 3 && first.nearBestPoolSize == 2,
           "Shared selector must reject hard-invalid and below-floor candidates.");

    StyleTargetProfile baseTarget {
        { 0.45f, 0.50f, 0.55f, 0.30f, 0.55f, 0.70f, 0.60f, 0.95f },
        { 0.15f, 0.20f, 0.20f, 0.20f, 0.15f, 0.20f, 0.20f, 0.10f },
        { 1.00f, 1.00f, 0.70f, 0.70f, 1.00f, 0.90f, 1.00f, 0.80f }
    };
    const auto sparseTarget = StyleTargetModel::withPerformanceIntent(baseTarget, 0.20f, 0.52f, 0.30f, 0.30f);
    const auto denseTarget = StyleTargetModel::withPerformanceIntent(baseTarget, 0.85f, 0.62f, 0.70f, 0.80f);
    expect(denseTarget.target.density > sparseTarget.target.density
               && denseTarget.target.negativeSpace < sparseTarget.target.negativeSpace
               && denseTarget.target.timingActivity > sparseTarget.target.timingActivity,
           "Style target must translate VST performance controls into coherent musical intent.");

    const auto exactMatch = StyleTargetModel::evaluate(denseTarget.target, denseTarget);
    auto poorFeatures = denseTarget.target;
    poorFeatures.density = 0.0f;
    poorFeatures.negativeSpace = 1.0f;
    poorFeatures.interlock = 0.0f;
    const auto poorMatch = StyleTargetModel::evaluate(poorFeatures, denseTarget);
    expect(exactMatch.fit > 0.999f && exactMatch.fit > poorMatch.fit,
           "Style target matching must prefer a candidate close to the requested musical profile.");
}

void testBoomBapProductionAlwaysUsesAlgebra()
{
    PatternProject project;
    project.params.genre = GenreType::BoomBap;
    project.params.bars = 2;
    project.params.bpm = 90.0f;
    project.params.densityAmount = 0.5f;
    project.params.swingPercent = 56.0f;
    project.params.boombapSubstyle = 0;

    project.runtimeLaneProfile = TrackRegistry::createDefaultRuntimeLaneProfile();
    project.tracks = TrackRegistry::createDefaultTrackStates(project.runtimeLaneProfile);
    BoomBapEngine engine;

    int algebraCount = 0;
    for (int seed = 1; seed <= 100; ++seed)
    {
        project.params.seed = seed;
        engine.generate(project);

        const bool actualAlgebra = project.generationDebugReport.startsWith("ALGEBRA");
        if (actualAlgebra)
            ++algebraCount;
        else
            fail("Production BoomBap must always use the Algebra backend; seed=" + juce::String(seed));
    }
    expect(algebraCount == 100, "All production BoomBap seeds must use Algebra.");
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
        return stepIndexOf(note) == step && note.timingOffsetTicks == microOffset && note.semanticRole == semanticRole;
    });
}

bool hasNoteAtStepAndMicro(const TrackState& track, int step, int microOffset)
{
    return std::any_of(track.notes.begin(), track.notes.end(), [&](const NoteEvent& note)
    {
        return stepIndexOf(note) == step && note.timingOffsetTicks == microOffset;
    });
}

bool hasNoteAtExactTick(const TrackState& track, int tick)
{
    return std::any_of(track.notes.begin(), track.notes.end(), [&](const NoteEvent& note)
    {
        return note.startTick() == tick;
    });
}

bool hasNoteAtExactTick(const TrackState& track, int tick, const juce::String& semanticRole)
{
    return std::any_of(track.notes.begin(), track.notes.end(), [&](const NoteEvent& note)
    {
        return note.startTick() == tick && note.semanticRole == semanticRole;
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
        return stepIndexOf(note) == step && (semanticRole.isEmpty() || note.semanticRole == semanticRole);
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
            || stepIndexOf(a) != stepIndexOf(b)
            || a.lengthTicks != b.lengthTicks
            || a.velocity != b.velocity
            || a.timingOffsetTicks != b.timingOffsetTicks
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
            || stepIndexOf(a) != stepIndexOf(b)
            || a.lengthTicks != b.lengthTicks
            || a.velocity != b.velocity
            || a.timingOffsetTicks != b.timingOffsetTicks
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
        if (current.gridTick + current.lengthTicks > next.gridTick + allowedOverlap * HiResTiming::kTicks1_16)
            return false;
        if (next.isSlide != current.glideToNext)
            return false;
    }

    return true;
}

bool noteMatchesBaseIdentity(const NoteEvent& note, const NoteEvent& baseNote)
{
    return note.pitch == baseNote.pitch
        && stepIndexOf(note) == stepIndexOf(baseNote)
        && note.lengthTicks == baseNote.lengthTicks
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

    {
NoteEvent note;
note.pitch = 36;
note.gridTick = (0) * HiResTiming::kTicks1_16;
note.lengthTicks = (1) * HiResTiming::kTicks1_16;
note.velocity = 112;
note.timingOffsetTicks = 0;
note.semanticRole = "smoke_kick";
kick->notes.push_back(note);
}
    {
NoteEvent note;
note.pitch = 36;
note.gridTick = (4) * HiResTiming::kTicks1_16;
note.lengthTicks = (1) * HiResTiming::kTicks1_16;
note.velocity = 96;
note.timingOffsetTicks = 0;
note.semanticRole = "smoke_kick_base";
kick->baseNotes.push_back(note);
}
    kick->performanceBaseParams.genre = GenreType::Trap;
    kick->performanceBaseParams.swingPercent = 58.0f;
    kick->performanceBaseParams.velocityAmount = 0.72f;
    kick->performanceBaseParams.timingAmount = 0.48f;
    kick->performanceBaseParams.humanizeAmount = 0.33f;
    kick->performanceBaseParams.densityAmount = 0.61f;
    kick->performanceBaseParams.bars = 4;
    kick->performanceBaseParams.trapSubstyle = 2;
    kick->hasPerformanceBaseParams = true;
    {
Sub808NoteEvent note;
note.pitch = 36;
note.gridTick = (0) * HiResTiming::kTicks1_16;
note.lengthTicks = (4) * HiResTiming::kTicks1_16;
note.velocity = 100;
note.timingOffsetTicks = 0;
note.semanticRole = "smoke_sub";
sub->sub808Notes.push_back(note);
}
    sub->notes = toLegacyNoteEvents(sub->sub808Notes);
    {
Sub808NoteEvent note;
note.pitch = 43;
note.gridTick = (6) * HiResTiming::kTicks1_16;
note.lengthTicks = (2) * HiResTiming::kTicks1_16;
note.velocity = 96;
note.timingOffsetTicks = 4;
note.semanticRole = "smoke_sub_base";
note.isSlide = true;
note.glideToNext = true;
sub->baseSub808Notes.push_back(note);
}
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

        {
    std::vector<NoteEvent> __notes;
    NoteEvent n0;
    n0.pitch = 42;
    n0.gridTick = (1) * HiResTiming::kTicks1_16;
    n0.lengthTicks = (1) * HiResTiming::kTicks1_16;
    n0.velocity = 90;
    n0.timingOffsetTicks = 0;
    n0.semanticRole = "visible_hat";
    __notes.push_back(n0);
    hat->notes = __notes;
}
        {
    std::vector<NoteEvent> __notes;
    NoteEvent __notes_n0;
    __notes_n0.pitch = 42;
    __notes_n0.gridTick = (7) * HiResTiming::kTicks1_16;
    __notes_n0.lengthTicks = (1) * HiResTiming::kTicks1_16;
    __notes_n0.velocity = 72;
    __notes_n0.timingOffsetTicks = 0;
    __notes_n0.semanticRole = "old_hat_base";
    __notes.push_back(__notes_n0);
    hat->baseNotes = __notes;
}

        {
    std::vector<NoteEvent> __notes;
    NoteEvent n0;
    n0.pitch = 36;
    n0.gridTick = (0) * HiResTiming::kTicks1_16;
    n0.lengthTicks = (1) * HiResTiming::kTicks1_16;
    n0.velocity = 116;
    n0.timingOffsetTicks = 0;
    n0.semanticRole = "visible_kick";
    __notes.push_back(n0);
    kick->notes = __notes;
}
        {
    std::vector<NoteEvent> __notes;
    NoteEvent __notes_n0;
    __notes_n0.pitch = 36;
    __notes_n0.gridTick = (8) * HiResTiming::kTicks1_16;
    __notes_n0.lengthTicks = (1) * HiResTiming::kTicks1_16;
    __notes_n0.velocity = 88;
    __notes_n0.timingOffsetTicks = 0;
    __notes_n0.semanticRole = "old_kick_base";
    __notes.push_back(__notes_n0);
    kick->baseNotes = __notes;
}

        {
    std::vector<Sub808NoteEvent> __notes;
    Sub808NoteEvent n0;
    n0.pitch = 36;
    n0.gridTick = (0) * HiResTiming::kTicks1_16;
    n0.lengthTicks = (4) * HiResTiming::kTicks1_16;
    n0.velocity = 98;
    n0.timingOffsetTicks = 0;
    n0.semanticRole = "visible_sub";
    __notes.push_back(n0);
    sub->sub808Notes = __notes;
}
        sub->notes = toLegacyNoteEvents(sub->sub808Notes);
        {
    std::vector<Sub808NoteEvent> __notes;
    Sub808NoteEvent __notes_n0;
    __notes_n0.pitch = 43;
    __notes_n0.gridTick = (10) * HiResTiming::kTicks1_16;
    __notes_n0.lengthTicks = (2) * HiResTiming::kTicks1_16;
    __notes_n0.velocity = 82;
    __notes_n0.timingOffsetTicks = 6;
    __notes_n0.semanticRole = "old_sub_base";
    __notes_n0.isSlide = true;
    __notes_n0.glideToNext = true;
    __notes.push_back(__notes_n0);
    sub->baseSub808Notes = __notes;
}
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

    {
    std::vector<NoteEvent> __notes;
    NoteEvent n0;
    n0.pitch = 42;
    n0.gridTick = (0) * HiResTiming::kTicks1_16;
    n0.lengthTicks = (1) * HiResTiming::kTicks1_16;
    n0.velocity = 92;
    n0.timingOffsetTicks = 0;
    n0.semanticRole = "drill_hat_backbone";
    __notes.push_back(n0);
    NoteEvent n1;
    n1.pitch = 42;
    n1.gridTick = (1) * HiResTiming::kTicks1_16;
    n1.lengthTicks = (1) * HiResTiming::kTicks1_16;
    n1.velocity = 76;
    n1.timingOffsetTicks = 0;
    n1.semanticRole = "drill_hat_support";
    __notes.push_back(n1);
    NoteEvent n2;
    n2.pitch = 42;
    n2.gridTick = (3) * HiResTiming::kTicks1_16;
    n2.lengthTicks = (1) * HiResTiming::kTicks1_16;
    n2.velocity = 72;
    n2.timingOffsetTicks = 0;
    n2.semanticRole = "drill_hat_support";
    __notes.push_back(n2);
    hat->notes = __notes;
}
    {
    std::vector<NoteEvent> __notes;
    NoteEvent n0;
    n0.pitch = 39;
    n0.gridTick = (5) * HiResTiming::kTicks1_16;
    n0.lengthTicks = (1) * HiResTiming::kTicks1_16;
    n0.velocity = 74;
    n0.timingOffsetTicks = 0;
    n0.semanticRole = "perc_support";
    __notes.push_back(n0);
    NoteEvent n1;
    n1.pitch = 39;
    n1.gridTick = (13) * HiResTiming::kTicks1_16;
    n1.lengthTicks = (1) * HiResTiming::kTicks1_16;
    n1.velocity = 78;
    n1.timingOffsetTicks = 0;
    n1.semanticRole = "perc_fill";
    __notes.push_back(n1);
    perc->notes = __notes;
}

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
    {
    std::vector<NoteEvent> __notes;
    NoteEvent n0;
    n0.pitch = 42;
    n0.gridTick = (0) * HiResTiming::kTicks1_16;
    n0.lengthTicks = (1) * HiResTiming::kTicks1_16;
    n0.velocity = 94;
    n0.timingOffsetTicks = 0;
    n0.semanticRole = "hat_backbone";
    __notes.push_back(n0);
    NoteEvent n1;
    n1.pitch = 42;
    n1.gridTick = (7) * HiResTiming::kTicks1_16;
    n1.lengthTicks = (1) * HiResTiming::kTicks1_16;
    n1.velocity = 82;
    n1.timingOffsetTicks = 0;
    n1.semanticRole = "hat_texture";
    __notes.push_back(n1);
    NoteEvent n2;
    n2.pitch = 42;
    n2.gridTick = (11) * HiResTiming::kTicks1_16;
    n2.lengthTicks = (1) * HiResTiming::kTicks1_16;
    n2.velocity = 74;
    n2.timingOffsetTicks = 0;
    n2.semanticRole = "hat_fill";
    __notes.push_back(n2);
    hat->notes = __notes;
}

    PatternPerformanceTransformEngine::captureBasePatterns(project, { TrackType::HiHat });
    const auto baseHat = hat->baseNotes;

    {
        NoteAuthoringState locked;
        locked.noteKey.gridTick = 7 * HiResTiming::kTicks1_16;
        locked.noteKey.timingOffsetTicks = 0;
        locked.noteKey.pitch = 42;
        locked.noteKey.lengthTicks = 1 * HiResTiming::kTicks1_16;
        locked.noteKey.isGhost = false;
        locked.anchorLocked = true;
        locked.importanceWeight = 100;

        NoteAuthoringState filler;
        filler.noteKey.gridTick = 11 * HiResTiming::kTicks1_16;
        filler.noteKey.timingOffsetTicks = 0;
        filler.noteKey.pitch = 42;
        filler.noteKey.lengthTicks = 1 * HiResTiming::kTicks1_16;
        filler.noteKey.isGhost = false;
        filler.anchorLocked = false;
        filler.importanceWeight = 5;

        project.authoring.noteMetadataByLane[hat->laneId] = { locked, filler };
    }

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
    {
    std::vector<Sub808NoteEvent> __notes;
    Sub808NoteEvent n0;
    n0.pitch = 36;
    n0.gridTick = (0) * HiResTiming::kTicks1_16;
    n0.lengthTicks = (2) * HiResTiming::kTicks1_16;
    n0.velocity = 100;
    n0.timingOffsetTicks = 0;
    n0.semanticRole = "trap_sub_anchor";
    __notes.push_back(n0);
    Sub808NoteEvent n1;
    n1.pitch = 38;
    n1.gridTick = (4) * HiResTiming::kTicks1_16;
    n1.lengthTicks = (2) * HiResTiming::kTicks1_16;
    n1.velocity = 92;
    n1.timingOffsetTicks = 0;
    n1.semanticRole = "trap_sub_move";
    n1.isLegato = true;
    n1.glideToNext = true;
    __notes.push_back(n1);
    Sub808NoteEvent n2;
    n2.pitch = 41;
    n2.gridTick = (6) * HiResTiming::kTicks1_16;
    n2.lengthTicks = (2) * HiResTiming::kTicks1_16;
    n2.velocity = 88;
    n2.timingOffsetTicks = 0;
    n2.semanticRole = "trap_sub_release";
    n2.isSlide = true;
    __notes.push_back(n2);
    Sub808NoteEvent n3;
    n3.pitch = 36;
    n3.gridTick = (8) * HiResTiming::kTicks1_16;
    n3.lengthTicks = (4) * HiResTiming::kTicks1_16;
    n3.velocity = 98;
    n3.timingOffsetTicks = 0;
    n3.semanticRole = "trap_sub_anchor";
    __notes.push_back(n3);
    sub->sub808Notes = __notes;
}
    sub->notes = toLegacyNoteEvents(sub->sub808Notes);

    PatternPerformanceTransformEngine::captureBasePatterns(project, { TrackType::Sub808 });
    const auto baseSub = sub->baseSub808Notes;

    project.params.densityAmount = 0.18f;
    PatternPerformanceTransformEngine::applyPerformanceFromBase(project);

    expect(sub->sub808Notes.size() < baseSub.size(),
        "Sub808 density smoke should reduce the number of visible Sub808 starts at lower density.");
    expect(hasSubStartAt(*sub, 0, "trap_sub_anchor") && hasSubStartAt(*sub, 8, "trap_sub_anchor"),
        "Sub808 density smoke should preserve core Trap sub anchors when thinning density.");
    expect(sub->sub808Notes.front().lengthTicks >= baseSub.front().lengthTicks,
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

        {
    std::vector<NoteEvent> __notes;
    NoteEvent n0;
    n0.pitch = 42;
    n0.gridTick = (0) * HiResTiming::kTicks1_16;
    n0.lengthTicks = (1) * HiResTiming::kTicks1_16;
    n0.velocity = 92;
    n0.timingOffsetTicks = 0;
    n0.semanticRole = "hat_backbone";
    __notes.push_back(n0);
    NoteEvent n1;
    n1.pitch = 42;
    n1.gridTick = (1) * HiResTiming::kTicks1_16;
    n1.lengthTicks = (1) * HiResTiming::kTicks1_16;
    n1.velocity = 78;
    n1.timingOffsetTicks = 0;
    n1.semanticRole = "hat_support";
    __notes.push_back(n1);
    NoteEvent n2;
    n2.pitch = 42;
    n2.gridTick = (3) * HiResTiming::kTicks1_16;
    n2.lengthTicks = (1) * HiResTiming::kTicks1_16;
    n2.velocity = 72;
    n2.timingOffsetTicks = 0;
    n2.semanticRole = "hat_support";
    __notes.push_back(n2);
    NoteEvent n3;
    n3.pitch = 42;
    n3.gridTick = (7) * HiResTiming::kTicks1_16;
    n3.lengthTicks = (1) * HiResTiming::kTicks1_16;
    n3.velocity = 66;
    n3.timingOffsetTicks = 0;
    n3.semanticRole = "hat_texture";
    __notes.push_back(n3);
    hat->notes = __notes;
}
        {
    std::vector<NoteEvent> __notes;
    NoteEvent n0;
    n0.pitch = 36;
    n0.gridTick = (0) * HiResTiming::kTicks1_16;
    n0.lengthTicks = (1) * HiResTiming::kTicks1_16;
    n0.velocity = 118;
    n0.timingOffsetTicks = 0;
    n0.semanticRole = "kick_anchor";
    __notes.push_back(n0);
    NoteEvent n1;
    n1.pitch = 36;
    n1.gridTick = (6) * HiResTiming::kTicks1_16;
    n1.lengthTicks = (1) * HiResTiming::kTicks1_16;
    n1.velocity = 92;
    n1.timingOffsetTicks = 0;
    n1.semanticRole = "kick_support";
    __notes.push_back(n1);
    kick->notes = __notes;
}
        {
    std::vector<NoteEvent> __notes;
    NoteEvent n0;
    n0.pitch = 38;
    n0.gridTick = (4) * HiResTiming::kTicks1_16;
    n0.lengthTicks = (1) * HiResTiming::kTicks1_16;
    n0.velocity = 108;
    n0.timingOffsetTicks = 0;
    n0.semanticRole = "snare_backbone";
    __notes.push_back(n0);
    NoteEvent n1;
    n1.pitch = 38;
    n1.gridTick = (12) * HiResTiming::kTicks1_16;
    n1.lengthTicks = (1) * HiResTiming::kTicks1_16;
    n1.velocity = 104;
    n1.timingOffsetTicks = 0;
    n1.semanticRole = "snare_backbone";
    __notes.push_back(n1);
    snare->notes = __notes;
}

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
            if (stepIndexOf(note) == 1 && note.semanticRole == "hat_support")
                snapshot.hatSupportOffset = note.timingOffsetTicks;
        }

        for (const auto& note : kick->notes)
        {
            if (stepIndexOf(note) == 0 && note.semanticRole == "kick_anchor")
            {
                snapshot.kickAnchorPresent = true;
                snapshot.kickAnchorOffset = note.timingOffsetTicks;
            }
        }

        for (const auto& note : snare->notes)
        {
            if (stepIndexOf(note) == 4 && note.semanticRole == "snare_backbone")
            {
                snapshot.snareAnchorPresent = true;
                snapshot.snareAnchorOffset = note.timingOffsetTicks;
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
    {
    std::vector<NoteEvent> __notes;
    NoteEvent n0;
    n0.pitch = 42;
    n0.gridTick = (0) * HiResTiming::kTicks1_16;
    n0.lengthTicks = (1) * HiResTiming::kTicks1_16;
    n0.velocity = 92;
    n0.timingOffsetTicks = 0;
    n0.semanticRole = "hat_backbone";
    __notes.push_back(n0);
    NoteEvent n1;
    n1.pitch = 42;
    n1.gridTick = (2) * HiResTiming::kTicks1_16;
    n1.lengthTicks = (1) * HiResTiming::kTicks1_16;
    n1.velocity = 78;
    n1.timingOffsetTicks = 0;
    n1.semanticRole = "hat_support";
    __notes.push_back(n1);
    NoteEvent n2;
    n2.pitch = 42;
    n2.gridTick = (6) * HiResTiming::kTicks1_16;
    n2.lengthTicks = (1) * HiResTiming::kTicks1_16;
    n2.velocity = 70;
    n2.timingOffsetTicks = 0;
    n2.semanticRole = "hat_fill";
    __notes.push_back(n2);
    hat->notes = __notes;
}
    {
    std::vector<Sub808NoteEvent> __notes;
    Sub808NoteEvent n0;
    n0.pitch = 36;
    n0.gridTick = (0) * HiResTiming::kTicks1_16;
    n0.lengthTicks = (4) * HiResTiming::kTicks1_16;
    n0.velocity = 100;
    n0.timingOffsetTicks = 0;
    n0.semanticRole = "sub_anchor";
    __notes.push_back(n0);
    Sub808NoteEvent n1;
    n1.pitch = 38;
    n1.gridTick = (8) * HiResTiming::kTicks1_16;
    n1.lengthTicks = (4) * HiResTiming::kTicks1_16;
    n1.velocity = 94;
    n1.timingOffsetTicks = 0;
    n1.semanticRole = "sub_support";
    __notes.push_back(n1);
    sub->sub808Notes = __notes;
}
    sub->notes = toLegacyNoteEvents(sub->sub808Notes);

    PatternPerformanceTransformEngine::captureBasePatterns(project, { TrackType::HiHat, TrackType::Sub808 });

    project.params.swingPercent = 60.0f;
    project.params.velocityAmount = 0.74f;
    project.params.timingAmount = 0.54f;
    project.params.humanizeAmount = 0.42f;
    project.params.densityAmount = 0.34f;
    PatternPerformanceTransformEngine::applyPerformanceFromBase(project);

    {
NoteEvent note;
note.pitch = 42;
note.gridTick = (10) * HiResTiming::kTicks1_16;
note.lengthTicks = (1) * HiResTiming::kTicks1_16;
note.velocity = 86;
note.timingOffsetTicks = 0;
note.semanticRole = "edited_hat_fill";
hat->notes.push_back(note);
}
    std::sort(hat->notes.begin(), hat->notes.end(), [](const NoteEvent& lhs, const NoteEvent& rhs)
    {
        if (stepIndexOf(lhs) != stepIndexOf(rhs))
            return stepIndexOf(lhs) < stepIndexOf(rhs);
        return lhs.pitch < rhs.pitch;
    });

    {
    std::vector<Sub808NoteEvent> __notes;
    Sub808NoteEvent n0;
    n0.pitch = 36;
    n0.gridTick = (0) * HiResTiming::kTicks1_16;
    n0.lengthTicks = (6) * HiResTiming::kTicks1_16;
    n0.velocity = 100;
    n0.timingOffsetTicks = 0;
    n0.semanticRole = "edited_sub_anchor";
    __notes.push_back(n0);
    Sub808NoteEvent n1;
    n1.pitch = 43;
    n1.gridTick = (8) * HiResTiming::kTicks1_16;
    n1.lengthTicks = (4) * HiResTiming::kTicks1_16;
    n1.velocity = 95;
    n1.timingOffsetTicks = 0;
    n1.semanticRole = "edited_sub_support";
    __notes.push_back(n1);
    sub->sub808Notes = __notes;
}
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

    {
    std::vector<NoteEvent> __notes;
    NoteEvent n0;
    n0.pitch = 42;
    n0.gridTick = (0) * HiResTiming::kTicks1_16;
    n0.lengthTicks = (1) * HiResTiming::kTicks1_16;
    n0.velocity = 92;
    n0.timingOffsetTicks = 0;
    n0.semanticRole = "drill_hat_backbone";
    __notes.push_back(n0);
    NoteEvent n1;
    n1.pitch = 42;
    n1.gridTick = (1) * HiResTiming::kTicks1_16;
    n1.lengthTicks = (1) * HiResTiming::kTicks1_16;
    n1.velocity = 78;
    n1.timingOffsetTicks = 0;
    n1.semanticRole = "drill_hat_support";
    __notes.push_back(n1);
    NoteEvent n2;
    n2.pitch = 42;
    n2.gridTick = (7) * HiResTiming::kTicks1_16;
    n2.lengthTicks = (1) * HiResTiming::kTicks1_16;
    n2.velocity = 66;
    n2.timingOffsetTicks = 0;
    n2.semanticRole = "drill_hat_transition";
    __notes.push_back(n2);
    hat->notes = __notes;
}
    {
    std::vector<NoteEvent> __notes;
    NoteEvent n0;
    n0.pitch = 36;
    n0.gridTick = (0) * HiResTiming::kTicks1_16;
    n0.lengthTicks = (1) * HiResTiming::kTicks1_16;
    n0.velocity = 118;
    n0.timingOffsetTicks = 0;
    n0.semanticRole = "drill_kick_anchor";
    __notes.push_back(n0);
    NoteEvent n1;
    n1.pitch = 36;
    n1.gridTick = (6) * HiResTiming::kTicks1_16;
    n1.lengthTicks = (1) * HiResTiming::kTicks1_16;
    n1.velocity = 94;
    n1.timingOffsetTicks = 0;
    n1.semanticRole = "drill_kick_support";
    __notes.push_back(n1);
    kick->notes = __notes;
}
    {
    std::vector<Sub808NoteEvent> __notes;
    Sub808NoteEvent n0;
    n0.pitch = 36;
    n0.gridTick = (0) * HiResTiming::kTicks1_16;
    n0.lengthTicks = (4) * HiResTiming::kTicks1_16;
    n0.velocity = 100;
    n0.timingOffsetTicks = 0;
    n0.semanticRole = "drill_sub_anchor";
    __notes.push_back(n0);
    Sub808NoteEvent n1;
    n1.pitch = 38;
    n1.gridTick = (6) * HiResTiming::kTicks1_16;
    n1.lengthTicks = (2) * HiResTiming::kTicks1_16;
    n1.velocity = 92;
    n1.timingOffsetTicks = 0;
    n1.semanticRole = "drill_sub_move";
    n1.isLegato = true;
    n1.glideToNext = true;
    __notes.push_back(n1);
    Sub808NoteEvent n2;
    n2.pitch = 41;
    n2.gridTick = (8) * HiResTiming::kTicks1_16;
    n2.lengthTicks = (4) * HiResTiming::kTicks1_16;
    n2.velocity = 96;
    n2.timingOffsetTicks = 0;
    n2.semanticRole = "drill_sub_hold";
    n2.isSlide = true;
    __notes.push_back(n2);
    sub->sub808Notes = __notes;
}
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
        expect(hasMidiNoteOnAt(fullNoteOns, 60, stepIndexOf(note) * 240 + note.timingOffsetTicks),
            "Export consistency smoke should export each visible transformed HiHat note at its visible tick.");

    for (const auto& note : kick->notes)
        expect(hasMidiNoteOnAt(fullNoteOns, 60, stepIndexOf(note) * 240 + note.timingOffsetTicks),
            "Export consistency smoke should export each visible transformed Kick note at its visible tick.");

    for (const auto& note : sub->sub808Notes)
        expect(hasMidiNoteOnAt(fullNoteOns, note.pitch, stepIndexOf(note) * 240 + note.timingOffsetTicks),
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
    {
    std::vector<NoteEvent> __notes;
    NoteEvent n0;
    n0.pitch = 42;
    n0.gridTick = (0) * HiResTiming::kTicks1_16;
    n0.lengthTicks = (1) * HiResTiming::kTicks1_16;
    n0.velocity = 92;
    n0.timingOffsetTicks = 0;
    n0.semanticRole = "drill_hat_backbone";
    __notes.push_back(n0);
    NoteEvent n1;
    n1.pitch = 42;
    n1.gridTick = (1) * HiResTiming::kTicks1_16;
    n1.lengthTicks = (1) * HiResTiming::kTicks1_16;
    n1.velocity = 76;
    n1.timingOffsetTicks = 0;
    n1.semanticRole = "drill_hat_support";
    __notes.push_back(n1);
    drillHat->notes = __notes;
}
    {
    std::vector<NoteEvent> __notes;
    NoteEvent n0;
    n0.pitch = 36;
    n0.gridTick = (0) * HiResTiming::kTicks1_16;
    n0.lengthTicks = (1) * HiResTiming::kTicks1_16;
    n0.velocity = 118;
    n0.timingOffsetTicks = 0;
    n0.semanticRole = "drill_kick_anchor";
    __notes.push_back(n0);
    NoteEvent n1;
    n1.pitch = 36;
    n1.gridTick = (6) * HiResTiming::kTicks1_16;
    n1.lengthTicks = (1) * HiResTiming::kTicks1_16;
    n1.velocity = 94;
    n1.timingOffsetTicks = 0;
    n1.semanticRole = "drill_kick_support";
    __notes.push_back(n1);
    drillKick->notes = __notes;
}

    PatternPerformanceTransformEngine::captureBasePatterns(drillProject, { TrackType::HiHat, TrackType::Kick });

    drillProject.params.swingPercent = 64.0f;
    PatternPerformanceTransformEngine::applyPerformanceFromBase(drillProject);

    expect(hasNoteAt(*drillKick, 0, 0, "drill_kick_anchor"),
        "Lane-aware swing smoke should keep Drill kick anchors grid-locked under swing changes.");
    expect(std::any_of(drillHat->notes.begin(), drillHat->notes.end(), [](const NoteEvent& note)
    {
        return stepIndexOf(note) == 1 && note.semanticRole == "drill_hat_support" && note.timingOffsetTicks > 0;
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
        {
    std::vector<NoteEvent> __notes;
    NoteEvent n0;
    n0.pitch = 42;
    n0.gridTick = (0) * HiResTiming::kTicks1_16;
    n0.lengthTicks = (1) * HiResTiming::kTicks1_16;
    n0.velocity = 92;
    n0.timingOffsetTicks = 0;
    n0.semanticRole = "hat_backbone";
    __notes.push_back(n0);
    NoteEvent n1;
    n1.pitch = 42;
    n1.gridTick = (1) * HiResTiming::kTicks1_16;
    n1.lengthTicks = (1) * HiResTiming::kTicks1_16;
    n1.velocity = 76;
    n1.timingOffsetTicks = 0;
    n1.semanticRole = "hat_support";
    __notes.push_back(n1);
    hat->notes = __notes;
}

        PatternPerformanceTransformEngine::captureBasePatterns(project, { TrackType::HiHat });

        project.params.swingPercent = 60.0f;
        PatternPerformanceTransformEngine::applyPerformanceFromBase(project);

        for (const auto& note : hat->notes)
            if (stepIndexOf(note) == 1 && note.semanticRole == "hat_support")
                return note.timingOffsetTicks;

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
            {
    std::vector<NoteEvent> __notes;
    NoteEvent n0;
    n0.pitch = 36;
    n0.gridTick = (0) * HiResTiming::kTicks1_16;
    n0.lengthTicks = (1) * HiResTiming::kTicks1_16;
    n0.velocity = 108;
    n0.timingOffsetTicks = 0;
    n0.semanticRole = "boom_bap_kick_anchor";
    __notes.push_back(n0);
    NoteEvent n1;
    n1.pitch = 36;
    n1.gridTick = (8) * HiResTiming::kTicks1_16;
    n1.lengthTicks = (1) * HiResTiming::kTicks1_16;
    n1.velocity = 102;
    n1.timingOffsetTicks = 0;
    n1.semanticRole = "boom_bap_kick_anchor";
    __notes.push_back(n1);
    kick->notes = __notes;
}
        }

        if (auto* hat = findTrackByType(project, TrackType::HiHat); hat != nullptr)
        {
            hat->laneRole = "boom_bap_hat";
            {
    std::vector<NoteEvent> __notes;
    NoteEvent n0;
    n0.pitch = 42;
    n0.gridTick = (0) * HiResTiming::kTicks1_16;
    n0.lengthTicks = (1) * HiResTiming::kTicks1_16;
    n0.velocity = 88;
    n0.timingOffsetTicks = 0;
    n0.semanticRole = "boom_bap_hat_backbone";
    __notes.push_back(n0);
    NoteEvent n1;
    n1.pitch = 42;
    n1.gridTick = (1) * HiResTiming::kTicks1_16;
    n1.lengthTicks = (1) * HiResTiming::kTicks1_16;
    n1.velocity = 80;
    n1.timingOffsetTicks = 0;
    n1.semanticRole = "boom_bap_hat_support";
    __notes.push_back(n1);
    NoteEvent n2;
    n2.pitch = 42;
    n2.gridTick = (3) * HiResTiming::kTicks1_16;
    n2.lengthTicks = (1) * HiResTiming::kTicks1_16;
    n2.velocity = 82;
    n2.timingOffsetTicks = 0;
    n2.semanticRole = "boom_bap_hat_support";
    __notes.push_back(n2);
    NoteEvent n3;
    n3.pitch = 42;
    n3.gridTick = (4) * HiResTiming::kTicks1_16;
    n3.lengthTicks = (1) * HiResTiming::kTicks1_16;
    n3.velocity = 88;
    n3.timingOffsetTicks = 0;
    n3.semanticRole = "boom_bap_hat_backbone";
    __notes.push_back(n3);
    hat->notes = __notes;
}
        }

        if (auto* openHat = findTrackByType(project, TrackType::OpenHat); openHat != nullptr)
        {
            openHat->laneRole = "boom_bap_open";
            {
    std::vector<NoteEvent> __notes;
    NoteEvent n0;
    n0.pitch = 46;
    n0.gridTick = (6) * HiResTiming::kTicks1_16;
    n0.lengthTicks = (1) * HiResTiming::kTicks1_16;
    n0.velocity = 84;
    n0.timingOffsetTicks = 0;
    n0.semanticRole = "boom_bap_hat_support";
    __notes.push_back(n0);
    openHat->notes = __notes;
}
        }

        if (auto* perc = findTrackByType(project, TrackType::Perc); perc != nullptr)
        {
            perc->laneRole = "boom_bap_texture";
            {
    std::vector<NoteEvent> __notes;
    NoteEvent n0;
    n0.pitch = 54;
    n0.gridTick = (7) * HiResTiming::kTicks1_16;
    n0.lengthTicks = (1) * HiResTiming::kTicks1_16;
    n0.velocity = 72;
    n0.timingOffsetTicks = 0;
    n0.semanticRole = "boom_bap_perc_support";
    __notes.push_back(n0);
    perc->notes = __notes;
}
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
    {
    std::vector<NoteEvent> __notes;
    NoteEvent n0;
    n0.pitch = 42;
    n0.gridTick = (0) * HiResTiming::kTicks1_16;
    n0.lengthTicks = (1) * HiResTiming::kTicks1_16;
    n0.velocity = 92;
    n0.timingOffsetTicks = 0;
    n0.semanticRole = "drill_hat_backbone";
    __notes.push_back(n0);
    NoteEvent n1;
    n1.pitch = 42;
    n1.gridTick = (1) * HiResTiming::kTicks1_16;
    n1.lengthTicks = (1) * HiResTiming::kTicks1_16;
    n1.velocity = 84;
    n1.timingOffsetTicks = 0;
    n1.semanticRole = "drill_hat_reference_copy";
    __notes.push_back(n1);
    NoteEvent n2;
    n2.pitch = 42;
    n2.gridTick = (3) * HiResTiming::kTicks1_16;
    n2.lengthTicks = (1) * HiResTiming::kTicks1_16;
    n2.velocity = 76;
    n2.timingOffsetTicks = 0;
    n2.semanticRole = "drill_hat_transition";
    __notes.push_back(n2);
    hat->notes = __notes;
}

    PatternPerformanceTransformEngine::captureBasePatterns(project, { TrackType::HiHat });
    const auto baseHat = hat->baseNotes;

    project.params.swingPercent = 75.0f;
    PatternPerformanceTransformEngine::applyPerformanceFromBase(project);

    int backboneOffset = 0;
    int referenceOffset = 0;
    int supportOffset = 0;

    for (const auto& note : hat->notes)
    {
        if (stepIndexOf(note) == 0 && note.semanticRole == "drill_hat_backbone")
            backboneOffset = note.timingOffsetTicks;
        else if (stepIndexOf(note) == 1 && note.semanticRole == "drill_hat_reference_copy")
            referenceOffset = note.timingOffsetTicks;
        else if (stepIndexOf(note) == 3 && note.semanticRole == "drill_hat_transition")
            supportOffset = note.timingOffsetTicks;
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
            if ((stepIndexOf(note) / 16) == bar.barIndex)
                ++barNoteCount;
            if ((stepIndexOf(note) / 16) == bar.barIndex && note.semanticRole == "drill_hat_reference_copy")
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
        return (note.startTick() % HiResTiming::kTicks1_16) != 0;
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
        const int expectedTick = step * 240 + 60;
        if (!hasNoteAtExactTick(*hat, expectedTick))
            missingPositions.add("tick=" + juce::String(expectedTick));
    }

    expect(missingPositions.isEmpty(),
           "Drill hats should preserve exact saved reference tick positions across the full engine path. Missing: "
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
        {
NoteEvent note;
note.pitch = 42;
note.gridTick = (0) * HiResTiming::kTicks1_16;
note.lengthTicks = (1) * HiResTiming::kTicks1_16;
note.velocity = 92;
note.timingOffsetTicks = 60;
note.semanticRole = "drill_hat_reference_copy";
hat->notes.push_back(note);
}
        {
NoteEvent note;
note.pitch = 42;
note.gridTick = (0) * HiResTiming::kTicks1_16;
note.lengthTicks = (1) * HiResTiming::kTicks1_16;
note.velocity = 70;
note.timingOffsetTicks = 90;
note.semanticRole = "drill_hat_subdivision";
hat->notes.push_back(note);
}
        {
NoteEvent note;
note.pitch = 42;
note.gridTick = (6) * HiResTiming::kTicks1_16;
note.lengthTicks = (1) * HiResTiming::kTicks1_16;
note.velocity = 86;
note.timingOffsetTicks = 0;
note.semanticRole = "drill_hat_backbone";
hat->notes.push_back(note);
}
        {
NoteEvent note;
note.pitch = 42;
note.gridTick = (6) * HiResTiming::kTicks1_16;
note.lengthTicks = (1) * HiResTiming::kTicks1_16;
note.velocity = 68;
note.timingOffsetTicks = 60;
note.semanticRole = "drill_hat_burst";
hat->notes.push_back(note);
}
        {
NoteEvent note;
note.pitch = 42;
note.gridTick = (1) * HiResTiming::kTicks1_16;
note.lengthTicks = (1) * HiResTiming::kTicks1_16;
note.velocity = 74;
note.timingOffsetTicks = 90;
note.semanticRole = "drill_hat_transition";
hat->notes.push_back(note);
}

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

        expect(hasNoteAtExactTick(*hat, 0 * HiResTiming::kTicksPerBar4_4 + 20, "drill_hat_reference_copy"),
               "Drill hats should use the first saved reference variant on the first bar when rotating through multiple references.");
        expect(hasNoteAtExactTick(*hat, 1 * HiResTiming::kTicksPerBar4_4 + 60, "drill_hat_reference_copy"),
               "Drill hats should rotate to the second saved reference variant on the next bar instead of reusing the first one again.");
        expect(hasNoteAtExactTick(*hat, 2 * HiResTiming::kTicksPerBar4_4 + 100, "drill_hat_reference_copy"),
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
            return stepIndexOf(note) == step;
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

    expect(std::any_of(kick->notes.begin(), kick->notes.end(), [](const NoteEvent& note) { return stepIndexOf(note) == 4; }),
           "Drill kick rotation should use the first saved kick reference variant on the first bar.");
        expect(std::any_of(kick->notes.begin(), kick->notes.end(), [](const NoteEvent& note) { return stepIndexOf(note) == 26; }),
           "Drill kick rotation should use the second saved kick reference variant on the second bar instead of repeating the first one.");
    expect(std::any_of(kick->notes.begin(), kick->notes.end(), [](const NoteEvent& note) { return stepIndexOf(note) == 44; }),
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
    {
NoteEvent note;
note.pitch = 36;
note.gridTick = (0) * HiResTiming::kTicks1_16;
note.lengthTicks = (1) * HiResTiming::kTicks1_16;
note.velocity = 112;
note.timingOffsetTicks = 0;
note.semanticRole = "drill_kick_anchor";
kick->notes.push_back(note);
}

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
    {
NoteEvent note;
note.pitch = 36;
note.gridTick = (0) * HiResTiming::kTicks1_16;
note.lengthTicks = (1) * HiResTiming::kTicks1_16;
note.velocity = 112;
note.timingOffsetTicks = 0;
note.semanticRole = "drill_kick_anchor";
kick->notes.push_back(note);
}
    {
NoteEvent note;
note.pitch = 36;
note.gridTick = (10) * HiResTiming::kTicks1_16;
note.lengthTicks = (1) * HiResTiming::kTicks1_16;
note.velocity = 104;
note.timingOffsetTicks = 0;
note.semanticRole = "drill_kick_support";
kick->notes.push_back(note);
}
    {
NoteEvent note;
note.pitch = 36;
note.gridTick = (16) * HiResTiming::kTicks1_16;
note.lengthTicks = (1) * HiResTiming::kTicks1_16;
note.velocity = 112;
note.timingOffsetTicks = 0;
note.semanticRole = "drill_kick_anchor";
kick->notes.push_back(note);
}
    {
NoteEvent note;
note.pitch = 36;
note.gridTick = (23) * HiResTiming::kTicks1_16;
note.lengthTicks = (1) * HiResTiming::kTicks1_16;
note.velocity = 106;
note.timingOffsetTicks = 0;
note.semanticRole = "drill_kick_support";
kick->notes.push_back(note);
}
    {
NoteEvent note;
note.pitch = 36;
note.gridTick = (28) * HiResTiming::kTicks1_16;
note.lengthTicks = (1) * HiResTiming::kTicks1_16;
note.velocity = 108;
note.timingOffsetTicks = 0;
note.semanticRole = "drill_kick_support";
kick->notes.push_back(note);
}
    {
NoteEvent note;
note.pitch = 36;
note.gridTick = (32) * HiResTiming::kTicks1_16;
note.lengthTicks = (1) * HiResTiming::kTicks1_16;
note.velocity = 114;
note.timingOffsetTicks = 0;
note.semanticRole = "drill_kick_anchor";
kick->notes.push_back(note);
}
    {
NoteEvent note;
note.pitch = 36;
note.gridTick = (39) * HiResTiming::kTicks1_16;
note.lengthTicks = (1) * HiResTiming::kTicks1_16;
note.velocity = 106;
note.timingOffsetTicks = 0;
note.semanticRole = "drill_kick_support";
kick->notes.push_back(note);
}
    {
NoteEvent note;
note.pitch = 36;
note.gridTick = (46) * HiResTiming::kTicks1_16;
note.lengthTicks = (1) * HiResTiming::kTicks1_16;
note.velocity = 108;
note.timingOffsetTicks = 0;
note.semanticRole = "drill_kick_support";
kick->notes.push_back(note);
}
    {
NoteEvent note;
note.pitch = 36;
note.gridTick = (48) * HiResTiming::kTicks1_16;
note.lengthTicks = (1) * HiResTiming::kTicks1_16;
note.velocity = 112;
note.timingOffsetTicks = 0;
note.semanticRole = "drill_kick_anchor";
kick->notes.push_back(note);
}
    {
NoteEvent note;
note.pitch = 36;
note.gridTick = (62) * HiResTiming::kTicks1_16;
note.lengthTicks = (1) * HiResTiming::kTicks1_16;
note.velocity = 110;
note.timingOffsetTicks = 0;
note.semanticRole = "drill_kick_support";
kick->notes.push_back(note);
}

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
            if ((stepIndexOf(note) / 16) == bar.barIndex)
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
        expect((stepIndexOf(note) / 16) == (stepIndexOf(sub->sub808Notes[index + 1]) / 16),
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
        {
NoteEvent note;
note.pitch = 36;
note.gridTick = (0) * HiResTiming::kTicks1_16;
note.lengthTicks = (1) * HiResTiming::kTicks1_16;
note.velocity = 112;
note.timingOffsetTicks = 0;
note.semanticRole = "drill_kick_anchor";
kick->notes.push_back(note);
}
        {
NoteEvent note;
note.pitch = 36;
note.gridTick = (4) * HiResTiming::kTicks1_16;
note.lengthTicks = (1) * HiResTiming::kTicks1_16;
note.velocity = 96;
note.timingOffsetTicks = 0;
note.semanticRole = "drill_kick_support";
kick->notes.push_back(note);
}
        {
NoteEvent note;
note.pitch = 36;
note.gridTick = (10) * HiResTiming::kTicks1_16;
note.lengthTicks = (1) * HiResTiming::kTicks1_16;
note.velocity = 104;
note.timingOffsetTicks = 0;
note.semanticRole = "drill_kick_support";
kick->notes.push_back(note);
}
        {
NoteEvent note;
note.pitch = 36;
note.gridTick = (16) * HiResTiming::kTicks1_16;
note.lengthTicks = (1) * HiResTiming::kTicks1_16;
note.velocity = 112;
note.timingOffsetTicks = 0;
note.semanticRole = "drill_kick_anchor";
kick->notes.push_back(note);
}
        {
NoteEvent note;
note.pitch = 36;
note.gridTick = (20) * HiResTiming::kTicks1_16;
note.lengthTicks = (1) * HiResTiming::kTicks1_16;
note.velocity = 96;
note.timingOffsetTicks = 0;
note.semanticRole = "drill_kick_support";
kick->notes.push_back(note);
}
        {
NoteEvent note;
note.pitch = 36;
note.gridTick = (23) * HiResTiming::kTicks1_16;
note.lengthTicks = (1) * HiResTiming::kTicks1_16;
note.velocity = 102;
note.timingOffsetTicks = 0;
note.semanticRole = "drill_kick_support";
kick->notes.push_back(note);
}
        {
NoteEvent note;
note.pitch = 36;
note.gridTick = (28) * HiResTiming::kTicks1_16;
note.lengthTicks = (1) * HiResTiming::kTicks1_16;
note.velocity = 106;
note.timingOffsetTicks = 0;
note.semanticRole = "drill_kick_support";
kick->notes.push_back(note);
}
        {
NoteEvent note;
note.pitch = 36;
note.gridTick = (32) * HiResTiming::kTicks1_16;
note.lengthTicks = (1) * HiResTiming::kTicks1_16;
note.velocity = 114;
note.timingOffsetTicks = 0;
note.semanticRole = "drill_kick_anchor";
kick->notes.push_back(note);
}
        {
NoteEvent note;
note.pitch = 36;
note.gridTick = (35) * HiResTiming::kTicks1_16;
note.lengthTicks = (1) * HiResTiming::kTicks1_16;
note.velocity = 98;
note.timingOffsetTicks = 0;
note.semanticRole = "drill_kick_support";
kick->notes.push_back(note);
}
        {
NoteEvent note;
note.pitch = 36;
note.gridTick = (39) * HiResTiming::kTicks1_16;
note.lengthTicks = (1) * HiResTiming::kTicks1_16;
note.velocity = 104;
note.timingOffsetTicks = 0;
note.semanticRole = "drill_kick_support";
kick->notes.push_back(note);
}
        {
NoteEvent note;
note.pitch = 36;
note.gridTick = (46) * HiResTiming::kTicks1_16;
note.lengthTicks = (1) * HiResTiming::kTicks1_16;
note.velocity = 108;
note.timingOffsetTicks = 0;
note.semanticRole = "drill_kick_support";
kick->notes.push_back(note);
}
        {
NoteEvent note;
note.pitch = 36;
note.gridTick = (48) * HiResTiming::kTicks1_16;
note.lengthTicks = (1) * HiResTiming::kTicks1_16;
note.velocity = 112;
note.timingOffsetTicks = 0;
note.semanticRole = "drill_kick_anchor";
kick->notes.push_back(note);
}
        {
NoteEvent note;
note.pitch = 36;
note.gridTick = (52) * HiResTiming::kTicks1_16;
note.lengthTicks = (1) * HiResTiming::kTicks1_16;
note.velocity = 96;
note.timingOffsetTicks = 0;
note.semanticRole = "drill_kick_support";
kick->notes.push_back(note);
}
        {
NoteEvent note;
note.pitch = 36;
note.gridTick = (57) * HiResTiming::kTicks1_16;
note.lengthTicks = (1) * HiResTiming::kTicks1_16;
note.velocity = 102;
note.timingOffsetTicks = 0;
note.semanticRole = "drill_kick_support";
kick->notes.push_back(note);
}
        {
NoteEvent note;
note.pitch = 36;
note.gridTick = (62) * HiResTiming::kTicks1_16;
note.lengthTicks = (1) * HiResTiming::kTicks1_16;
note.velocity = 110;
note.timingOffsetTicks = 0;
note.semanticRole = "drill_kick_support";
kick->notes.push_back(note);
}

        sub->sub808Notes.clear();
        {
Sub808NoteEvent note;
note.pitch = 24;
note.gridTick = (0) * HiResTiming::kTicks1_16;
note.lengthTicks = (4) * HiResTiming::kTicks1_16;
note.velocity = 100;
note.timingOffsetTicks = 0;
note.semanticRole = "drill_sub_anchor";
sub->sub808Notes.push_back(note);
}
        {
Sub808NoteEvent note;
note.pitch = 27;
note.gridTick = (6) * HiResTiming::kTicks1_16;
note.lengthTicks = (2) * HiResTiming::kTicks1_16;
note.velocity = 94;
note.timingOffsetTicks = 0;
note.semanticRole = "drill_sub_anchor";
sub->sub808Notes.push_back(note);
}
        {
Sub808NoteEvent note;
note.pitch = 24;
note.gridTick = (16) * HiResTiming::kTicks1_16;
note.lengthTicks = (3) * HiResTiming::kTicks1_16;
note.velocity = 102;
note.timingOffsetTicks = 0;
note.semanticRole = "drill_sub_move";
sub->sub808Notes.push_back(note);
}
        {
Sub808NoteEvent note;
note.pitch = 31;
note.gridTick = (20) * HiResTiming::kTicks1_16;
note.lengthTicks = (2) * HiResTiming::kTicks1_16;
note.velocity = 92;
note.timingOffsetTicks = 0;
note.semanticRole = "drill_sub_move";
sub->sub808Notes.push_back(note);
}
        {
Sub808NoteEvent note;
note.pitch = 27;
note.gridTick = (28) * HiResTiming::kTicks1_16;
note.lengthTicks = (2) * HiResTiming::kTicks1_16;
note.velocity = 96;
note.timingOffsetTicks = 0;
note.semanticRole = "drill_sub_move";
note.isSlide = true;
note.isLegato = true;
sub->sub808Notes.push_back(note);
}
        {
Sub808NoteEvent note;
note.pitch = 24;
note.gridTick = (32) * HiResTiming::kTicks1_16;
note.lengthTicks = (3) * HiResTiming::kTicks1_16;
note.velocity = 104;
note.timingOffsetTicks = 0;
note.semanticRole = "drill_sub_move";
sub->sub808Notes.push_back(note);
}
        {
Sub808NoteEvent note;
note.pitch = 29;
note.gridTick = (36) * HiResTiming::kTicks1_16;
note.lengthTicks = (2) * HiResTiming::kTicks1_16;
note.velocity = 90;
note.timingOffsetTicks = 0;
note.semanticRole = "drill_sub_move";
sub->sub808Notes.push_back(note);
}
        {
Sub808NoteEvent note;
note.pitch = 31;
note.gridTick = (40) * HiResTiming::kTicks1_16;
note.lengthTicks = (2) * HiResTiming::kTicks1_16;
note.velocity = 92;
note.timingOffsetTicks = 0;
note.semanticRole = "drill_sub_move";
note.glideToNext = true;
sub->sub808Notes.push_back(note);
}
        {
Sub808NoteEvent note;
note.pitch = 24;
note.gridTick = (48) * HiResTiming::kTicks1_16;
note.lengthTicks = (5) * HiResTiming::kTicks1_16;
note.velocity = 106;
note.timingOffsetTicks = 0;
note.semanticRole = "drill_sub_release";
sub->sub808Notes.push_back(note);
}
        {
Sub808NoteEvent note;
note.pitch = 27;
note.gridTick = (56) * HiResTiming::kTicks1_16;
note.lengthTicks = (3) * HiResTiming::kTicks1_16;
note.velocity = 94;
note.timingOffsetTicks = 0;
note.semanticRole = "drill_sub_release";
sub->sub808Notes.push_back(note);
}
        {
Sub808NoteEvent note;
note.pitch = 31;
note.gridTick = (62) * HiResTiming::kTicks1_16;
note.lengthTicks = (2) * HiResTiming::kTicks1_16;
note.velocity = 96;
note.timingOffsetTicks = 0;
note.semanticRole = "drill_sub_release";
sub->sub808Notes.push_back(note);
}

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
             if ((stepIndexOf(note) / 16) == bar.barIndex)
              ++kickCount;
         for (const auto& note : sub->sub808Notes)
             if ((stepIndexOf(note) / 16) == bar.barIndex)
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
         expect((stepIndexOf(note) / 16) == (stepIndexOf(sub->sub808Notes[index + 1]) / 16),
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
            if ((stepIndexOf(note) / 16) != bar.barIndex)
                continue;

            const int stepInBar = stepIndexOf(note) % 16;
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
            if ((stepIndexOf(note) / 16) == bar.barIndex)
                ++hatFxBarCount;
        for (const auto& note : clapGhost->notes)
        {
            if ((stepIndexOf(note) / 16) != bar.barIndex)
                continue;

                ++clapGhostBarCount;
            const int stepInBar = stepIndexOf(note) % 16;
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
            if ((stepIndexOf(note) / 16) == bar.barIndex)
                ++kickBarCount;
         for (const auto& note : sub->sub808Notes)
             if ((stepIndexOf(note) / 16) == bar.barIndex)
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
        const int barIndex = stepIndexOf(kickNote) / 16;
        const int stepInBar = stepIndexOf(kickNote) % 16;
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
        expect((stepIndexOf(note) / 16) == (stepIndexOf(sub->sub808Notes[index + 1]) / 16),
               "Drill sub808 glides should stay inside a single bar after low-end simplification.");
    }

    expect(glideCount <= 1, "Drill sub808 should keep slide usage restrained after low-end simplification.");

    for (size_t index = 0; index < sub->sub808Notes.size(); ++index)
    {
        const auto& note = sub->sub808Notes[index];
        expect(isPitchInScale(note.pitch, project.params.keyRoot, project.params.scaleMode),
               "Drill sub808 pitches must stay inside the selected scale.");
        expect(note.lengthTicks >= 1, "Drill sub808 note length must remain positive.");
        expect(sub->notes[index].pitch == note.pitch && stepIndexOf(sub->notes[index]) == stepIndexOf(note),
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

    {
NoteEvent note;
note.pitch = 36;
note.gridTick = (63) * HiResTiming::kTicks1_16;
note.lengthTicks = (1) * HiResTiming::kTicks1_16;
note.velocity = 110;
note.timingOffsetTicks = 0;
note.semanticRole = "tail";
kick->notes.push_back(note);
}
    {
Sub808NoteEvent note;
note.pitch = 36;
note.gridTick = (62) * HiResTiming::kTicks1_16;
note.lengthTicks = (2) * HiResTiming::kTicks1_16;
note.velocity = 100;
note.timingOffsetTicks = 0;
note.semanticRole = "tail808";
sub->sub808Notes.push_back(note);
}
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

void testTrapAlgebraEngineSmoke()
{
    TrapAlgebraParams params;
    params.seed = 9090;
    params.bars = 4;
    params.density = 0.58f;
    params.swing = 0.55f;
    params.humanize = 0.42f;
    params.variation = 0.52f;
    params.temperature = 0.40f;
    params.qMin = 0.62f;
    params.candidateCount = 32;
    params.substyle = TrapAlgebraSubstyle::ATLClassic;

    TrapAlgebraEngine engine;
    const auto first = engine.generate(params);
    const auto second = engine.generate(params);

    const auto notesEqual = [](const TrapAlgebraPattern& a, const TrapAlgebraPattern& b)
    {
        const auto lhs = a.matrix.allNotes();
        const auto rhs = b.matrix.allNotes();
        if (lhs.size() != rhs.size())
            return false;

        for (size_t index = 0; index < lhs.size(); ++index)
        {
            const auto& left = lhs[index];
            const auto& right = rhs[index];
            if (left.laneIndex != right.laneIndex
                || left.barIndex != right.barIndex
                || left.tick64 != right.tick64
                || left.durationTicks != right.durationTicks
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

    expect(notesEqual(first, second),
           "Trap Algebra Engine should be deterministic for the same seed and params.");

    for (int bar = 0; bar < params.bars; ++bar)
        expect(first.matrix.hasNote(TrapAlgebraLanes::Snare, bar * 64 + 32),
               "Trap Algebra Engine must keep snare backbone on tick 32 in every bar.");

    int barsWithSnareAnswer = 0;
    for (int bar = 0; bar < params.bars; ++bar)
    {
        const auto kicks = first.matrix.notesForLane(TrapAlgebraLanes::Kick);
        std::vector<int> barKickTicks;
        for (const auto& kick : kicks)
            if (kick.barIndex == bar)
                barKickTicks.push_back(kick.tick64 % 64);

        expect(static_cast<int>(barKickTicks.size()) >= 1
                   && static_cast<int>(barKickTicks.size()) <= ((bar % 4) == 3 ? 4 : 3),
               "Trap Algebra Engine should keep main kick count inside the pocket range.");
        expect(std::none_of(barKickTicks.begin(), barKickTicks.end(), [bar](int tick)
        {
            juce::ignoreUnused(bar);
            return tick == 12 || tick == 28 || tick == 44 || tick == 52 || tick == 60;
        }), "Trap Algebra Engine should avoid stumbling main kick positions.");

        const bool hasSnareAnswer = std::any_of(barKickTicks.begin(), barKickTicks.end(), [](int tick)
        {
            return tick == 16 || tick == 24 || tick == 36 || tick == 40;
        });
        if (hasSnareAnswer)
            ++barsWithSnareAnswer;

        for (const auto& kick : kicks)
            if (kick.barIndex == bar)
                expect(kick.microTimingTicks == 0,
                       "Trap Algebra Engine should keep main kicks locked; hats carry swing, not the kick backbone.");
    }
    juce::ignoreUnused(barsWithSnareAnswer);

    // A distribution property, not one seed's: phrases where >= 3 of 4 bars answer the snare with a
    // kick on 16 / 24 / 36 / 40. Played trap loops: 52 % (docs/audit/reference/trap_kick_bars.tsv);
    // HPDG after Trap steps 1-3: 79-86 % (docs/audit/TRAP_STAGE.md). The fixed-phrase engine passed
    // it for every seed; with articulated phrases a single seed is a coin toss (RULE 3).
    int answeringPhrases = 0;
    constexpr int answerSeeds = 50;
    for (int s = 0; s < answerSeeds; ++s)
    {
        auto seedParams = params;
        seedParams.seed = 9090 + s * 7919;
        const auto pattern = engine.generate(seedParams);
        int answeringBars = 0;
        for (int bar = 0; bar < seedParams.bars; ++bar)
        {
            const auto kicks = pattern.matrix.notesForLane(TrapAlgebraLanes::Kick);
            answeringBars += std::any_of(kicks.begin(), kicks.end(), [bar](const auto& kick)
            {
                const int tick = kick.tick64 % 64;
                return kick.barIndex == bar && (tick == 16 || tick == 24 || tick == 36 || tick == 40);
            }) ? 1 : 0;
        }
        answeringPhrases += answeringBars >= 3 ? 1 : 0;
    }
    expect(answeringPhrases >= answerSeeds * 6 / 10,
           "Trap Algebra Engine should bind the kick phrase to the snare backbone across the phrase (>= 60 % of seeds).");

    // Kick-808 coupling as a rate over seeds (RULE 3): >= 60 % of 50 seeds with >= 70 % of kicks
    // carrying an 808 (measured 74-75 % of seeds at Trap steps 3-4; played trap: 43-45 % of kicks
    // carry an 808, docs/audit/TRAP_STAGE.md - HPDG keeps a tighter low end by design).
    int stronglyCoupled = 0;
    for (int s = 0; s < 50; ++s)
    {
        auto coupledParams = params;
        coupledParams.seed = 1000 + s * 131;
        stronglyCoupled += engine.generate(coupledParams).score.kick808CouplingRatio >= 0.70f ? 1 : 0;
    }
    expect(stronglyCoupled >= 30,
           "Trap Algebra Engine should strongly couple kick starts with 808 starts (>= 60 % of seeds).");
    expect(first.score.sub808Density >= 0.18f && first.score.sub808Density <= 0.65f,
           "Trap Algebra Engine should keep 808 density inside the musical negative-space range.");
    expect(first.score.hatVelocityVariance >= 40.0f,
           "Trap Algebra Engine hats should have non-flat velocity variance.");
    expect(first.score.kick808CouplingScore >= 0.60f,
           "Trap Algebra Engine should pass the core low-end trap validator.");
    expect(first.score.hiHatMovementScore >= 0.60f,
           "Trap Algebra Engine should pass the core hat-driver validator.");
    expect(first.score.negativeSpaceScore >= 0.35f,
           "Trap Algebra Engine should preserve trap negative space.");
    const auto firstAllNotes = first.matrix.allNotes();
    expect(std::none_of(firstAllNotes.begin(), firstAllNotes.end(), [](const auto& note)
    {
        return std::abs(note.microTimingTicks) >= 60;
    }), "Trap Algebra Engine microtiming must stay in PPQ subticks, not whole 1/64 ticks.");
    expect(first.score.d01 > 0.001f || first.score.d02 > 0.001f || first.score.d03 > 0.001f,
           "Trap Algebra Engine should not return four identical bars.");
    expect(first.score.quality > 0.50f,
           "Trap Algebra Engine should select by positive Q score.");
    expect(first.debugSummary.contains("Trap Algebra Engine")
               && first.debugSummary.contains("kick/808 coupling ratio")
               && first.debugSummary.contains("bar distances D01/D02/D03"),
           "Trap Algebra Engine debug summary should expose optimizer diagnostics.");

    std::vector<std::vector<int>> kickSignatures;
    for (int i = 0; i < 24; ++i)
    {
        auto variedParams = params;
        variedParams.seed = params.seed + i * 41;
        const auto pattern = engine.generate(variedParams);
        std::vector<int> signature;
        for (const auto& kick : pattern.matrix.notesForLane(TrapAlgebraLanes::Kick))
            signature.push_back(kick.tick64);
        std::sort(signature.begin(), signature.end());
        kickSignatures.push_back(signature);
    }

    int uniqueKickSignatures = 0;
    for (size_t i = 0; i < kickSignatures.size(); ++i)
    {
        const bool firstSeen = std::none_of(kickSignatures.begin(), kickSignatures.begin() + static_cast<std::ptrdiff_t>(i), [&](const auto& previous)
        {
            return previous == kickSignatures[i];
        });
        if (firstSeen)
            ++uniqueKickSignatures;
    }
    expect(uniqueKickSignatures >= 8,
           "Trap Algebra Engine should produce varied low-end phrases across generation seeds.");

    for (int absoluteTick = 1; absoluteTick < params.bars * 64; ++absoluteTick)
    {
        const bool presentInEveryPattern = std::all_of(kickSignatures.begin(), kickSignatures.end(), [absoluteTick](const auto& signature)
        {
            return std::find(signature.begin(), signature.end(), absoluteTick) != signature.end();
        });
        expect(!presentInEveryPattern,
               "Trap Algebra Engine should not hard-wire non-mandatory kick positions across generations: tick "
                   + std::to_string(absoluteTick));
    }

    const std::array<TrapAlgebraSubstyle, 6> substyles {{
        TrapAlgebraSubstyle::ATLClassic,
        TrapAlgebraSubstyle::DarkTrap,
        TrapAlgebraSubstyle::CloudTrap,
        TrapAlgebraSubstyle::RageTrap,
        TrapAlgebraSubstyle::MemphisTrap,
        TrapAlgebraSubstyle::LuxuryTrap
    }};
    std::vector<int> profileHatCounts;
    std::vector<int> profileRollCounts;
    for (const auto substyle : substyles)
    {
        auto styledParams = params;
        styledParams.substyle = substyle;
        styledParams.seed += static_cast<int>(profileHatCounts.size()) * 97;
        const auto pattern = engine.generate(styledParams);
        // Low-end core as a rate, not one seed's: 50 seeds, >= 70 % with kick-808 coupling >= 0.60
        // (measured: 83-97 % per substyle after Trap step 2, 79-99 % after step 3; docs/audit/TRAP_STAGE.md).
        int coupledSeeds = 0;
        for (int s = 0; s < 50; ++s)
        {
            auto coreParams = styledParams;
            coreParams.seed = 1000 + s * 131;
            coupledSeeds += engine.generate(coreParams).score.kick808CouplingScore >= 0.60f ? 1 : 0;
        }
        expect(coupledSeeds >= 35,
               "Every Trap Algebra substyle should retain trap low-end core (>= 70 % of seeds).");
        expect(pattern.score.hiHatMovementScore >= 0.55f,
               "Every Trap Algebra substyle should retain a readable hat driver.");
        expect(pattern.score.negativeSpaceScore >= 0.35f,
               "Every Trap Algebra substyle should retain negative space.");
        profileHatCounts.push_back(pattern.matrix.countLane(TrapAlgebraLanes::HiHat) + pattern.matrix.countLane(TrapAlgebraLanes::HatAccent));
        profileRollCounts.push_back(pattern.score.rollCount);
    }
    expect(*std::max_element(profileHatCounts.begin(), profileHatCounts.end()) > *std::min_element(profileHatCounts.begin(), profileHatCounts.end()),
           "Trap Algebra substyles should separate through hat-rate priors.");
    expect(*std::max_element(profileRollCounts.begin(), profileRollCounts.end()) > *std::min_element(profileRollCounts.begin(), profileRollCounts.end()),
           "Trap Algebra substyles should separate through roll-rate priors.");
}

void testTrapEngineUsesAlgebraGenerationSmoke()
{
    TrapEngine engine;

    auto project = createDefaultProject();
    project.params.genre = GenreType::Trap;
    project.params.trapSubstyle = 0;
    project.params.bars = 4;
    project.params.seed = 30303;
    project.params.bpm = 140.0f;
    project.params.densityAmount = 0.58f;
    project.params.swingPercent = 55.0f;
    project.params.humanizeAmount = 0.42f;
    project.params.timingAmount = 0.44f;
    project.params.velocityAmount = 0.50f;

    for (auto& track : project.tracks)
        track.enabled = true;

    engine.generate(project);

    const auto* snare = findTrackByType(project, TrackType::Snare);
    const auto* kick = findTrackByType(project, TrackType::Kick);
    const auto* sub = findTrackByType(project, TrackType::Sub808);
    const auto* hat = findTrackByType(project, TrackType::HiHat);
    expect(snare != nullptr && kick != nullptr && sub != nullptr && hat != nullptr,
           "Trap Algebra integration smoke requires Snare, Kick, Sub808 and HiHat tracks.");

    const auto hasStep = [](const TrackState* track, int step)
    {
        return track != nullptr && std::any_of(track->notes.begin(), track->notes.end(), [step](const NoteEvent& note)
        {
            return stepIndexOf(note) == step;
        });
    };

    for (int bar = 0; bar < project.params.bars; ++bar)
        expect(hasStep(snare, bar * 16 + 8),
               "TrapEngine Algebra path should keep the trap snare backbone on beat 3.");

    int coupledKicks = 0;
    for (const auto& kickNote : kick->notes)
    {
        const int kickTick = HiResTiming::noteTick(kickNote);
        const bool coupled = std::any_of(sub->notes.begin(), sub->notes.end(), [kickTick](const NoteEvent& subNote)
        {
            return std::abs(HiResTiming::noteTick(subNote) - kickTick) <= HiResTiming::kTicks1_32;
        });
        if (coupled)
            ++coupledKicks;
    }
    expect(!kick->notes.empty() && coupledKicks >= static_cast<int>(kick->notes.size() * 0.65f),
           "TrapEngine Algebra path should preserve measurable kick/808 coupling after conversion.");

    const auto velocityVariance = [](const std::vector<NoteEvent>& notes)
    {
        if (notes.size() < 2)
            return 0.0f;
        float mean = 0.0f;
        for (const auto& note : notes)
            mean += static_cast<float>(note.velocity);
        mean /= static_cast<float>(notes.size());
        float variance = 0.0f;
        for (const auto& note : notes)
        {
            const float delta = static_cast<float>(note.velocity) - mean;
            variance += delta * delta;
        }
        return variance / static_cast<float>(notes.size());
    };

    expect(velocityVariance(hat->notes) > 30.0f,
           "TrapEngine Algebra path should avoid flat metronomic hats.");
    expect(std::any_of(hat->notes.begin(), hat->notes.end(), [](const NoteEvent& note)
    {
        return note.semanticRole.startsWith("trap_algebra_");
    }), "TrapEngine should source the generated hat notes from the Algebra path.");
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
    {
    std::vector<NoteEvent> __notes;
    NoteEvent __notes_n0;
    __notes_n0.pitch = 36;
    __notes_n0.gridTick = (0) * HiResTiming::kTicks1_16;
    __notes_n0.lengthTicks = (1) * HiResTiming::kTicks1_16;
    __notes_n0.velocity = 112;
    __notes_n0.timingOffsetTicks = 0;
    __notes_n0.semanticRole = "genre_kick";
    __notes.push_back(__notes_n0);
    NoteEvent __notes_n1;
    __notes_n1.pitch = 36;
    __notes_n1.gridTick = (8) * HiResTiming::kTicks1_16;
    __notes_n1.lengthTicks = (1) * HiResTiming::kTicks1_16;
    __notes_n1.velocity = 108;
    __notes_n1.timingOffsetTicks = 0;
    __notes_n1.semanticRole = "genre_kick";
    __notes.push_back(__notes_n1);
    ProjectStateController::setTrackNotes(project, TrackType::Kick, __notes);
}
    {
    std::vector<NoteEvent> __notes;
    NoteEvent __notes_n0;
    __notes_n0.pitch = 38;
    __notes_n0.gridTick = (4) * HiResTiming::kTicks1_16;
    __notes_n0.lengthTicks = (1) * HiResTiming::kTicks1_16;
    __notes_n0.velocity = 118;
    __notes_n0.timingOffsetTicks = 0;
    __notes_n0.semanticRole = "genre_snare";
    __notes.push_back(__notes_n0);
    NoteEvent __notes_n1;
    __notes_n1.pitch = 38;
    __notes_n1.gridTick = (12) * HiResTiming::kTicks1_16;
    __notes_n1.lengthTicks = (1) * HiResTiming::kTicks1_16;
    __notes_n1.velocity = 118;
    __notes_n1.timingOffsetTicks = 0;
    __notes_n1.semanticRole = "genre_snare";
    __notes.push_back(__notes_n1);
    ProjectStateController::setTrackNotes(project, TrackType::Snare, __notes);
}
    {
    std::vector<NoteEvent> __notes;
    NoteEvent __notes_n0;
    __notes_n0.pitch = 42;
    __notes_n0.gridTick = (0) * HiResTiming::kTicks1_16;
    __notes_n0.lengthTicks = (1) * HiResTiming::kTicks1_16;
    __notes_n0.velocity = 92;
    __notes_n0.timingOffsetTicks = 0;
    __notes_n0.semanticRole = "genre_hat";
    __notes.push_back(__notes_n0);
    NoteEvent __notes_n1;
    __notes_n1.pitch = 42;
    __notes_n1.gridTick = (2) * HiResTiming::kTicks1_16;
    __notes_n1.lengthTicks = (1) * HiResTiming::kTicks1_16;
    __notes_n1.velocity = 88;
    __notes_n1.timingOffsetTicks = 0;
    __notes_n1.semanticRole = "genre_hat";
    __notes.push_back(__notes_n1);
    ProjectStateController::setTrackNotes(project, TrackType::HiHat, __notes);
}
    {
    std::vector<NoteEvent> __notes;
    NoteEvent __notes_n0;
    __notes_n0.pitch = 36;
    __notes_n0.gridTick = (0) * HiResTiming::kTicks1_16;
    __notes_n0.lengthTicks = (4) * HiResTiming::kTicks1_16;
    __notes_n0.velocity = 100;
    __notes_n0.timingOffsetTicks = 0;
    __notes_n0.semanticRole = "genre_sub";
    __notes.push_back(__notes_n0);
    ProjectStateController::setTrackNotes(project, TrackType::Sub808, __notes);
}

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
    {
    std::vector<NoteEvent> __notes;
    NoteEvent __notes_n0;
    __notes_n0.pitch = 38;
    __notes_n0.gridTick = (4) * HiResTiming::kTicks1_16;
    __notes_n0.lengthTicks = (1) * HiResTiming::kTicks1_16;
    __notes_n0.velocity = 118;
    __notes_n0.timingOffsetTicks = 0;
    __notes_n0.semanticRole = "snare_backbone";
    __notes.push_back(__notes_n0);
    NoteEvent __notes_n1;
    __notes_n1.pitch = 38;
    __notes_n1.gridTick = (12) * HiResTiming::kTicks1_16;
    __notes_n1.lengthTicks = (1) * HiResTiming::kTicks1_16;
    __notes_n1.velocity = 116;
    __notes_n1.timingOffsetTicks = 0;
    __notes_n1.semanticRole = "snare_backbone";
    __notes.push_back(__notes_n1);
    ProjectStateController::setTrackNotes(project, TrackType::Snare, __notes);
}
    {
    std::vector<NoteEvent> __notes;
    NoteEvent __notes_n0;
    __notes_n0.pitch = 39;
    __notes_n0.gridTick = (4) * HiResTiming::kTicks1_16;
    __notes_n0.lengthTicks = (1) * HiResTiming::kTicks1_16;
    __notes_n0.velocity = 90;
    __notes_n0.timingOffsetTicks = 0;
    __notes_n0.semanticRole = "clap_layer";
    __notes.push_back(__notes_n0);
    NoteEvent __notes_n1;
    __notes_n1.pitch = 39;
    __notes_n1.gridTick = (10) * HiResTiming::kTicks1_16;
    __notes_n1.lengthTicks = (1) * HiResTiming::kTicks1_16;
    __notes_n1.velocity = 88;
    __notes_n1.timingOffsetTicks = 0;
    __notes_n1.semanticRole = "clap_support";
    __notes.push_back(__notes_n1);
    NoteEvent __notes_n2;
    __notes_n2.pitch = 39;
    __notes_n2.gridTick = (12) * HiResTiming::kTicks1_16;
    __notes_n2.lengthTicks = (1) * HiResTiming::kTicks1_16;
    __notes_n2.velocity = 89;
    __notes_n2.timingOffsetTicks = 0;
    __notes_n2.semanticRole = "clap_layer";
    __notes.push_back(__notes_n2);
    ProjectStateController::setTrackNotes(project, TrackType::ClapGhostSnare, __notes);
}
    {
    std::vector<NoteEvent> __notes;
    NoteEvent __notes_n0;
    __notes_n0.pitch = 50;
    __notes_n0.gridTick = (2) * HiResTiming::kTicks1_16;
    __notes_n0.lengthTicks = (1) * HiResTiming::kTicks1_16;
    __notes_n0.velocity = 84;
    __notes_n0.timingOffsetTicks = 0;
    __notes_n0.semanticRole = "perc_texture";
    __notes.push_back(__notes_n0);
    NoteEvent __notes_n1;
    __notes_n1.pitch = 50;
    __notes_n1.gridTick = (10) * HiResTiming::kTicks1_16;
    __notes_n1.lengthTicks = (1) * HiResTiming::kTicks1_16;
    __notes_n1.velocity = 90;
    __notes_n1.timingOffsetTicks = 0;
    __notes_n1.semanticRole = "perc_texture";
    __notes.push_back(__notes_n1);
    NoteEvent __notes_n2;
    __notes_n2.pitch = 50;
    __notes_n2.gridTick = (14) * HiResTiming::kTicks1_16;
    __notes_n2.lengthTicks = (1) * HiResTiming::kTicks1_16;
    __notes_n2.velocity = 92;
    __notes_n2.timingOffsetTicks = 0;
    __notes_n2.semanticRole = "perc_texture";
    __notes.push_back(__notes_n2);
    ProjectStateController::setTrackNotes(project, TrackType::Perc, __notes);
}
    {
    std::vector<NoteEvent> __notes;
    NoteEvent __notes_n0;
    __notes_n0.pitch = 46;
    __notes_n0.gridTick = (7) * HiResTiming::kTicks1_16;
    __notes_n0.lengthTicks = (1) * HiResTiming::kTicks1_16;
    __notes_n0.velocity = 86;
    __notes_n0.timingOffsetTicks = 0;
    __notes_n0.semanticRole = "open_hat";
    __notes.push_back(__notes_n0);
    NoteEvent __notes_n1;
    __notes_n1.pitch = 46;
    __notes_n1.gridTick = (14) * HiResTiming::kTicks1_16;
    __notes_n1.lengthTicks = (1) * HiResTiming::kTicks1_16;
    __notes_n1.velocity = 96;
    __notes_n1.timingOffsetTicks = 0;
    __notes_n1.semanticRole = "open_hat";
    __notes.push_back(__notes_n1);
    ProjectStateController::setTrackNotes(project, TrackType::OpenHat, __notes);
}

    const auto report = SubstyleRuleEnforcer::enforce(project);
    expect(report.applied, "Classic rule enforcer should activate for BoomBap Classic.");

    clapGhost = findTrackByType(project, TrackType::ClapGhostSnare);
    perc = findTrackByType(project, TrackType::Perc);
    openHat = findTrackByType(project, TrackType::OpenHat);
    expect(clapGhost != nullptr && perc != nullptr && openHat != nullptr,
        "Classic rule smoke lost one of the decorated lanes after enforcement.");
    expect(std::none_of(clapGhost->notes.begin(), clapGhost->notes.end(), [](const NoteEvent& note)
    {
     return stepIndexOf(note) == 4 || stepIndexOf(note) == 12;
    }),
        "Classic rule enforcer should remove clap-layer collisions on the main snare backbeat.");
    expect(static_cast<int>(clapGhost->notes.size()) <= 1,
        "Classic rule enforcer should keep the clap/ghost support lane sparse per bar.");
    expect(static_cast<int>(perc->notes.size()) <= 1,
        "Classic rule enforcer should clamp decorative perc density in Classic mode.");
    expect(static_cast<int>(openHat->notes.size()) <= 1,
        "Classic rule enforcer should keep open hats sparse in Classic mode.");
}

void validateBoomBapAlgebraProjectCore(const PatternProject& project, const juce::String& label)
{
    const auto* snare = findTrackByType(const_cast<PatternProject&>(project), TrackType::Snare);
    const auto* kick = findTrackByType(const_cast<PatternProject&>(project), TrackType::Kick);
    const auto* hat = findTrackByType(const_cast<PatternProject&>(project), TrackType::HiHat);
    const auto* sub = findTrackByType(const_cast<PatternProject&>(project), TrackType::Sub808);

    expect(snare != nullptr && kick != nullptr && hat != nullptr,
           label + " requires Snare, Kick and HiHat tracks.");

    // Some builds route BoomBap generation between algebraic and procedural paths.
    // This validator is algebra-specific; if we're not on the algebra path, skip it.
    if (!project.generationDebugReport.contains("style: Boom Bap Classic Algebra"))
        return;

    expect(project.generationDebugReport.contains("trapLeak/earlySnare/overMicro/sub808"),
           label + " debug report should expose core penalties.");

    const auto countInBar = [](const TrackState* track, int bar)
    {
        if (track == nullptr)
            return 0;
        return static_cast<int>(std::count_if(track->notes.begin(), track->notes.end(), [bar](const NoteEvent& note)
        {
            return stepIndexOf(note) / 16 == bar;
        }));
    };

    const auto hasStep = [](const TrackState* track, int step, bool requireMain = false)
    {
        return track != nullptr && std::any_of(track->notes.begin(), track->notes.end(), [step, requireMain](const NoteEvent& note)
        {
            return stepIndexOf(note) == step && (!requireMain || !note.isGhost);
        });
    };

    for (int bar = 0; bar < project.params.bars; ++bar)
    {
        const int beat2 = bar * 16 + 4;
        const int beat4 = bar * 16 + 12;
        expect(hasStep(snare, beat2, true), label + " must keep the main snare on beat 2.");
        expect(hasStep(snare, beat4, true), label + " must keep the main snare on beat 4.");
        expect(!hasStep(kick, beat2) && !hasStep(kick, beat4),
               label + " kick should not collide with the main snare backbeat.");
        expect(countInBar(kick, bar) >= 1 && countInBar(kick, bar) <= 5,
               label + " should keep kick rhetoric focused (bar " + juce::String(bar + 1) + " of "
                   + juce::String(project.params.bars) + " has " + juce::String(countInBar(kick, bar)) + " kicks).");
        expect(countInBar(hat, bar) >= 3 && countInBar(hat, bar) <= 12,
               label + " should keep a readable hat carrier without trap noise carpet.");
    }

    for (const auto& note : snare->notes)
    {
        if (!note.isGhost && (stepIndexOf(note) % 16 == 4 || stepIndexOf(note) % 16 == 12))
        {
            expect(note.timingOffsetTicks >= 0,
                   label + " should never rush the main snare.");
            expect(std::abs(note.timingOffsetTicks) < HiResTiming::kTicks1_64,
                   label + " should use PPQ-subtick pocket, not whole 1/64 shifts.");
        }
    }

    int hatRun = 0;
    std::array<bool, 256> hatGrid {};
    for (const auto& note : hat->notes)
    {
        const int tick64 = std::clamp(HiResTiming::noteTick(note) / HiResTiming::kTicks1_64, 0, 255);
        hatGrid[static_cast<size_t>(tick64)] = true;
    }
    for (bool on : hatGrid)
    {
        hatRun = on ? hatRun + 1 : 0;
        expect(hatRun < 4, label + " should not create 1/64 machine-gun hats.");
    }

    if (sub != nullptr)
        expect(static_cast<int>(sub->notes.size()) <= 1,
               label + " should keep Sub808 as optional reinforcement, not the narrator.");
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
        validateBoomBapAlgebraProjectCore(project, "BoomBap Classic");
        if (project.generationDebugReport.contains("style: Boom Bap Classic Algebra"))
            continue;

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
            const int step = ((stepIndexOf(note) % 16) + 16) % 16;
            expect(step != 4 && step != 12,
                   "BoomBap Classic ghost snare lane should not double the main backbeat.");
        }

        for (int bar = 0; bar < project.params.bars; ++bar)
        {
            const int beat2 = bar * 16 + 4;
            const int beat4 = bar * 16 + 12;
            expect(std::any_of(snare->notes.begin(), snare->notes.end(), [beat2](const NoteEvent& note)
            {
                return stepIndexOf(note) == beat2 && !note.isGhost;
            }), "BoomBap Classic must keep the main snare on beat 2.");
            expect(std::any_of(snare->notes.begin(), snare->notes.end(), [beat4](const NoteEvent& note)
            {
                return stepIndexOf(note) == beat4 && !note.isGhost;
            }), "BoomBap Classic must keep the main snare on beat 4.");

            const int barKickCount = static_cast<int>(std::count_if(kick->notes.begin(), kick->notes.end(), [bar](const NoteEvent& note)
            {
                return stepIndexOf(note) / 16 == bar;
            }));
            expect(barKickCount >= 1 && barKickCount <= 5,
                   "BoomBap Classic kick density should stay in a focused head-nod range.");
            expect(std::none_of(kick->notes.begin(), kick->notes.end(), [beat2, beat4](const NoteEvent& note)
            {
                return stepIndexOf(note) == beat2 || stepIndexOf(note) == beat4;
            }), "BoomBap Classic kick should not collide with the main snare backbeat.");

            const int eighthHatCount = static_cast<int>(std::count_if(hat->notes.begin(), hat->notes.end(), [bar](const NoteEvent& note)
            {
                return stepIndexOf(note) / 16 == bar && ((stepIndexOf(note) % 16) % 2) == 0;
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
        validateBoomBapAlgebraProjectCore(project, "BoomBap Dusty");
        if (project.generationDebugReport.contains("style: Boom Bap Classic Algebra"))
            continue;

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
            return ((stepIndexOf(note) % 16) + 16) % 16;
        };

        const auto countInBar = [](const TrackState* track, int bar)
        {
            if (track == nullptr)
                return 0;

            return static_cast<int>(std::count_if(track->notes.begin(), track->notes.end(), [bar](const NoteEvent& note)
            {
                return stepIndexOf(note) / 16 == bar;
            }));
        };

        for (int bar = 0; bar < project.params.bars; ++bar)
        {
            const int beat2 = bar * 16 + 4;
            const int beat4 = bar * 16 + 12;
            const int endingLimit = bar == project.params.bars - 1 ? 2 : 1;

            const auto beat2Snare = std::find_if(snare->notes.begin(), snare->notes.end(), [beat2](const NoteEvent& note)
            {
                return stepIndexOf(note) == beat2 && !note.isGhost;
            });
            const auto beat4Snare = std::find_if(snare->notes.begin(), snare->notes.end(), [beat4](const NoteEvent& note)
            {
                return stepIndexOf(note) == beat4 && !note.isGhost;
            });
            expect(beat2Snare != snare->notes.end() && beat4Snare != snare->notes.end(),
                   "BoomBap Dusty must keep the main snare on beat 2 and beat 4.");
            expect(beat2Snare->timingOffsetTicks >= 8 && beat2Snare->timingOffsetTicks <= 20
                   && beat4Snare->timingOffsetTicks >= 8 && beat4Snare->timingOffsetTicks <= 20,
                   "BoomBap Dusty snare anchors should sit slightly late, tied to the hat swing.");

            const int carrierHatCount = static_cast<int>(std::count_if(hat->notes.begin(), hat->notes.end(), [bar, stepInBar](const NoteEvent& note)
            {
                return stepIndexOf(note) / 16 == bar && (stepInBar(note) % 2) == 0;
            }));
            const int swungOffbeatHatCount = static_cast<int>(std::count_if(hat->notes.begin(), hat->notes.end(), [bar, stepInBar](const NoteEvent& note)
            {
                return stepIndexOf(note) / 16 == bar && (stepInBar(note) % 4) == 2;
            }));
            expect(carrierHatCount >= 6,
                   "BoomBap Dusty hats should keep a slow swung eighth-note carrier. Seed "
                       + juce::String(seed) + " bar " + juce::String(bar)
                       + " carrier " + juce::String(carrierHatCount));
            expect(swungOffbeatHatCount >= 3,
                   "BoomBap Dusty hats should keep delayed offbeats in every bar.");

            for (const auto& note : hat->notes)
            {
                if (stepIndexOf(note) / 16 == bar && (stepInBar(note) % 4) == 2)
                {
                    expect(note.timingOffsetTicks >= 28 && note.timingOffsetTicks <= 56,
                           "BoomBap Dusty offbeat hats should use the Dusty swing delay range.");
                }
            }

            const int barKickCount = countInBar(kick, bar);
            expect(barKickCount >= 1 && barKickCount <= 5,
                   "BoomBap Dusty kicks should stay slow, grounded and uncluttered.");
            expect(std::none_of(kick->notes.begin(), kick->notes.end(), [beat2, beat4](const NoteEvent& note)
            {
                return stepIndexOf(note) == beat2 || stepIndexOf(note) == beat4;
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
        validateBoomBapAlgebraProjectCore(project, "BoomBap Jazzy");
        if (project.generationDebugReport.contains("style: Boom Bap Classic Algebra"))
            continue;

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
                return stepIndexOf(note) == step;
            });
        };

        const auto findStep = [](const TrackState* track, int step)
        {
            return std::find_if(track->notes.begin(), track->notes.end(), [step](const NoteEvent& note)
            {
                return stepIndexOf(note) == step;
            });
        };

        const auto countInBar = [](const TrackState* track, int bar)
        {
            if (track == nullptr)
                return 0;

            return static_cast<int>(std::count_if(track->notes.begin(), track->notes.end(), [bar](const NoteEvent& note)
            {
                return stepIndexOf(note) / 16 == bar;
            }));
        };

        int phraseCompingGhosts = 0;
        for (const auto& note : snare->notes)
        {
            const int step = ((stepIndexOf(note) % 16) + 16) % 16;
            if (note.isGhost && step != 4 && step != 12)
                ++phraseCompingGhosts;
        }

        int phraseKickCompHits = 0;
        for (const auto& note : kick->notes)
        {
            const int step = ((stepIndexOf(note) % 16) + 16) % 16;
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
                   && hatSkipA->timingOffsetTicks >= 96 && hatSkipA->timingOffsetTicks <= 188
                   && hatSkipB->timingOffsetTicks >= 96 && hatSkipB->timingOffsetTicks <= 188,
                   "BoomBap Jazzy hi-hat skips should use the cymbal tempo-derived swing delay.");

            for (const int stepInBar : { 4, 12 })
            {
                const int absoluteStep = bar * 16 + stepInBar;
                const auto cymbalFoot = findStep(cymbal, absoluteStep);
                expect(cymbalFoot != cymbal->notes.end(),
                       "BoomBap Jazzy cymbal should inherit the old hi-hat 2 and 4 foot layer.");
                expect(cymbalFoot->velocity <= 44 && cymbalFoot->timingOffsetTicks >= -1 && cymbalFoot->timingOffsetTicks <= 12,
                       "BoomBap Jazzy cymbal foot layer should be much quieter than the carrier.");
            }

            for (const int stepInBar : { 0, 4, 8, 12 })
            {
                const int absoluteStep = bar * 16 + stepInBar;
                const auto kickFeather = findStep(kick, absoluteStep);
                expect(kickFeather != kick->notes.end(),
                       "BoomBap Jazzy kick should feather quiet quarter notes.");
                expect(kickFeather->velocity <= 66 && kickFeather->timingOffsetTicks >= -3 && kickFeather->timingOffsetTicks <= 8,
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

// Boom Bap bass is opt-in: off by default (and then never generated); when enabled it is a
// line - root on every downbeat, no overlapping notes, in key (bar passing / leading tones).
void testBoomBapBassOptInAndLine()
{
    BoomBapEngine engine;
    {
        auto project = createDefaultProject();
        project.params.genre = GenreType::BoomBap;
        project.params.bars = 4;
        const auto& lane = getLaneStyleDefaults(getGenreStyleDefaults(GenreType::BoomBap, 0), TrackType::Sub808);
        expect(!lane.enabledByDefault, "Boom Bap bass lane must be off by default.");
        if (auto* bass = findTrackByType(project, TrackType::Sub808); bass != nullptr)
            bass->enabled = lane.enabledByDefault;
        engine.generate(project);
        const auto* bass = findTrackByType(project, TrackType::Sub808);
        expect(bass == nullptr || bass->notes.empty(), "A disabled Boom Bap bass lane must stay empty.");
    }

    const std::array<int, 7> minorScale { 0, 2, 3, 5, 7, 8, 10 };
    const std::array<int, 7> majorScale { 0, 2, 4, 5, 7, 9, 11 };
    auto checkLine = [&](const PatternProject& project, const std::vector<NoteEvent>& notes, const juce::String& label)
    {
        expect(!notes.empty(), label + "enabled bass produced no notes.");
        const auto& scale = project.params.scaleMode == 1 ? majorScale : minorScale;
        auto inKey = [&](int pitch)
        {
            const int degree = ((pitch % 12) - project.params.keyRoot + 12) % 12;
            return std::find(scale.begin(), scale.end(), degree) != scale.end();
        };

        const int patternTicks = project.params.bars * TimingGrid::TicksPerBar4_4;
        for (int bar = 0; bar < project.params.bars; ++bar)
        {
            const bool downbeat = std::any_of(notes.begin(), notes.end(), [bar](const NoteEvent& note)
            {
                return note.gridTick == bar * TimingGrid::TicksPerBar4_4;
            });
            expect(downbeat, label + "bar " + juce::String(bar + 1) + " has no bass note on beat 1.");
        }

        for (size_t i = 0; i < notes.size(); ++i)
        {
            const auto& note = notes[i];
            expect(note.pitch >= 24 && note.pitch <= 59, label + "bass pitch out of register: " + juce::String(note.pitch));
            // Kick notes may use the chord fifth, chromatic passing / leading tones are deliberate.
            if (note.semanticRole != "bass_kick" && note.semanticRole != "bass_chromatic" && note.semanticRole != "bass_approach")
                expect(inKey(note.pitch), label + "bass note out of key: " + juce::String(note.pitch) + " (" + note.semanticRole + ")");
            const int end = note.gridTick + note.lengthTicks;
            const int nextStart = i + 1 < notes.size() ? notes[i + 1].gridTick : patternTicks;
            expect(end <= nextStart, label + "bass notes overlap at tick " + juce::String(note.gridTick));
            expect(note.lengthTicks >= TimingGrid::ThirtySecond, label + "bass note too short");
        }
    };

    for (int seed = 1; seed <= 60; ++seed)
    {
        auto project = createDefaultProject();
        project.params.genre = GenreType::BoomBap;
        project.params.boombapSubstyle = seed % 6;
        project.params.bars = seed % 3 == 0 ? 8 : 4;
        project.params.seed = seed * 13;
        project.params.keyRoot = seed % 12;
        project.params.scaleMode = seed % 2;
        project.params.densityAmount = 0.2f + 0.1f * static_cast<float>(seed % 7);
        auto* bassLane = findTrackByType(project, TrackType::Sub808);
        expect(bassLane != nullptr, "Boom Bap project needs the bass (Sub808) lane.");
        bassLane->enabled = true;

        engine.generate(project);
        const auto* bass = findTrackByType(project, TrackType::Sub808);
        checkLine(project, bass->notes, "seed " + juce::String(seed) + ": ");

        // Every style, forced, on the same drums.
        for (int style = 0; style < static_cast<int>(BoomBapBassGenerator::Style::Count); ++style)
        {
            std::mt19937 rng(static_cast<std::mt19937::result_type>(seed * 101 + style));
            const auto notes = BoomBapBassGenerator::generate(project, rng, static_cast<BoomBapBassGenerator::Style>(style));
            checkLine(project, notes, "seed " + juce::String(seed) + " style "
                                          + BoomBapBassGenerator::styleName(static_cast<BoomBapBassGenerator::Style>(style)) + ": ");
        }
    }
}

// A calm, sad, sustained sample gets spacious bass: Sparse Low / Classic lead, the busy swung
// style is rare. A busy swung sample may still get it now and then.
void testBoomBapBassFollowsSampleMood()
{
    SampleMood calm;
    calm.valid = true;
    calm.busyness = 0.2f;
    calm.sustain = 0.6f;
    calm.minor = true;
    calm.swing = 0.9f;
    calm.bpm = 84.0f;
    std::array<int, static_cast<size_t>(BoomBapBassGenerator::Style::Count)> calmCounts {};     // amount 1 (Low)
    std::array<int, static_cast<size_t>(BoomBapBassGenerator::Style::Count)> calmMoreCounts {}; // amount 2 (More)
    std::array<int, static_cast<size_t>(BoomBapBassGenerator::Style::Count)> plainCounts {};    // amount 2, no sample
    std::mt19937 rng(77);
    for (int i = 0; i < 2000; ++i)
    {
        ++calmCounts[static_cast<size_t>(BoomBapBassGenerator::pickStyle(rng, calm, 0))];
        ++calmMoreCounts[static_cast<size_t>(BoomBapBassGenerator::pickStyle(rng, calm, 1))];
        ++plainCounts[static_cast<size_t>(BoomBapBassGenerator::pickStyle(rng, {}, 1))];
    }
    const auto share = [](const auto& counts, BoomBapBassGenerator::Style style)
    {
        return static_cast<float>(counts[static_cast<size_t>(style)]) / 2000.0f;
    };
    expect(share(calmCounts, BoomBapBassGenerator::Style::SwingMelodic) < 0.08f, "calm sample: Swing Melodic too frequent");
    expect(share(calmCounts, BoomBapBassGenerator::Style::SparseLow) > 0.30f, "calm sample: Sparse Low should lead");
    expect(share(calmMoreCounts, BoomBapBassGenerator::Style::SparseLow) > share(plainCounts, BoomBapBassGenerator::Style::SparseLow),
           "More: a calm sample should lean to Sparse Low");
    expect(share(calmMoreCounts, BoomBapBassGenerator::Style::SwingMelodic) < share(plainCounts, BoomBapBassGenerator::Style::SwingMelodic),
           "More: a calm sample should swing less");
    expect(share(plainCounts, BoomBapBassGenerator::Style::SwingMelodic) < 0.14f, "Swing Melodic must stay occasional");
    for (int style = 0; style < static_cast<int>(BoomBapBassGenerator::Style::Count); ++style)
        expect(plainCounts[static_cast<size_t>(style)] > 0, "every bass style must stay reachable without a sample");
}

// RG on the bass lane writes a new line; the [1][2][3] amount sets how much it plays and is
// saved with the project.
void testBoomBapBassRegenerateAndAmount()
{
    BoomBapEngine engine;
    auto project = createDefaultProject();
    project.params.genre = GenreType::BoomBap;
    project.params.bars = 4;
    project.params.seed = 321;
    auto* lane = findTrackByType(project, TrackType::Sub808);
    lane->enabled = true;
    engine.generate(project);

    auto lineOf = [&project]
    {
        juce::String line;
        for (const auto& note : findTrackByType(project, TrackType::Sub808)->notes)
            line << note.gridTick << ":" << note.pitch << " ";
        return line;
    };
    std::set<juce::String> lines { lineOf() };
    for (int rg = 1; rg <= 6; ++rg)
    {
        ++project.generationCounter;
        project.params.seed += 7;
        engine.regenerateTrack(project, TrackType::Sub808);
        expect(!findTrackByType(project, TrackType::Sub808)->notes.empty(), "RG left the bass lane empty");
        lines.insert(lineOf());
    }
    expect(lines.size() >= 5, "RG on the bass did not produce new lines (" + juce::String(static_cast<int>(lines.size())) + " distinct of 7)");

    std::array<int, 3> totalNotes {};
    for (int amount = 0; amount < 3; ++amount)
    {
        findTrackByType(project, TrackType::Sub808)->sub808Settings.bassAmount = amount;
        for (int seed = 1; seed <= 40; ++seed)
        {
            project.generationCounter = seed;
            engine.generateTrackNew(project, TrackType::Sub808);
            totalNotes[static_cast<size_t>(amount)] += static_cast<int>(findTrackByType(project, TrackType::Sub808)->notes.size());
        }
    }
    expect(totalNotes[0] < totalNotes[1] && totalNotes[1] < totalNotes[2],
           "bass amount 1/2/3 must play progressively more notes: " + juce::String(totalNotes[0]) + "/"
               + juce::String(totalNotes[1]) + "/" + juce::String(totalNotes[2]));

    findTrackByType(project, TrackType::Sub808)->sub808Settings.bassAmount = 2;
    PatternProject restored;
    expect(PatternProjectSerialization::deserialize(wrapSerializedProject(project), restored), "project round trip failed");
    expect(findTrackByType(restored, TrackType::Sub808)->sub808Settings.bassAmount == 2, "bass amount not saved with the project");
}



// Drum & Bass: the backbone is always readable, every ghost belongs to a snare and is quieter
// than it, kicks never sit on the backbone, and the selected candidate passed the gates.
void testDnBGrammarInvariants()
{
    for (int sub = 0; sub < static_cast<int>(DnBSubstyle::Count); ++sub)
    {
        for (const int bars : { 2, 4, 8 })
        {
            for (int seed = 1; seed <= 12; ++seed)
            {
                DnBGenerationParams params;
                params.seed = seed * 31 + sub;
                params.bars = bars;
                params.substyle = sub;
                params.density = 0.2f + 0.06f * static_cast<float>(seed);
                const auto pattern = DnBEngine::search(params);
                const juce::String label = juce::String(getDnBStyleProfile(sub).name) + " " + juce::String(bars) + " bars seed "
                                         + juce::String(params.seed) + ": ";
                expect(pattern.score.passedGates, label + "selected candidate failed gate " + pattern.score.failedGate);
                expect(pattern.score.quality > 0.8f, label + "low quality " + juce::String(pattern.score.quality, 3));
                for (int bar = 0; bar < bars; ++bar)
                {
                    // The answer bar may displace the second snare by an 8th (4 + 10 / 4 + 14).
                    const bool secondSnare = pattern.has(TrackType::Snare, bar, 48, false)
                        || (bar % 2 == 1 && (pattern.has(TrackType::Snare, bar, 40, false) || pattern.has(TrackType::Snare, bar, 56, false)));
                    expect(pattern.has(TrackType::Snare, bar, 16, false) && secondSnare,
                           label + "bar " + juce::String(bar + 1) + " lost the 2 & 4 backbone");
                    const bool twoStepOnly = bar % 2 == 1 && std::any_of(pattern.events.begin(), pattern.events.end(), [bar](const DnBEvent& e)
                    {
                        return e.bar == bar && e.lane == TrackType::Kick && e.tick >= 24 && e.tick <= 44;
                    });
                    expect(pattern.has(TrackType::Kick, bar, 0, false) || twoStepOnly,
                           label + "bar " + juce::String(bar + 1) + " has neither a downbeat nor a 2-step kick");
                }
                for (const auto& e : pattern.events)
                {
                    expect(e.tick >= 0 && e.tick < 64 && e.bar >= 0 && e.bar < bars, label + "event outside the loop");
                    expect(!(e.lane == TrackType::Kick && (e.tick == 16 || e.tick == 48)), label + "kick on the snare backbone");
                    expect(!(e.lane == TrackType::Kick && std::any_of(pattern.events.begin(), pattern.events.end(), [&e](const DnBEvent& s)
                    {
                        return s.lane == TrackType::Snare && s.role == DnBRole::SnareBackbeat && s.bar == e.bar && s.tick == e.tick;
                    })), label + "kick on a displaced backbone snare");
                    if ((e.lane == TrackType::Snare && e.ghost) || e.lane == TrackType::GhostKick)
                    {
                        const auto anchor = std::find_if(pattern.events.begin(), pattern.events.end(), [&e](const DnBEvent& a)
                        {
                            return a.lane == TrackType::Snare && !a.ghost && a.bar == e.bar && a.tick == e.anchorTick;
                        });
                        expect(anchor != pattern.events.end(), label + "orphan ghost at tick " + juce::String(e.tick));
                        if (anchor != pattern.events.end())
                        {
                            expect(std::abs(e.tick - e.anchorTick) <= 12, label + "ghost too far from its snare");
                            expect(e.velocity < anchor->velocity, label + "ghost louder than its snare");
                        }
                    }
                }
            }
        }
    }
}

// Techno (docs/techno-engine.md): the four-on-the-floor axis in every non-fill bar, the clap
// never on 1 / 3, percussion never masking the kick, every search inside the gates.
void testTechnoGrammarInvariants()
{
    for (int sub = 0; sub < static_cast<int>(TechnoSubstyle::Count); ++sub)
        for (const int bars : { 1, 2, 4, 8 })
            for (int seed = 1; seed <= 12; ++seed)
            {
                TechnoGenerationParams params;
                params.seed = seed * 97 + sub;
                params.bars = bars;
                params.substyle = sub;
                params.density = 0.25f + 0.05f * static_cast<float>(seed % 10);
                const auto pattern = TechnoEngine::search(params);
                const juce::String label = juce::String(getTechnoStyleProfile(sub).name) + " " + juce::String(bars) + " bars seed " + juce::String(seed);
                expect(pattern.score.passedGates, label + ": failed gate " + pattern.score.failedGate);
                for (int bar = 0; bar < bars; ++bar)
                {
                    const bool fill = pattern.barRoles[static_cast<size_t>(bar)] == TechnoBarRole::Fill;
                    for (int beat = 0; beat < (fill ? 3 : 4); ++beat)
                        expect(pattern.has(TrackType::Kick, bar, beat * 4), label + ": no kick on beat " + juce::String(beat + 1) + " of bar " + juce::String(bar + 1));
                }
                for (const auto& e : pattern.events)
                {
                    if (e.lane == TrackType::Snare)
                        expect(e.step != 0 && e.step != 8, label + ": clap on beat 1 / 3");
                    if (e.lane == TrackType::Perc)
                        expect(!pattern.has(TrackType::Kick, e.bar, e.step), label + ": perc masks the kick");
                    if (e.lane == TrackType::HiHat)
                        expect(!pattern.has(TrackType::OpenHat, e.bar, e.step), label + ": closed hat under the open hat (no choke)");
                    if (e.lane == TrackType::Kick && e.role == TechnoRole::KickAxis)
                        expect(std::abs(e.micro) <= 2, label + ": the axis drifts off the grid");
                }
            }
}

// The bass never attacks with the kick (sidechain gate), stays in its register and in key.
void testTechnoBassRules()
{
    for (int sub = 0; sub < static_cast<int>(TechnoSubstyle::Count); ++sub)
        for (int amount = 0; amount < 3; ++amount)
            for (int seed = 1; seed <= 8; ++seed)
            {
                TechnoGenerationParams params;
                params.seed = seed * 31 + amount;
                params.bars = 4;
                params.substyle = sub;
                const auto drums = TechnoEngine::search(params);
                std::vector<std::vector<int>> kicks(4);
                for (const auto& e : drums.events)
                    if (e.lane == TrackType::Kick)
                        kicks[static_cast<size_t>(e.bar)].push_back(e.step);

                TechnoBassParams bassParams;
                bassParams.seed = seed * 7 + sub;
                bassParams.bars = 4;
                bassParams.substyle = sub;
                bassParams.keyRoot = 9;   // A minor
                bassParams.amount = amount;
                juce::String report;
                const auto line = TechnoBassGenerator::search(bassParams, kicks, {}, &report);
                const auto& style = getTechnoStyleProfile(sub);
                const juce::String label = juce::String(style.name) + " amount " + juce::String(amount + 1) + " seed " + juce::String(seed)
                    + " (" + toString(line.archetype) + ")";
                expect(!line.notes.empty(), label + ": no bass");
                static const std::set<int> aMinorWithB2 { 9, 11, 0, 2, 4, 5, 7, 10 }; // A natural minor + b2 (Bb)
                for (const auto& n : line.notes)
                {
                    const auto& barKicks = kicks[static_cast<size_t>(n.step / 16)];
                    expect(std::find(barKicks.begin(), barKicks.end(), n.step % 16) == barKicks.end(), label + ": bass attacks with the kick");
                    expect(n.pitch >= style.bassLow && n.pitch <= style.bassHigh, label + ": out of register " + juce::String(n.pitch));
                    expect(aMinorWithB2.count(n.pitch % 12) > 0, label + ": out of key " + juce::String(n.pitch));
                }
            }
}

// Same settings -> same loop; Generate presses -> different loops. Prints the grammar's own
// distribution of S / D / R per style (the scorer targets are calibrated on it).
void testTechnoDeterministicVariedAndCalibration()
{
    TechnoGenerationParams params;
    params.seed = 4242;
    params.bars = 4;
    const auto a = TechnoEngine::search(params);
    const auto b = TechnoEngine::search(params);
    expect(a.events.size() == b.events.size(), "techno search is not deterministic");

    for (int sub = 0; sub < static_cast<int>(TechnoSubstyle::Count); ++sub)
    {
        const auto& style = getTechnoStyleProfile(sub);
        std::vector<float> s, d, r;
        for (int i = 0; i < 300; ++i)
        {
            std::mt19937 rng(static_cast<std::mt19937::result_type>(i * 7919 + sub));
            TechnoGenerationParams p;
            p.bars = 4;
            p.substyle = sub;
            p.density = style.densityDefault;
            const auto pattern = TechnoGrammar::generateCandidate(p, style, rng);
            const auto score = TechnoScorer::score(pattern, style);
            s.push_back(score.syncopation);
            d.push_back(score.density);
            r.push_back(score.repetition);
        }
        auto q = [](std::vector<float> v, float f) { std::sort(v.begin(), v.end()); return v[static_cast<size_t>(f * (v.size() - 1))]; };
        std::cout << "    " << style.name << ": S p25/50/75 " << q(s, 0.25f) << " " << q(s, 0.5f) << " " << q(s, 0.75f)
                  << " | D " << q(d, 0.25f) << " " << q(d, 0.5f) << " " << q(d, 0.75f)
                  << " | R " << q(r, 0.25f) << " " << q(r, 0.5f) << " " << q(r, 0.75f)
                  << " | targets S " << style.syncTarget << " D " << style.densityTarget << " R " << style.repetitionTarget << std::endl;
        std::set<juce::String> loops;
        for (int seed = 1; seed <= 20; ++seed)
        {
            params.seed = seed * 131;
            params.substyle = sub;
            juce::String key;
            for (const auto& e : TechnoEngine::search(params).events)
                key << static_cast<int>(e.lane) << ":" << e.bar << ":" << e.step << ":" << e.subTick << " ";
            loops.insert(key);
        }
        expect(loops.size() >= 12, juce::String(style.name) + ": 20 generations gave only " + juce::String(static_cast<int>(loops.size())) + " loops");

    }
}

// Same settings -> same loop; consecutive seeds (Generate presses) -> different loops.
void testDnBDeterministicAndVaried()
{
    DnBGenerationParams params;
    params.seed = 4242;
    params.bars = 4;
    const auto first = DnBEngine::search(params);
    const auto second = DnBEngine::search(params);
    expect(first.events.size() == second.events.size(), "DnB search is not deterministic");
    for (size_t i = 0; i < std::min(first.events.size(), second.events.size()); ++i)
        expect(first.events[i].absoluteTick() == second.events[i].absoluteTick() && first.events[i].lane == second.events[i].lane
                   && first.events[i].velocity == second.events[i].velocity && first.events[i].micro == second.events[i].micro,
               "DnB search is not deterministic");

    for (int sub = 0; sub < static_cast<int>(DnBSubstyle::Count); ++sub)
    {
        std::set<juce::String> loops;
        for (int seed = 100; seed < 120; ++seed)
        {
            params.seed = seed;
            params.substyle = sub;
            juce::String skeleton;
            for (const auto& e : DnBEngine::search(params).events)
                if (e.lane == TrackType::Kick || e.lane == TrackType::Snare || e.lane == TrackType::HiHat)
                    skeleton << static_cast<int>(e.lane) << ":" << e.absoluteTick() << " ";
            loops.insert(skeleton);
        }
        expect(loops.size() >= 15, juce::String(getDnBStyleProfile(sub).name) + ": 20 generations gave only "
                                       + juce::String(static_cast<int>(loops.size())) + " different loops");
    }
}

// The substyles keep their character (measured on the selected loops).
void testDnBSubstyleCharacter()
{
    struct Stats { float sync = 0.0f; float repetition = 0.0f; float ghosts = 0.0f; };
    auto measure = [](DnBSubstyle substyle)
    {
        Stats stats;
        for (int seed = 1; seed <= 40; ++seed)
        {
            DnBGenerationParams params;
            params.seed = seed * 13;
            params.bars = 4;
            params.substyle = static_cast<int>(substyle);
            const auto p = DnBEngine::search(params);
            stats.sync += p.score.syncopation / 40.0f;
            stats.repetition += p.score.repetition / 40.0f;
            for (const auto& e : p.events)
                stats.ghosts += ((e.lane == TrackType::Snare && e.ghost) || e.lane == TrackType::GhostKick) ? 1.0f / 160.0f : 0.0f;
        }
        return stats;
    };
    const auto liquid = measure(DnBSubstyle::Liquid);
    const auto roller = measure(DnBSubstyle::Roller);
    const auto jumpUp = measure(DnBSubstyle::JumpUp);
    const auto breakbeat = measure(DnBSubstyle::Breakbeat);
    expect(breakbeat.sync > liquid.sync, "Breakbeat must be more syncopated than Liquid");
    expect(roller.repetition > breakbeat.repetition, "Roller must repeat more than Breakbeat");
    expect(breakbeat.ghosts > jumpUp.ghosts, "Breakbeat must have more ghost notes than Jump-Up");
}

// In a project: DnB writes the drum lanes, never the bass, respects locked lanes, and RG on a
// lane rewrites only that lane's group.
void testDnBEngineInProject()
{
    DnBEngine engine;
    auto project = createDefaultProject();
    project.params.genre = GenreType::DnB;
    project.params.dnbSubstyle = 1;
    project.params.bars = 4;
    project.params.seed = 77;
    for (auto& track : project.tracks)
        track.enabled = true;
    auto* bass = findTrackByType(project, TrackType::Sub808);
    NoteEvent bassNote;
    bassNote.pitch = 29;
    bass->notes = { bassNote };
    bass->locked = true; // a locked bass lane is left alone (the DnB bass itself: testDnBBassInProject)

    engine.generate(project);
    for (const auto lane : { TrackType::Kick, TrackType::Snare, TrackType::HiHat })
        expect(!findTrackByType(project, lane)->notes.empty(), juce::String("DnB left a drum lane empty: ") + toString(lane));
    expect(findTrackByType(project, TrackType::Sub808)->notes.size() == 1, "DnB changed a locked bass lane");
    expect(findTrackByType(project, TrackType::ClapGhostSnare)->notes.empty(), "DnB ghosts belong on the Snare lane");
    expect(project.generationDebugReport.contains("DNB ALGEBRA"), "DnB debug report missing");

    const auto hats = findTrackByType(project, TrackType::HiHat)->notes;
    const auto snares = findTrackByType(project, TrackType::Snare)->notes;
    findTrackByType(project, TrackType::Snare)->locked = true;
    auto kickLine = [&project]
    {
        juce::String line;
        for (const auto& n : findTrackByType(project, TrackType::Kick)->notes)
            line << n.gridTick << " ";
        return line;
    };
    std::set<juce::String> kickLines { kickLine() };
    for (int rg = 1; rg <= 5; ++rg)
    {
        project.generationCounter = rg;
        engine.regenerateTrack(project, TrackType::Kick);
        kickLines.insert(kickLine());
        expect(findTrackByType(project, TrackType::HiHat)->notes.size() == hats.size(), "RG on the kick changed the hats");
        expect(findTrackByType(project, TrackType::Snare)->notes.size() == snares.size(), "RG changed a locked lane");
    }
    expect(kickLines.size() >= 3, "RG on the DnB kick did not give new kick lines");

    project.params.bpm = 90.0f;
    for (int seed = 1; seed <= 30; ++seed)
    {
        project.params.seed = seed;
        for (int sub = 0; sub < static_cast<int>(DnBSubstyle::Count); ++sub)
        {
            project.params.dnbSubstyle = sub;
            const float bpm = chooseDeterministicStyleBpm(project.params);
            expect(bpm >= 164.0f && bpm <= 176.0f, "DnB style tempo out of range: " + juce::String(bpm, 1));
        }
    }
}


// DnB bass: a root on beat 1, never an attack on the snare backbone, in register, in key
// (except tension / approach notes), glides only from a note that is still sounding, and the
// [1][2][3] amount plays progressively more.
void testDnBBassLine()
{
    std::array<float, 3> notesPerAmount {};
    for (int sub = 0; sub < static_cast<int>(DnBSubstyle::Count); ++sub)
    {
        for (int seed = 1; seed <= 10; ++seed)
        {
            DnBGenerationParams drumParams;
            drumParams.seed = seed * 19 + sub;
            drumParams.bars = 4;
            drumParams.substyle = sub;
            const auto frame = DnBDrumFrame::fromPattern(DnBEngine::search(drumParams));
            for (int amount = 0; amount < 3; ++amount)
            {
                DnBBassParams params;
                params.seed = seed * 3 + amount;
                params.bars = 4;
                params.substyle = sub;
                params.amount = amount;
                params.keyRoot = (seed + sub) % 12;
                params.scaleMode = seed % 2;
                const auto line = DnBBassGenerator::search(params, frame, {});
                const juce::String label = juce::String(getDnBStyleProfile(sub).name) + " seed " + juce::String(seed) + " amount "
                                         + juce::String(amount + 1) + " (" + toString(line.archetype) + "): ";
                notesPerAmount[static_cast<size_t>(amount)] += static_cast<float>(line.notes.size());
                expect(line.score.passedGates, label + "failed gate " + line.score.failedGate);
                expect(!line.notes.empty() && line.notes.front().start == 0, label + "no root on the first downbeat");
                for (size_t i = 0; i < line.notes.size(); ++i)
                {
                    const auto& n = line.notes[i];
                    const int tick = n.start % 64;
                    const bool sustained = line.archetype == DnBBassArchetype::SubReese || line.archetype == DnBBassArchetype::DubSub
                        || line.archetype == DnBBassArchetype::MelodicSub;
                    if (sustained)
                        expect(tick == 0 || (std::abs(tick - 16) > 1 && std::abs(tick - 48) > 1), label + "sustained bass attack on the snare at " + juce::String(n.start));
                    expect(n.pitch >= 24 && n.pitch <= 52, label + "bass out of register: " + juce::String(n.pitch));
                    expect(n.length > 0, label + "zero-length bass note");
                    const int degree = ((n.pitch - params.keyRoot) % 12 + 12) % 12;
                    const auto& scale = params.scaleMode == 1 ? std::array<int, 7> { 0, 2, 4, 5, 7, 9, 11 } : std::array<int, 7> { 0, 2, 3, 5, 7, 8, 10 };
                    if (!n.tension)
                        expect(std::find(scale.begin(), scale.end(), degree) != scale.end(), label + "bass note out of key: " + juce::String(n.pitch));
                    if (i + 1 < line.notes.size())
                    {
                        const auto& next = line.notes[i + 1];
                        if (next.glide)
                            expect(n.start + n.length >= next.start, label + "glide from a note that already stopped");
                        else
                            expect(n.start + n.length <= next.start, label + "bass notes overlap");
                    }
                }
            }
        }
    }
    expect(notesPerAmount[0] < notesPerAmount[1] && notesPerAmount[1] < notesPerAmount[2],
           "bass amount 1/2/3 must play progressively more: " + juce::String(notesPerAmount[0]) + "/" + juce::String(notesPerAmount[1])
               + "/" + juce::String(notesPerAmount[2]));
}

// The analyzer is the edge: with a loaded sample the bass plays the sample's chord roots (per
// half bar, from its own bass notes), and a bass-heavy sample is supported, not fought.
void testDnBBassFollowsSampleRoots()
{
    SampleAwareGenerationContext context;
    context.enabled = true;
    context.harmonyBpm = 174.0;
    context.harmonyOriginSeconds = 0.0;
    context.harmony.valid = true;
    context.harmony.keyRoot = 5; // F minor
    context.harmony.scaleMode = 0;
    const double beat = 60.0 / 174.0;
    const std::array<int, 4> barRoots { 29, 27, 25, 25 }; // F1, D#1, C#1, C#1
    for (int bar = 0; bar < 4; ++bar)
        for (int b = 0; b < 4; ++b)
        {
            SampleBassSegment segment;
            segment.startSeconds = (bar * 4 + b) * beat;
            segment.endSeconds = segment.startSeconds + beat;
            segment.midiNote = barRoots[static_cast<size_t>(bar)];
            segment.confidence = 0.9f;
            segment.lowEnergy = 0.8f;
            context.harmony.bass.push_back(segment);
        }

    const auto lens = DnBSampleLens::build(context, 4);
    expect(lens.valid && lens.rootsFromSample, "the lens did not read the sample's roots");

    // Over a half-time loop DnB runs at double time: 77 -> 154, and one sample bar spans two
    // pattern bars (the lens maps the sample's seconds with the pattern tempo).
    expect(std::abs(generationBpmForSample(77.0f, GenreType::DnB) - 154.0f) < 0.01f, "DnB must double a 77 BPM sample");
    expect(std::abs(generationBpmForSample(174.0f, GenreType::DnB) - 174.0f) < 0.01f, "DnB keeps a 174 BPM sample");
    expect(std::abs(generationBpmForSample(77.0f, GenreType::BoomBap) - 77.0f) < 0.01f, "other genres keep the sample tempo");
    {
        auto halfTime = context;
        halfTime.harmonyBpm = 87.0;
        halfTime.harmony.bass.clear();
        const double halfBeat = 60.0 / 87.0;
        for (int bar = 0; bar < 2; ++bar)
            for (int b = 0; b < 4; ++b)
            {
                SampleBassSegment segment;
                segment.startSeconds = (bar * 4 + b) * halfBeat;
                segment.endSeconds = segment.startSeconds + halfBeat;
                segment.midiNote = bar == 0 ? 29 : 27;
                segment.confidence = 0.9f;
                segment.lowEnergy = 0.8f;
                halfTime.harmony.bass.push_back(segment);
            }
        const auto doubled = DnBSampleLens::build(halfTime, 4, 174.0);
        expect(doubled.halfBarRoot[0] == 5 && doubled.halfBarRoot[3] == 5 && doubled.halfBarRoot[4] == 3 && doubled.halfBarRoot[7] == 3,
               "at double time one sample bar (F, then D#) must span two DnB bars");
    }
    expect(lens.mode == DnBSampleLens::Mode::Support, "a confident, loud sample bass must be supported, not fought");

    DnBGenerationParams drumParams;
    drumParams.seed = 5;
    drumParams.bars = 4;
    const auto frame = DnBDrumFrame::fromPattern(DnBEngine::search(drumParams));
    for (int seed = 1; seed <= 12; ++seed)
    {
        DnBBassParams params;
        params.seed = seed;
        params.bars = 4;
        params.substyle = seed % static_cast<int>(DnBSubstyle::Count);
        params.keyRoot = 5;
        const auto line = DnBBassGenerator::search(params, frame, lens);
        expect(line.sampleLed, "the bass did not take its roots from the sample");
        for (int bar = 0; bar < 4; ++bar)
        {
            // The note sounding on the downbeat (a held reese counts).
            const auto downbeat = std::find_if(line.notes.begin(), line.notes.end(), [bar](const DnBBassNote& n)
            {
                return n.start <= bar * 64 && n.start + n.length > bar * 64;
            });
            expect(downbeat != line.notes.end(), "no bass sounding on the downbeat of bar " + juce::String(bar + 1));
            if (downbeat != line.notes.end())
                expect(downbeat->pitch % 12 == barRoots[static_cast<size_t>(bar)] % 12,
                       "bar " + juce::String(bar + 1) + " bass root " + juce::String(downbeat->pitch) + " does not follow the sample's "
                           + juce::String(barRoots[static_cast<size_t>(bar)]));
        }
    }
}

// In a project: the DnB bass lane is on by default, Generate writes it against the drums, RG on
// the bass rewrites only the bass, and a locked bass is left alone.
void testDnBBassInProject()
{
    DnBEngine engine;
    auto project = createDefaultProject();
    project.params.genre = GenreType::DnB;
    project.params.bars = 4;
    project.params.seed = 91;
    expect(getLaneStyleDefaults(getGenreStyleDefaults(GenreType::DnB, 0), TrackType::Sub808).enabledByDefault, "DnB bass lane must be on by default");
    for (auto& track : project.tracks)
        track.enabled = true;

    engine.generate(project);
    const auto* bass = findTrackByType(project, TrackType::Sub808);
    expect(!bass->notes.empty(), "DnB Generate wrote no bass");
    expect(project.generationDebugReport.contains("DNB BASS"), "DnB bass report missing");

    const auto kicks = findTrackByType(project, TrackType::Kick)->notes.size();
    std::set<juce::String> lines;
    for (int rg = 1; rg <= 5; ++rg)
    {
        project.generationCounter = rg;
        engine.regenerateTrack(project, TrackType::Sub808);
        juce::String line;
        for (const auto& n : findTrackByType(project, TrackType::Sub808)->notes)
            line << n.gridTick << ":" << n.pitch << " ";
        lines.insert(line);
        expect(findTrackByType(project, TrackType::Kick)->notes.size() == kicks, "RG on the bass changed the drums");
    }
    expect(lines.size() >= 3, "RG on the DnB bass did not give new lines");

    findTrackByType(project, TrackType::Sub808)->locked = true;
    const auto locked = findTrackByType(project, TrackType::Sub808)->notes.size();
    project.generationCounter = 9;
    engine.generate(project);
    expect(findTrackByType(project, TrackType::Sub808)->notes.size() == locked, "Generate changed a locked bass lane");
}


// Consecutive Generates must not keep producing the same bass: styles and lines vary.
void testBoomBapBassVariety()
{
    BoomBapEngine engine;
    auto project = createDefaultProject();
    project.params.genre = GenreType::BoomBap;
    project.params.bars = 4;
    project.params.seed = 4242;
    findTrackByType(project, TrackType::Sub808)->enabled = true;

    std::set<juce::String> styles;
    std::set<juce::String> lines;
    // A new Generate advances the seed (Seed Lock off); the same seed + settings give the same line
    // whatever the session history (RULE 13).
    juce::String firstLine;
    for (int generation = 0; generation < 30; ++generation)
    {
        project.params.seed = 4242 + generation;
        project.generationCounter = generation;
        engine.generate(project);
        styles.insert(project.generationDebugReport.fromLastOccurrenceOf("bass style: ", false, false).upToFirstOccurrenceOf("\n", false, false));
        juce::String line;
        for (const auto& note : findTrackByType(project, TrackType::Sub808)->notes)
            line << note.gridTick << ":" << note.pitch << " ";
        lines.insert(line);
        if (generation == 0)
            firstLine = line;
    }
    project.params.seed = 4242;
    project.generationCounter = 77;
    engine.generate(project);
    juce::String replay;
    for (const auto& note : findTrackByType(project, TrackType::Sub808)->notes)
        replay << note.gridTick << ":" << note.pitch << " ";
    expect(replay == firstLine, "The same seed gave a different bass line after other generations.");
    // Without a sample the bass leans calm by design (300 seeds: Sparse Low 51 %, Classic 24 %, Kick Riff 17 %,
    // Pump 4 %, Chromatic Walk 3 %): about 4.75 styles are expected in 30 Generates.
    expect(styles.size() >= 4, "30 Generates used only " + juce::String(static_cast<int>(styles.size())) + " bass styles.");
    expect(lines.size() >= 27, "30 Generates gave only " + juce::String(static_cast<int>(lines.size())) + " different bass lines.");
}

// Derived bars thin notes out at random (dropout / pickup removal). The downbeat kick must
// survive that in every bar, for every substyle and phrase length.
void testBoomBapNoBarWithoutKick()
{
    BoomBapEngine engine;
    for (int substyle = 0; substyle < 6; ++substyle)
    {
        for (const int bars : { 4, 8 })
        {
            for (int seed = 1; seed <= 80; ++seed)
            {
                auto project = createDefaultProject();
                project.params.genre = GenreType::BoomBap;
                project.params.boombapSubstyle = substyle;
                project.params.bars = bars;
                project.params.seed = 6200 + seed * 7 + substyle;
                project.params.bpm = 92.0f;
                project.params.swingPercent = 58.5f;
                project.params.densityAmount = 0.25f + 0.1f * static_cast<float>(seed % 6);
                project.params.timingAmount = 0.38f;
                project.params.humanizeAmount = 0.28f;
                engine.generate(project);

                const auto* kick = findTrackByType(project, TrackType::Kick);
                expect(kick != nullptr, "BoomBap project needs a Kick track.");
                for (int bar = 0; bar < bars; ++bar)
                {
                    const bool hasKick = std::any_of(kick->notes.begin(), kick->notes.end(), [bar](const NoteEvent& note)
                    {
                        return stepIndexOf(note) / 16 == bar;
                    });
                    if (!hasKick)
                    {
                        juce::String kicks;
                        for (const auto& note : kick->notes)
                            kicks << stepIndexOf(note) << "[" << note.gridTick << "+" << note.timingOffsetTicks
                                  << "](" << note.semanticRole << ") ";
                        expect(false, "BoomBap substyle " + juce::String(substyle) + ", " + juce::String(bars)
                                          + " bars, seed " + juce::String(project.params.seed)
                                          + ": bar " + juce::String(bar + 1) + " has no kick. Kick steps: " + kicks);
                    }
                }
            }
        }
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
        validateBoomBapAlgebraProjectCore(project, "BoomBapGold");
        if (project.generationDebugReport.contains("style: Boom Bap Classic Algebra"))
            continue;

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
                return stepIndexOf(note) == step;
            });
        };

        const auto findStep = [](const TrackState* track, int step)
        {
            return std::find_if(track->notes.begin(), track->notes.end(), [step](const NoteEvent& note)
            {
                return stepIndexOf(note) == step;
            });
        };

        const auto countInBar = [](const TrackState* track, int bar)
        {
            if (track == nullptr)
                return 0;

            return static_cast<int>(std::count_if(track->notes.begin(), track->notes.end(), [bar](const NoteEvent& note)
            {
                return stepIndexOf(note) / 16 == bar;
            }));
        };

        int phraseKickPickups = 0;
        for (const auto& note : kick->notes)
        {
            const int step = ((stepIndexOf(note) % 16) + 16) % 16;
            if (step == 10 || step == 11 || step == 14)
                ++phraseKickPickups;
        }

        int phraseSnareGhosts = 0;
        for (const auto& note : snare->notes)
        {
            const int step = ((stepIndexOf(note) % 16) + 16) % 16;
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
            expect(beat2Snare->timingOffsetTicks >= 6 && beat2Snare->timingOffsetTicks <= 16
                   && beat4Snare->timingOffsetTicks >= 6 && beat4Snare->timingOffsetTicks <= 16,
                   "BoomBapGold snare anchors should sit slightly late in the pocket.");

            for (const int stepInBar : { 0, 2, 4, 6, 8, 10, 12, 14 })
            {
                const int absoluteStep = bar * 16 + stepInBar;
                expect(hasStep(hat, absoluteStep),
                       "BoomBapGold should keep a swung eighth-note hat carrier.");

                const auto hatHit = findStep(hat, absoluteStep);
                if ((stepInBar % 4) == 2)
                {
                    expect(hatHit != hat->notes.end() && hatHit->timingOffsetTicks >= 28 && hatHit->timingOffsetTicks <= 58,
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
        validateBoomBapAlgebraProjectCore(project, "BoomBap RussianUnderground");
        if (project.generationDebugReport.contains("style: Boom Bap Classic Algebra"))
            continue;

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
                return stepIndexOf(note) == step;
            });
        };

        const auto findStep = [](const TrackState* track, int step)
        {
            return std::find_if(track->notes.begin(), track->notes.end(), [step](const NoteEvent& note)
            {
                return stepIndexOf(note) == step;
            });
        };

        const auto countInBar = [](const TrackState* track, int bar)
        {
            if (track == nullptr)
                return 0;

            return static_cast<int>(std::count_if(track->notes.begin(), track->notes.end(), [bar](const NoteEvent& note)
            {
                return stepIndexOf(note) / 16 == bar;
            }));
        };

        int phraseLateKicks = 0;
        for (const auto& note : kick->notes)
        {
            const int step = ((stepIndexOf(note) % 16) + 16) % 16;
            if (step == 10 || step == 11 || step == 14 || step == 15)
                ++phraseLateKicks;
        }

        int phraseSnareGhosts = 0;
        for (const auto& note : snare->notes)
        {
            const int step = ((stepIndexOf(note) % 16) + 16) % 16;
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
            expect(beat2Snare->timingOffsetTicks >= 10 && beat2Snare->timingOffsetTicks <= 24
                   && beat4Snare->timingOffsetTicks >= 10 && beat4Snare->timingOffsetTicks <= 24,
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
                    expect(hatHit->timingOffsetTicks >= 20 && hatHit->timingOffsetTicks <= 50,
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
        validateBoomBapAlgebraProjectCore(project, "BoomBap LofiRap");
        if (project.generationDebugReport.contains("style: Boom Bap Classic Algebra"))
            continue;

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
                return stepIndexOf(note) == step;
            });
        };

        const auto findStep = [](const TrackState* track, int step)
        {
            return std::find_if(track->notes.begin(), track->notes.end(), [step](const NoteEvent& note)
            {
                return stepIndexOf(note) == step;
            });
        };

        const auto countInBar = [](const TrackState* track, int bar)
        {
            if (track == nullptr)
                return 0;

            return static_cast<int>(std::count_if(track->notes.begin(), track->notes.end(), [bar](const NoteEvent& note)
            {
                return stepIndexOf(note) / 16 == bar;
            }));
        };

        int phraseLateKicks = 0;
        for (const auto& note : kick->notes)
        {
            const int step = ((stepIndexOf(note) % 16) + 16) % 16;
            if (step == 10 || step == 11 || step == 14 || step == 15)
                ++phraseLateKicks;
        }

        int phraseSnareGhosts = 0;
        for (const auto& note : snare->notes)
        {
            const int step = ((stepIndexOf(note) % 16) + 16) % 16;
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
            expect(beat2Snare->timingOffsetTicks >= 10 && beat2Snare->timingOffsetTicks <= 26
                   && beat4Snare->timingOffsetTicks >= 10 && beat4Snare->timingOffsetTicks <= 26,
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
                    expect(hatHit->timingOffsetTicks >= 22 && hatHit->timingOffsetTicks <= 58,
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
                return stepIndexOf(note) == step;
            });
        };

        const auto findStep = [](const TrackState* track, int step)
        {
            return std::find_if(track->notes.begin(), track->notes.end(), [step](const NoteEvent& note)
            {
                return stepIndexOf(note) == step;
            });
        };

        const auto countInBar = [](const TrackState* track, int bar)
        {
            if (track == nullptr)
                return 0;

            return static_cast<int>(std::count_if(track->notes.begin(), track->notes.end(), [bar](const NoteEvent& note)
            {
                return stepIndexOf(note) / 16 == bar;
            }));
        };

        int phraseLateKicks = 0;
        for (const auto& note : kick->notes)
        {
            const int step = ((stepIndexOf(note) % 16) + 16) % 16;
            if (step == 10 || step == 14 || step == 15)
                ++phraseLateKicks;
        }

        int phraseSnareGhosts = 0;
        for (const auto& note : snare->notes)
        {
            const int step = ((stepIndexOf(note) % 16) + 16) % 16;
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
            expect(beat2Snare->timingOffsetTicks >= 2 && beat2Snare->timingOffsetTicks <= 15
                   && beat4Snare->timingOffsetTicks >= 2 && beat4Snare->timingOffsetTicks <= 15,
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
                    expect(hatHit->timingOffsetTicks >= 8 && hatHit->timingOffsetTicks <= 42,
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
                if (stepIndexOf(note) / 16 == bar && (((stepIndexOf(note) % 16) + 16) % 16) % 2 == 1)
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
            const int step = ((stepIndexOf(note) % 16) + 16) % 16;
            if (step == 2 || step == 6 || step == 10 || step == 14)
            {
                sum += note.timingOffsetTicks;
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
            out = std::max(out, std::abs(note.timingOffsetTicks));
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
            const int step = ((stepIndexOf(note) % 16) + 16) % 16;
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
                return stepIndexOf(note) == step;
            });
        };

        const auto findStep = [](const TrackState* track, int step)
        {
            return std::find_if(track->notes.begin(), track->notes.end(), [step](const NoteEvent& note)
            {
                return stepIndexOf(note) == step;
            });
        };

        const auto countInBar = [](const TrackState* track, int bar)
        {
            if (track == nullptr)
                return 0;

            return static_cast<int>(std::count_if(track->notes.begin(), track->notes.end(), [bar](const NoteEvent& note)
            {
                return stepIndexOf(note) / 16 == bar;
            }));
        };

        int phraseFunkKicks = 0;
        for (const auto& note : kick->notes)
        {
            const int step = ((stepIndexOf(note) % 16) + 16) % 16;
            if (step == 5 || step == 7 || step == 10 || step == 13 || step == 15)
                ++phraseFunkKicks;
        }

        int phraseOpenHats = 0;
        for (const auto& note : openHat->notes)
        {
            if (((stepIndexOf(note) % 16) + 16) % 16 == 6 || ((stepIndexOf(note) % 16) + 16) % 16 == 14 || ((stepIndexOf(note) % 16) + 16) % 16 == 15)
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
            expect(beat2Snare->timingOffsetTicks >= 7 && beat2Snare->timingOffsetTicks <= 26
                   && beat4Snare->timingOffsetTicks >= 7 && beat4Snare->timingOffsetTicks <= 26,
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
                    expect(hatHit->timingOffsetTicks >= 18 && hatHit->timingOffsetTicks <= 66,
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
                if (stepIndexOf(note) / 16 == bar && (((stepIndexOf(note) % 16) + 16) % 16) % 2 == 1)
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
            const int step = ((stepIndexOf(note) % 16) + 16) % 16;
            if (step == 2 || step == 6 || step == 10 || step == 14)
            {
                sum += note.timingOffsetTicks;
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
            out = std::max(out, std::abs(note.timingOffsetTicks));
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
            const int step = ((stepIndexOf(note) % 16) + 16) % 16;
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
                return stepIndexOf(note) == step;
            });
        };

        const auto findStep = [](const TrackState* track, int step)
        {
            return std::find_if(track->notes.begin(), track->notes.end(), [step](const NoteEvent& note)
            {
                return stepIndexOf(note) == step;
            });
        };

        const auto countInBar = [](const TrackState* track, int bar)
        {
            if (track == nullptr)
                return 0;

            return static_cast<int>(std::count_if(track->notes.begin(), track->notes.end(), [bar](const NoteEvent& note)
            {
                return stepIndexOf(note) / 16 == bar;
            }));
        };

        int phrasePickupKicks = 0;
        for (const auto& note : kick->notes)
        {
            const int step = ((stepIndexOf(note) % 16) + 16) % 16;
            if (step == 3 || step == 6 || step == 10 || step == 14 || step == 15)
                ++phrasePickupKicks;
        }

        int phraseOpenHats = 0;
        for (const auto& note : openHat->notes)
        {
            const int step = ((stepIndexOf(note) % 16) + 16) % 16;
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
            expect(beat2Snare->timingOffsetTicks >= 6 && beat2Snare->timingOffsetTicks <= 26
                   && beat4Snare->timingOffsetTicks >= 6 && beat4Snare->timingOffsetTicks <= 26,
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
                    expect(hatHit->timingOffsetTicks >= 14 && hatHit->timingOffsetTicks <= 58,
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
                if (stepIndexOf(note) / 16 == bar && (((stepIndexOf(note) % 16) + 16) % 16) % 2 == 1)
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
            const int step = ((stepIndexOf(note) % 16) + 16) % 16;
            if (step == 2 || step == 6 || step == 10 || step == 14)
            {
                sum += note.timingOffsetTicks;
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
            out = std::max(out, std::abs(note.timingOffsetTicks));
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
            const int step = ((stepIndexOf(note) % 16) + 16) % 16;
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
                return stepIndexOf(note) == step;
            });
        };

        const auto findStep = [](const TrackState* track, int step)
        {
            return std::find_if(track->notes.begin(), track->notes.end(), [step](const NoteEvent& note)
            {
                return stepIndexOf(note) == step;
            });
        };

        const auto countInBar = [](const TrackState* track, int bar)
        {
            if (track == nullptr)
                return 0;

            return static_cast<int>(std::count_if(track->notes.begin(), track->notes.end(), [bar](const NoteEvent& note)
            {
                return stepIndexOf(note) / 16 == bar;
            }));
        };

        int phraseHardKicks = 0;
        for (const auto& note : kick->notes)
        {
            const int step = ((stepIndexOf(note) % 16) + 16) % 16;
            if (step == 6 || step == 8 || step == 10 || step == 14 || step == 15)
                ++phraseHardKicks;
        }

        int ghostSnares = 0;
        for (const auto& note : snare->notes)
        {
            const int step = ((stepIndexOf(note) % 16) + 16) % 16;
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
            expect(beat2Snare->timingOffsetTicks >= -2 && beat2Snare->timingOffsetTicks <= 12
                   && beat4Snare->timingOffsetTicks >= -2 && beat4Snare->timingOffsetTicks <= 12,
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
                    expect(hatHit->timingOffsetTicks >= 3 && hatHit->timingOffsetTicks <= 32,
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
                if (stepIndexOf(note) / 16 == bar && (((stepIndexOf(note) % 16) + 16) % 16) % 2 == 1)
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
            const int step = ((stepIndexOf(note) % 16) + 16) % 16;
            if (step == 2 || step == 6 || step == 10 || step == 14)
            {
                sum += note.timingOffsetTicks;
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
            out = std::max(out, std::abs(note.timingOffsetTicks));
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
            const int step = ((stepIndexOf(note) % 16) + 16) % 16;
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

void testCompactSampleNamesAndTechnoKit()
{
    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("HPDG_CompactSamples_" + juce::Uuid().toString());
    const auto kick = root.getChildFile("Techno/Kick");
    const auto clap = root.getChildFile("Techno/ClapGhost");
    const auto ride = root.getChildFile("DnB/Ride");
    expect(kick.createDirectory() && clap.createDirectory() && ride.createDirectory(), "Create sample test directories.");
    juce::DynamicObject::Ptr aliases = new juce::DynamicObject();
    for (int index = 1; index <= 12; ++index)
    {
        const auto name = "TKk" + juce::String(index) + ".wav";
        expect(kick.getChildFile(name).replaceWithText("placeholder"), "Create renamed kick fixture.");
        aliases->setProperty(juce::Identifier(name), "Dirty_South_" + juce::String(index).paddedLeft('0', 3) + " - F#");
    }
    expect(kick.getChildFile("sample-names.json").replaceWithText(juce::JSON::toString(juce::var(aliases.get()))), "Write original sample metadata.");
    expect(clap.getChildFile("TCG1.wav").replaceWithText("placeholder")
           && ride.getChildFile("DRD1.wav").replaceWithText("placeholder"), "Create own clap and fallback ride.");
    SampleLibraryManager library;
    library.setRootDirectory(root);
    library.setGenre(GenreType::Techno);
    library.scan();
    const auto& kicks = library.getSamples(TrackType::Kick);
    expect(kicks.size() == 12 && kicks[1].name == "TKk2" && kicks[11].name == "TKk12",
           "Renamed files retain the original sample index order past index nine.");
    expect(library.getSamples(TrackType::Snare).front().file == clap.getChildFile("TCG1.wav"),
           "Techno backbeat uses its own clap kit.");
    expect(library.getSamples(TrackType::GhostKick).front().file == kick.getChildFile("TKk1.wav"),
           "Techno ghost kick uses its own kick kit.");
    expect(library.getSamples(TrackType::Ride).front().file == ride.getChildFile("DRD1.wav"),
           "Missing Techno instruments retain the existing fallback.");
    LaneSampleBank bank;
    bank.applyLibrary(library);
    expect(bank.getSelectedName(TrackType::Kick) == "TKk1" && bank.getSelectedRootPitchClass(TrackType::Kick) == 6,
           "Compact labels preserve the sample's original tuning.");
    expect(bank.hasSamplesMatchingAnyTag(TrackType::Kick, { "Dirty_South" }), "Original style tags survive renaming.");
    expect(bank.selectIndex(TrackType::Kick, 11) && bank.getSelectedRootPitchClass(TrackType::Kick) == 6,
           "Changing compact sample selection preserves tuning.");
    const auto snare = root.getChildFile("Techno/Snare");
    expect(snare.createDirectory() && snare.getChildFile("TSN1.wav").replaceWithText("placeholder"), "Create dedicated Techno snare.");
    library.scan();
    expect(library.getSamples(TrackType::Snare).front().file == snare.getChildFile("TSN1.wav"),
           "Dedicated Techno snare samples take precedence over clap aliases.");
    root.deleteRecursively();
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
    params.candidateCount = 48;

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
    const auto statementHatCount = std::count_if(hats.begin(), hats.end(), [](const auto& note) { return note.barIndex == 0; });
    expect(statementHatCount >= 8,
           "BoomBap Classic Algebra statement must establish a readable eighth-note hat motif.");
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
    {
        maxSnareVelocity = std::max(maxSnareVelocity, note.velocity);
        expect(note.microTimingTicks >= 0,
               "BoomBap Algebra should never rush main backbeat snares.");
        expect(std::abs(note.microTimingTicks) < HiResTiming::kTicks1_64,
               "BoomBap Algebra pocket should stay in PPQ subticks, not whole 1/64 shifts.");
    }
    for (const auto& note : first.notesByLane[BoomBapClassicLanes::ClapGhost])
        expect(note.velocity < maxSnareVelocity,
               "BoomBap Classic Algebra clap ghosts should be lower velocity than main snare.");

    const auto hasMachineGunHatRun = [](const BoomBapClassicAlgebraPattern& pattern)
    {
        for (const int lane : { BoomBapClassicLanes::HiHat, BoomBapClassicLanes::HatAccent })
        {
            std::array<bool, 256> active {};
            for (const auto& note : pattern.notesByLane[static_cast<size_t>(lane)])
                if (note.tick64 >= 0 && note.tick64 < static_cast<int>(active.size()))
                    active[static_cast<size_t>(note.tick64)] = true;

            int run = 0;
            for (bool on : active)
            {
                run = on ? run + 1 : 0;
                if (run >= 4)
                    return true;
            }
        }
        return false;
    };
    expect(!hasMachineGunHatRun(first),
           "BoomBap Algebra hats should not leak into trap-like 1/64 machine-gun texture.");

    expect(first.score.quality > 0.0f,
           "BoomBap Classic Algebra scorer should produce a positive selected quality.");
    expect(first.debugSummary.contains("style: Boom Bap Classic Algebra")
               && first.debugSummary.contains("substyle: Classic")
               && first.debugSummary.contains("selected candidate index")
               && first.debugSummary.contains("repairs applied"),
           "BoomBap Classic Algebra debug summary should include compact generation diagnostics.");

    const auto averageBackbeatMicro = [](const BoomBapClassicAlgebraPattern& pattern)
    {
        int total = 0;
        int count = 0;
        for (const auto& note : pattern.notesByLane[BoomBapClassicLanes::Snare])
        {
            if ((note.tick64 % 64) == 16 || (note.tick64 % 64) == 48)
            {
                total += note.microTimingTicks;
                ++count;
            }
        }
        return count > 0 ? static_cast<float>(total) / static_cast<float>(count) : 0.0f;
    };

    BoomBapClassicAlgebraPattern russian;
    BoomBapClassicAlgebraPattern lofi;
    for (const auto substyle : { BoomBapSubstyle::Classic,
                                 BoomBapSubstyle::Dusty,
                                 BoomBapSubstyle::Jazzy,
                                 BoomBapSubstyle::BoomBapGold,
                                 BoomBapSubstyle::RussianUnderground,
                                 BoomBapSubstyle::LofiRap })
    {
        auto profiledParams = params;
        profiledParams.substyle = substyle;
        profiledParams.candidateCount = 32;

        const auto profiled = generator.generate(profiledParams);
        const auto profiledAgain = generator.generate(profiledParams);
        expect(allNotesEqual(profiled, profiledAgain),
               "BoomBap profile generation should be deterministic for same seed and profile.");

        for (int bar = 0; bar < profiledParams.bars; ++bar)
        {
            expect(hasLaneTick(profiled, BoomBapClassicLanes::Snare, bar, 16),
                   "Every BoomBap profile should keep beat-2 backbeat.");
            expect(hasLaneTick(profiled, BoomBapClassicLanes::Snare, bar, 48),
                   "Every BoomBap profile should keep beat-4 backbeat.");
        }

        expect(profiled.score.trapLeakPenalty < 0.5f,
               "BoomBap profile should score with minimal trap leakage.");
        expect(profiled.score.earlySnarePenalty == 0.0f,
               "BoomBap profile should repair any early main snare.");
        expect(!hasMachineGunHatRun(profiled),
               "BoomBap profile should not make hats a 1/64 noise carpet.");
        expect(static_cast<int>(profiled.notesByLane[BoomBapClassicLanes::Sub808].size()) <= 1,
               "BoomBap profile should keep 808 reinforcement rare.");

        auto changedSeedParams = profiledParams;
        changedSeedParams.seed += 1;
        const auto changedSeed = generator.generate(changedSeedParams);
        expect(!allNotesEqual(profiled, changedSeed),
               "BoomBap profile should vary musically across different seeds.");

        if (substyle == BoomBapSubstyle::RussianUnderground)
            russian = profiled;
        if (substyle == BoomBapSubstyle::LofiRap)
            lofi = profiled;
    }

    expect(averageBackbeatMicro(russian) >= averageBackbeatMicro(lofi),
           "RussianUnderground profile should keep a heavier late-snare pocket than LofiRap.");
}

void testBoomBapEngine21TimingArchitecture()
{
    expect(BoomBapTiming::getSixteenthSwingOffsetPPQ(0.50) == 0, "Straight swing must be zero PPQ.");
    expect(BoomBapTiming::getSixteenthSwingOffsetPPQ(0.55) == 24, "55% swing must be 24 PPQ.");
    expect(BoomBapTiming::getSixteenthSwingOffsetPPQ(0.60) == 48, "60% swing must be 48 PPQ.");
    expect(std::abs(BoomBapTiming::ppqToMilliseconds(48, 85.0) - 35.294) < 0.02,
           "48 PPQ at 85 BPM must be about 35.3 ms.");
    expect(std::abs(BoomBapTiming::ppqToMilliseconds(48, 90.0) - 33.333) < 0.02,
           "48 PPQ at 90 BPM must be about 33.3 ms.");

    BoomBapClassicAlgebraParams params;
    params.seed = 731;
    params.bars = 4;
    params.swing = 0.60f;
    params.candidateCount = 64;
    BoomBapClassicAlgebraGenerator generator;
    const auto first = generator.generate(params);
    const auto second = generator.generate(params);
    expect(first.debugSummary == second.debugSummary, "Same seed/settings must remain deterministic.");
    for (const auto& note : first.notesByLane[BoomBapClassicLanes::Snare])
    {
        expect(note.timing.structuralSwingPPQ == 0, "Main snare must never receive structural swing.");
        expect(note.microTimingTicks == note.timing.total(), "Timing components must sum to microOffset.");
    }
    for (const auto& lane : first.notesByLane)
        for (const auto& note : lane)
            expect(std::abs(note.timing.humanJitterPPQ) <= 4, "Human jitter must remain profile-bounded.");
}

void testBoomBapEngine22ContextualDiversity()
{
    BoomBapClassicAlgebraGenerator generator;
    BoomBapClassicAlgebraParams params;
    params.seed = 2202; params.bars = 4; params.candidateCount = 64;
    for (int archetype = 0; archetype < 5; ++archetype)
    {
        params.forcedArchetype = static_cast<BoomBapGrooveArchetype>(archetype);
        const auto pattern = generator.generate(params);
        expect(pattern.context.archetype == *params.forcedArchetype, "Forced diagnostic archetype must be honored.");
        expect(pattern.score.quality > 0.0f, "Every hidden archetype must produce valid BoomBap.");
    }
    params.forcedArchetype.reset();
    for (const auto event : { RarePhraseEvent::KickTurnaround,
                              RarePhraseEvent::HatDropout,
                              RarePhraseEvent::OpenHatLift,
                              RarePhraseEvent::BreakStop })
    {
        params.seed += 1;
        params.forcedRareEvent = event;
        const auto pattern = generator.generate(params);
        expect(pattern.context.requestedEvent == *params.forcedRareEvent, "Forced rare event intent must be deterministic.");
        expect(pattern.realizedEvent == *params.forcedRareEvent, "Valid forced phrase event must survive generation.");
        expect(!pattern.score.hardTrapLeak, "Rare phrase event must not create persistent trap leakage.");
        for (const auto& ghost : pattern.notesByLane[BoomBapClassicLanes::ClapGhost])
        {
            if (ghost.role == BoomBapClassicRole::FillSupport || ghost.role == BoomBapClassicRole::ClapLayer)
                continue;
            expect(ghost.anchorLane == BoomBapClassicLanes::Snare && ghost.anchorTick64 >= 0,
                   "Normal support snare must retain a main-snare anchor.");
            const auto anchor = std::find_if(pattern.notesByLane[BoomBapClassicLanes::Snare].begin(),
                                             pattern.notesByLane[BoomBapClassicLanes::Snare].end(),
                                             [&](const auto& note) { return note.tick64 == ghost.anchorTick64; });
            expect(anchor != pattern.notesByLane[BoomBapClassicLanes::Snare].end() && ghost.velocity < anchor->velocity,
                   "Contextual ghost must remain quieter than its anchor.");
        }
    }
    params.forcedRareEvent.reset();
    const auto a = generator.generate(params);
    const auto b = generator.generate(params);
    expect(a.debugSummary == b.debugSummary, "2.2 context and near-best selection must remain deterministic.");
    expect(a.nearBestPoolSize >= 1, "Near-best selection pool must never be empty.");
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
    failures += runTest("Compact sample names and own Techno kit", testCompactSampleNamesAndTechnoKit);
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
    failures += runTest("Tempo interpretation half-time band selection", testTempoInterpretationHalfTimeBandSelection);
    failures += runTest("Tempo interpretation auto genre folding", testTempoInterpretationAutoGenreFolding);
    failures += runTest("Trap scientific tempo context", testTrapTempoContextScientificMapping);
    failures += runTest("Shared generation model core", testSharedGenerationModelCore);
    failures += runTest("BoomBap production Algebra routing", testBoomBapProductionAlwaysUsesAlgebra);
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
    failures += runTest("Trap Algebra engine smoke", testTrapAlgebraEngineSmoke);
    failures += runTest("TrapEngine uses Algebra generation smoke", testTrapEngineUsesAlgebraGenerationSmoke);
    failures += runTest("Sample apply weights smoke", testSampleApplyWeightsSmoke);
    failures += runTest("Extract pattern blend and copy smoke", testExtractPatternBlendAndCopySmoke);
    if (kShowRapAndDrillGenres) // Rap's Dirty South kit is parked while Rap / Drill are hidden.
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
    failures += runTest("BoomBap Engine 2.1 timing architecture", testBoomBapEngine21TimingArchitecture);
    failures += runTest("BoomBap Engine 2.2 contextual diversity", testBoomBapEngine22ContextualDiversity);
    failures += runTest("BoomBap Dusty pocket generation smoke", testBoomBapDustyPocketGenerationSmoke);
    failures += runTest("BoomBap Jazzy pocket generation smoke", testBoomBapJazzyPocketGenerationSmoke);
    failures += runTest("BoomBap Gold pocket generation smoke", testBoomBapGoldPocketGenerationSmoke);
    failures += runTest("BoomBap never leaves a bar without a kick", testBoomBapNoBarWithoutKick);
    failures += runTest("BoomBap bass is opt-in and plays a line", testBoomBapBassOptInAndLine);
    failures += runTest("BoomBap bass varies between generations", testBoomBapBassVariety);
    failures += runTest("BoomBap bass follows the sample mood", testBoomBapBassFollowsSampleMood);
    failures += runTest("BoomBap bass RG and amount [1][2][3]", testBoomBapBassRegenerateAndAmount);
    failures += runTest("DnB grammar invariants", testDnBGrammarInvariants);
    failures += runTest("DnB deterministic and varied", testDnBDeterministicAndVaried);
    failures += runTest("DnB substyle character", testDnBSubstyleCharacter);
    failures += runTest("Techno grammar invariants", testTechnoGrammarInvariants);
    failures += runTest("Techno bass rules", testTechnoBassRules);
    failures += runTest("Techno deterministic, varied, calibration", testTechnoDeterministicVariedAndCalibration);
    failures += runTest("DnB engine in a project", testDnBEngineInProject);
    failures += runTest("DnB bass line", testDnBBassLine);
    failures += runTest("DnB bass follows the sample's roots", testDnBBassFollowsSampleRoots);
    failures += runTest("DnB bass in a project", testDnBBassInProject);
    failures += runTest("BoomBap Russian Underground pocket generation smoke", testBoomBapRussianUndergroundPocketGenerationSmoke);
    failures += runTest("BoomBap LofiRap pocket generation smoke", testBoomBapLofiRapPocketGenerationSmoke);
    return failures == 0 ? 0 : 1;
}
