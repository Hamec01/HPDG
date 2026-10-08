#include "TrapEngine.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include "../Core/TrackRegistry.h"
#include "../Core/Sub808Types.h"
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
    for (auto& t : project.tracks)
        if (t.type == type)
            return &t;
    return nullptr;
}

int stepIndexOf(const NoteEvent& note) { return note.gridTick / HiResTiming::kTicks1_16; }
int tickForStep(int step) { return step * HiResTiming::kTicks1_16; }
// Algebra notes carry their micro-timing inside gridTick, so a note played slightly early
// would floor into the previous step. Collision checks tolerate an early push of up to a
// 1/64 (late pocket and swing already stay on their own step).
int nearestStepOf(const NoteEvent& note) { return (note.gridTick + HiResTiming::kTicks1_16 / 4) / HiResTiming::kTicks1_16; }

juce::String roleForTrack(TrackType type)
{
    switch (type)
    {
        case TrackType::Kick: return "trap_kick";
        case TrackType::Sub808: return "trap_sub";
        case TrackType::HiHat: return "trap_hat";
        case TrackType::HatFX: return "trap_hat_fx";
        case TrackType::Snare: return "trap_backbeat";
        case TrackType::ClapGhostSnare: return "trap_layer";
        case TrackType::OpenHat: return "trap_open";
        case TrackType::GhostKick: return "trap_support";
        case TrackType::Cymbal: return "trap_marker";
        case TrackType::Perc: return "trap_texture";
        case TrackType::Ride: return "trap_ride";
        default: return "trap_lane";
    }
}

void dedupeAndSort(std::vector<NoteEvent>& notes)
{
    std::sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b)
    {
        if (a.gridTick != b.gridTick)
            return a.gridTick < b.gridTick;
        if (a.timingOffsetTicks != b.timingOffsetTicks)
            return a.timingOffsetTicks < b.timingOffsetTicks;
        if (a.pitch != b.pitch)
            return a.pitch < b.pitch;
        return a.velocity > b.velocity;
    });

    notes.erase(std::unique(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b)
    {
        return a.gridTick == b.gridTick && a.timingOffsetTicks == b.timingOffsetTicks && a.pitch == b.pitch;
    }), notes.end());
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

void applyTrapStyleInfluence(PatternProject& project)
{
    juce::String applyError;
    TrapStyleInfluence::apply(project, &applyError);
    juce::ignoreUnused(applyError);
}

void clampLowEndStartsPerBar(TrackState& sub, int bars, int maxStartsPerBar)
{
    for (int bar = 0; bar < bars; ++bar)
    {
        std::vector<size_t> indices;
        for (size_t i = 0; i < sub.notes.size(); ++i)
            if ((stepIndexOf(sub.notes[i]) / 16) == bar)
                indices.push_back(i);

        if (static_cast<int>(indices.size()) <= maxStartsPerBar)
            continue;

        std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b)
        {
            const int scoreA = sub.notes[a].velocity + (stepIndexOf(sub.notes[a]) % 16 >= 12 ? 6 : 0);
            const int scoreB = sub.notes[b].velocity + (stepIndexOf(sub.notes[b]) % 16 >= 12 ? 6 : 0);
            return scoreA > scoreB;
        });

        for (size_t i = static_cast<size_t>(maxStartsPerBar); i < indices.size(); ++i)
            sub.notes[indices[i]].gridTick = -1;
    }

    sub.notes.erase(std::remove_if(sub.notes.begin(), sub.notes.end(), [](const NoteEvent& n)
    {
        return n.gridTick < 0;
    }), sub.notes.end());
}

void clampLowEndPitchChanges(TrackState& sub, int bars, int maxPitchChangesPerTwoBars)
{
    for (int window = 0; window < std::max(1, (bars + 1) / 2); ++window)
    {
        const int startTick = tickForStep(window * 32);
        const int endTick = tickForStep(std::min(bars * 16, window * 32 + 32));
        int changes = 0;
        int prevPitch = -1;

        for (auto& note : sub.notes)
        {
            if (note.gridTick < startTick || note.gridTick >= endTick)
                continue;

            if (prevPitch > 0 && note.pitch != prevPitch)
            {
                if (changes >= maxPitchChangesPerTwoBars)
                    note.pitch = prevPitch;
                else
                    ++changes;
            }
            prevPitch = note.pitch;
        }
    }
}

