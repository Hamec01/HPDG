#pragma once

#include <random>
#include <unordered_set>
#include <vector>

#include "GenreEngine.h"
#include "Rap/RapHatGenerator.h"
#include "Rap/RapKickGenerator.h"
#include "Rap/RapPhrasePlanner.h"
#include "Rap/RapSnareGenerator.h"
#include "Rap/RapStyleProfile.h"

namespace bbg
{
class RapEngine final : public GenreEngine
{
public:
    RapEngine();

    void generate(PatternProject& project) override;
    void regenerateTrack(PatternProject& project, TrackType trackType) override;
    void generateTrackNew(PatternProject& project, TrackType trackType);
    void regenerateTrackVariation(PatternProject& project, TrackType trackType);
    void mutatePattern(PatternProject& project);
    void mutateTrack(PatternProject& project, TrackType trackType);

private:
    void regenerateTrackInternal(PatternProject& project,
                                 TrackState& track,
                                 const RapStyleProfile& style,
                                 const std::vector<RapPhraseRole>& phrasePlan,
                                 std::mt19937& rng) const;
    void generateDependentTracks(PatternProject& project,
                                 const RapStyleProfile& style,
                                 const std::vector<RapPhraseRole>& phrasePlan,
                                 std::mt19937& rng,
                                 const std::unordered_set<TrackType>& mutableTracks) const;
    void postProcess(PatternProject& project,
                     const RapStyleProfile& style,
                     std::mt19937& rng,
                     const std::unordered_set<TrackType>& mutableTracks) const;
    void validatePattern(PatternProject& project, const std::unordered_set<TrackType>& mutableTracks) const;

    RapKickGenerator kickGenerator;
    RapSnareGenerator snareGenerator;
    RapHatGenerator hatGenerator;
};
} // namespace bbg
