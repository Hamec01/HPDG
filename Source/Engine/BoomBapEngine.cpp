#include "BoomBapEngine.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../Core/TrackRegistry.h"
#include "HiResTiming.h"
#include "PatternPerformanceTransformEngine.h"
#include "StyleInfluence.h"
#include "StyleDefaults.h"

namespace bbg
{
namespace
{
TrackState* findTrack(PatternProject& project, TrackType type)
{
    auto it = std::find_if(project.tracks.begin(), project.tracks.end(), [type](const TrackState& t) { return t.type == type; });
    return it != project.tracks.end() ? &(*it) : nullptr;
}

// Algebra notes carry their micro-timing inside gridTick. A note pushed slightly early
// (up to a 1/64) still belongs to its grid step; late pocket and swing stay on theirs.
int stepIndexOf(const NoteEvent& note) { return (note.gridTick + HiResTiming::kTicks1_16 / 4) / HiResTiming::kTicks1_16; }
int tickForStep(int step) { return step * HiResTiming::kTicks1_16; }

bool containsStep(const std::vector<NoteEvent>& notes, int step)
{
    return std::any_of(notes.begin(), notes.end(), [step](const NoteEvent& n) { return stepIndexOf(n) == step; });
}

void applyBoomBapStyleInfluence(PatternProject& project)
{
    juce::String applyError;
    BoomBapStyleInfluence::apply(project, &applyError);
    juce::ignoreUnused(applyError);
}

juce::String roleForTrack(TrackType type)
{
    switch (type)
    {
        case TrackType::Kick: return "foundation";
        case TrackType::Snare: return "backbeat";
        case TrackType::HiHat: return "carrier";
        case TrackType::OpenHat: return "phrase_air";
        case TrackType::GhostKick: return "support_ghost";
        case TrackType::ClapGhostSnare: return "backbeat_layer";
        case TrackType::Perc: return "punctuation";
        case TrackType::Ride: return "carrier_support";
        case TrackType::Cymbal: return "ending_mark";
        default: return "lane";
    }
}

bool isAnchorStepForTrack(TrackType type, int stepInBar)
{
    if (type == TrackType::Snare || type == TrackType::ClapGhostSnare)
        return stepInBar == 4 || stepInBar == 12;

    if (type == TrackType::Kick || type == TrackType::GhostKick)
        return stepInBar == 0 || stepInBar == 8;

    if (type == TrackType::HiHat)
        return (stepInBar % 4) == 0;

    return false;
}

bool isBackbeatStep(int stepInBar)
{
    return stepInBar == 4 || stepInBar == 12;
}

void dedupeAndSortNotes(std::vector<NoteEvent>& notes)
{
    std::sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b)
    {
        if (stepIndexOf(a) != stepIndexOf(b))
            return stepIndexOf(a) < stepIndexOf(b);

        if (a.isGhost != b.isGhost)
            return !a.isGhost;

        return a.velocity > b.velocity;
    });

    notes.erase(std::unique(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b)
    {
        return stepIndexOf(a) == stepIndexOf(b) && a.pitch == b.pitch;
    }), notes.end());
}

std::vector<NoteEvent> mergeVariationNotes(TrackType type,
                                           const std::vector<NoteEvent>& previous,
                                           const std::vector<NoteEvent>& fresh,
                                           float rgVariationIntensity,
                                           std::mt19937& rng)
{
    if (previous.empty())
        return fresh;

    std::vector<NoteEvent> out;
    out.reserve(previous.size() + fresh.size());

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);

    for (const auto& n : previous)
    {
        const int barStep = stepIndexOf(n) % 16;
        const bool anchor = isAnchorStepForTrack(type, barStep) || (type == TrackType::Kick && n.velocity >= 108);
        const float keepChance = anchor ? 1.0f : std::clamp((type == TrackType::HiHat ? 0.58f : 0.50f) * rgVariationIntensity, 0.2f, 0.95f);
        if (chance(rng) <= keepChance)
            out.push_back(n);
    }

    for (const auto& n : fresh)
    {
        const int barStep = stepIndexOf(n) % 16;
        const bool anchor = isAnchorStepForTrack(type, barStep);

        if (anchor && containsStep(out, stepIndexOf(n)))
            continue;

        if (!anchor)
        {
            const float baseChance = type == TrackType::GhostKick || type == TrackType::Perc ? 0.38f : 0.58f;
            const float addChance = std::clamp(baseChance * rgVariationIntensity, 0.12f, 0.95f);
            if (chance(rng) > addChance)
                continue;
        }

        out.push_back(n);
    }

    dedupeAndSortNotes(out);
    return out;
}

