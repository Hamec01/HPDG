#include "DrillEngine.h"

#include <algorithm>
#include <cmath>

#include "Drill/DrillGrooveBlueprint.h"
#include "../Core/TrackRegistry.h"
#include "HiResTiming.h"
#include "StyleInfluence.h"
#include "StyleDefaults.h"

namespace bbg
{
namespace
{
TrackState* findTrack(PatternProject& project, TrackType type)
{
    for (auto& t : project.tracks)
        if (t.type == type)
            return &t;
    return nullptr;
}

float laneActivityWeight(const PatternProject& project, TrackType type)
{
    return std::clamp(laneBiasFor(project.styleInfluence, type).activityWeight, 0.5f, 1.6f);
}

bool isAnchorStep(TrackType type, int stepInBar)
{
    if (type == TrackType::Kick || type == TrackType::Sub808)
        return stepInBar == 0 || stepInBar == 10;
    if (type == TrackType::Snare || type == TrackType::ClapGhostSnare)
        return stepInBar == 4 || stepInBar == 12;
    if (type == TrackType::HiHat)
        return (stepInBar % 3) == 0;
    return false;
}

const StepFeature* featureAtStep(const AudioFeatureMap& map, int step)
{
    if (map.steps.empty() || map.stepsPerBar <= 0)
        return nullptr;

    const int normalized = std::max(0, step);
    const size_t idx = static_cast<size_t>(normalized) % map.steps.size();
    return &map.steps[idx];
}

bool isDrillHatFamily(TrackType type)
{
    return type == TrackType::HiHat
        || type == TrackType::HatFX
        || type == TrackType::OpenHat
        || type == TrackType::Perc
        || type == TrackType::Ride
        || type == TrackType::Cymbal;
}

void applySampleAwareDrillFlavor(PatternProject& project, const std::unordered_set<TrackType>& mutableTracks)
{
    const auto& ctx = project.sampleContext;
    if (!ctx.enabled || ctx.featureMap.steps.empty())
        return;

    const float react = std::clamp(ctx.reactivity, 0.0f, 1.0f);
    const float contrast = std::clamp(ctx.supportVsContrast, 0.0f, 1.0f);
    const float support = 1.0f - contrast;

    for (auto& track : project.tracks)
    {
        if (mutableTracks.count(track.type) == 0)
            continue;

        for (auto& note : track.notes)
        {
            const auto* f = featureAtStep(ctx.featureMap, note.step);
            if (f == nullptr)
                continue;

            float guide = 0.42f * f->accent + 0.32f * f->onset + 0.26f * f->energy;
            if (track.type == TrackType::Kick || track.type == TrackType::Sub808 || track.type == TrackType::GhostKick)
                guide = 0.48f * f->low + 0.28f * f->accent + 0.24f * f->onset;
            else if (isDrillHatFamily(track.type))
                guide = 0.46f * f->high + 0.34f * f->energy + 0.20f * f->onset;

            const float gain = std::clamp(1.0f
                                              + 0.24f * react * support * (guide - 0.5f)
                                              + 0.20f * react * contrast * (0.5f - guide),
                                          0.64f,
                                          1.46f);
            note.velocity = std::clamp(static_cast<int>(static_cast<float>(note.velocity) * gain), 1, 127);

            if ((track.type == TrackType::Snare || track.type == TrackType::ClapGhostSnare)
                && f->nearPhraseBoundary
                && contrast > 0.10f)
            {
                const int nudge = static_cast<int>(1.0f + 3.0f * contrast * react);
                note.microOffset = std::clamp(note.microOffset + nudge, -120, 120);
            }
        }

        if (isDrillHatFamily(track.type) && support * react > 0.44f)
        {
            track.notes.erase(std::remove_if(track.notes.begin(), track.notes.end(), [&](const NoteEvent& note)
            {
                const auto* f = featureAtStep(ctx.featureMap, note.step);
                if (f == nullptr)
                    return false;

                const int phase = (note.step + note.velocity + static_cast<int>(track.type)) % 10;
                return phase == 0 && f->high > 0.83f && f->onset < 0.24f && !f->isStrongBeat;
            }), track.notes.end());
        }
    }
}

juce::String roleForTrack(TrackType type)
{
    switch (type)
    {
        case TrackType::Kick: return "drill_kick";
        case TrackType::Sub808: return "drill_sub";
        case TrackType::HiHat: return "drill_hat";
        case TrackType::HatFX: return "drill_hat_fx";
        case TrackType::Snare: return "drill_backbeat";
        case TrackType::ClapGhostSnare: return "drill_layer";
        case TrackType::OpenHat: return "drill_open";
        case TrackType::GhostKick: return "drill_support";
        case TrackType::Cymbal: return "drill_marker";
        case TrackType::Perc: return "drill_texture";
        case TrackType::Ride: return "drill_ride";
        default: return "drill_lane";
    }
}

void dedupeAndSort(std::vector<NoteEvent>& notes)
{
    std::sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b)
    {
        if (a.step != b.step)
            return a.step < b.step;
        if (a.microOffset != b.microOffset)
            return a.microOffset < b.microOffset;
        if (a.pitch != b.pitch)
            return a.pitch < b.pitch;
        return a.velocity > b.velocity;
    });

