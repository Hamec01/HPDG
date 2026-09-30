#pragma once

#include <unordered_set>

#include "GenreEngine.h"
#include "Trap/TrapAlgebraEngine.h"
#include "Trap/TrapStyleProfile.h"

namespace bbg
{
class TrapEngine final : public GenreEngine
{
public:
    TrapEngine();

    void generate(PatternProject& project) override;
    void regenerateTrack(PatternProject& project, TrackType trackType) override;
    void generateTrackNew(PatternProject& project, TrackType trackType);
    void regenerateTrackVariation(PatternProject& project, TrackType trackType);
    void mutatePattern(PatternProject& project);
    void mutateTrack(PatternProject& project, TrackType trackType);

private:
    void validatePattern(PatternProject& project, const std::unordered_set<TrackType>& mutableTracks) const;
    void applyTrapAlgebraPattern(PatternProject& project,
                                 const TrapAlgebraPattern& pattern,
                                 const std::unordered_set<TrackType>& mutableTracks) const;
};
} // namespace bbg