int algebraLaneForTrack(TrackType type)
{
    switch (type)
    {
        case TrackType::HiHat: return BoomBapClassicLanes::HiHat;
        case TrackType::HatFX: return BoomBapClassicLanes::HatAccent;
        case TrackType::OpenHat: return BoomBapClassicLanes::OpenHat;
        case TrackType::Snare: return BoomBapClassicLanes::Snare;
        case TrackType::ClapGhostSnare: return BoomBapClassicLanes::ClapGhost;
        case TrackType::Kick: return BoomBapClassicLanes::Kick;
        case TrackType::GhostKick: return BoomBapClassicLanes::KickGhost;
        case TrackType::Ride: return BoomBapClassicLanes::Ride;
        case TrackType::Cymbal: return BoomBapClassicLanes::Cymbal;
        case TrackType::Perc: return BoomBapClassicLanes::Perc;
        case TrackType::Sub808: return BoomBapClassicLanes::Sub808;
        default: return -1;
    }
}

// Lanes the algebra never writes in BoomBap: Ghost Snare/Clap and Perc are retired, and
// the BoomBap profiles have no Sub808 reinforcement. Per-lane actions leave them as-is.
bool isAlgebraRetiredTrack(TrackType type)
{
    return type == TrackType::ClapGhostSnare || type == TrackType::Perc || type == TrackType::Sub808;
}

// Lanes that belong to one musical gesture: ghost kicks answer main kicks, and hat accents
// sit between the carrier hats. They are regenerated together so they never disagree.
std::vector<TrackType> coupledTracksFor(TrackType type)
{
    switch (type)
    {
        case TrackType::Kick: return { TrackType::GhostKick };
        case TrackType::GhostKick: return { TrackType::Kick };
        case TrackType::HiHat: return { TrackType::HatFX };
        case TrackType::HatFX: return { TrackType::HiHat };
        default: return {};
    }
}

std::unordered_set<TrackType> editableLaneGroup(PatternProject& project, TrackType trackType)
{
    std::unordered_set<TrackType> tracks { trackType };
    for (const auto coupledType : coupledTracksFor(trackType))
    {
        const auto* coupled = findTrack(project, coupledType);
        if (coupled != nullptr && !coupled->locked && coupled->enabled)
            tracks.insert(coupledType);
    }
    return tracks;
}

BoomBapClassicAlgebraParams makeAlgebraParams(const PatternProject& project, const BoomBapStyleProfile& style, int seed)
{
    BoomBapClassicAlgebraParams params;
    params.seed = seed;
    params.bars = std::clamp(project.params.bars, 1, 16);
    params.bpm = project.params.bpm;
    params.density = std::clamp(project.params.densityAmount, 0.0f, 1.0f);
    params.swing = std::clamp(project.params.swingPercent / 100.0f, 0.50f, 0.75f);
    params.humanize = std::clamp(project.params.humanizeAmount * (0.65f + project.params.timingAmount * 0.55f), 0.0f, 1.0f);
    params.variation = std::clamp(style.barVariationAmount + project.params.densityAmount * 0.24f, 0.0f, 1.0f);
    params.candidateCount = 64;
    params.substyle = style.substyle;
    return params;
}

int pitchForTrack(TrackType type)
{
    if (const auto* info = TrackRegistry::find(type); info != nullptr)
        return info->defaultMidiNote;

    switch (type)
    {
        case TrackType::Kick: return 36;
        case TrackType::Snare: return 38;
        case TrackType::OpenHat: return 46;
        case TrackType::Cymbal: return 49;
        case TrackType::Ride: return 51;
        case TrackType::Perc: return 39;
        default: return 42;
    }
}

