#pragma once

#include <optional>

#include "PatternProject.h"

namespace bbg
{
namespace ProjectLaneAccess
{
inline RuntimeLaneDefinition* findLaneDefinition(PatternProject& project, const RuntimeLaneId& laneId)
{
    for (auto& lane : project.runtimeLaneProfile.lanes)
    {
        if (lane.laneId == laneId)
            return &lane;
    }

    return nullptr;
}

inline const RuntimeLaneDefinition* findLaneDefinition(const PatternProject& project, const RuntimeLaneId& laneId)
{
    for (const auto& lane : project.runtimeLaneProfile.lanes)
    {
        if (lane.laneId == laneId)
            return &lane;
    }

    return nullptr;
}

inline RuntimeLaneDefinition* findLaneDefinitionForTrack(PatternProject& project, TrackType type)
{
    for (auto& lane : project.runtimeLaneProfile.lanes)
    {
        if (lane.runtimeTrackType.has_value() && *lane.runtimeTrackType == type)
            return &lane;
    }

    return nullptr;
}

inline const RuntimeLaneDefinition* findLaneDefinitionForTrack(const PatternProject& project, TrackType type)
{
    for (const auto& lane : project.runtimeLaneProfile.lanes)
    {
        if (lane.runtimeTrackType.has_value() && *lane.runtimeTrackType == type)
            return &lane;
    }

    return nullptr;
}

inline TrackState* findTrackState(PatternProject& project, TrackType type)
{
    for (auto& track : project.tracks)
    {
        if (track.type == type)
            return &track;
    }

    return nullptr;
}

inline const TrackState* findTrackState(const PatternProject& project, TrackType type)
{
    for (const auto& track : project.tracks)
    {
        if (track.type == type)
            return &track;
    }

    return nullptr;
}

inline std::optional<TrackType> backingTrackTypeForLaneId(const PatternProject& project, const RuntimeLaneId& laneId)
{
    const auto* lane = findLaneDefinition(project, laneId);
    if (lane == nullptr || !lane->runtimeTrackType.has_value())
        return std::nullopt;

    return lane->runtimeTrackType;
}

inline bool hasBackingTrackType(const PatternProject& project, const RuntimeLaneId& laneId)
{
    return backingTrackTypeForLaneId(project, laneId).has_value();
}

inline bool isBackedLane(const PatternProject& project, const RuntimeLaneId& laneId)
{
    return hasBackingTrackType(project, laneId);
}

inline bool isUnbackedLane(const PatternProject& project, const RuntimeLaneId& laneId)
{
    const auto* lane = findLaneDefinition(project, laneId);
    return lane != nullptr && !lane->runtimeTrackType.has_value();
}

inline RuntimeLaneId canonicalLaneIdForTrack(const PatternProject& project, TrackType type)
{
    if (const auto* lane = findLaneDefinitionForTrack(project, type))
        return lane->laneId;

    return TrackRegistry::defaultRuntimeLaneId(type);
}

inline TrackState* findTrackState(PatternProject& project, const RuntimeLaneId& laneId)
{
    for (auto& track : project.tracks)
    {
        if (track.laneId.isNotEmpty() && track.laneId == laneId)
            return &track;
    }

    if (const auto type = backingTrackTypeForLaneId(project, laneId); type.has_value())
        return findTrackState(project, *type);

    return nullptr;
}

inline const TrackState* findTrackState(const PatternProject& project, const RuntimeLaneId& laneId)
{
    for (const auto& track : project.tracks)
    {
        if (track.laneId.isNotEmpty() && track.laneId == laneId)
            return &track;
    }

    if (const auto type = backingTrackTypeForLaneId(project, laneId); type.has_value())
        return findTrackState(project, *type);

    return nullptr;
}
}
} // namespace bbg