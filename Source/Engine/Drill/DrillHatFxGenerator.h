#pragma once

#include <random>
#include <vector>

#include "DrillPhrasePlanner.h"
#include "DrillStyleProfile.h"
#include "../../Core/TrackState.h"

namespace bbg
{
class DrillHatFxGenerator
{
public:
    void generate(TrackState& hatFxTrack,
                  const TrackState& hatTrack,
                  const DrillStyleProfile& style,
                  float fxIntensity,
                  const std::vector<DrillPhraseRole>& phrase,
                  std::mt19937& rng) const;
};
} // namespace bbg