void ensureLowEndPhraseEnding(TrackState& sub, int bars)
{
    if (sub.notes.empty())
        return;

    const int finalBarStart = (bars - 1) * 16;
    const int finalBarStartTick = tickForStep(finalBarStart);
    const bool hasEnding = std::any_of(sub.notes.begin(), sub.notes.end(), [finalBarStartTick](const NoteEvent& n)
    {
        return n.gridTick >= finalBarStartTick + 12 * HiResTiming::kTicks1_16;
    });

    if (!hasEnding)
    {
        const int velocity = std::clamp(sub.notes.back().velocity, 84, 120);
        NoteEvent note;
        note.pitch = std::clamp(sub.notes.back().pitch, 24, 60);
        note.gridTick = tickForStep(finalBarStart + 14);
        note.lengthTicks = 2 * HiResTiming::kTicks1_16;
        note.velocity = velocity;
        sub.notes.push_back(note);
    }
}

bool hasStrongNoteAtStep(const TrackState* track, int step)
{
    if (track == nullptr)
        return false;

    return std::any_of(track->notes.begin(), track->notes.end(), [step](const NoteEvent& n)
    {
        return !n.isGhost && nearestStepOf(n) == step;
    });
}

void moveSubOffBackbeat(TrackState& sub,
                        const TrackState* snare,
                        const TrackState* clap,
                        int bars)
{
    for (auto& note : sub.notes)
    {
        const int step = nearestStepOf(note);
        const bool backbeatCollision = hasStrongNoteAtStep(snare, step) || hasStrongNoteAtStep(clap, step);
        if (!backbeatCollision)
            continue;

        const int backStep = std::max(0, step - 1);
        const int fwdStep = std::min(bars * 16 - 1, step + 1);
        const bool backFree = !hasStrongNoteAtStep(snare, backStep) && !hasStrongNoteAtStep(clap, backStep);
        const bool fwdFree = !hasStrongNoteAtStep(snare, fwdStep) && !hasStrongNoteAtStep(clap, fwdStep);

        if (backFree)
            note.gridTick = tickForStep(backStep);
        else if (fwdFree)
            note.gridTick = tickForStep(fwdStep);
        else
            note.velocity = std::max(1, note.velocity - 12);
    }
}

void pruneHatFxOverLowEndAnchors(TrackState& hatFx,
                                 const TrackState* kick,
                                 const TrackState* sub)
{
    if (kick == nullptr || sub == nullptr)
        return;

    hatFx.notes.erase(std::remove_if(hatFx.notes.begin(), hatFx.notes.end(), [&](const NoteEvent& n)
    {
        const int step = nearestStepOf(n);
        const int stepInBar = ((step % 16) + 16) % 16;
        if (!(stepInBar == 0 || stepInBar == 8 || stepInBar == 10 || stepInBar >= 14))
            return false;

        const bool kickHere = std::any_of(kick->notes.begin(), kick->notes.end(), [&](const NoteEvent& k)
        {
            return !k.isGhost && nearestStepOf(k) == step;
        });
        const bool subHere = std::any_of(sub->notes.begin(), sub->notes.end(), [&](const NoteEvent& s)
        {
            return !s.isGhost && nearestStepOf(s) == step;
        });

        return kickHere && subHere && n.velocity >= 86;
    }), hatFx.notes.end());
}

std::optional<TrackType> trackTypeForTrapAlgebraLane(int lane)
{
    switch (lane)
    {
        case TrapAlgebraLanes::HiHat: return TrackType::HiHat;
        case TrapAlgebraLanes::HatAccent: return TrackType::HatFX;
        case TrapAlgebraLanes::OpenHat: return TrackType::OpenHat;
        case TrapAlgebraLanes::Snare: return TrackType::Snare;
        case TrapAlgebraLanes::ClapGhost: return TrackType::ClapGhostSnare;
        case TrapAlgebraLanes::Kick: return TrackType::Kick;
        case TrapAlgebraLanes::KickGhost: return TrackType::GhostKick;
        case TrapAlgebraLanes::Ride: return TrackType::Ride;
        case TrapAlgebraLanes::Cymbal: return TrackType::Cymbal;
        case TrapAlgebraLanes::Perc: return TrackType::Perc;
        case TrapAlgebraLanes::Sub808: return TrackType::Sub808;
        default: return std::nullopt;
    }
}

