#pragma once

#include <array>
#include <vector>

#include "TrackInfo.h"
#include "TrackState.h"

namespace bbg
{
class TrackRegistry
{
public:
    static const std::array<TrackInfo, 11>& all();
    static const TrackInfo* find(TrackType type);
    static std::vector<TrackState> createDefaultTrackStates();
};
} // namespace bbg