void writeAlgebraLane(TrackState& track, const BoomBapClassicAlgebraPattern& pattern, int bars)
{
    track.notes.clear();
    track.sub808Notes.clear();

    const int lane = algebraLaneForTrack(track.type);
    if (lane < 0)
        return;

    const int pitch = pitchForTrack(track.type);
    for (const auto& algebraNote : pattern.notesByLane[static_cast<size_t>(lane)])
    {
        // Clamp to this note's own bar start: a swung/humanized downbeat with a small
        // negative micro-offset must never cross into the previous bar's tick range, or
        // every bar-boundary tick/16 classification downstream (density checks, MIDI
        // export bar bounds, etc.) silently misattributes it to the wrong bar.
        const int barStartPpq = algebraNote.barIndex * HiResTiming::kTicksPerBar4_4;
        const int ppqTick = std::max(barStartPpq,
                                     algebraNote.tick64 * HiResTiming::kTicks1_64 + algebraNote.microTimingTicks);
        const int lengthSteps = std::max(1, static_cast<int>(std::ceil(algebraNote.length / 4.0f)));
        const bool isGhost = algebraNote.role == BoomBapClassicRole::Ghost
            || lane == BoomBapClassicLanes::ClapGhost
            || lane == BoomBapClassicLanes::KickGhost;
        HiResTiming::addNoteAtTick(track, pitch, ppqTick, algebraNote.velocity, isGhost, bars, lengthSteps);
        if (!track.notes.empty())
            track.notes.back().semanticRole = algebraNote.roleString;
    }

    dedupeAndSortNotes(track.notes);
}

void markFreshLane(TrackState& track, const BoomBapStyleProfile& style)
{
    track.templateId = static_cast<int>(style.substyle) * 100 + static_cast<int>(track.type) * 7;
    track.variationId = 0;
    track.mutationDepth = 0.0f;
    track.subProfile = style.name;
    track.laneRole = roleForTrack(track.type);
}
} // namespace

BoomBapEngine::BoomBapEngine() = default;

void BoomBapEngine::generateWithAlgebra(PatternProject& project, const BoomBapStyleProfile& style) const
{
    const auto algebraParams = makeAlgebraParams(project, style, project.params.seed);
    const auto pattern = BoomBapClassicAlgebraGenerator().generate(algebraParams);

    std::unordered_set<TrackType> mutableTracks;
    for (auto& track : project.tracks)
    {
        if (track.locked)
            continue;

        if (track.type == TrackType::ClapGhostSnare || track.type == TrackType::Perc)
        {
            track.notes.clear();
            track.sub808Notes.clear();
            track.enabled = false;
            track.muted = true;
            continue;
        }

        markFreshLane(track, style);
        mutableTracks.insert(track.type);

        if (track.enabled)
            writeAlgebraLane(track, pattern, algebraParams.bars);
        else
        {
            track.notes.clear();
            track.sub808Notes.clear();
        }
    }

    project.phraseLengthBars = algebraParams.bars;
    project.phraseRoleSummary = "statement | confirmation | development | turnaround";
    project.generationDebugReport = "ALGEBRA\n" + pattern.debugSummary;
    PatternPerformanceTransformEngine::captureBasePatterns(project, mutableTracks);
}

void BoomBapEngine::generate(PatternProject& project)
{
    applyBoomBapStyleInfluence(project);
    generateWithAlgebra(project, getBoomBapProfile(project.params.boombapSubstyle));
}

void BoomBapEngine::regenerateTrack(PatternProject& project, TrackType trackType)
{
    applyBoomBapStyleInfluence(project);
    regenerateTrackVariation(project, trackType);
}

