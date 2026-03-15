#pragma once

#include <unordered_set>

#include "../Core/PatternProject.h"
#include "BoomBap/BoomBapStyleProfile.h"

namespace bbg
{
class VelocityEngine
{
public:
    static void applyVelocityShape(PatternProject& project,
                                   const BoomBapStyleProfile& style,
                                   const std::unordered_set<TrackType>& mutableTracks);
};
} // namespace bbg