    notes.erase(std::unique(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b)
    {
        return a.step == b.step && a.microOffset == b.microOffset && a.pitch == b.pitch;
    }), notes.end());
}

bool isDrillHatBackbone(const NoteEvent& note)
{
    const int tick = HiResTiming::noteTick(note);
    return (tick % HiResTiming::kTicks1_8) == 0
        || (tick % HiResTiming::kTicks1_24) == 0;
}

void expandCoupledTracks(TrackType trigger, std::unordered_set<TrackType>& tracks)
{
    if (trigger == TrackType::HiHat || trigger == TrackType::HatFX)
    {
        tracks.insert(TrackType::HiHat);
        tracks.insert(TrackType::HatFX);
    }

    if (trigger == TrackType::Kick || trigger == TrackType::Sub808)
    {
        tracks.insert(TrackType::Kick);
        tracks.insert(TrackType::Sub808);
    }
}

int ghostSnareLimitPerTwoBars(DrillSubstyle substyle)
{
    switch (substyle)
    {
        case DrillSubstyle::UKDrill: return 2;
        case DrillSubstyle::BrooklynDrill: return 3;
        case DrillSubstyle::NYDrill: return 2;
        case DrillSubstyle::DarkDrill: return 1;
    }

    return 2;
}

void snapNoteToMicroGrid(NoteEvent& note, int bars, int gridTicks)
{
    const int safeGrid = std::max(1, gridTicks);
    const int totalTicks = std::max(1, bars * 16 * ticksPerStep());

    int tick = note.step * ticksPerStep() + note.microOffset;
    tick = std::clamp(tick, 0, totalTicks - 1);
    tick = HiResTiming::quantizeTicks(tick, safeGrid);
    tick = std::clamp(tick, 0, totalTicks - 1);

    int step = tick / ticksPerStep();
    int micro = tick - step * ticksPerStep();
    if (micro > ticksPerStep() / 2)
    {
        micro -= ticksPerStep();
        ++step;
    }

    note.step = std::clamp(step, 0, std::max(0, bars * 16 - 1));
    note.microOffset = std::clamp(micro, -ticksPerStep() / 2, ticksPerStep() / 2);
}

void applyDrillStyleInfluence(PatternProject& project)
{
    juce::String applyError;
    DrillStyleInfluence::apply(project, &applyError);
    juce::ignoreUnused(applyError);
}
}

DrillEngine::DrillEngine() = default;

void DrillEngine::generate(PatternProject& project)
{
    applyDrillStyleInfluence(project);
    const auto& style = getDrillProfile(project.params.drillSubstyle);
    const auto styleSpec = getDrillStyleSpec(style.substyle);
    const auto bassSpec = getDrill808StyleSpec(style.substyle);
    std::mt19937 rng(static_cast<std::mt19937::result_type>(project.params.seed + project.generationCounter * 59 + 907));
    const int bars = std::max(1, project.params.bars);
    const auto phrasePlan = DrillPhrasePlanner::createDetailedPlan(bars, project.params.densityAmount, rng);

    std::vector<DrillPhraseRole> phrase;
    phrase.reserve(phrasePlan.bars.size());
    for (const auto& barPlan : phrasePlan.bars)
        phrase.push_back(barPlan.role);

    auto grooveBlueprint = DrillGrooveBlueprintBuilder::build(phrasePlan, bars, style.substyle, project.params.densityAmount);

    std::unordered_set<TrackType> mutableTracks;
    for (auto& track : project.tracks)
    {
        if (track.locked || !track.enabled)
            continue;

        track.templateId += 1;
        track.variationId = 0;
        track.mutationDepth = 0.0f;
        track.subProfile = style.name;
        track.laneRole = roleForTrack(track.type);
        mutableTracks.insert(track.type);
    }

    generateDrillSnareBackbone(project, style, phrase, rng, mutableTracks);
    if (const auto* snare = findTrack(project, TrackType::Snare); snare != nullptr)
        grooveBlueprint.ingestSnareTrack(*snare, 1);

    generateDrillKickSkeleton(project, style, phrase, &grooveBlueprint, rng, mutableTracks);
    if (const auto* kick = findTrack(project, TrackType::Kick); kick != nullptr)
        grooveBlueprint.ingestKickTrack(*kick);

    generateDrillHiHatStructure(project, style, phrase, &grooveBlueprint, rng, mutableTracks);
    generateSub808RhythmFromDrillKicks(project, style, bassSpec, phrase, &grooveBlueprint, rng, mutableTracks);
    assignDrillSub808Pitches(project, style, bassSpec, phrase, &grooveBlueprint, rng, mutableTracks);
    applyDrillSub808Slides(project, bassSpec, phrase, &grooveBlueprint, rng, mutableTracks);
    generateDrillHatFX(project, style, phrase, &grooveBlueprint, rng, mutableTracks);
    generateDrillSupportLanes(project, style, styleSpec, phrase, &grooveBlueprint, rng, mutableTracks);
    crossTrackResolver.resolve(project, style, grooveBlueprint, mutableTracks);
    applyDrillHumanization(project, style, rng, mutableTracks);
    validatePattern(project, mutableTracks);
}