void BoomBapEngine::generateTrackNew(PatternProject& project, TrackType trackType)
{
    applyBoomBapStyleInfluence(project);
    auto* track = findTrack(project, trackType);
    if (track == nullptr || track->locked || algebraLaneForTrack(trackType) < 0 || isAlgebraRetiredTrack(trackType))
        return;

    const auto& style = getBoomBapProfile(project.params.boombapSubstyle);
    const auto mutableTracks = editableLaneGroup(project, trackType);
    const auto algebraParams = makeAlgebraParams(project,
                                                 style,
                                                 project.params.seed + static_cast<int>(trackType) * 131 + project.generationCounter * 17);
    const auto pattern = BoomBapClassicAlgebraGenerator().generate(algebraParams);

    for (const auto type : mutableTracks)
    {
        auto* lane = findTrack(project, type);
        if (lane == nullptr)
            continue;

        markFreshLane(*lane, style);
        if (lane->enabled)
            writeAlgebraLane(*lane, pattern, algebraParams.bars);
        else
            lane->notes.clear();
    }

    PatternPerformanceTransformEngine::captureBasePatterns(project, mutableTracks);
}

void BoomBapEngine::regenerateTrackVariation(PatternProject& project, TrackType trackType)
{
    auto* target = findTrack(project, trackType);
    if (target == nullptr || target->locked || algebraLaneForTrack(trackType) < 0 || isAlgebraRetiredTrack(trackType))
        return;

    const auto mutableTracks = editableLaneGroup(project, trackType);
    std::vector<std::pair<TrackType, std::vector<NoteEvent>>> previous;
    for (const auto type : mutableTracks)
        if (const auto* lane = findTrack(project, type); lane != nullptr)
            previous.emplace_back(type, lane->notes);

    generateTrackNew(project, trackType);

    const auto& style = getBoomBapProfile(project.params.boombapSubstyle);
    const auto& styleDefaults = getGenreStyleDefaults(GenreType::BoomBap, project.params.boombapSubstyle);
    std::mt19937 rng(static_cast<std::mt19937::result_type>(project.params.seed + static_cast<int>(trackType) * 199 + project.generationCounter * 29));

    for (const auto& [type, oldNotes] : previous)
    {
        auto* lane = findTrack(project, type);
        if (lane == nullptr)
            continue;

        const auto& laneDefaults = getLaneStyleDefaults(styleDefaults, type);
        lane->notes = mergeVariationNotes(type, oldNotes, lane->notes, laneDefaults.rgVariationIntensity, rng);
        lane->variationId += 1;
        lane->mutationDepth = std::clamp(lane->mutationDepth + 0.08f, 0.0f, 1.0f);
        lane->laneRole = roleForTrack(type);
        lane->subProfile = style.name;
    }

    PatternPerformanceTransformEngine::captureBasePatterns(project, mutableTracks);
}

void BoomBapEngine::mutatePattern(PatternProject& project)
{
    applyBoomBapStyleInfluence(project);
    const auto& style = getBoomBapProfile(project.params.boombapSubstyle);
    std::mt19937 rng(static_cast<std::mt19937::result_type>(project.params.seed + project.mutationCounter * 911 + 17));

    std::vector<TrackType> candidates;
    for (const auto& track : project.tracks)
    {
        if (!track.locked && track.enabled)
            candidates.push_back(track.type);
    }

    if (candidates.empty())
        return;

    std::shuffle(candidates.begin(), candidates.end(), rng);
    const int mutateCount = style.substyle == BoomBapSubstyle::LofiRap
        ? std::max(1, static_cast<int>(candidates.size() / 4))
        : std::max(1, static_cast<int>(candidates.size() / 3));

    for (int i = 0; i < mutateCount && i < static_cast<int>(candidates.size()); ++i)
        mutateTrack(project, candidates[static_cast<size_t>(i)]);

    project.mutationCounter += 1;
    project.phraseLengthBars = std::max(1, project.params.bars);
}

