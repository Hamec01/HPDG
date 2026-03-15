#pragma once

#include <random>
#include <vector>

#include "../../Core/GeneratorParams.h"
#include "BoomBapGrooveBlueprint.h"
#include "../../Core/TrackState.h"
#include "BoomBapPhrasePlanner.h"
#include "BoomBapStyleProfile.h"

namespace bbg
{
class BoomBapHatGenerator
{
public:
    void generate(TrackState& track,
                  const GeneratorParams& params,
                  const BoomBapStyleProfile& style,
                  const std::vector<PhraseRole>& phraseRoles,
                  const BoomBapGrooveBlueprint& blueprint,
                  std::mt19937& rng) const;
};
} // namespace bbg
