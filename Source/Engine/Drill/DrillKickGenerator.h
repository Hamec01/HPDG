#pragma once

#include <random>
#include <vector>

#include "DrillPhrasePlanner.h"
#include "DrillStyleProfile.h"
#include "DrillGrooveBlueprint.h"
#include "../../Core/GeneratorParams.h"
#include "../../Core/TrackState.h"

namespace bbg
{
struct StyleInfluenceState;

class DrillKickGenerator
{
public:
    void generate(TrackState& track,
                  const GeneratorParams& params,
                  const DrillStyleProfile& style,
                  const StyleInfluenceState& styleInfluence,
                  const std::vector<DrillPhraseRole>& phrase,
                  const DrillGrooveBlueprint* blueprint,
                  std::mt19937& rng) const;
};
} // namespace bbg