void BoomBapEngine::mutateTrack(PatternProject& project, TrackType trackType)
{
    applyBoomBapStyleInfluence(project);
    auto* track = findTrack(project, trackType);
    if (track == nullptr || track->locked || !track->enabled)
        return;

    if (track->notes.empty())
    {
        generateTrackNew(project, trackType);
        return;
    }

    const auto& style = getBoomBapProfile(project.params.boombapSubstyle);
    const auto& styleDefaults = getGenreStyleDefaults(GenreType::BoomBap, project.params.boombapSubstyle);
    const auto& laneDefaults = getLaneStyleDefaults(styleDefaults, trackType);
    const int mutationSeed = project.params.seed + project.mutationCounter * 101 + static_cast<int>(trackType) * 43;
    std::mt19937 rng(static_cast<std::mt19937::result_type>(mutationSeed));
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);

    const bool skeletonLane = trackType == TrackType::Kick || trackType == TrackType::Snare;
    float mutationIntensity = skeletonLane ? 0.86f : 1.0f;
    if (style.substyle == BoomBapSubstyle::RussianUnderground)
        mutationIntensity *= skeletonLane ? 0.74f : 0.82f;
    else if (style.substyle == BoomBapSubstyle::BoomBapGold)
        mutationIntensity *= skeletonLane ? 0.92f : 1.14f;
    else if (style.substyle == BoomBapSubstyle::LofiRap)
        mutationIntensity *= skeletonLane ? 0.58f : 0.64f;
    mutationIntensity *= laneDefaults.mutationIntensity;

    const auto isAnchor = [trackType](const NoteEvent& n)
    {
        return isAnchorStepForTrack(trackType, stepIndexOf(n) % 16);
    };

    if (chance(rng) < (0.6f * mutationIntensity))
    {
        std::vector<size_t> removable;
        for (size_t i = 0; i < track->notes.size(); ++i)
            if (!isAnchor(track->notes[i]))
                removable.push_back(i);

        if (!removable.empty())
        {
            std::uniform_int_distribution<size_t> pick(0, removable.size() - 1);
            track->notes.erase(track->notes.begin() + static_cast<long long>(removable[pick(rng)]));
        }
    }

    if (chance(rng) < (0.7f * mutationIntensity) && !track->notes.empty())
    {
        std::uniform_int_distribution<size_t> pick(0, track->notes.size() - 1);
        auto& note = track->notes[pick(rng)];
        std::uniform_int_distribution<int> vel(-12, 12);
        std::uniform_int_distribution<int> micro(-10, 10);
        note.velocity = std::clamp(note.velocity + vel(rng), 1, 127);
        note.timingOffsetTicks = std::clamp(note.timingOffsetTicks + micro(rng), -120, 120);
    }

    if (chance(rng) < (0.5f * mutationIntensity))
    {
        std::vector<size_t> movable;
        for (size_t i = 0; i < track->notes.size(); ++i)
            if (!isAnchor(track->notes[i]))
                movable.push_back(i);

        if (!movable.empty())
        {
            std::uniform_int_distribution<size_t> pick(0, movable.size() - 1);
            auto& n = track->notes[movable[pick(rng)]];
            std::uniform_int_distribution<int> shift(-2, 2);
            const int nStep = stepIndexOf(n);
            const int newStep = std::clamp(nStep + shift(rng), 0, std::max(0, project.params.bars * 16 - 1));
            const bool kickLane = trackType == TrackType::Kick || trackType == TrackType::GhostKick;
            // Kicks never move onto the backbeat; the note keeps its own pocket offset.
            if (!(kickLane && isBackbeatStep(newStep % 16)))
                n.gridTick = std::max(0, tickForStep(newStep) + (n.gridTick - tickForStep(nStep)));
        }
    }

    if (chance(rng) < (0.55f * mutationIntensity) && !isAlgebraRetiredTrack(trackType) && algebraLaneForTrack(trackType) >= 0)
    {
        auto algebraParams = makeAlgebraParams(project, style, mutationSeed + 7);
        algebraParams.candidateCount = 24;
        const auto pattern = BoomBapClassicAlgebraGenerator().generate(algebraParams);
        TrackState candidate = *track;
        writeAlgebraLane(candidate, pattern, algebraParams.bars);

        for (const auto& note : candidate.notes)
        {
            if (!containsStep(track->notes, stepIndexOf(note)) && !isAnchor(note))
            {
                track->notes.push_back(note);
                break;
            }
        }
    }

    dedupeAndSortNotes(track->notes);
    track->mutationDepth = std::clamp(track->mutationDepth + 0.12f, 0.0f, 1.0f);
    track->variationId += 1;
    project.mutationCounter += 1;

    PatternPerformanceTransformEngine::captureBasePatterns(project, { trackType });
}
} // namespace bbg
