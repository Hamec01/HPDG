#pragma once

#include <random>
#include <unordered_set>
#include <vector>

#include <juce_core/juce_core.h>

#include "GenreEngine.h"
#include "BoomBap/BoomBapGhostGenerator.h"
#include "BoomBap/BoomBapHatGenerator.h"
#include "BoomBap/BoomBapKickGenerator.h"
#include "BoomBap/BoomBapOpenHatGenerator.h"
#include "BoomBap/BoomBapPercGenerator.h"
#include "BoomBap/BoomBapPhrasePlanner.h"
#include "BoomBap/BoomBapSnareGenerator.h"
#include "BoomBap/BoomBapStyleProfile.h"

namespace bbg
{
class BoomBapEngine final : public GenreEngine
{
public:
    BoomBapEngine();

    void generate(PatternProject& project) override;
    void regenerateTrack(PatternProject& project, TrackType trackType) override;
    void generateTrackNew(PatternProject& project, TrackType trackType);
    void regenerateTrackVariation(PatternProject& project, TrackType trackType);
    void mutatePattern(PatternProject& project);
    void mutateTrack(PatternProject& project, TrackType trackType);

private:
    struct GrooveContext
    {
        CarrierMode carrierMode = CarrierMode::Hat;
        bool halfTimeReference = false;
        float phraseVariationAmount = 0.3f;
    };

    GrooveContext buildGrooveContext(const PatternProject& project,
                                     const BoomBapStyleProfile& style,
                                     std::mt19937& rng) const;
    void applyCarrierMode(PatternProject& project,
                          const BoomBapStyleProfile& style,
                          const std::vector<PhraseRole>& phrasePlan,
                          const GrooveContext& grooveContext,
                          std::mt19937& rng,
                          const std::unordered_set<TrackType>& mutableTracks) const;

    void regenerateTrackInternal(PatternProject& project,
                                 TrackState& track,
                                 const BoomBapStyleProfile& style,
                                 const std::vector<PhraseRole>& phrasePlan,
                                 std::mt19937& rng);
    void generateDependentTracks(PatternProject& project,
                                 const BoomBapStyleProfile& style,
                                 const std::vector<PhraseRole>& phrasePlan,
                                 std::mt19937& rng,
                                 const std::unordered_set<TrackType>& mutableTracks);
    void postProcess(PatternProject& project,
                     const BoomBapStyleProfile& style,
                     std::mt19937& rng,
                     const std::unordered_set<TrackType>& mutableTracks);
    void applyPhraseEndingAccents(PatternProject& project,
                                  const BoomBapStyleProfile& style,
                                  std::mt19937& rng,
                                  const std::vector<PhraseRole>& phrasePlan,
                                  const std::unordered_set<TrackType>& mutableTracks);
    void validatePattern(PatternProject& project, const std::unordered_set<TrackType>& mutableTracks) const;
    static juce::String phraseSummaryString(const std::vector<PhraseRole>& roles);
    static bool isKickAnchorStep(int stepInBar);
    static bool isSnareAnchorStep(int stepInBar);

    BoomBapKickGenerator kickGenerator;
    BoomBapSnareGenerator snareGenerator;
    BoomBapHatGenerator hatGenerator;
    BoomBapGhostGenerator ghostGenerator;
    BoomBapOpenHatGenerator openHatGenerator;
    BoomBapPercGenerator percGenerator;
};
} // namespace bbg
