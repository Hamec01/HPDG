#include "TrapEngine.h"

#include <algorithm>
#include <cmath>

#include "../Core/TrackSemantics.h"
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
    return std::clamp(laneBiasFor(project.styleInfluence, type).activityWeight, 0.55f, 1.6f);
}

float bounceWeight(const PatternProject& project)
{
    return std::clamp(project.styleInfluence.bounceWeight, 0.65f, 1.5f);
}

bool isAnchorStep(TrackType type, int stepInBar)
{
    if (type == TrackType::Kick || type == TrackType::Sub808)
        return stepInBar == 0 || stepInBar == 8;
    if (type == TrackType::Snare || type == TrackType::ClapGhostSnare)
        return stepInBar == 4 || stepInBar == 12;
    if (type == TrackType::HiHat)
        return (stepInBar % 2) == 0;
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

bool isTrapHatFamily(TrackType type)
{
    const auto family = familyFromTrackType(type);
    return family == TrackFamily::HatFamily || family == TrackFamily::CymbalFamily;
}

void applySampleAwareTrapFlavor(PatternProject& project, const std::unordered_set<TrackType>& mutableTracks)
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

            float guide = 0.42f * f->accent + 0.30f * f->onset + 0.28f * f->energy;
            if (track.type == TrackType::Kick || track.type == TrackType::Sub808 || track.type == TrackType::GhostKick)
                guide = 0.46f * f->low + 0.30f * f->accent + 0.24f * f->onset;
            else if (isTrapHatFamily(track.type))
                guide = 0.48f * f->high + 0.32f * f->energy + 0.20f * f->onset;

            const float gain = std::clamp(1.0f
                                              + 0.26f * react * support * (guide - 0.5f)
                                              + 0.18f * react * contrast * (0.5f - guide),
                                          0.66f,
                                          1.45f);
            note.velocity = std::clamp(static_cast<int>(static_cast<float>(note.velocity) * gain), 1, 127);

            if ((track.type == TrackType::Sub808 || track.type == TrackType::Kick)
                && f->nearPhraseBoundary
                && contrast > 0.12f)
            {
                const int nudge = static_cast<int>(-2.0f - 3.0f * contrast * react);
                note.microOffset = std::clamp(note.microOffset + nudge, -120, 120);
            }
        }

        if (isTrapHatFamily(track.type) && support * react > 0.42f)
        {
            track.notes.erase(std::remove_if(track.notes.begin(), track.notes.end(), [&](const NoteEvent& note)
            {
                const auto* f = featureAtStep(ctx.featureMap, note.step);
                if (f == nullptr)
                    return false;

                const int phase = (note.step + note.velocity + static_cast<int>(track.type)) % 9;
                return phase == 0 && f->high > 0.84f && f->onset < 0.26f && !f->isStrongBeat;
            }), track.notes.end());
        }
    }
}

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

bool isTrapHatBackbone(const NoteEvent& note)
{
    return (HiResTiming::noteTick(note) % HiResTiming::kTicks1_8) == 0;
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
            if ((sub.notes[i].step / 16) == bar)
                indices.push_back(i);

        if (static_cast<int>(indices.size()) <= maxStartsPerBar)
            continue;

        std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b)
        {
            const int scoreA = sub.notes[a].velocity + (sub.notes[a].step % 16 >= 12 ? 6 : 0);
            const int scoreB = sub.notes[b].velocity + (sub.notes[b].step % 16 >= 12 ? 6 : 0);
            return scoreA > scoreB;
        });

        for (size_t i = static_cast<size_t>(maxStartsPerBar); i < indices.size(); ++i)
            sub.notes[indices[i]].step = -1;
    }

    sub.notes.erase(std::remove_if(sub.notes.begin(), sub.notes.end(), [](const NoteEvent& n)
    {
        return n.step < 0;
    }), sub.notes.end());
}

