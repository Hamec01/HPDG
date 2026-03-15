#pragma once

#include <random>
#include <vector>

#include "DrillPhrasePlanner.h"
#include "DrillStyleProfile.h"
#include "../../Core/GeneratorParams.h"
#include "../../Core/TrackState.h"

namespace bbg
{
class DrillSnareGenerator
{
public:
    void generate(TrackState& track,
                  const GeneratorParams& params,
                  const DrillStyleProfile& style,
                  const std::vector<DrillPhraseRole>& phrase,
                  std::mt19937& rng) const;
};
} // namespace bbg
