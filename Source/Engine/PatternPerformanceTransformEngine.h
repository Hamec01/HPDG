#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_set>

#include "../Core/PatternProject.h"
#include "GenrePerformanceProfile.h"

namespace bbg::PatternPerformanceTransformEngine
{
namespace detail
{
inline bool approximatelyEqual(float lhs, float rhs, float epsilon)
{
    return std::abs(lhs - rhs) <= epsilon;
}

template <typename NoteLike>
inline bool isGhostNote(const NoteLike&)
{
    return false;
}

inline bool isGhostNote(const NoteEvent& note)
{
    return note.isGhost;
}

template <typename NoteLike>
inline uint32_t noteHash(TrackType trackType, const NoteLike& note, uint32_t salt)
{
    uint32_t value = 2166136261u;

    auto mix = [&value](uint32_t component)
    {
        value ^= component + 0x9e3779b9u + (value << 6) + (value >> 2);
    };

    mix(static_cast<uint32_t>(trackType));
    mix(static_cast<uint32_t>(note.step));
    mix(static_cast<uint32_t>(note.pitch));
    mix(static_cast<uint32_t>(note.length));
    mix(static_cast<uint32_t>(note.velocity));
    mix(static_cast<uint32_t>(note.microOffset + 2048));
    mix(salt);

    return value;
}

template <typename NoteLike>
inline float stableUnit(TrackType trackType, const NoteLike& note, uint32_t salt)
{
    return static_cast<float>(static_cast<int>(noteHash(trackType, note, salt) % 2001u) - 1000) / 1000.0f;
}

inline float semanticRigidity(const juce::String& semanticRole)
{
    const auto normalized = semanticRole.toLowerCase();
    if (normalized.contains("reference_copy") || normalized.contains("backbone"))
        return 1.0f;
    if (normalized.contains("anchor") || normalized.contains("main"))
        return 0.85f;
    if (normalized.contains("accent") || normalized.contains("downbeat"))
        return 0.65f;
    if (normalized.contains("ghost") || normalized.contains("fill") || normalized.contains("support"))
        return 0.18f;
    return 0.35f;
}

template <typename NoteLike>
inline int stepInBar(const NoteLike& note)
{
    const int normalized = note.step % 16;
    return normalized < 0 ? normalized + 16 : normalized;
}

inline float laneCoreStrength(const TrackState& track, GenreType genre)
{
    const auto normalizedRole = track.laneRole.toLowerCase();
    if (normalizedRole.contains("foundation")
        || normalizedRole.contains("backbeat")
        || normalizedRole.contains("core_pulse")
        || normalizedRole.contains("trap_kick")
        || normalizedRole.contains("trap_sub")
        || normalizedRole.contains("drill_kick")
        || normalizedRole.contains("drill_sub"))
        return 1.0f;

    switch (track.type)
    {
        case TrackType::Kick:
        case TrackType::Snare:
        case TrackType::Sub808:
            return genre == GenreType::Trap || genre == GenreType::Drill ? 1.0f : 0.88f;
        case TrackType::ClapGhostSnare:
            return normalizedRole.contains("layer") || normalizedRole.contains("ghost") ? 0.32f : 0.46f;
        case TrackType::GhostKick:
            return 0.22f;
        case TrackType::HiHat:
            return genre == GenreType::BoomBap ? 0.22f : 0.12f;
        case TrackType::OpenHat:
        case TrackType::Ride:
        case TrackType::Cymbal:
        case TrackType::Perc:
        case TrackType::HatFX:
            return 0.10f;
    }

    return 0.15f;
}

template <typename NoteLike>
inline float supportFactor(const TrackState& track, const NoteLike& note)
{
    float factor = 0.0f;

    switch (track.type)
    {
        case TrackType::HiHat:
        case TrackType::Ride:
            factor = 1.0f;
            break;
        case TrackType::Perc:
        case TrackType::HatFX:
            factor = 0.92f;
            break;
        case TrackType::OpenHat:
            factor = 0.68f;
            break;
        case TrackType::ClapGhostSnare:
            factor = 0.52f;
            break;
        case TrackType::GhostKick:
            factor = 0.36f;
            break;
        case TrackType::Cymbal:
            factor = 0.20f;
            break;
        default:
            break;
    }

    const auto semantic = note.semanticRole.toLowerCase();
    if (semantic.contains("support") || semantic.contains("ghost") || semantic.contains("fill")
        || semantic.contains("layer") || semantic.contains("transition") || semantic.contains("triplet")
        || semantic.contains("burst") || semantic.contains("pickup") || semantic.contains("move")
        || semantic.contains("release") || semantic.contains("drag"))
        factor = std::max(factor, 0.72f);

    if (isGhostNote(note))
        factor = std::max(factor, 0.78f);

    const auto normalizedRole = track.laneRole.toLowerCase();
    if (normalizedRole.contains("support") || normalizedRole.contains("texture") || normalizedRole.contains("carrier")
        || normalizedRole.contains("hat") || normalizedRole.contains("ride") || normalizedRole.contains("perc")
        || normalizedRole.contains("layer") || normalizedRole.contains("ghost") || normalizedRole.contains("accent")
        || normalizedRole.contains("punctuation") || normalizedRole.contains("open"))
        factor = std::max(factor, 0.66f);

    return std::clamp(factor, 0.0f, 1.0f);
}

template <typename NoteLike>
inline float metricAnchorStrength(const TrackState& track, const NoteLike& note, const GenrePerformanceProfile& profile)
{
    const int localStep = stepInBar(note);

    switch (track.type)
    {
        case TrackType::Kick:
            if (localStep == 0 || localStep == 8)
                return profile.genre == GenreType::Trap || profile.genre == GenreType::Drill ? 1.0f : 0.84f;
            if (localStep == 4 || localStep == 12)
                return profile.genre == GenreType::BoomBap ? 0.48f : 0.28f;
            return 0.0f;

        case TrackType::Snare:
            if (localStep == 4 || localStep == 12)
                return profile.genre == GenreType::Trap || profile.genre == GenreType::Drill ? 1.0f : 0.90f;
            if (localStep == 8)
                return profile.genre == GenreType::Drill ? 0.82f : 0.54f;
            return 0.0f;

        case TrackType::Sub808:
            if (localStep == 0 || localStep == 8)
                return profile.genre == GenreType::Trap || profile.genre == GenreType::Drill ? 0.94f : 0.74f;
            return 0.0f;

        case TrackType::ClapGhostSnare:
            if (localStep == 4 || localStep == 12)
                return profile.genre == GenreType::Drill ? 0.30f : 0.22f;
            return 0.0f;

        case TrackType::HiHat:
            return (localStep % 4) == 0 ? (profile.genre == GenreType::BoomBap ? 0.18f : 0.10f) : 0.0f;

        default:
            return 0.0f;
    }
}

template <typename NoteLike>
inline float movementAnchorStrength(const TrackState& track, const NoteLike& note, const GenrePerformanceProfile& profile)
{
    const float semantic = semanticRigidity(note.semanticRole);
    const float metric = metricAnchorStrength(track, note, profile);
    const float laneCore = laneCoreStrength(track, profile.genre);
    const float support = supportFactor(track, note);

    float anchor = std::max(semantic, metric * (0.55f + laneCore * 0.45f));
    if (metric > 0.0f && laneCore > 0.75f)
        anchor = std::max(anchor, 0.72f + metric * 0.28f);

    if (support > 0.6f && semantic < 0.80f)
        anchor = std::min(anchor, 0.50f);

    return std::clamp(anchor, 0.0f, 1.0f);
}

template <typename NoteLike>
inline float swingPhaseWeight(const TrackState& track, const NoteLike& note, const GenrePerformanceProfile& profile)
{
    const int localStep = stepInBar(note);

    if (profile.genre == GenreType::Trap || profile.genre == GenreType::Drill)
    {
        if (track.type == TrackType::Kick || track.type == TrackType::Snare || track.type == TrackType::Sub808)
            return 0.0f;
    }

    switch (track.type)
    {
        case TrackType::HiHat:
        case TrackType::Ride:
        case TrackType::HatFX:
        case TrackType::Perc:
            if ((localStep % 4) == 0)
                return 0.0f;
            return (localStep % 2) == 1 ? 1.0f : 0.62f;

        case TrackType::OpenHat:
            return (localStep % 4) == 2 || (localStep % 4) == 3 ? 0.50f : 0.0f;

        case TrackType::ClapGhostSnare:
            return (localStep % 4) != 0 ? 0.34f : 0.0f;

        case TrackType::GhostKick:
            return profile.genre == GenreType::BoomBap && (localStep % 4) != 0 ? 0.12f : 0.0f;

        default:
            return 0.0f;
    }
}

inline float biasedTimingNoise(float noise, const GenrePerformanceProfile& profile, float support)
{
    if (support <= 0.0f || profile.supportLateBias <= 0.0f)
        return noise;

    return std::clamp(noise + support * profile.supportLateBias, -1.0f, 1.0f);
}

template <typename NoteLike>
inline float accentWeight(TrackType trackType, const NoteLike& note)
{
    const int stepInBar = note.step % 16;
    float accent = 0.50f;

    switch (trackType)
    {
        case TrackType::Kick:
            accent = (stepInBar == 0 || stepInBar == 8) ? 1.0f : ((stepInBar == 4 || stepInBar == 12) ? 0.65f : 0.35f);
            break;
        case TrackType::Snare:
        case TrackType::ClapGhostSnare:
            accent = (stepInBar == 4 || stepInBar == 12 || stepInBar == 8) ? 1.0f : 0.30f;
            break;
        case TrackType::HiHat:
        case TrackType::Ride:
        case TrackType::HatFX:
            accent = (stepInBar % 4 == 0) ? 0.80f : ((stepInBar % 2 == 0) ? 0.55f : 0.35f);
            break;
        case TrackType::OpenHat:
        case TrackType::Cymbal:
            accent = (stepInBar == 0 || stepInBar == 8 || stepInBar == 12) ? 0.85f : 0.45f;
            break;
        case TrackType::GhostKick:
        case TrackType::Perc:
            accent = 0.35f;
            break;
        case TrackType::Sub808:
            accent = (stepInBar == 0 || stepInBar == 8) ? 0.85f : 0.40f;
            break;
    }

    accent = std::max(accent, semanticRigidity(note.semanticRole));
    if (isGhostNote(note))
        accent = std::min(accent, 0.20f);

    return std::clamp(accent, 0.0f, 1.0f);
}

inline int clampMicroOffset(TrackType trackType, int microOffset)
{
    switch (trackType)
    {
        case TrackType::Kick:
        case TrackType::GhostKick:
            return std::clamp(microOffset, -72, 72);
        case TrackType::Snare:
        case TrackType::ClapGhostSnare:
            return std::clamp(microOffset, -96, 96);
        case TrackType::Sub808:
            return std::clamp(microOffset, -96, 96);
        case TrackType::HiHat:
        case TrackType::OpenHat:
        case TrackType::Ride:
        case TrackType::Cymbal:
        case TrackType::Perc:
        case TrackType::HatFX:
            return std::clamp(microOffset, -120, 120);
    }

    return std::clamp(microOffset, -120, 120);
}

inline int clampVelocity(const NoteEvent& baseNote, int velocity)
{
    if (baseNote.isGhost)
        velocity = std::min(velocity, baseNote.velocity + 4);

    return std::clamp(velocity, 1, 127);
}

inline int clampVelocity(const Sub808NoteEvent&, int velocity)
{
    return std::clamp(velocity, 1, 127);
}

struct DensityAuthoringInfo
{
    bool matched = false;
    bool anchorLocked = false;
    float importance = 0.5f;
};

template <typename NoteLike>
inline NoteAuthoringKey makeAuthoringKey(const NoteLike& note)
{
    NoteAuthoringKey key;
    key.step = note.step;
    key.microOffset = note.microOffset;
    key.pitch = note.pitch;
    key.length = note.length;
    key.isGhost = isGhostNote(note);
    return key;
}

template <typename NoteLike>
inline DensityAuthoringInfo densityAuthoringInfo(const PatternAuthoringState& authoring,
                                                 const TrackState& track,
                                                 const NoteLike& note)
{
    DensityAuthoringInfo info;

    const auto laneIt = authoring.noteMetadataByLane.find(track.laneId);
    if (laneIt == authoring.noteMetadataByLane.end())
        return info;

    const auto key = makeAuthoringKey(note);
    for (const auto& state : laneIt->second)
    {
        const auto& noteKey = state.noteKey;
        if (noteKey.step != key.step
            || noteKey.microOffset != key.microOffset
            || noteKey.pitch != key.pitch
            || noteKey.length != key.length
            || noteKey.isGhost != key.isGhost)
        {
            continue;
        }

        info.matched = true;
        info.anchorLocked = state.anchorLocked;
        info.importance = std::clamp(static_cast<float>(state.importanceWeight) / 100.0f, 0.0f, 1.0f);
        return info;
    }

    return info;
}

inline float semanticDensityPriority(const juce::String& semanticRole)
{
    const auto normalized = semanticRole.trim().toLowerCase();
    if (normalized.isEmpty())
        return -1.0f;

    if (normalized.contains("reference_copy") || normalized.contains("backbone") || normalized.contains("anchor"))
        return 1.0f;
    if (normalized.contains("foundation") || normalized.contains("core") || normalized.contains("main"))
        return 0.90f;
    if (normalized.contains("hold") || normalized.contains("support") || normalized.contains("open")
        || normalized.contains("layer") || normalized.contains("accent") || normalized.contains("downbeat"))
    {
        return 0.68f;
    }
    if (normalized.contains("pickup") || normalized.contains("move") || normalized.contains("release"))
        return 0.48f;
    if (normalized.contains("ghost"))
        return 0.34f;
    if (normalized.contains("transition") || normalized.contains("fill") || normalized.contains("triplet")
        || normalized.contains("burst") || normalized.contains("texture") || normalized.contains("fx"))
    {
        return 0.18f;
    }

    return -1.0f;
}

inline float fallbackTrackDensityPriority(const TrackState& track, GenreType genre)
{
    const auto normalizedRole = track.laneRole.toLowerCase();
    if (normalizedRole.contains("foundation") || normalizedRole.contains("backbeat")
        || normalizedRole.contains("core_pulse") || normalizedRole.contains("trap_kick")
        || normalizedRole.contains("trap_sub") || normalizedRole.contains("drill_kick")
        || normalizedRole.contains("drill_sub"))
    {
        return 0.90f;
    }

    if (normalizedRole.contains("support") || normalizedRole.contains("carrier") || normalizedRole.contains("layer"))
        return 0.58f;
    if (normalizedRole.contains("texture") || normalizedRole.contains("accent") || normalizedRole.contains("punctuation"))
        return 0.34f;

    switch (track.type)
    {
        case TrackType::Kick:
        case TrackType::Snare:
        case TrackType::Sub808:
            return genre == GenreType::Trap || genre == GenreType::Drill ? 0.86f : 0.78f;
        case TrackType::HiHat:
        case TrackType::Ride:
            return genre == GenreType::BoomBap ? 0.56f : 0.48f;
        case TrackType::OpenHat:
            return 0.52f;
        case TrackType::ClapGhostSnare:
        case TrackType::GhostKick:
        case TrackType::Perc:
        case TrackType::HatFX:
            return 0.38f;
        case TrackType::Cymbal:
            return 0.32f;
    }

    return 0.40f;
}

template <typename NoteLike>
inline float densityPriority(const PatternAuthoringState& authoring,
                             const TrackState& track,
                             const NoteLike& note,
                             const GenrePerformanceProfile& profile,
                             DensityAuthoringInfo* outAuthoringInfo = nullptr)
{
    const auto authoringInfo = densityAuthoringInfo(authoring, track, note);
    if (outAuthoringInfo != nullptr)
        *outAuthoringInfo = authoringInfo;

    if (authoringInfo.anchorLocked)
        return 1.0f;

    float priority = -1.0f;
    if (authoringInfo.matched)
        priority = authoringInfo.importance;

    const float semanticPriority = semanticDensityPriority(note.semanticRole);
    if (priority < 0.0f)
    {
        priority = semanticPriority >= 0.0f
            ? semanticPriority
            : fallbackTrackDensityPriority(track, profile.genre);
    }
    else if (semanticPriority >= 0.0f)
    {
        priority = std::max(priority, semanticPriority * 0.94f);
    }

    const float metric = metricAnchorStrength(track, note, profile);
    const float laneCore = laneCoreStrength(track, profile.genre);
    const float support = supportFactor(track, note);
    const auto semantic = note.semanticRole.toLowerCase();

    priority = std::max(priority, metric * (0.72f + laneCore * 0.18f));

    switch (profile.genre)
    {
        case GenreType::BoomBap:
            if (semantic.contains("ghost") || semantic.contains("support"))
                priority += 0.08f;
            else if (track.type == TrackType::HiHat || track.type == TrackType::Ride || track.type == TrackType::ClapGhostSnare)
                priority += support * 0.04f;
            break;

        case GenreType::Rap:
            if (profile.substyleName == "EastCoast" && (track.type == TrackType::HiHat || track.type == TrackType::Ride))
                priority += 0.04f;
            if (semantic.contains("support"))
                priority += 0.02f;
            break;

        case GenreType::Trap:
            if (track.type == TrackType::Kick || track.type == TrackType::Snare || track.type == TrackType::Sub808)
                priority = std::max(priority, metric * 0.96f);
            if (support > 0.55f || semantic.contains("transition") || semantic.contains("texture") || semantic.contains("fill"))
                priority -= 0.08f;
            break;

        case GenreType::Drill:
            if (track.type == TrackType::Kick || track.type == TrackType::Snare || track.type == TrackType::Sub808)
                priority = std::max(priority, metric * 0.98f);
            if (support > 0.48f || semantic.contains("transition") || semantic.contains("triplet")
                || semantic.contains("burst") || semantic.contains("ghost"))
            {
                priority -= 0.10f;
            }
            break;
    }

    if (track.type == TrackType::Sub808 && note.length >= 4)
        priority = std::max(priority, 0.64f);

    return std::clamp(priority, 0.0f, 1.0f);
}

template <typename NoteLike>
inline bool shouldKeepForDensity(const PatternAuthoringState& authoring,
                                 const TrackState& track,
                                 const NoteLike& baseNote,
                                 float densityDecrease,
                                 float densityWeight,
                                 const GenrePerformanceProfile& profile)
{
    DensityAuthoringInfo authoringInfo;
    const float priority = densityPriority(authoring, track, baseNote, profile, &authoringInfo);
    if (authoringInfo.anchorLocked || priority >= 0.95f)
        return true;

    const float noise = (stableUnit(track.type, baseNote, 131u) + 1.0f) * 0.5f;
    const float score = std::clamp(priority * 0.86f + noise * 0.14f, 0.0f, 1.0f);

    float threshold = std::clamp(densityDecrease * densityWeight, 0.0f, 0.95f);
    const float support = supportFactor(track, baseNote);
    switch (profile.genre)
    {
        case GenreType::BoomBap:
            threshold -= support * 0.10f;
            break;
        case GenreType::Rap:
            threshold += support * 0.02f;
            break;
        case GenreType::Trap:
            threshold += support * 0.05f;
            break;
        case GenreType::Drill:
            threshold += support * 0.07f;
            break;
    }

    if (isGhostNote(baseNote) && profile.genre != GenreType::BoomBap)
        threshold += 0.04f;

    threshold = std::clamp(threshold, 0.0f, 0.97f);
    return score >= threshold;
}

inline int noteEndStep(const Sub808NoteEvent& note)
{
    return note.step + std::max(note.length, 1);
}

inline bool hasDirectSub808Link(const Sub808NoteEvent& current, const Sub808NoteEvent& next)
{
    return current.glideToNext || current.isLegato || next.isSlide;
}

template <typename NoteLike>
inline void applyTransformSequence(std::vector<NoteLike>& visibleNotes,
                                   const std::vector<NoteLike>& baseNotes,
                                   const PatternAuthoringState& authoring,
                                   const TrackState& track,
                                   const GeneratorParams& currentParams,
                                   const GeneratorParams& baseParams,
                                   const GenrePerformanceProfile& profile)
{
    visibleNotes = baseNotes;
    if (visibleNotes.empty())
        return;

    const auto trackType = track.type;
    const auto& laneProfile = profile.lanes[trackTypeIndex(trackType)];
    const float swingDelta = currentParams.swingPercent - baseParams.swingPercent;
    const float timingDelta = std::clamp(currentParams.timingAmount - baseParams.timingAmount, -1.0f, 1.0f);
    const float velocityDelta = std::clamp(currentParams.velocityAmount - baseParams.velocityAmount, -1.0f, 1.0f);
    const float humanizeDelta = std::clamp(currentParams.humanizeAmount - baseParams.humanizeAmount, -1.0f, 1.0f);
    const float densityDecrease = std::clamp(baseParams.densityAmount - currentParams.densityAmount, 0.0f, 1.0f);

    std::vector<NoteLike> transformed;
    transformed.reserve(baseNotes.size());

    for (const auto& baseNote : baseNotes)
    {
        if (densityDecrease > 0.0f && !shouldKeepForDensity(authoring,
                                                            track,
                                                            baseNote,
                                                            densityDecrease,
                                                            laneProfile.densityWeight,
                                                            profile))
            continue;

        auto note = baseNote;
        const float support = supportFactor(track, baseNote);
        const float anchor = movementAnchorStrength(track, baseNote, profile);
        const float swingAllowance = std::clamp(1.0f - anchor * laneProfile.anchorSwingProtection * profile.anchorSwingTightness, 0.0f, 1.0f);
        const float timingAllowance = std::clamp(1.0f - anchor * laneProfile.anchorTimingProtection * profile.anchorTimingTightness, 0.08f, 1.0f);
        const float supportTimingScale = 1.0f + support * (laneProfile.supportTimingBoost - 1.0f);
        const float supportHumanizeScale = 1.0f + support * (laneProfile.supportHumanizeBoost - 1.0f);
        const float baseTimingScale = std::max(0.35f,
                               1.0f + timingDelta
                                  * profile.timingScaleAmount
                                  * laneProfile.timingScale
                                  * timingAllowance);

        int microOffset = static_cast<int>(std::lround(static_cast<float>(baseNote.microOffset) * baseTimingScale));

        const float timingNoise = biasedTimingNoise(stableUnit(trackType, baseNote, 17u), profile, support);
        const float humanizeNoise = biasedTimingNoise(stableUnit(trackType, baseNote, 43u), profile, support * 0.75f);

        if (std::abs(baseNote.microOffset) < 2)
        {
            microOffset += static_cast<int>(std::lround(timingDelta
                                                        * profile.zeroTimingTicks
                                                        * laneProfile.zeroTimingWeight
                                                        * timingAllowance
                                                        * supportTimingScale
                                                        * timingNoise));
        }

        const float swingPhase = swingPhaseWeight(track, baseNote, profile);
        if (swingPhase > 0.0f)
        {
            microOffset += static_cast<int>(std::lround(swingDelta
                                                        * profile.swingTicksPerPercent
                                                        * laneProfile.swingWeight
                                                        * laneProfile.lateSwingBias
                                                        * swingPhase
                                                        * swingAllowance));
        }

        microOffset += static_cast<int>(std::lround(humanizeDelta
                                                    * profile.humanizeTimingTicks
                                                    * laneProfile.humanizeTimingWeight
                                                    * timingAllowance
                                                    * supportHumanizeScale
                                                    * humanizeNoise));
        note.microOffset = clampMicroOffset(trackType, microOffset);

        const float accent = accentWeight(trackType, baseNote);
        const float velocityAllowance = std::clamp(1.0f - anchor * 0.48f, 0.30f, 1.0f);
        int velocity = baseNote.velocity;
        velocity += static_cast<int>(std::lround(velocityDelta
                                                 * profile.velocityRange
                                                 * laneProfile.velocityWeight
                                                 * profile.accentVelocityDepth
                                                 * velocityAllowance
                                                 * ((accent - 0.5f) * 2.0f)));
        velocity += static_cast<int>(std::lround(humanizeDelta
                                                 * profile.humanizeVelocityRange
                                                 * laneProfile.humanizeVelocityWeight
                                                 * velocityAllowance
                                                 * stableUnit(trackType, baseNote, 71u)));
        note.velocity = clampVelocity(baseNote, velocity);
        transformed.push_back(std::move(note));
    }

    visibleNotes = std::move(transformed);
}

inline void applySub808TransformSequence(std::vector<Sub808NoteEvent>& visibleNotes,
                                         const std::vector<Sub808NoteEvent>& baseNotes,
                                         const PatternAuthoringState& authoring,
                                         const TrackState& track,
                                         const GeneratorParams& currentParams,
                                         const GeneratorParams& baseParams,
                                         const GenrePerformanceProfile& profile)
{
    visibleNotes = baseNotes;
    if (visibleNotes.empty())
        return;

    const auto& laneProfile = profile.lanes[trackTypeIndex(TrackType::Sub808)];
    const float swingDelta = currentParams.swingPercent - baseParams.swingPercent;
    const float timingDelta = std::clamp(currentParams.timingAmount - baseParams.timingAmount, -1.0f, 1.0f);
    const float velocityDelta = std::clamp(currentParams.velocityAmount - baseParams.velocityAmount, -1.0f, 1.0f);
    const float humanizeDelta = std::clamp(currentParams.humanizeAmount - baseParams.humanizeAmount, -1.0f, 1.0f);
    const float densityDecrease = std::clamp(baseParams.densityAmount - currentParams.densityAmount, 0.0f, 1.0f);

    std::vector<Sub808NoteEvent> densityNotes = baseNotes;
    if (densityDecrease > 0.0f)
    {
        struct RankedSubNote
        {
            size_t index = 0;
            float priority = 0.0f;
            bool protect = false;
        };

        std::vector<RankedSubNote> ranked;
        ranked.reserve(baseNotes.size());

        int protectedCount = 0;
        for (size_t index = 0; index < baseNotes.size(); ++index)
        {
            DensityAuthoringInfo authoringInfo;
            const auto& note = baseNotes[index];
            const float priority = densityPriority(authoring, track, note, profile, &authoringInfo);
            const bool protect = authoringInfo.anchorLocked || priority >= 0.95f
                || metricAnchorStrength(track, note, profile) >= 0.92f;
            if (protect)
                ++protectedCount;

            ranked.push_back({ index, priority, protect });
        }

        const float keepRatio = 1.0f - std::clamp(densityDecrease * laneProfile.densityWeight, 0.0f, 0.95f);
        int targetCount = static_cast<int>(std::ceil(static_cast<float>(baseNotes.size()) * keepRatio));
        targetCount = std::max(targetCount, protectedCount > 0 ? protectedCount : 1);
        targetCount = std::min(targetCount, static_cast<int>(baseNotes.size()));

        if (targetCount < static_cast<int>(baseNotes.size()))
        {
            std::sort(ranked.begin(), ranked.end(), [&](const RankedSubNote& lhs, const RankedSubNote& rhs)
            {
                if (lhs.protect != rhs.protect)
                    return lhs.protect && !rhs.protect;
                if (!approximatelyEqual(lhs.priority, rhs.priority, 0.0001f))
                    return lhs.priority > rhs.priority;
                return baseNotes[lhs.index].step < baseNotes[rhs.index].step;
            });

            std::vector<bool> keep(baseNotes.size(), false);
            for (int rankIndex = 0; rankIndex < targetCount; ++rankIndex)
                keep[ranked[rankIndex].index] = true;

            std::vector<size_t> keptIndices;
            keptIndices.reserve(static_cast<size_t>(targetCount));
            for (size_t index = 0; index < keep.size(); ++index)
            {
                if (keep[index])
                    keptIndices.push_back(index);
            }

            densityNotes.clear();
            densityNotes.reserve(keptIndices.size());

            const int maxPatternSteps = std::max(currentParams.bars, baseParams.bars) * 16;
            for (size_t keptPosition = 0; keptPosition < keptIndices.size(); ++keptPosition)
            {
                const size_t noteIndex = keptIndices[keptPosition];
                auto note = baseNotes[noteIndex];

                const bool hasNextKept = keptPosition + 1 < keptIndices.size();
                int spanEnd = noteEndStep(note);

                const size_t endIndexExclusive = hasNextKept ? keptIndices[keptPosition + 1] : baseNotes.size();
                for (size_t index = noteIndex + 1; index < endIndexExclusive; ++index)
                    spanEnd = std::max(spanEnd, noteEndStep(baseNotes[index]));

                const bool preserveDirectLink = hasNextKept
                    && keptIndices[keptPosition + 1] == noteIndex + 1
                    && hasDirectSub808Link(baseNotes[noteIndex], baseNotes[keptIndices[keptPosition + 1]]);

                if (hasNextKept)
                {
                    const int nextStep = baseNotes[keptIndices[keptPosition + 1]].step;
                    spanEnd = std::max(spanEnd, nextStep);
                    spanEnd = std::min(spanEnd, nextStep + (preserveDirectLink ? 1 : 0));
                }

                spanEnd = std::min(spanEnd, maxPatternSteps);
                note.length = std::max(1, spanEnd - note.step);
                note.glideToNext = preserveDirectLink;
                note.isLegato = preserveDirectLink;
                note.isSlide = false;
                densityNotes.push_back(std::move(note));
            }

            for (size_t index = 1; index < densityNotes.size(); ++index)
                densityNotes[index].isSlide = densityNotes[index - 1].glideToNext;
        }
    }

    std::vector<Sub808NoteEvent> transformed;
    transformed.reserve(densityNotes.size());

    for (const auto& baseNote : densityNotes)
    {
        auto note = baseNote;
        const float support = supportFactor(track, baseNote);
        const float anchor = movementAnchorStrength(track, baseNote, profile);
        const float swingAllowance = std::clamp(1.0f - anchor * laneProfile.anchorSwingProtection * profile.anchorSwingTightness, 0.0f, 1.0f);
        const float timingAllowance = std::clamp(1.0f - anchor * laneProfile.anchorTimingProtection * profile.anchorTimingTightness, 0.08f, 1.0f);
        const float supportTimingScale = 1.0f + support * (laneProfile.supportTimingBoost - 1.0f);
        const float supportHumanizeScale = 1.0f + support * (laneProfile.supportHumanizeBoost - 1.0f);
        const float baseTimingScale = std::max(0.35f,
                                               1.0f + timingDelta
                                                   * profile.timingScaleAmount
                                                   * laneProfile.timingScale
                                                   * timingAllowance);

        int microOffset = static_cast<int>(std::lround(static_cast<float>(baseNote.microOffset) * baseTimingScale));

        const float timingNoise = biasedTimingNoise(stableUnit(TrackType::Sub808, baseNote, 17u), profile, support);
        const float humanizeNoise = biasedTimingNoise(stableUnit(TrackType::Sub808, baseNote, 43u), profile, support * 0.75f);

        if (std::abs(baseNote.microOffset) < 2)
        {
            microOffset += static_cast<int>(std::lround(timingDelta
                                                        * profile.zeroTimingTicks
                                                        * laneProfile.zeroTimingWeight
                                                        * timingAllowance
                                                        * supportTimingScale
                                                        * timingNoise));
        }

        const float swingPhase = swingPhaseWeight(track, baseNote, profile);
        if (swingPhase > 0.0f)
        {
            microOffset += static_cast<int>(std::lround(swingDelta
                                                        * profile.swingTicksPerPercent
                                                        * laneProfile.swingWeight
                                                        * laneProfile.lateSwingBias
                                                        * swingPhase
                                                        * swingAllowance));
        }

        microOffset += static_cast<int>(std::lround(humanizeDelta
                                                    * profile.humanizeTimingTicks
                                                    * laneProfile.humanizeTimingWeight
                                                    * timingAllowance
                                                    * supportHumanizeScale
                                                    * humanizeNoise));
        note.microOffset = clampMicroOffset(TrackType::Sub808, microOffset);

        const float accent = accentWeight(TrackType::Sub808, baseNote);
        const float velocityAllowance = std::clamp(1.0f - anchor * 0.48f, 0.30f, 1.0f);
        int velocity = baseNote.velocity;
        velocity += static_cast<int>(std::lround(velocityDelta
                                                 * profile.velocityRange
                                                 * laneProfile.velocityWeight
                                                 * profile.accentVelocityDepth
                                                 * velocityAllowance
                                                 * ((accent - 0.5f) * 2.0f)));
        velocity += static_cast<int>(std::lround(humanizeDelta
                                                 * profile.humanizeVelocityRange
                                                 * laneProfile.humanizeVelocityWeight
                                                 * velocityAllowance
                                                 * stableUnit(TrackType::Sub808, baseNote, 71u)));
        note.velocity = clampVelocity(baseNote, velocity);
        transformed.push_back(std::move(note));
    }

    visibleNotes = std::move(transformed);
}
} // namespace detail

inline bool hasLivePerformanceParamChange(const GeneratorParams& previousParams, const GeneratorParams& nextParams)
{
    return !detail::approximatelyEqual(previousParams.swingPercent, nextParams.swingPercent, 0.05f)
        || !detail::approximatelyEqual(previousParams.velocityAmount, nextParams.velocityAmount, 0.005f)
        || !detail::approximatelyEqual(previousParams.timingAmount, nextParams.timingAmount, 0.005f)
        || !detail::approximatelyEqual(previousParams.humanizeAmount, nextParams.humanizeAmount, 0.005f)
        || !detail::approximatelyEqual(previousParams.densityAmount, nextParams.densityAmount, 0.005f);
}

inline bool hasPlaybackTimingParamChange(const GeneratorParams& previousParams, const GeneratorParams& nextParams)
{
    return previousParams.syncDawTempo != nextParams.syncDawTempo
        || !detail::approximatelyEqual(previousParams.bpm, nextParams.bpm, 0.01f);
}

inline void captureBasePattern(TrackState& track)
{
    if (track.type == TrackType::Sub808)
    {
        const auto visibleSub808Notes = track.sub808Notes.empty() ? toSub808NoteEvents(track.notes) : track.sub808Notes;
        track.baseSub808Notes = visibleSub808Notes;
        track.baseNotes = toLegacyNoteEvents(track.baseSub808Notes);
        return;
    }

    track.baseNotes = track.notes;
    track.baseSub808Notes.clear();
}

inline void captureBasePattern(TrackState& track, const GeneratorParams& params)
{
    captureBasePattern(track);
    track.performanceBaseParams = params;
    track.hasPerformanceBaseParams = true;
}

inline void captureBasePatterns(PatternProject& project, const std::unordered_set<TrackType>& trackTypes)
{
    for (auto& track : project.tracks)
    {
        if (trackTypes.count(track.type) == 0 || track.locked)
            continue;

        captureBasePattern(track, project.params);
    }
}

inline void backfillMissingPerformanceBaseParams(PatternProject& project, const GeneratorParams& params)
{
    for (auto& track : project.tracks)
    {
        if (track.hasPerformanceBaseParams)
            continue;

        const bool hasVisibleNotes = track.type == TrackType::Sub808 ? !track.sub808Notes.empty() : !track.notes.empty();
        const bool hasBaseNotes = track.type == TrackType::Sub808 ? !track.baseSub808Notes.empty() : !track.baseNotes.empty();
        if (!hasVisibleNotes && !hasBaseNotes)
            continue;

        if (!hasBaseNotes)
            captureBasePattern(track);

        track.performanceBaseParams = params;
        track.hasPerformanceBaseParams = true;
    }
}

inline void applyPerformanceFromBase(PatternProject& project, const GeneratorParams& currentParams)
{
    backfillMissingPerformanceBaseParams(project, currentParams);

    for (auto& track : project.tracks)
    {
        if (track.locked || !track.hasPerformanceBaseParams)
            continue;

        const auto profile = getGenrePerformanceProfile(track.performanceBaseParams);

        if (track.type == TrackType::Sub808)
        {
            detail::applySub808TransformSequence(track.sub808Notes,
                                                 track.baseSub808Notes,
                                                 project.authoring,
                                                 track,
                                                 currentParams,
                                                 track.performanceBaseParams,
                                                 profile);
            track.notes = toLegacyNoteEvents(track.sub808Notes);
            continue;
        }

        detail::applyTransformSequence(track.notes,
                                       track.baseNotes,
                                       project.authoring,
                                       track,
                                       currentParams,
                                       track.performanceBaseParams,
                                       profile);
    }
}

inline void applyPerformanceFromBase(PatternProject& project)
{
    applyPerformanceFromBase(project, project.params);
}
} // namespace bbg::PatternPerformanceTransformEngine