void clampLowEndPitchChanges(TrackState& sub, int bars, int maxPitchChangesPerTwoBars)
{
    for (int window = 0; window < std::max(1, (bars + 1) / 2); ++window)
    {
        const int startStep = window * 32;
        const int endStep = std::min(bars * 16, startStep + 32);
        int changes = 0;
        int prevPitch = -1;

        for (auto& note : sub.notes)
        {
            if (note.step < startStep || note.step >= endStep)
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
    const bool hasEnding = std::any_of(sub.notes.begin(), sub.notes.end(), [finalBarStart](const NoteEvent& n)
    {
        return n.step >= finalBarStart + 12;
    });

    if (!hasEnding)
    {
        const int velocity = std::clamp(sub.notes.back().velocity, 84, 120);
        sub.notes.push_back({ std::clamp(sub.notes.back().pitch, 24, 60), finalBarStart + 14, 2, velocity, 0, false });
    }
}

bool hasStrongNoteAtStep(const TrackState* track, int step)
{
    if (track == nullptr)
        return false;

    return std::any_of(track->notes.begin(), track->notes.end(), [step](const NoteEvent& n)
    {
        return !n.isGhost && n.step == step;
    });
}

void moveSubOffBackbeat(TrackState& sub,
                        const TrackState* snare,
                        const TrackState* clap,
                        int bars)
{
    for (auto& note : sub.notes)
    {
        const bool backbeatCollision = hasStrongNoteAtStep(snare, note.step) || hasStrongNoteAtStep(clap, note.step);
        if (!backbeatCollision)
            continue;

        const int backStep = std::max(0, note.step - 1);
        const int fwdStep = std::min(bars * 16 - 1, note.step + 1);
        const bool backFree = !hasStrongNoteAtStep(snare, backStep) && !hasStrongNoteAtStep(clap, backStep);
        const bool fwdFree = !hasStrongNoteAtStep(snare, fwdStep) && !hasStrongNoteAtStep(clap, fwdStep);

        if (backFree)
            note.step = backStep;
        else if (fwdFree)
            note.step = fwdStep;
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
        const int stepInBar = ((n.step % 16) + 16) % 16;
        if (!(stepInBar == 0 || stepInBar == 8 || stepInBar == 10 || stepInBar >= 14))
            return false;

        const bool kickHere = std::any_of(kick->notes.begin(), kick->notes.end(), [&](const NoteEvent& k)
        {
            return !k.isGhost && std::abs(k.step - n.step) <= 0;
        });
        const bool subHere = std::any_of(sub->notes.begin(), sub->notes.end(), [&](const NoteEvent& s)
        {
            return !s.isGhost && std::abs(s.step - n.step) <= 0;
        });

        return kickHere && subHere && n.velocity >= 86;
    }), hatFx.notes.end());
}
}

TrapEngine::TrapEngine() = default;

void TrapEngine::generate(PatternProject& project)
{
    applyTrapStyleInfluence(project);
    const auto& style = getTrapProfile(project.params.trapSubstyle);
    const auto styleSpec = getTrapStyleSpec(style.substyle);
    std::mt19937 rng(static_cast<std::mt19937::result_type>(project.params.seed + project.generationCounter * 41 + 601));
    const auto phrase = TrapPhrasePlanner::createPlan(std::max(1, project.params.bars), project.params.densityAmount, rng);

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

    generateTrapSnareBackbone(project, style, phrase, rng, mutableTracks);
    generateTrapKickSkeleton(project, style, phrase, rng, mutableTracks);
    generateSub808RhythmFromTrapKicks(project, style, styleSpec, phrase, rng, mutableTracks);
    assignTrapSub808Pitches(project, style, styleSpec, phrase, rng, mutableTracks);
    applyTrapSub808Slides(project, style, styleSpec, phrase, rng, mutableTracks);
    generateTrapHiHatScaffold(project, style, phrase, rng, mutableTracks);
    generateTrapHatFX(project, style, phrase, rng, mutableTracks);
    generateTrapSupportLanes(project, style, styleSpec, phrase, rng, mutableTracks);
    applyTrapHumanization(project, style, rng, mutableTracks);
    validatePattern(project, mutableTracks);
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
    const auto styleSpec = getTrapStyleSpec(style.substyle);
    std::mt19937 rng(static_cast<std::mt19937::result_type>(project.params.seed + static_cast<int>(trackType) * 37 + project.generationCounter * 19 + 509));
    const auto phrase = TrapPhrasePlanner::createPlan(std::max(1, project.params.bars), project.params.densityAmount, rng);

    std::unordered_set<TrackType> mutableTracks { trackType };
    expandCoupledTracks(trackType, mutableTracks);
    generateTrapSnareBackbone(project, style, phrase, rng, mutableTracks);
    generateTrapKickSkeleton(project, style, phrase, rng, mutableTracks);
    generateSub808RhythmFromTrapKicks(project, style, styleSpec, phrase, rng, mutableTracks);
    assignTrapSub808Pitches(project, style, styleSpec, phrase, rng, mutableTracks);
    applyTrapSub808Slides(project, style, styleSpec, phrase, rng, mutableTracks);
    generateTrapHiHatScaffold(project, style, phrase, rng, mutableTracks);
    generateTrapHatFX(project, style, phrase, rng, mutableTracks);
    generateTrapSupportLanes(project, style, styleSpec, phrase, rng, mutableTracks);
    applyTrapHumanization(project, style, rng, mutableTracks);
    validatePattern(project, mutableTracks);
}

