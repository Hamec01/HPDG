#pragma once

#include <random>

#include "DrillPhrasePlanner.h"

namespace bbg
{
class DrillSnareGenerator
{
public:
    void generate(TrackState& snareTrack,
                  TrackState* clapGhostTrack,
                  const PatternProject& project,
                  const DrillPhrasePlan& phrasePlan,
                  std::mt19937& rng) const;
};
} // namespace bbg