void DrillEngine::regenerateTrack(PatternProject& project, TrackType trackType)
{
    applyDrillStyleInfluence(project);
    regenerateTrackVariation(project, trackType);
}

void DrillEngine::generateTrackNew(PatternProject& project, TrackType trackType)
{
    applyDrillStyleInfluence(project);
    auto* track = findTrack(project, trackType);
    if (track == nullptr || track->locked)
        return;

    const auto& style = getDrillProfile(project.params.drillSubstyle);
    const auto styleSpec = getDrillStyleSpec(style.substyle);
    const auto bassSpec = getDrill808StyleSpec(style.substyle);
    std::mt19937 rng(static_cast<std::mt19937::result_type>(project.params.seed + static_cast<int>(trackType) * 53 + project.generationCounter * 23 + 997));
    const int bars = std::max(1, project.params.bars);
    const auto phrasePlan = DrillPhrasePlanner::createDetailedPlan(bars, project.params.densityAmount, rng);

    std::vector<DrillPhraseRole> phrase;
    phrase.reserve(phrasePlan.bars.size());
    for (const auto& barPlan : phrasePlan.bars)
        phrase.push_back(barPlan.role);

    auto grooveBlueprint = DrillGrooveBlueprintBuilder::build(phrasePlan, bars, style.substyle, project.params.densityAmount);

    std::unordered_set<TrackType> mutableTracks { trackType };
    expandCoupledTracks(trackType, mutableTracks);
    generateDrillSnareBackbone(project, style, phrase, rng, mutableTracks);
    if (const auto* snare = findTrack(project, TrackType::Snare); snare != nullptr)
        grooveBlueprint.ingestSnareTrack(*snare, 1);

    generateDrillKickSkeleton(project, style, phrase, &grooveBlueprint, rng, mutableTracks);
    if (const auto* kick = findTrack(project, TrackType::Kick); kick != nullptr)
        grooveBlueprint.ingestKickTrack(*kick);

    generateDrillHiHatStructure(project, style, phrase, &grooveBlueprint, rng, mutableTracks);
    generateSub808RhythmFromDrillKicks(project, style, bassSpec, phrase, &grooveBlueprint, rng, mutableTracks);
    assignDrillSub808Pitches(project, style, bassSpec, phrase, &grooveBlueprint, rng, mutableTracks);
    applyDrillSub808Slides(project, bassSpec, phrase, &grooveBlueprint, rng, mutableTracks);
    generateDrillHatFX(project, style, phrase, &grooveBlueprint, rng, mutableTracks);
    generateDrillSupportLanes(project, style, styleSpec, phrase, &grooveBlueprint, rng, mutableTracks);
    crossTrackResolver.resolve(project, style, grooveBlueprint, mutableTracks);
    applyDrillHumanization(project, style, rng, mutableTracks);
    validatePattern(project, mutableTracks);
}

void DrillEngine::regenerateTrackVariation(PatternProject& project, TrackType trackType)
{
    auto* target = findTrack(project, trackType);
    if (target == nullptr || target->locked || !target->enabled)
        return;

    const auto before = target->notes;
    generateTrackNew(project, trackType);

    target = findTrack(project, trackType);
    if (target == nullptr)
        return;

    const auto& styleDefaults = getGenreStyleDefaults(GenreType::Drill, project.params.drillSubstyle);
    const auto& laneDefaults = getLaneStyleDefaults(styleDefaults, trackType);
    const float rgIntensity = std::clamp(laneDefaults.rgVariationIntensity, 0.6f, 1.35f);

    if (trackType == TrackType::HiHat)
    {
        std::vector<NoteEvent> merged = target->notes;
        for (const auto& oldNote : before)
            if (isDrillHatBackbone(oldNote))
                merged.push_back(oldNote);
        target->notes = std::move(merged);
    }
    else if (trackType == TrackType::HatFX)
    {
        std::vector<NoteEvent> merged = target->notes;
        const size_t keep = std::min(static_cast<size_t>(before.size() * 0.24f * rgIntensity), before.size());
        for (size_t i = 0; i < keep; ++i)
            merged.push_back(before[i]);
        target->notes = std::move(merged);
    }
    else
    {
        const size_t keep = std::min(static_cast<size_t>(before.size() * 0.30f * rgIntensity), target->notes.size());
        for (size_t i = 0; i < keep; ++i)
            target->notes[i] = before[i];
    }

    dedupeAndSort(target->notes);
    target->variationId += 1;
    target->mutationDepth = std::clamp(target->mutationDepth + 0.08f, 0.0f, 1.0f);
}