void TrapEngine::regenerateTrackVariation(PatternProject& project, TrackType trackType)
{
    auto* target = findTrack(project, trackType);
    if (target == nullptr || target->locked || !target->enabled)
        return;

    const auto before = target->notes;
    generateTrackNew(project, trackType);

    target = findTrack(project, trackType);
    if (target == nullptr)
        return;

    const auto& styleDefaults = getGenreStyleDefaults(GenreType::Trap, project.params.trapSubstyle);
    const auto& laneDefaults = getLaneStyleDefaults(styleDefaults, trackType);
    const float rgIntensity = std::clamp(laneDefaults.rgVariationIntensity, 0.6f, 1.35f);

    if (trackType == TrackType::HiHat)
    {
        std::vector<NoteEvent> merged = target->notes;
        for (const auto& oldNote : before)
            if (isTrapHatBackbone(oldNote))
                merged.push_back(oldNote);
        target->notes = std::move(merged);
    }
    else if (trackType == TrackType::HatFX)
    {
        std::vector<NoteEvent> merged = target->notes;
        const size_t keep = std::min(static_cast<size_t>(before.size() * 0.2f * rgIntensity), before.size());
        for (size_t i = 0; i < keep; ++i)
            merged.push_back(before[i]);
        target->notes = std::move(merged);
    }
    else
    {
        const size_t keep = std::min(static_cast<size_t>(before.size() * 0.32f * rgIntensity), target->notes.size());
        for (size_t i = 0; i < keep; ++i)
            target->notes[i] = before[i];
    }

    dedupeAndSort(target->notes);
    target->variationId += 1;
    target->mutationDepth = std::clamp(target->mutationDepth + 0.08f, 0.0f, 1.0f);
}

void TrapEngine::mutatePattern(PatternProject& project)
{
    applyTrapStyleInfluence(project);
    std::vector<TrackType> candidates;
    for (const auto& track : project.tracks)
        if (!track.locked && track.enabled)
            candidates.push_back(track.type);

    if (candidates.empty())
        return;

    std::mt19937 rng(static_cast<std::mt19937::result_type>(project.params.seed + project.mutationCounter * 401 + 3));
    std::shuffle(candidates.begin(), candidates.end(), rng);
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);

    const int count = std::max(1, static_cast<int>(candidates.size() / 3));
    for (int i = 0; i < count && i < static_cast<int>(candidates.size()); ++i)
        mutateTrack(project, candidates[static_cast<size_t>(i)]);

    if (chance(rng) < 0.68f)
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

