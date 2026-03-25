#pragma once

#include <random>
#include <vector>

#include "TrapPhrasePlanner.h"
#include "TrapStyleProfile.h"
#include "../../Core/GeneratorParams.h"
#include "../../Core/TrackState.h"

namespace bbg
{
struct StyleInfluenceState;

class TrapKickGenerator
{
public:
    void generate(TrackState& track,
                  const GeneratorParams& params,
                  const TrapStyleProfile& style,
                  const StyleInfluenceState& styleInfluence,
                  const std::vector<TrapPhraseRole>& phrase,
                  std::mt19937& rng) const;
};
} // namespace bbg