int pitchForTrapAlgebraLane(int lane)
{
    switch (lane)
    {
        case TrapAlgebraLanes::HiHat: return 42;
        case TrapAlgebraLanes::HatAccent: return 44;
        case TrapAlgebraLanes::OpenHat: return 46;
        case TrapAlgebraLanes::Snare: return 38;
        case TrapAlgebraLanes::ClapGhost: return 39;
        case TrapAlgebraLanes::Kick: return 36;
        case TrapAlgebraLanes::KickGhost: return 35;
        case TrapAlgebraLanes::Ride: return 51;
        case TrapAlgebraLanes::Cymbal: return 49;
        case TrapAlgebraLanes::Perc: return 50;
        case TrapAlgebraLanes::Sub808: return 36;
        default: return 36;
    }
}

int phrase808PitchForTrapAlgebraNote(const TrapAlgebraNote& note, const GeneratorParams& params)
{
    if (note.laneIndex != TrapAlgebraLanes::Sub808)
        return pitchForTrapAlgebraLane(note.laneIndex);

    const int root = ((params.keyRoot % 12) + 12) % 12;
    int base = 24 + root;
    if (base < 28)
        base += 12;
    while (base > 40)
        base -= 12;

    const bool major = params.scaleMode == 1;
    const int third = major ? 4 : 3;
    const int local = note.tick64 % 64;
    const int bar = note.barIndex % 4;

    int interval = 0;
    if (note.role == TrapAlgebraRole::BassAnswer)
        interval = ((note.tick64 / 8 + params.seed) % 3 == 0) ? 12 : 7;
    else if (note.role == TrapAlgebraRole::BassPickup)
        interval = 7;
    else if (note.role == TrapAlgebraRole::BassAnchor)
        interval = 0;
    else if (bar == 1 && (local == 24 || local == 36))
        interval = 7;
    else if (bar == 2 && local >= 32)
        interval = 7;
    else if (bar == 3 && local >= 56)
        interval = 12;
    else if (params.trapSubstyle == 5 && local == 40)
        interval = third;

    int pitch = base + interval;
    while (pitch > 48)
        pitch -= 12;
    while (pitch < 28)
        pitch += 12;
    return std::clamp(pitch, 28, 48);
}

TrapAlgebraSubstyle trapAlgebraSubstyleForProject(int trapSubstyle)
{
    switch (trapSubstyle)
    {
        case 1: return TrapAlgebraSubstyle::DarkTrap;
        case 2: return TrapAlgebraSubstyle::CloudTrap;
        case 3: return TrapAlgebraSubstyle::RageTrap;
        case 4: return TrapAlgebraSubstyle::MemphisTrap;
        case 5: return TrapAlgebraSubstyle::LuxuryTrap;
        case 0:
        default: return TrapAlgebraSubstyle::ATLClassic;
    }
}

juce::String semanticRoleForTrapAlgebraNote(const TrapAlgebraNote& note)
{
    return juce::String("trap_algebra_") + note.roleString;
}
}

TrapEngine::TrapEngine() = default;

void TrapEngine::generate(PatternProject& project)
{
    applyTrapStyleInfluence(project);
    const auto& style = getTrapProfile(project.params.trapSubstyle);

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

    TrapAlgebraParams algebraParams;
    algebraParams.seed = project.params.seed + 601; // seed + settings decide a Generate (RULE 13)
    algebraParams.bars = std::max(1, project.params.bars);
    algebraParams.bpm = project.params.bpm;
    algebraParams.density = project.params.densityAmount;
    algebraParams.swing = std::clamp(project.params.swingPercent / 100.0f, 0.50f, 0.60f);
    algebraParams.humanize = project.params.humanizeAmount;
    algebraParams.variation = std::max(project.params.velocityAmount, project.params.timingAmount);
    algebraParams.temperature = 0.40f;
    algebraParams.qMin = 0.62f;
    algebraParams.candidateCount = 32;
    algebraParams.substyle = trapAlgebraSubstyleForProject(project.params.trapSubstyle);

    const auto algebraPattern = TrapAlgebraEngine().generate(algebraParams);
    applyTrapAlgebraPattern(project, algebraPattern, mutableTracks);

    juce::ignoreUnused(style);
    validatePattern(project, mutableTracks);
    PatternPerformanceTransformEngine::captureBasePatterns(project, mutableTracks);
}

