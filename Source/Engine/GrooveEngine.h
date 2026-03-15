#pragma once

#include <unordered_set>

#include "../Core/PatternProject.h"
#include "BoomBap/BoomBapStyleProfile.h"

namespace bbg
{
class GrooveEngine
{
public:
    static void applySwing(PatternProject& project,
                           const BoomBapStyleProfile& style,
                           const std::unordered_set<TrackType>& mutableTracks);
};
} // namespace bbg
