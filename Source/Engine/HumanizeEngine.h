#pragma once

#include <random>
#include <unordered_set>

#include "../Core/PatternProject.h"
#include "BoomBap/BoomBapStyleProfile.h"

namespace bbg
{
class HumanizeEngine
{
public:
    static void applyHumanize(PatternProject& project,
                              const BoomBapStyleProfile& style,
                              const std::unordered_set<TrackType>& mutableTracks,
                              std::mt19937& rng);
};
} // namespace bbg