void DrillEngine::mutatePattern(PatternProject& project)
{
    applyDrillStyleInfluence(project);
    std::vector<TrackType> candidates;
    for (const auto& track : project.tracks)
        if (!track.locked && track.enabled)
            candidates.push_back(track.type);

    if (candidates.empty())
        return;

    std::mt19937 rng(static_cast<std::mt19937::result_type>(project.params.seed + project.mutationCounter * 467 + 11));
    std::shuffle(candidates.begin(), candidates.end(), rng);
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);

    const int count = std::max(1, static_cast<int>(candidates.size() / 3));
    for (int i = 0; i < count && i < static_cast<int>(candidates.size()); ++i)
        mutateTrack(project, candidates[static_cast<size_t>(i)]);

    if (chance(rng) < 0.72f)
    {
        auto* kick = findTrack(project, TrackType::Kick);
        auto* sub = findTrack(project, TrackType::Sub808);
        if (kick != nullptr && sub != nullptr
            && !kick->locked && kick->enabled
            && !sub->locked && sub->enabled)
        {
            mutateTrack(project, TrackType::Kick);
            mutateTrack(project, TrackType::Sub808);
        }
    }
}

void DrillEngine::mutateTrack(PatternProject& project, TrackType trackType)
{
    applyDrillStyleInfluence(project);
    auto* track = findTrack(project, trackType);
    if (track == nullptr || track->locked || !track->enabled)
        return;

    if (track->notes.empty())
    {
        generateTrackNew(project, trackType);
        return;
    }

    std::mt19937 rng(static_cast<std::mt19937::result_type>(project.params.seed + project.mutationCounter * 379 + static_cast<int>(trackType) * 79));
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);

    const auto& styleDefaults = getGenreStyleDefaults(GenreType::Drill, project.params.drillSubstyle);
    const auto& lane = getLaneStyleDefaults(styleDefaults, trackType);
    const float intensity = std::clamp(lane.mutationIntensity, 0.5f, 1.45f);

    if (trackType == TrackType::HiHat || trackType == TrackType::HatFX)
    {
        TrackState rebuilt;
        rebuilt.type = track->type;

        std::uniform_int_distribution<int> shiftPicker(0, 4);
        for (const auto& note : track->notes)
        {
            const bool backbone = trackType == TrackType::HiHat && isDrillHatBackbone(note);
            if (!backbone && chance(rng) < 0.22f * intensity)
                continue;

            int tick = HiResTiming::noteTick(note);
            if (!backbone && chance(rng) < 0.6f * intensity)
            {
                const int pick = shiftPicker(rng);
                const int delta = pick == 0 ? -HiResTiming::kTicks1_64
                                            : pick == 1 ? HiResTiming::kTicks1_64
                                                        : pick == 2 ? -HiResTiming::kTicks1_24
                                                                    : pick == 3 ? HiResTiming::kTicks1_24
                                                                                : HiResTiming::kTicks1_32;
                tick += delta;
            }

            const int velJ = static_cast<int>(std::round((chance(rng) - 0.5f) * 14.0f));
            HiResTiming::addNoteAtTick(rebuilt,
                                       note.pitch,
                                       tick,
                                       std::clamp(note.velocity + velJ, 1, 127),
                                       note.isGhost,
                                       std::max(1, project.params.bars),
                                       note.length);
        }

        if (chance(rng) < 0.48f * intensity)
        {
            const int bars = std::max(1, project.params.bars);
            const int edgeTick = bars * HiResTiming::kTicksPerBar4_4 - HiResTiming::kTicks1_8;
            for (int i = 0; i < 3; ++i)
                HiResTiming::addNoteAtTick(rebuilt,
                                           44,
                                           edgeTick + i * HiResTiming::kTicks1_24,
                                           std::clamp(78 - i * 6, 1, 127),
                                           true,
                                           bars);
        }

        track->notes = std::move(rebuilt.notes);
        dedupeAndSort(track->notes);
        track->variationId += 1;
        track->mutationDepth = std::clamp(track->mutationDepth + 0.14f, 0.0f, 1.0f);
        return;
    }

    if (chance(rng) < 0.54f * intensity)
    {
        std::vector<size_t> removable;
        for (size_t i = 0; i < track->notes.size(); ++i)
            if (!isAnchorStep(trackType, track->notes[i].step % 16))
                removable.push_back(i);

        if (!removable.empty())
        {
            std::uniform_int_distribution<size_t> pick(0, removable.size() - 1);
            track->notes.erase(track->notes.begin() + static_cast<long long>(removable[pick(rng)]));
        }
    }

    if (!track->notes.empty() && chance(rng) < 0.68f * intensity)
    {
        std::uniform_int_distribution<size_t> pick(0, track->notes.size() - 1);
        auto& n = track->notes[pick(rng)];
        if (!isAnchorStep(trackType, n.step % 16))
        {
            std::uniform_int_distribution<int> shift(-1, 2);
            n.step = std::clamp(n.step + shift(rng), 0, std::max(0, project.params.bars * 16 - 1));
        }
    }

    if (!track->notes.empty() && chance(rng) < 0.72f * intensity)
    {
        std::uniform_int_distribution<size_t> pick(0, track->notes.size() - 1);
        auto& n = track->notes[pick(rng)];
        std::uniform_int_distribution<int> v(-10, 10);
        n.velocity = std::clamp(n.velocity + v(rng), 1, 127);
    }

    dedupeAndSort(track->notes);
    track->variationId += 1;
    track->mutationDepth = std::clamp(track->mutationDepth + 0.12f, 0.0f, 1.0f);
}

