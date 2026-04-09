#pragma once

#include <unordered_set>

#include "Drill/Drill808Generator.h"
#include "Drill/DrillHatFxGenerator.h"
#include "Drill/DrillHatGenerator.h"
#include "Drill/DrillKickGenerator.h"
#include "Drill/DrillPatternValidator.h"
#include "Drill/DrillPhrasePlanner.h"
#include "Drill/DrillSnareGenerator.h"
#include "GenreEngine.h"

namespace bbg
{
class DrillEngine final : public GenreEngine
{
public:
    DrillEngine() = default;

    void generate(PatternProject& project) override;
    void regenerateTrack(PatternProject& project, TrackType trackType) override;
    void generateTrackNew(PatternProject& project, TrackType trackType);
    void regenerateTrackVariation(PatternProject& project, TrackType trackType);
    void mutatePattern(PatternProject& project);
    void mutateTrack(PatternProject& project, TrackType trackType);

private:
    void applyPhrasePlan(PatternProject& project, const DrillPhrasePlan& plan) const;
    void runGenerationPass(PatternProject& project,
                           const std::unordered_set<TrackType>& mutableTracks,
                           int seedSalt) const;
    static std::unordered_set<TrackType> expandMutableTracks(TrackType trackType);

    Drill808Generator subGenerator;
    DrillHatFxGenerator hatFxGenerator;
    DrillHatGenerator hatGenerator;
    DrillKickGenerator kickGenerator;
    DrillPatternValidator validator;
    DrillSnareGenerator snareGenerator;
};
} // namespace bbg