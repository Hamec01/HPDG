#pragma once

#include <unordered_set>

#include "GenreEngine.h"
#include "Techno/TechnoBass.h"
#include "Techno/TechnoGrammar.h"

namespace bbg
{
// Techno (docs/techno-engine.md): GENERATE (four-on-the-floor grammar) -> SCORE (gates + fits)
// -> NEAR-BEST POOL -> softmax selection; the bass is searched against the written kicks.
// Self-contained: it touches only the project it is given and no other genre's code.
class TechnoEngine final : public GenreEngine
{
public:
    TechnoEngine() = default;

    void generate(PatternProject& project) override;
    void regenerateTrack(PatternProject& project, TrackType trackType) override;
    void generateTrackNew(PatternProject& project, TrackType trackType);
    void regenerateTrackVariation(PatternProject& project, TrackType trackType);
    void mutatePattern(PatternProject& project);
    void mutateTrack(PatternProject& project, TrackType trackType);

    // candidatesOut (audit only, Tests/GenerationQualityLab): every scored candidate of this
    // call in candidate order; the returned pattern is unchanged by passing it.
    static TechnoPattern search(const TechnoGenerationParams& params, juce::String* debugReport = nullptr,
                                std::vector<TechnoPattern>* candidatesOut = nullptr);
    static TechnoGenerationParams paramsFromProject(const PatternProject& project, int seedSalt);

private:
    static void writePattern(PatternProject& project, const TechnoPattern& pattern, const std::unordered_set<TrackType>& lanes,
                             const juce::String& debugReport);
    static std::unordered_set<TrackType> laneGroupFor(TrackType trackType);
    static juce::String writeBass(PatternProject& project, int seedSalt);
    static const std::unordered_set<TrackType>& allLanes();
};
} // namespace bbg