void TrapEngine::mutateTrack(PatternProject& project, TrackType trackType)
{
    applyTrapStyleInfluence(project);
    auto* track = findTrack(project, trackType);
    if (track == nullptr || track->locked || !track->enabled)
        return;

    if (track->notes.empty())
    {
        generateTrackNew(project, trackType);
        return;
    }

    std::mt19937 rng(static_cast<std::mt19937::result_type>(project.params.seed + project.mutationCounter * 313 + static_cast<int>(trackType) * 71));
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);

    const auto& styleDefaults = getGenreStyleDefaults(GenreType::Trap, project.params.trapSubstyle);
    const auto& lane = getLaneStyleDefaults(styleDefaults, trackType);
    const float intensity = std::clamp(lane.mutationIntensity, 0.5f, 1.4f);

    if (trackType == TrackType::HiHat || trackType == TrackType::HatFX)
    {
        TrackState rebuilt;
        rebuilt.type = track->type;

        std::uniform_int_distribution<int> shiftPicker(0, 3);
        for (const auto& note : track->notes)
        {
            const bool backbone = trackType == TrackType::HiHat && isTrapHatBackbone(note);
            if (!backbone && chance(rng) < 0.24f * intensity)
                continue;

            int tick = HiResTiming::noteTick(note);
            if (!backbone && chance(rng) < 0.56f * intensity)
            {
                const int pick = shiftPicker(rng);
                const int delta = pick == 0 ? -HiResTiming::kTicks1_64
                                            : pick == 1 ? HiResTiming::kTicks1_64
                                                        : pick == 2 ? -HiResTiming::kTicks1_32
                                                                    : HiResTiming::kTicks1_32;
                tick += delta;
            }

            const int velJ = static_cast<int>(std::round((chance(rng) - 0.5f) * 18.0f));
            HiResTiming::addNoteAtTick(rebuilt,
                                       note.pitch,
                                       tick,
                                       std::clamp(note.velocity + velJ, 1, 127),
                                       note.isGhost,
                                       std::max(1, project.params.bars),
                                       note.length);
        }

        if (chance(rng) < 0.52f * intensity)
        {
            const int bars = std::max(1, project.params.bars);
            const int edgeTick = bars * HiResTiming::kTicksPerBar4_4 - HiResTiming::kTicks1_8;
            for (int i = 0; i < 4; ++i)
                HiResTiming::addNoteAtTick(rebuilt,
                                           44,
                                           edgeTick + i * HiResTiming::kTicks1_64,
                                           std::clamp(84 + i * 8, 1, 127),
                                           true,
                                           bars);
        }

        track->notes = std::move(rebuilt.notes);
        dedupeAndSort(track->notes);
        track->variationId += 1;
        track->mutationDepth = std::clamp(track->mutationDepth + 0.14f, 0.0f, 1.0f);
        return;
    }

    if (chance(rng) < 0.52f * intensity)
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

    if (!track->notes.empty() && chance(rng) < 0.66f * intensity)
    {
        std::uniform_int_distribution<size_t> pick(0, track->notes.size() - 1);
        auto& n = track->notes[pick(rng)];
        if (!isAnchorStep(trackType, n.step % 16))
        {
            std::uniform_int_distribution<int> shift(-1, 1);
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

void TrapEngine::regenerateTrackInternal(PatternProject& project,
                                         TrackState& track,
                                         const TrapStyleProfile& style,
                                         const std::vector<TrapPhraseRole>& phrase,
                                         std::mt19937& rng) const
{
    if (!track.enabled)
    {
        track.notes.clear();
        return;
    }

    switch (track.type)
    {
        case TrackType::Kick: kickGenerator.generate(track, project.params, style, project.styleInfluence, phrase, rng); break;
        case TrackType::Snare: snareGenerator.generate(track, project.params, style, phrase, rng); break;
        case TrackType::HiHat: hatGenerator.generate(track, project.params, style, project.styleInfluence, phrase, rng); break;
        default: track.notes.clear(); break;
    }

    track.subProfile = style.name;
    track.laneRole = roleForTrack(track.type);
}

void TrapEngine::generateTrapSnareBackbone(PatternProject& project,
                                           const TrapStyleProfile& style,
                                           const std::vector<TrapPhraseRole>& phrase,
                                           std::mt19937& rng,
                                           const std::unordered_set<TrackType>& mutableTracks) const
{
    auto* snare = findTrack(project, TrackType::Snare);
    if (snare == nullptr || mutableTracks.count(snare->type) == 0 || snare->locked || !snare->enabled)
        return;

    snareGenerator.generate(*snare, project.params, style, phrase, rng);
}

void TrapEngine::generateTrapHiHatScaffold(PatternProject& project,
                                           const TrapStyleProfile& style,
                                           const std::vector<TrapPhraseRole>& phrase,
                                           std::mt19937& rng,
                                           const std::unordered_set<TrackType>& mutableTracks) const
{
    auto* hat = findTrack(project, TrackType::HiHat);
    if (hat == nullptr || mutableTracks.count(hat->type) == 0 || hat->locked || !hat->enabled)
        return;

    hatGenerator.generate(*hat, project.params, style, project.styleInfluence, phrase, rng);
}

void TrapEngine::generateTrapKickSkeleton(PatternProject& project,
                                          const TrapStyleProfile& style,
                                          const std::vector<TrapPhraseRole>& phrase,
                                          std::mt19937& rng,
                                          const std::unordered_set<TrackType>& mutableTracks) const
{
    auto* kick = findTrack(project, TrackType::Kick);
    if (kick == nullptr || mutableTracks.count(kick->type) == 0 || kick->locked || !kick->enabled)
        return;

    kickGenerator.generate(*kick, project.params, style, project.styleInfluence, phrase, rng);
}

void TrapEngine::generateSub808RhythmFromTrapKicks(PatternProject& project,
                                                    const TrapStyleProfile& style,
                                                    const TrapStyleSpec& spec,
                                                    const std::vector<TrapPhraseRole>& phrase,
                                                    std::mt19937& rng,
                                                    const std::unordered_set<TrackType>& mutableTracks) const
{
    const auto& styleDefaults = getGenreStyleDefaults(GenreType::Trap, project.params.trapSubstyle);
    auto* kick = findTrack(project, TrackType::Kick);
    auto* sub = findTrack(project, TrackType::Sub808);
    auto* hat = findTrack(project, TrackType::HiHat);
    auto* hatFx = findTrack(project, TrackType::HatFX);
    auto* openHat = findTrack(project, TrackType::OpenHat);
    auto* snare = findTrack(project, TrackType::Snare);
    if (kick == nullptr || sub == nullptr)
        return;
    if (mutableTracks.count(sub->type) == 0 || sub->locked || !sub->enabled)
        return;

    const auto& lane = getLaneStyleDefaults(styleDefaults, sub->type);
    subGenerator.generateRhythm(*sub,
                                *kick,
                                hat,
                                hatFx,
                                openHat,
                                snare,
                                project.params,
                                style,
                                spec,
                                project.styleInfluence,
                                lane.sub808Activity,
                                phrase,
                                rng);
}

void TrapEngine::assignTrapSub808Pitches(PatternProject& project,
                                         const TrapStyleProfile& style,
                                         const TrapStyleSpec& spec,
                                         const std::vector<TrapPhraseRole>& phrase,
                                         std::mt19937& rng,
                                         const std::unordered_set<TrackType>& mutableTracks) const
{
    auto* sub = findTrack(project, TrackType::Sub808);
    if (sub == nullptr || mutableTracks.count(sub->type) == 0 || sub->locked || !sub->enabled)
        return;

    subGenerator.assignPitches(*sub, project.params, style, spec, phrase, rng);
}

void TrapEngine::applyTrapSub808Slides(PatternProject& project,
                                       const TrapStyleProfile& style,
                                       const TrapStyleSpec& spec,
                                       const std::vector<TrapPhraseRole>& phrase,
                                       std::mt19937& rng,
                                       const std::unordered_set<TrackType>& mutableTracks) const
{
    auto* sub = findTrack(project, TrackType::Sub808);
    if (sub == nullptr || mutableTracks.count(sub->type) == 0 || sub->locked || !sub->enabled)
        return;

    subGenerator.applySlides(*sub, style, spec, phrase, rng);
}

void TrapEngine::generateTrapHatFX(PatternProject& project,
                                   const TrapStyleProfile& style,
                                   const std::vector<TrapPhraseRole>& phrase,
                                   std::mt19937& rng,
                                   const std::unordered_set<TrackType>& mutableTracks) const
{
    const auto& styleDefaults = getGenreStyleDefaults(GenreType::Trap, project.params.trapSubstyle);
    auto* hat = findTrack(project, TrackType::HiHat);
    auto* hatFx = findTrack(project, TrackType::HatFX);
    auto* kick = findTrack(project, TrackType::Kick);
    auto* snare = findTrack(project, TrackType::Snare);
    auto* openHat = findTrack(project, TrackType::OpenHat);
    auto* sub = findTrack(project, TrackType::Sub808);
    if (hat == nullptr || hatFx == nullptr)
        return;
    if (mutableTracks.count(hatFx->type) == 0 || hatFx->locked || !hatFx->enabled)
        return;

    const auto& lane = getLaneStyleDefaults(styleDefaults, hatFx->type);
    hatFxGenerator.generate(*hatFx,
                            *hat,
                            kick,
                            snare,
                            openHat,
                            sub,
                            style,
                            lane.hatFxIntensity * laneActivityWeight(project, TrackType::HatFX) * bounceWeight(project),
                            phrase,
                            rng);
}

void TrapEngine::generateTrapSupportLanes(PatternProject& project,
                                          const TrapStyleProfile& style,
                                          const TrapStyleSpec& spec,
                                          const std::vector<TrapPhraseRole>& phrase,
                                          std::mt19937& rng,
                                          const std::unordered_set<TrackType>& mutableTracks) const
{
    (void) phrase;
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    const auto& styleDefaults = getGenreStyleDefaults(GenreType::Trap, project.params.trapSubstyle);

    auto* hat = findTrack(project, TrackType::HiHat);
    auto* kick = findTrack(project, TrackType::Kick);
    auto* snare = findTrack(project, TrackType::Snare);

    if (auto* open = findTrack(project, TrackType::OpenHat);
        open != nullptr && hat != nullptr && mutableTracks.count(open->type) != 0 && !open->locked && open->enabled)
    {
        open->notes.clear();
        std::uniform_int_distribution<int> vel(style.hatVelocityMin + 4, style.hatVelocityMax);
        const auto& lane = getLaneStyleDefaults(styleDefaults, open->type);
        const float openBias = spec.allowOpenHatFrequently ? 1.28f : 0.7f;
        for (const auto& h : hat->notes)
        {
            float gate = std::clamp(style.openHatChance * lane.noteProbability * openBias * laneActivityWeight(project, TrackType::OpenHat), 0.01f, 0.6f);
            const int s = h.step % 16;
            if (s == 7 || s == 15)
                gate += 0.14f;
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
        for (const auto& s : snare->notes)
        {
            if (chance(rng) < std::clamp(style.clapLayerChance * lane.noteProbability, 0.1f, 0.95f))
                clap->notes.push_back({ 39, s.step, 1, std::clamp(vel(rng), 1, 127), 2, false });
        }
    }

    if (auto* ghost = findTrack(project, TrackType::GhostKick);
        ghost != nullptr && kick != nullptr && mutableTracks.count(ghost->type) != 0 && !ghost->locked && ghost->enabled)
    {
        ghost->notes.clear();
        std::uniform_int_distribution<int> vel(style.kickVelocityMin - 22, style.kickVelocityMin - 8);
        const auto& lane = getLaneStyleDefaults(styleDefaults, ghost->type);
        const float ghostDensity = std::clamp((spec.ghostKickDensityMin + spec.ghostKickDensityMax) * 0.5f, 0.01f, 0.3f);
        for (const auto& k : kick->notes)
        {
            if ((k.step % 16) == 0 || (k.step % 16) == 8)
                continue;
            if (chance(rng) < std::clamp(style.ghostKickChance * lane.noteProbability * (ghostDensity / 0.08f), 0.01f, 0.22f))
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
        const float cymBias = spec.allowCymbalTransitions ? 1.2f : 0.42f;
        if (chance(rng) < std::clamp(0.32f * lane.phraseEndingProbability * cymBias, 0.02f, 0.78f))
            cym->notes.push_back({ 49, std::max(0, project.params.bars * 16 - 1), 2, vel(rng), 0, false });
    }

    if (auto* perc = findTrack(project, TrackType::Perc);
        perc != nullptr && mutableTracks.count(perc->type) != 0 && !perc->locked && perc->enabled)
    {
        perc->notes.clear();
        const auto& lane = getLaneStyleDefaults(styleDefaults, perc->type);
        std::uniform_int_distribution<int> vel(style.hatVelocityMin, style.hatVelocityMax);
        for (int bar = 0; bar < std::max(1, project.params.bars); ++bar)
        {
            for (int step : { 5, 13 })
                if (chance(rng) < std::clamp(style.percChance * lane.noteProbability, 0.01f, 0.44f))
                    perc->notes.push_back({ 50, bar * 16 + step, 1, vel(rng), 0, false });
        }
    }
}

void TrapEngine::applyTrapHumanization(PatternProject& project,
                                       const TrapStyleProfile& style,
                                       std::mt19937& rng,
                                       const std::unordered_set<TrackType>& mutableTracks) const
{
    juce::ignoreUnused(style);
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    std::uniform_int_distribution<int> velJitter(-6, 6);
    const auto& styleDefaults = getGenreStyleDefaults(GenreType::Trap, project.params.trapSubstyle);

    for (auto& track : project.tracks)
    {
        if (mutableTracks.count(track.type) == 0)
            continue;

        const auto& lane = getLaneStyleDefaults(styleDefaults, track.type);
        for (auto& note : track.notes)
        {
            if ((note.step % 2) == 1 && track.type != TrackType::Kick && track.type != TrackType::Sub808 && track.type != TrackType::Snare)
                note.microOffset += static_cast<int>(juce::jmap(project.params.swingPercent, 50.0f, 58.0f, 0.0f, 12.0f));

            int minJitter = -std::max(1, static_cast<int>(6.0f * lane.humanizeBias));
            int maxJitter = std::max(2, static_cast<int>(6.0f * lane.humanizeBias) + 1);

            if (track.type == TrackType::HiHat)
            {
                switch (style.substyle)
                {
                    case TrapSubstyle::ATLClassic: minJitter = -3; maxJitter = 4; break;
                    case TrapSubstyle::DarkTrap: minJitter = -2; maxJitter = 4; break;
                    case TrapSubstyle::CloudTrap: minJitter = -4; maxJitter = 5; break;
                    case TrapSubstyle::RageTrap: minJitter = -2; maxJitter = 3; break;
                    case TrapSubstyle::MemphisTrap: minJitter = -4; maxJitter = 6; break;
                    case TrapSubstyle::LuxuryTrap: minJitter = -2; maxJitter = 3; break;
                    default: break;
                }
            }
            else if (track.type == TrackType::HatFX)
            {
                switch (style.substyle)
                {
                    case TrapSubstyle::ATLClassic: minJitter = -2; maxJitter = 3; break;
                    case TrapSubstyle::DarkTrap: minJitter = -1; maxJitter = 5; break;
                    case TrapSubstyle::CloudTrap: minJitter = -3; maxJitter = 4; break;
                    case TrapSubstyle::RageTrap: minJitter = -2; maxJitter = 2; break;
                    case TrapSubstyle::MemphisTrap: minJitter = -1; maxJitter = 2; break;
                    case TrapSubstyle::LuxuryTrap: minJitter = -1; maxJitter = 2; break;
                    default: break;
                }
            }

            std::uniform_int_distribution<int> timing(minJitter, maxJitter);
            if (!isAnchorStep(track.type, note.step % 16))
                note.microOffset += timing(rng);

            note.microOffset = std::clamp(static_cast<int>(note.microOffset * lane.timingBias), -120, 120);
            if (chance(rng) < 0.88f)
                note.velocity = std::clamp(note.velocity + velJitter(rng), 1, 127);
        }
    }

    applySampleAwareTrapFlavor(project, mutableTracks);
}

void TrapEngine::validatePattern(PatternProject& project, const std::unordered_set<TrackType>& mutableTracks) const
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

    auto* sub = findTrack(project, TrackType::Sub808);
    if (sub != nullptr && mutableTracks.count(sub->type) != 0)
    {
        dedupeAndSort(sub->notes);
        moveSubOffBackbeat(*sub, snare, clap, bars);
        clampLowEndStartsPerBar(*sub, bars, 3);
        clampLowEndPitchChanges(*sub, bars, 3);
        ensureLowEndPhraseEnding(*sub, bars);
        dedupeAndSort(sub->notes);
    }

    auto* hatFx = findTrack(project, TrackType::HatFX);
    if (hatFx != nullptr && mutableTracks.count(hatFx->type) != 0)
    {
        pruneHatFxOverLowEndAnchors(*hatFx, kick, sub);
        dedupeAndSort(hatFx->notes);
    }
}
} // namespace bbg
