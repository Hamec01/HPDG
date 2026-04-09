#pragma once

#include <random>

#include "DrillPhrasePlanner.h"

namespace bbg
{
class DrillKickGenerator
{
public:
    void generate(TrackState& kickTrack,
                  const PatternProject& project,
                  const DrillPhrasePlan& phrasePlan,
                  const TrackState* snareTrack,
                  std::mt19937& rng) const;
};
} // namespace bbg