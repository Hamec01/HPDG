#pragma once

#include <random>
#include <vector>

#include "../../Core/GeneratorParams.h"
#include "../../Core/TrackState.h"
#include "BoomBapPhrasePlanner.h"
#include "BoomBapStyleProfile.h"

namespace bbg
{
class BoomBapOpenHatGenerator
{
public:
    void generate(TrackState& track,
                  const TrackState& hatTrack,
                  const GeneratorParams& params,
                  const BoomBapStyleProfile& style,
                  const std::vector<PhraseRole>& phraseRoles,
                  std::mt19937& rng) const;
};
} // namespace bbg
