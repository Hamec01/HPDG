#pragma once

#include <random>

#include "DrillPhrasePlanner.h"

namespace bbg
{
class DrillHatGenerator
{
public:
    void generate(TrackState& track,
                  const PatternProject& project,
                  const DrillPhrasePlan& phrasePlan,
                  std::mt19937& rng) const;
};
} // namespace bbg