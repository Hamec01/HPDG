#pragma once

#include <random>
#include <unordered_set>
#include <vector>

#include "GenreEngine.h"
#include "Trap/Trap808Generator.h"
#include "Trap/TrapHatFxGenerator.h"
#include "Trap/TrapHatGenerator.h"
#include "Trap/TrapKickGenerator.h"
#include "Trap/TrapPhrasePlanner.h"
#include "Trap/TrapSnareGenerator.h"
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
    void regenerateTrackInternal(PatternProject& project,
                                 TrackState& track,
                                 const TrapStyleProfile& style,
                                 const std::vector<TrapPhraseRole>& phrase,
                                 std::mt19937& rng) const;

    void generateTrapSnareBackbone(PatternProject& project,
                                   const TrapStyleProfile& style,
                                   const std::vector<TrapPhraseRole>& phrase,
                                   std::mt19937& rng,
                                   const std::unordered_set<TrackType>& mutableTracks) const;
    void generateTrapHiHatScaffold(PatternProject& project,
                                   const TrapStyleProfile& style,
                                   const std::vector<TrapPhraseRole>& phrase,
                                   std::mt19937& rng,
                                   const std::unordered_set<TrackType>& mutableTracks) const;
    void generateTrapKickSkeleton(PatternProject& project,
                                  const TrapStyleProfile& style,
                                  const std::vector<TrapPhraseRole>& phrase,
                                  std::mt19937& rng,
                                  const std::unordered_set<TrackType>& mutableTracks) const;
    void generateSub808RhythmFromTrapKicks(PatternProject& project,
                                           const TrapStyleProfile& style,
                                           const TrapStyleSpec& spec,
                                           const std::vector<TrapPhraseRole>& phrase,
                                           std::mt19937& rng,
                                           const std::unordered_set<TrackType>& mutableTracks) const;
    void assignTrapSub808Pitches(PatternProject& project,
                                 const TrapStyleProfile& style,
                                 const TrapStyleSpec& spec,
                                 const std::vector<TrapPhraseRole>& phrase,
                                 std::mt19937& rng,
                                 const std::unordered_set<TrackType>& mutableTracks) const;
    void applyTrapSub808Slides(PatternProject& project,
                               const TrapStyleProfile& style,
                               const TrapStyleSpec& spec,
                               const std::vector<TrapPhraseRole>& phrase,
                               std::mt19937& rng,
                               const std::unordered_set<TrackType>& mutableTracks) const;
    void generateTrapHatFX(PatternProject& project,
                           const TrapStyleProfile& style,
                           const std::vector<TrapPhraseRole>& phrase,
                           std::mt19937& rng,
                           const std::unordered_set<TrackType>& mutableTracks) const;
    void generateTrapSupportLanes(PatternProject& project,
                                  const TrapStyleProfile& style,
                                  const TrapStyleSpec& spec,
                                  const std::vector<TrapPhraseRole>& phrase,
                                  std::mt19937& rng,
                                  const std::unordered_set<TrackType>& mutableTracks) const;
    void applyTrapHumanization(PatternProject& project,
                               const TrapStyleProfile& style,
                               std::mt19937& rng,
                               const std::unordered_set<TrackType>& mutableTracks) const;
    void validatePattern(PatternProject& project, const std::unordered_set<TrackType>& mutableTracks) const;

    TrapKickGenerator kickGenerator;
    TrapSnareGenerator snareGenerator;
    TrapHatGenerator hatGenerator;
    TrapHatFxGenerator hatFxGenerator;
    Trap808Generator subGenerator;
};
} // namespace bbg
