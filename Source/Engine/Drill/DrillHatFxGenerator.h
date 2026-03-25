#pragma once

#include <random>
#include <vector>

#include "DrillGrooveBlueprint.h"
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
                  const TrackState* kickTrack,
                  const TrackState* snareTrack,
                  const TrackState* openHatTrack,
                  const TrackState* subTrack,
                  const DrillStyleProfile& style,
                  float fxIntensity,
                  const std::vector<DrillPhraseRole>& phrase,
                  const DrillGrooveBlueprint* blueprint,
                  std::mt19937& rng) const;
};
} // namespace bbg
