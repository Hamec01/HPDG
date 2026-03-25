#pragma once

#include <vector>

#include "PatternProject.h"

namespace bbg
{
namespace RuntimeLaneLifecycle
{
RuntimeLaneDefinition createCustomLaneDefinition(const PatternProject& project, const juce::String& preferredName = {});
std::vector<TrackType> listAvailableRegistryLaneTypes(const PatternProject& project);
bool addLane(PatternProject& project, RuntimeLaneDefinition lane, int insertIndex = -1);
bool addRegistryLane(PatternProject& project, TrackType type, int insertIndex = -1);
bool renameLane(PatternProject& project, const RuntimeLaneId& laneId, const juce::String& newLaneName);
bool deleteLane(PatternProject& project, const RuntimeLaneId& laneId);
bool deleteAllLanes(PatternProject& project);
}
} // namespace bbg