#pragma once

#include "TrackRowComponent.h"

namespace bbg
{
class LaneHeaderComponent final : public TrackRowComponent
{
public:
    explicit LaneHeaderComponent(const RuntimeLaneRowState& state)
        : TrackRowComponent(state)
    {
    }
};
} // namespace bbg