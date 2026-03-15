#pragma once

#include <random>
#include <vector>

#include "../../Core/TrackState.h"
#include "BoomBapPhrasePlanner.h"
#include "BoomBapStyleProfile.h"

namespace bbg
{
class BoomBapGhostGenerator
{
public:
    void generateGhostKick(TrackState& ghostKickTrack,
                           const TrackState& kickTrack,
                           const BoomBapStyleProfile& style,
                           float density,
                           const std::vector<PhraseRole>& phraseRoles,
                           std::mt19937& rng) const;

    void generateClapLayer(TrackState& clapTrack,
                           const TrackState& snareTrack,
                           const BoomBapStyleProfile& style,
                           const std::vector<PhraseRole>& phraseRoles,
                           std::mt19937& rng) const;
};
} // namespace bbg