void DrillEngine::regenerateTrackInternal(PatternProject& project,
                                          TrackState& track,
                                          const DrillStyleProfile& style,
                                          const std::vector<DrillPhraseRole>& phrase,
                                          std::mt19937& rng) const
{
    if (!track.enabled)
    {
        track.notes.clear();
        return;
    }

    switch (track.type)
    {
        case TrackType::Kick: kickGenerator.generate(track, project.params, style, project.styleInfluence, phrase, nullptr, rng); break;
        case TrackType::Snare: snareGenerator.generate(track, project.params, style, phrase, rng); break;
        case TrackType::HiHat:
        {
            const auto* snareTrack = findTrack(project, TrackType::Snare);
            const auto* clapTrack = findTrack(project, TrackType::ClapGhostSnare);
            const auto* kickTrack = findTrack(project, TrackType::Kick);
            hatGenerator.generate(track, project.params, style, project.styleInfluence, phrase, nullptr, snareTrack, clapTrack, kickTrack, rng);
            break;
        }
        default: track.notes.clear(); break;
    }

    track.subProfile = style.name;
    track.laneRole = roleForTrack(track.type);
}

void DrillEngine::generateDrillSnareBackbone(PatternProject& project,
                                             const DrillStyleProfile& style,
                                             const std::vector<DrillPhraseRole>& phrase,
                                             std::mt19937& rng,
                                             const std::unordered_set<TrackType>& mutableTracks) const
{
    auto* snare = findTrack(project, TrackType::Snare);
    if (snare == nullptr || mutableTracks.count(snare->type) == 0 || snare->locked || !snare->enabled)
        return;

    snareGenerator.generate(*snare, project.params, style, phrase, rng);
}

void DrillEngine::generateDrillHiHatStructure(PatternProject& project,
                                              const DrillStyleProfile& style,
                                              const std::vector<DrillPhraseRole>& phrase,
                                              const DrillGrooveBlueprint* blueprint,
                                              std::mt19937& rng,
                                              const std::unordered_set<TrackType>& mutableTracks) const
{
    auto* hat = findTrack(project, TrackType::HiHat);
    if (hat == nullptr || mutableTracks.count(hat->type) == 0 || hat->locked || !hat->enabled)
        return;

    const auto* snare = findTrack(project, TrackType::Snare);
    const auto* clap = findTrack(project, TrackType::ClapGhostSnare);
    const auto* kick = findTrack(project, TrackType::Kick);
    hatGenerator.generate(*hat, project.params, style, project.styleInfluence, phrase, blueprint, snare, clap, kick, rng);
}

void DrillEngine::generateDrillKickSkeleton(PatternProject& project,
                                            const DrillStyleProfile& style,
                                            const std::vector<DrillPhraseRole>& phrase,
                                            const DrillGrooveBlueprint* blueprint,
                                            std::mt19937& rng,
                                            const std::unordered_set<TrackType>& mutableTracks) const
{
    auto* kick = findTrack(project, TrackType::Kick);
    if (kick == nullptr || mutableTracks.count(kick->type) == 0 || kick->locked || !kick->enabled)
        return;

    kickGenerator.generate(*kick, project.params, style, project.styleInfluence, phrase, blueprint, rng);
}

void DrillEngine::generateSub808RhythmFromDrillKicks(PatternProject& project,
                                                     const DrillStyleProfile& style,
                                                     const Drill808StyleSpec& spec,
                                                     const std::vector<DrillPhraseRole>& phrase,
                                                     const DrillGrooveBlueprint* blueprint,
                                                     std::mt19937& rng,
                                                     const std::unordered_set<TrackType>& mutableTracks) const
{
    const auto& styleDefaults = getGenreStyleDefaults(GenreType::Drill, project.params.drillSubstyle);
    auto* kick = findTrack(project, TrackType::Kick);
    auto* sub = findTrack(project, TrackType::Sub808);
    if (kick == nullptr || sub == nullptr)
        return;
    if (mutableTracks.count(sub->type) == 0 || sub->locked || !sub->enabled)
        return;

    const auto& lane = getLaneStyleDefaults(styleDefaults, sub->type);
    subGenerator.generateRhythm(*sub, *kick, project.params, style, spec, project.styleInfluence, lane.sub808Activity, phrase, blueprint, rng);
}

void DrillEngine::assignDrillSub808Pitches(PatternProject& project,
                                           const DrillStyleProfile& style,
                                           const Drill808StyleSpec& spec,
                                           const std::vector<DrillPhraseRole>& phrase,
                                           const DrillGrooveBlueprint* blueprint,
                                           std::mt19937& rng,
                                           const std::unordered_set<TrackType>& mutableTracks) const
{
    auto* sub = findTrack(project, TrackType::Sub808);
    if (sub == nullptr || mutableTracks.count(sub->type) == 0 || sub->locked || !sub->enabled)
        return;

    subGenerator.assignPitches(*sub, project.params, style, spec, phrase, blueprint, rng);
}

