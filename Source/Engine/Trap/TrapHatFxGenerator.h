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
                  TrackState& hatTrack,
                  const TrackState* kickTrack,
                  const TrackState* snareTrack,
                  const TrackState* openHatTrack,
                  const TrackState* subTrack,
                  const TrapStyleProfile& style,
                  float fxIntensity,
                  const std::vector<TrapPhraseRole>& phrase,
                  std::mt19937& rng) const;
};
} // namespace bbg
