#pragma once

#include <random>

#include "DrillPhrasePlanner.h"

namespace bbg
{
class DrillHatFxGenerator
{
public:
    void generate(TrackState& track,
                  const TrackState& hiHatTrack,
                  const PatternProject& project,
                  const DrillPhrasePlan& phrasePlan,
                  std::mt19937& rng) const;
};
} // namespace bbg