void DrillEngine::applyDrillSub808Slides(PatternProject& project,
                                         const Drill808StyleSpec& spec,
                                         const std::vector<DrillPhraseRole>& phrase,
                                         const DrillGrooveBlueprint* blueprint,
                                         std::mt19937& rng,
                                         const std::unordered_set<TrackType>& mutableTracks) const
{
    auto* sub = findTrack(project, TrackType::Sub808);
    if (sub == nullptr || mutableTracks.count(sub->type) == 0 || sub->locked || !sub->enabled)
        return;

    subGenerator.applySlides(*sub, spec, phrase, blueprint, rng);
}

void DrillEngine::generateDrillHatFX(PatternProject& project,
                                     const DrillStyleProfile& style,
                                     const std::vector<DrillPhraseRole>& phrase,
                                     const DrillGrooveBlueprint* blueprint,
                                     std::mt19937& rng,
                                     const std::unordered_set<TrackType>& mutableTracks) const
{
    const auto& styleDefaults = getGenreStyleDefaults(GenreType::Drill, project.params.drillSubstyle);
    auto* hat = findTrack(project, TrackType::HiHat);
    auto* hatFx = findTrack(project, TrackType::HatFX);
    if (hat == nullptr || hatFx == nullptr)
        return;
    if (mutableTracks.count(hatFx->type) == 0 || hatFx->locked || !hatFx->enabled)
        return;

    const auto& lane = getLaneStyleDefaults(styleDefaults, hatFx->type);
    const auto* kick = findTrack(project, TrackType::Kick);
    const auto* snare = findTrack(project, TrackType::Snare);
    const auto* openHat = findTrack(project, TrackType::OpenHat);
    const auto* sub = findTrack(project, TrackType::Sub808);
    hatFxGenerator.generate(*hatFx,
                            *hat,
                            kick,
                            snare,
                            openHat,
                            sub,
                            style,
                            lane.hatFxIntensity * laneActivityWeight(project, TrackType::HatFX),
                            phrase,
                            blueprint,
                            rng);
}