void TrapEngine::regenerateTrack(PatternProject& project, TrackType trackType)
{
    applyTrapStyleInfluence(project);
    regenerateTrackVariation(project, trackType);
}

void TrapEngine::generateTrackNew(PatternProject& project, TrackType trackType)
{
    applyTrapStyleInfluence(project);
    auto* track = findTrack(project, trackType);
    if (track == nullptr || track->locked)
        return;

    const auto& style = getTrapProfile(project.params.trapSubstyle);

    std::unordered_set<TrackType> mutableTracks { trackType };
    expandCoupledTracks(trackType, mutableTracks);
    for (auto it = mutableTracks.begin(); it != mutableTracks.end();)
    {
        const auto* coupled = findTrack(project, *it);
        if (*it != trackType && (coupled == nullptr || coupled->locked || !coupled->enabled))
            it = mutableTracks.erase(it);
        else
            ++it;
    }

    TrapAlgebraParams algebraParams;
    algebraParams.seed = project.params.seed + static_cast<int>(trackType) * 37 + project.generationCounter * 19 + 509;
    algebraParams.bars = std::max(1, project.params.bars);
    algebraParams.bpm = project.params.bpm;
    algebraParams.density = project.params.densityAmount;
    algebraParams.swing = std::clamp(project.params.swingPercent / 100.0f, 0.50f, 0.60f);
    algebraParams.humanize = project.params.humanizeAmount;
    algebraParams.variation = std::max(project.params.velocityAmount, project.params.timingAmount);
    algebraParams.temperature = 0.40f;
    algebraParams.qMin = 0.62f;
    algebraParams.candidateCount = 24;
    algebraParams.substyle = trapAlgebraSubstyleForProject(project.params.trapSubstyle);

    const auto algebraPattern = TrapAlgebraEngine().generate(algebraParams);
    applyTrapAlgebraPattern(project, algebraPattern, mutableTracks);

    juce::ignoreUnused(style);
    validatePattern(project, mutableTracks);
    PatternPerformanceTransformEngine::captureBasePatterns(project, mutableTracks);
}

void TrapEngine::regenerateTrackVariation(PatternProject& project, TrackType trackType)
{
    auto* target = findTrack(project, trackType);
    if (target == nullptr || target->locked || !target->enabled)
        return;

    generateTrackNew(project, trackType);

    target = findTrack(project, trackType);
    if (target == nullptr)
        return;

    dedupeAndSort(target->notes);
    target->variationId += 1;
    target->mutationDepth = std::clamp(target->mutationDepth + 0.10f, 0.0f, 1.0f);

    PatternPerformanceTransformEngine::captureBasePatterns(project, { trackType });
}

void TrapEngine::mutatePattern(PatternProject& project)
{
    generate(project);
}

void TrapEngine::mutateTrack(PatternProject& project, TrackType trackType)
{
    auto* track = findTrack(project, trackType);
    if (track == nullptr || track->locked || !track->enabled)
        return;

    generateTrackNew(project, trackType);
}

