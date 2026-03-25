#pragma once

#include <random>
#include <vector>

#include "RapPhrasePlanner.h"
#include "RapStyleProfile.h"
#include "../../Core/GeneratorParams.h"
#include "../../Core/TrackState.h"

namespace bbg
{
struct StyleInfluenceState;

class RapKickGenerator
{
public:
    void generate(TrackState& track,
                  const GeneratorParams& params,
                  const RapStyleProfile& style,
                  const StyleInfluenceState& styleInfluence,
                  const std::vector<RapPhraseRole>& phraseRoles,
                  std::mt19937& rng) const;
};
} // namespace bbg