void DrillEngine::generateDrillSupportLanes(PatternProject& project,
                                            const DrillStyleProfile& style,
                                            const DrillStyleSpec& spec,
                                            const std::vector<DrillPhraseRole>& phrase,
                                            const DrillGrooveBlueprint* blueprint,
                                            std::mt19937& rng,
                                            const std::unordered_set<TrackType>& mutableTracks) const
{
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    const auto& styleDefaults = getGenreStyleDefaults(GenreType::Drill, project.params.drillSubstyle);
    const int bars = std::max(1, project.params.bars);

    auto* hat = findTrack(project, TrackType::HiHat);
    auto* kick = findTrack(project, TrackType::Kick);
    auto* snare = findTrack(project, TrackType::Snare);

    if (auto* open = findTrack(project, TrackType::OpenHat);
        open != nullptr && hat != nullptr && mutableTracks.count(open->type) != 0 && !open->locked && open->enabled)
    {
        open->notes.clear();
        std::uniform_int_distribution<int> vel(style.hatVelocityMin + 4, style.hatVelocityMax);
        const auto& lane = getLaneStyleDefaults(styleDefaults, open->type);
        for (const auto& h : hat->notes)
        {
            const auto* slot = blueprint != nullptr ? blueprint->slotAt(h.step) : nullptr;
            if (slot != nullptr && (slot->snareProtection || slot->majorEventReserved || slot->kickPlaced))
                continue;

            const int bar = h.step / 16;
            const auto role = bar < static_cast<int>(phrase.size()) ? phrase[static_cast<size_t>(bar)] : DrillPhraseRole::Statement;
            const float openBias = spec.allowOpenHatFrequently ? 1.3f : 0.75f;
            float gate = std::clamp(style.openHatChance * lane.noteProbability * openBias, 0.01f, 0.55f);
            if ((h.step % 16) >= 13)
                gate += 0.12f;
            if (role == DrillPhraseRole::Response)
                gate *= 0.7f;
            if (chance(rng) < std::clamp(gate, 0.01f, 0.9f))
                open->notes.push_back({ 46, h.step, 1, vel(rng), 0, false });
        }
    }

    if (auto* clap = findTrack(project, TrackType::ClapGhostSnare);
        clap != nullptr && snare != nullptr && mutableTracks.count(clap->type) != 0 && !clap->locked && clap->enabled)
    {
        clap->notes.clear();
        std::uniform_int_distribution<int> vel(style.snareVelocityMin - 8, style.snareVelocityMax - 2);
        const auto& lane = getLaneStyleDefaults(styleDefaults, clap->type);

        const int windows = std::max(1, (bars + 1) / 2);
        std::vector<int> usedPerWindow(static_cast<size_t>(windows), 0);
        const int limitPerWindow = ghostSnareLimitPerTwoBars(style.substyle);

        auto tryAddGhost = [&](int step, bool asGhost, int velocity)
        {
            if (step < 0 || step >= bars * 16)
                return;
            const int window = std::clamp((step / 16) / 2, 0, windows - 1);
            if (usedPerWindow[static_cast<size_t>(window)] >= limitPerWindow)
                return;

            const bool collidesWithSnare = std::any_of(snare->notes.begin(), snare->notes.end(), [step](const NoteEvent& s)
            {
                return !s.isGhost && s.step == step;
            });
            if (collidesWithSnare)
                return;

            clap->notes.push_back({ 39, step, 1, std::clamp(velocity, 1, 127), 2, asGhost });
            usedPerWindow[static_cast<size_t>(window)] += 1;
        };

        for (int bar = 0; bar < bars; ++bar)
        {
            const auto role = bar < static_cast<int>(phrase.size()) ? phrase[static_cast<size_t>(bar)] : DrillPhraseRole::Base;
            const int b = bar * 16;

            // Pre-snare push and after-snare echo as support grammar.
            float preSnareGate = std::clamp(style.clapLayerChance * lane.noteProbability * 0.48f, 0.02f, 0.72f);
            float echoGate = std::clamp(style.clapLayerChance * lane.noteProbability * 0.34f, 0.02f, 0.62f);
            float phraseAnswerGate = std::clamp(style.clapLayerChance * lane.noteProbability * 0.4f, 0.02f, 0.68f);

            if (style.substyle == DrillSubstyle::BrooklynDrill)
            {
                preSnareGate += 0.08f;
                echoGate += 0.06f;
            }
            else if (style.substyle == DrillSubstyle::DarkDrill)
            {
                preSnareGate *= 0.7f;
                echoGate *= 0.55f;
                phraseAnswerGate *= 0.6f;
            }

            if (chance(rng) < preSnareGate)
                tryAddGhost(b + 3, true, std::max(style.snareVelocityMin, vel(rng) - 14));
            if (chance(rng) < preSnareGate)
                tryAddGhost(b + 11, true, std::max(style.snareVelocityMin, vel(rng) - 12));

            if (chance(rng) < echoGate)
                tryAddGhost(b + 5, true, std::max(style.snareVelocityMin, vel(rng) - 16));
            if (chance(rng) < echoGate)
                tryAddGhost(b + 13, true, std::max(style.snareVelocityMin, vel(rng) - 14));

            if (role == DrillPhraseRole::Ending && chance(rng) < phraseAnswerGate)
                tryAddGhost(b + 15,
                            style.substyle != DrillSubstyle::BrooklynDrill,
                            std::max(style.snareVelocityMin, vel(rng) - (style.substyle == DrillSubstyle::BrooklynDrill ? 8 : 14)));
        }

        dedupeAndSort(clap->notes);
    }

    if (auto* ghost = findTrack(project, TrackType::GhostKick);
        ghost != nullptr && kick != nullptr && mutableTracks.count(ghost->type) != 0 && !ghost->locked && ghost->enabled)
    {
        ghost->notes.clear();
        std::uniform_int_distribution<int> vel(style.kickVelocityMin - 24, style.kickVelocityMin - 10);
        const auto& lane = getLaneStyleDefaults(styleDefaults, ghost->type);
        const float ghostDensity = std::clamp((spec.ghostKickDensityMin + spec.ghostKickDensityMax) * 0.5f, 0.01f, 0.3f);
        for (const auto& k : kick->notes)
        {
            if ((k.step % 16) == 0 || (k.step % 16) == 10)
                continue;
            const auto* slot = blueprint != nullptr ? blueprint->slotAt(k.step) : nullptr;
            if (slot != nullptr && (slot->snareProtection || slot->majorEventReserved))
                continue;
            const bool nearSnare = snare != nullptr && std::any_of(snare->notes.begin(), snare->notes.end(), [&k](const NoteEvent& s)
            {
                return !s.isGhost && std::abs(s.step - k.step) <= 1;
            });
            if (nearSnare)
                continue;

            if (chance(rng) < std::clamp(style.ghostKickChance * lane.noteProbability * (ghostDensity / 0.06f), 0.01f, 0.16f))
                ghost->notes.push_back({ 35, std::max(0, k.step - 1), 1, std::clamp(vel(rng), 1, 127), 0, true });
        }
    }

    if (auto* ride = findTrack(project, TrackType::Ride); ride != nullptr && mutableTracks.count(ride->type) != 0)
        ride->notes.clear();

    if (auto* cym = findTrack(project, TrackType::Cymbal);
        cym != nullptr && mutableTracks.count(cym->type) != 0 && !cym->locked && cym->enabled)
    {
        cym->notes.clear();
        const auto& lane = getLaneStyleDefaults(styleDefaults, cym->type);
        std::uniform_int_distribution<int> vel(style.snareVelocityMin, style.snareVelocityMax);
        const float cymBias = spec.allowCymbalTransitions ? 1.2f : 0.45f;
        if (chance(rng) < std::clamp(0.34f * lane.phraseEndingProbability * cymBias, 0.02f, 0.72f))
            cym->notes.push_back({ 49, std::max(0, project.params.bars * 16 - 1), 2, vel(rng), 0, false });
    }

    if (auto* perc = findTrack(project, TrackType::Perc);
        perc != nullptr && mutableTracks.count(perc->type) != 0 && !perc->locked && perc->enabled)
    {
        perc->notes.clear();
        const auto& lane = getLaneStyleDefaults(styleDefaults, perc->type);
        std::uniform_int_distribution<int> vel(style.hatVelocityMin, style.hatVelocityMax);
        float percGate = std::clamp(style.percChance * lane.noteProbability * laneActivityWeight(project, TrackType::Perc), 0.01f, 0.3f);
        if (style.substyle == DrillSubstyle::DarkDrill)
            percGate *= 0.45f;
        for (int bar = 0; bar < bars; ++bar)
        {
            for (int step : { 5, 11, 15 })
                if (chance(rng) < percGate)
                    perc->notes.push_back({ 50, bar * 16 + step, 1, vel(rng), 0, false });
        }
    }
}