void TrapEngine::validatePattern(PatternProject& project, const std::unordered_set<TrackType>& mutableTracks) const
{
    const int bars = std::max(1, project.params.bars);

    const auto isEditable = [&mutableTracks](const TrackState* track)
    {
        return track != nullptr && !track->locked && mutableTracks.count(track->type) != 0;
    };

    for (auto& track : project.tracks)
    {
        if (!isEditable(&track))
            continue;

        dedupeAndSort(track.notes);
        for (auto& note : track.notes)
        {
            note.gridTick = std::clamp(note.gridTick, 0, bars * 16 * HiResTiming::kTicks1_16 - 1);
            note.lengthTicks = std::max(HiResTiming::kTicks1_16, note.lengthTicks);
            note.velocity = std::clamp(note.velocity, 1, 127);
            note.timingOffsetTicks = std::clamp(note.timingOffsetTicks, -120, 120);
        }

        int maxHits = bars * 10;
        if (track.type == TrackType::HiHat)
            maxHits = bars * 56;
        else if (track.type == TrackType::HatFX)
            maxHits = bars * 88;
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
    if (isEditable(kick))
    {
        kick->notes.erase(std::remove_if(kick->notes.begin(), kick->notes.end(), [snare, clap](const NoteEvent& k)
        {
            const bool onSnare = snare != nullptr && std::any_of(snare->notes.begin(), snare->notes.end(), [&k](const NoteEvent& s)
            {
                return !s.isGhost && nearestStepOf(s) == nearestStepOf(k);
            });

            const bool onClap = clap != nullptr && std::any_of(clap->notes.begin(), clap->notes.end(), [&k](const NoteEvent& c)
            {
                return !c.isGhost && nearestStepOf(c) == nearestStepOf(k);
            });

            return onSnare || onClap;
        }), kick->notes.end());
    }

    auto* sub = findTrack(project, TrackType::Sub808);
    if (isEditable(sub))
    {
        dedupeAndSort(sub->notes);
        moveSubOffBackbeat(*sub, snare, clap, bars);
        const bool algebraSub = std::any_of(sub->notes.begin(), sub->notes.end(), [](const NoteEvent& note)
        {
            return note.semanticRole.startsWith("trap_algebra_");
        });
        clampLowEndStartsPerBar(*sub, bars, algebraSub ? 4 : 3);
        clampLowEndPitchChanges(*sub, bars, 3);
        if (!algebraSub)
            ensureLowEndPhraseEnding(*sub, bars);
        dedupeAndSort(sub->notes);
        sub->sub808Notes = toSub808NoteEvents(sub->notes);
    }

    auto* hatFx = findTrack(project, TrackType::HatFX);
    if (isEditable(hatFx))
    {
        pruneHatFxOverLowEndAnchors(*hatFx, kick, sub);
        dedupeAndSort(hatFx->notes);
    }
}

void TrapEngine::applyTrapAlgebraPattern(PatternProject& project,
                                         const TrapAlgebraPattern& pattern,
                                         const std::unordered_set<TrackType>& mutableTracks) const
{
    const int bars = std::max(1, project.params.bars);

    for (auto& track : project.tracks)
    {
        if (mutableTracks.count(track.type) == 0 || track.locked || !track.enabled)
            continue;

        track.notes.clear();
        if (track.type == TrackType::Sub808)
            track.sub808Notes.clear();
    }

    for (const auto& note : pattern.matrix.allNotes())
    {
        const auto type = trackTypeForTrapAlgebraLane(note.laneIndex);
        if (!type.has_value() || mutableTracks.count(*type) == 0)
            continue;

        auto* track = findTrack(project, *type);
        if (track == nullptr || track->locked || !track->enabled)
            continue;

        const int ppqTick = note.tick64 * HiResTiming::kTicks1_64 + note.microTimingTicks;
        const int lengthSteps = std::max(1, static_cast<int>(std::ceil(static_cast<float>(note.durationTicks) / 4.0f)));
        const bool isGhost = note.role == TrapAlgebraRole::Ghost || note.laneIndex == TrapAlgebraLanes::KickGhost || note.laneIndex == TrapAlgebraLanes::ClapGhost;

        HiResTiming::addNoteAtTick(*track,
                                   phrase808PitchForTrapAlgebraNote(note, project.params),
                                   ppqTick,
                                   note.velocity,
                                   isGhost,
                                   bars,
                                   lengthSteps);

        if (!track->notes.empty())
            track->notes.back().semanticRole = semanticRoleForTrapAlgebraNote(note);
    }

    for (auto& track : project.tracks)
    {
        if (mutableTracks.count(track.type) == 0)
            continue;

        dedupeAndSort(track.notes);
        if (track.type == TrackType::Sub808)
            track.sub808Notes = toSub808NoteEvents(track.notes);
    }
}
} // namespace bbg
