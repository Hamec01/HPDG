#pragma once

#include <array>
#include <vector>

#include "RuntimeLaneProfile.h"
#include "TrackInfo.h"
#include "TrackState.h"

namespace bbg
{
class TrackRegistry
{
public:
    static const std::array<TrackInfo, 11>& all();
    static const TrackInfo* find(TrackType type);
    static juce::String defaultRuntimeLaneId(TrackType type);
    static RuntimeLaneProfile createDefaultRuntimeLaneProfile();
    static std::vector<TrackState> createDefaultTrackStates();
    static std::vector<TrackState> createDefaultTrackStates(const RuntimeLaneProfile& profile);
};
} // namespace bbg
