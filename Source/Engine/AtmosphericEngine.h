#pragma once

#include <unordered_set>

#include "GenreEngine.h"
#include "Atmospheric/AtmosphericBass.h"
#include "Atmospheric/AtmosphericGrammar.h"

namespace bbg
{
// Atmospheric (docs/audit/atmos/ATMOS_ENGINE_SPEC.md): GENERATE (substyle grammar from the
// Izzamuzzic passport) -> SCORE (gates + trait fits) -> NEAR-BEST POOL -> softmax selection;
// the bass is searched against the written kicks.
// Self-contained: it touches only the project it is given and no other genre's code.
class AtmosphericEngine final : public GenreEngine
{
public:
    AtmosphericEngine() = default;

    void generate(PatternProject& project) override;
    void regenerateTrack(PatternProject& project, TrackType trackType) override;
    void generateTrackNew(PatternProject& project, TrackType trackType);
    void regenerateTrackVariation(PatternProject& project, TrackType trackType);
    void mutatePattern(PatternProject& project);
    void mutateTrack(PatternProject& project, TrackType trackType);

    // candidatesOut (audit only): every scored candidate of this call in candidate order.
    static AtmosPattern search(const AtmosGenerationParams& params, juce::String* debugReport = nullptr,
                               std::vector<AtmosPattern>* candidatesOut = nullptr);
    static AtmosGenerationParams paramsFromProject(const PatternProject& project, int seedSalt);

private:
    static void writePattern(PatternProject& project, const AtmosPattern& pattern, const std::unordered_set<TrackType>& lanes,
                             const juce::String& debugReport);
    static std::unordered_set<TrackType> laneGroupFor(TrackType trackType);
    static juce::String writeBass(PatternProject& project, int seedSalt);
    static const std::unordered_set<TrackType>& allLanes();
};
} // namespace bbg
