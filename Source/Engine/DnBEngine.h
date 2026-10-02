#pragma once

#include <unordered_set>

#include "GenreEngine.h"
#include "DnB/DnBBass.h"
#include "DnB/DnBGrammar.h"

namespace bbg
{
// Drum & Bass: relational grammar + dual-meter model + candidate search with hard rejection.
//   GENERATE (grammar) -> REPAIR -> SCORE (gates + core + secondary) -> PRUNE secondary notes
//   -> NEAR-BEST POOL -> controlled (softmax) selection.
// Randomness only ever chooses between candidates inside the quality zone.
class DnBEngine final : public GenreEngine
{
public:
    DnBEngine() = default;

    void generate(PatternProject& project) override;
    void regenerateTrack(PatternProject& project, TrackType trackType) override;
    void generateTrackNew(PatternProject& project, TrackType trackType);
    void regenerateTrackVariation(PatternProject& project, TrackType trackType);
    void mutatePattern(PatternProject& project);
    void mutateTrack(PatternProject& project, TrackType trackType);

    // The search on its own (tests / audit): best candidate inside the quality zone.
    static DnBPattern search(const DnBGenerationParams& params, juce::String* debugReport = nullptr);
    static DnBGenerationParams paramsFromProject(const PatternProject& project, int seedSalt);

private:
    static void writePattern(PatternProject& project, const DnBPattern& pattern, const std::unordered_set<TrackType>& lanes,
                             const juce::String& debugReport);
    static std::unordered_set<TrackType> laneGroupFor(TrackType trackType);
    // The bass line (Sub808 lane, when enabled and unlocked) against the given drums.
    static juce::String writeBass(PatternProject& project, const DnBDrumFrame& drums, int seedSalt);
    static DnBDrumFrame drumsFromProject(const PatternProject& project);
    static const std::unordered_set<TrackType>& allLanes();
};
} // namespace bbg
