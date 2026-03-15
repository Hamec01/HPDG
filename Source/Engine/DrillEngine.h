#pragma once

#include <random>
#include <unordered_set>
#include <vector>

#include "GenreEngine.h"
#include "Drill/Drill808Generator.h"
#include "Drill/DrillHatFxGenerator.h"
#include "Drill/DrillHatGenerator.h"
#include "Drill/DrillKickGenerator.h"
#include "Drill/DrillPhrasePlanner.h"
#include "Drill/DrillSnareGenerator.h"
#include "Drill/DrillStyleProfile.h"

namespace bbg
{
class DrillEngine final : public GenreEngine
{
public:
    DrillEngine();

    void generate(PatternProject& project) override;
    void regenerateTrack(PatternProject& project, TrackType trackType) override;
    void generateTrackNew(PatternProject& project, TrackType trackType);
    void regenerateTrackVariation(PatternProject& project, TrackType trackType);
    void mutatePattern(PatternProject& project);
    void mutateTrack(PatternProject& project, TrackType trackType);

private:
    void regenerateTrackInternal(PatternProject& project,
                                 TrackState& track,
                                 const DrillStyleProfile& style,
                                 const std::vector<DrillPhraseRole>& phrase,
                                 std::mt19937& rng) const;

    void generateDrillSnareBackbone(PatternProject& project,
                                    const DrillStyleProfile& style,
                                    const std::vector<DrillPhraseRole>& phrase,
                                    std::mt19937& rng,
                                    const std::unordered_set<TrackType>& mutableTracks) const;
    void generateDrillHiHatStructure(PatternProject& project,
                                     const DrillStyleProfile& style,
                                     const std::vector<DrillPhraseRole>& phrase,
                                     std::mt19937& rng,
                                     const std::unordered_set<TrackType>& mutableTracks) const;
    void generateDrillKickSkeleton(PatternProject& project,
                                   const DrillStyleProfile& style,
                                   const std::vector<DrillPhraseRole>& phrase,
                                   std::mt19937& rng,
                                   const std::unordered_set<TrackType>& mutableTracks) const;
    void generateSub808RhythmFromDrillKicks(PatternProject& project,
                                            const DrillStyleProfile& style,
                                            const DrillStyleSpec& spec,
                                            const std::vector<DrillPhraseRole>& phrase,
                                            std::mt19937& rng,
                                            const std::unordered_set<TrackType>& mutableTracks) const;
    void assignDrillSub808Pitches(PatternProject& project,
                                  const DrillStyleProfile& style,
                                  const DrillStyleSpec& spec,
                                  const std::vector<DrillPhraseRole>& phrase,
                                  std::mt19937& rng,
                                  const std::unordered_set<TrackType>& mutableTracks) const;
    void applyDrillSub808Slides(PatternProject& project,
                                const DrillStyleSpec& spec,
                                const std::vector<DrillPhraseRole>& phrase,
                                std::mt19937& rng,
                                const std::unordered_set<TrackType>& mutableTracks) const;
    void generateDrillHatFX(PatternProject& project,
                            const DrillStyleProfile& style,
                            const std::vector<DrillPhraseRole>& phrase,
                            std::mt19937& rng,
                            const std::unordered_set<TrackType>& mutableTracks) const;
    void generateDrillSupportLanes(PatternProject& project,
                                   const DrillStyleProfile& style,
                                   const DrillStyleSpec& spec,
                                   const std::vector<DrillPhraseRole>& phrase,
                                   std::mt19937& rng,
                                   const std::unordered_set<TrackType>& mutableTracks) const;
    void applyDrillHumanization(PatternProject& project,
                                const DrillStyleProfile& style,
                                std::mt19937& rng,
                                const std::unordered_set<TrackType>& mutableTracks) const;
    void validatePattern(PatternProject& project, const std::unordered_set<TrackType>& mutableTracks) const;

    DrillKickGenerator kickGenerator;
    DrillSnareGenerator snareGenerator;
    DrillHatGenerator hatGenerator;
    DrillHatFxGenerator hatFxGenerator;
    Drill808Generator subGenerator;
};
} // namespace bbg