void DrillEngine::applyDrillHumanization(PatternProject& project,
                                         const DrillStyleProfile& style,
                                         std::mt19937& rng,
                                         const std::unordered_set<TrackType>& mutableTracks) const
{
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    std::uniform_int_distribution<int> velJitter(-6, 6);
    const auto& styleDefaults = getGenreStyleDefaults(GenreType::Drill, project.params.drillSubstyle);
    const int bars = std::max(1, project.params.bars);
    const bool cleanHatTiming = style.cleanHatMode;
    const bool strictHatSnap = cleanHatTiming
        || style.substyle == DrillSubstyle::UKDrill
        || style.substyle == DrillSubstyle::BrooklynDrill;
    const int hatSnapGrid = HiResTiming::kTicks1_24 / 4; // 1/6 step (40 ticks at PPQ 960)

    for (auto& track : project.tracks)
    {
        if (mutableTracks.count(track.type) == 0)
            continue;

        const auto& lane = getLaneStyleDefaults(styleDefaults, track.type);
        const bool isHatLane = track.type == TrackType::HiHat || track.type == TrackType::HatFX;
        for (auto& note : track.notes)
        {
            if ((note.step % 2) == 1
                && track.type != TrackType::Kick
                && track.type != TrackType::Sub808
                && track.type != TrackType::Snare
                && track.type != TrackType::HiHat
                && track.type != TrackType::HatFX)
                note.microOffset += static_cast<int>(juce::jmap(project.params.swingPercent, 50.0f, 58.0f, 0.0f, 10.0f));

            const int jitterMax = std::max(1, static_cast<int>(5.0f * lane.humanizeBias));
            std::uniform_int_distribution<int> timing(-jitterMax, jitterMax + 1);
            if (!isAnchorStep(track.type, note.step % 16))
            {
                if (track.type == TrackType::HatFX)
                    note.microOffset += timing(rng) / 3;
                else if (track.type == TrackType::HiHat)
                    note.microOffset += cleanHatTiming ? 0 : timing(rng) / 2;
                else
                    note.microOffset += timing(rng);
            }

            note.microOffset = std::clamp(static_cast<int>(note.microOffset * lane.timingBias), -120, 120);
            if (isHatLane && strictHatSnap)
                snapNoteToMicroGrid(note, bars, hatSnapGrid);

            if (chance(rng) < 0.86f)
                note.velocity = std::clamp(note.velocity + velJitter(rng), 1, 127);
        }
    }

    applySampleAwareDrillFlavor(project, mutableTracks);
}

void DrillEngine::validatePattern(PatternProject& project, const std::unordered_set<TrackType>& mutableTracks) const
{
    const int bars = std::max(1, project.params.bars);

    for (auto& track : project.tracks)
    {
        if (mutableTracks.count(track.type) == 0)
            continue;

        dedupeAndSort(track.notes);
        for (auto& note : track.notes)
        {
            note.step = std::clamp(note.step, 0, bars * 16 - 1);
            note.length = std::max(1, note.length);
            note.velocity = std::clamp(note.velocity, 1, 127);
            note.microOffset = std::clamp(note.microOffset, -120, 120);
        }

        int maxHits = bars * 10;
        if (track.type == TrackType::HiHat)
            maxHits = bars * 42;
        else if (track.type == TrackType::HatFX)
            maxHits = bars * 20;
        else if (track.type == TrackType::Sub808)
            maxHits = bars * 10;
        else if (track.type == TrackType::OpenHat)
            maxHits = bars * 4;

        if (static_cast<int>(track.notes.size()) > maxHits)
            track.notes.resize(static_cast<size_t>(maxHits));
    }

    auto* kick = findTrack(project, TrackType::Kick);
    const auto* snare = findTrack(project, TrackType::Snare);
    const auto* clap = findTrack(project, TrackType::ClapGhostSnare);
    if (kick != nullptr && mutableTracks.count(kick->type) != 0)
    {
        kick->notes.erase(std::remove_if(kick->notes.begin(), kick->notes.end(), [snare, clap](const NoteEvent& k)
        {
            const bool onSnare = snare != nullptr && std::any_of(snare->notes.begin(), snare->notes.end(), [&k](const NoteEvent& s)
            {
                return !s.isGhost && s.step == k.step;
            });

            const bool onClap = clap != nullptr && std::any_of(clap->notes.begin(), clap->notes.end(), [&k](const NoteEvent& c)
            {
                return !c.isGhost && c.step == k.step;
            });

            return onSnare || onClap;
        }), kick->notes.end());
    }
}
} // namespace bbg
