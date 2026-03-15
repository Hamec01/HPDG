#pragma once

#include <random>
#include <vector>

#include "../../Core/GeneratorParams.h"
#include "DrillPhrasePlanner.h"
#include "DrillStyleProfile.h"
#include "../../Core/TrackState.h"

namespace bbg
{
class Drill808Generator
{
public:
    void generateRhythm(TrackState& subTrack,
                        const TrackState& kickTrack,
                        const GeneratorParams& params,
                        const DrillStyleProfile& style,
                        const DrillStyleSpec& spec,
                        float subActivity,
                        const std::vector<DrillPhraseRole>& phrase,
                        std::mt19937& rng) const;

    void assignPitches(TrackState& subTrack,
                       const GeneratorParams& params,
                       const DrillStyleProfile& style,
                       const DrillStyleSpec& spec,
                       const std::vector<DrillPhraseRole>& phrase,
                       std::mt19937& rng) const;

    void applySlides(TrackState& subTrack,
                     const DrillStyleSpec& spec,
                     const std::vector<DrillPhraseRole>& phrase,
                     std::mt19937& rng) const;

    void generate(TrackState& subTrack,
                  const TrackState& kickTrack,
                  const GeneratorParams& params,
                  const DrillStyleProfile& style,
                  const DrillStyleSpec& spec,
                  float subActivity,
                  const std::vector<DrillPhraseRole>& phrase,
                  std::mt19937& rng) const;
};
} // namespace bbg
