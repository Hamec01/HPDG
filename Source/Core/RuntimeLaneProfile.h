#pragma once

#include <optional>
#include <vector>

#include <juce_core/juce_core.h>

#include "LaneEditorCapabilities.h"
#include "TrackInfo.h"

namespace bbg
{
using RuntimeLaneId = juce::String;

struct RuntimeLaneDefinition
{
    RuntimeLaneId laneId;
    juce::String laneName;
    juce::String groupName;
    juce::String dependencyName;
    int generationPriority = 50;
    bool isCore = true;
    bool isVisibleInEditor = true;
    bool enabledByDefault = true;
    bool supportsDragExport = true;
    bool isGhostTrack = false;
    int defaultMidiNote = 36;
    bool isRuntimeRegistryLane = false;
    std::optional<TrackType> runtimeTrackType;
    LaneEditorCapabilities editorCapabilities;
};

struct RuntimeLaneProfile
{
    juce::String genre;
    juce::String substyle;
    std::vector<RuntimeLaneDefinition> lanes;
};

inline const RuntimeLaneDefinition* findRuntimeLaneById(const RuntimeLaneProfile& profile, const RuntimeLaneId& laneId)
{
    for (const auto& lane : profile.lanes)
    {
        if (lane.laneId == laneId)
            return &lane;
    }

    return nullptr;
}

inline const RuntimeLaneDefinition* findRuntimeLaneForTrack(const RuntimeLaneProfile& profile, TrackType type)
{
    for (const auto& lane : profile.lanes)
    {
        if (lane.runtimeTrackType.has_value() && *lane.runtimeTrackType == type)
            return &lane;
    }

    return nullptr;
}
} // namespace bbg