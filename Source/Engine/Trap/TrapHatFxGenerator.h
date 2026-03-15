#pragma once

#include <random>
#include <vector>

#include "TrapPhrasePlanner.h"
#include "TrapStyleProfile.h"
#include "../../Core/TrackState.h"

namespace bbg
{
class TrapHatFxGenerator
{
public:
    void generate(TrackState& hatFxTrack,
                  const TrackState& hatTrack,
                  const TrapStyleProfile& style,
                  float fxIntensity,
                  const std::vector<TrapPhraseRole>& phrase,
                  std::mt19937& rng) const;
};
} // namespace bbg
