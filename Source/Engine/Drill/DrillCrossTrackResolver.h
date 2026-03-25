#pragma once

#include <unordered_set>

#include "DrillGrooveBlueprint.h"
#include "DrillStyleProfile.h"
#include "../../Core/PatternProject.h"

namespace bbg
{
class DrillCrossTrackResolver
{
public:
    void resolve(PatternProject& project,
                 const DrillStyleProfile& style,
                 const DrillGrooveBlueprint& blueprint,
                 const std::unordered_set<TrackType>& mutableTracks) const;
};
} // namespace bbg
