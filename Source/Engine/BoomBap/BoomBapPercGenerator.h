#pragma once

#include <random>
#include <vector>

#include "../../Core/GeneratorParams.h"
#include "../../Core/TrackState.h"
#include "BoomBapPhrasePlanner.h"
#include "BoomBapStyleProfile.h"

namespace bbg
{
struct StyleInfluenceState;

class BoomBapPercGenerator
{
public:
    void generate(TrackState& track,
                  const GeneratorParams& params,
                  const BoomBapStyleProfile& style,
                  const StyleInfluenceState& styleInfluence,
                  const std::vector<PhraseRole>& phraseRoles,
                  std::mt19937& rng) const;
};
} // namespace bbg
