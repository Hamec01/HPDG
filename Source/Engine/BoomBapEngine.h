#pragma once

#include "GenreEngine.h"
#include "BoomBap/BoomBapClassicAlgebraGenerator.h"
#include "BoomBap/BoomBapStyleProfile.h"

namespace bbg
{
// BoomBap generation runs entirely on the Classic Algebra generator. Whole-pattern
// generation and every per-lane action (new / variation / mutate) draw from the same
// statistical model, so a regenerated lane always matches the rest of the groove.
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
    void generateWithAlgebra(PatternProject& project, const BoomBapStyleProfile& style) const;
};
} // namespace bbg
