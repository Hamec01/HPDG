#pragma once

#include <unordered_set>

#include "DrillPhrasePlanner.h"

namespace bbg
{
class DrillPatternValidator
{
public:
    void validate(PatternProject& project,
                  const DrillPhrasePlan& phrasePlan,
                  const std::unordered_set<TrackType>& mutableTracks) const;
};
} // namespace bbg