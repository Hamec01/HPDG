#pragma once

#include <random>

#include "DrillPhrasePlanner.h"

namespace bbg
{
class Drill808Generator
{
public:
    void generate(TrackState& subTrack,
                  const TrackState& kickTrack,
                  const PatternProject& project,
                  const DrillPhrasePlan& phrasePlan,
                  const TrackState* snareTrack,
                  std::mt19937& rng) const;
};
} // namespace bbg