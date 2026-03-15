#pragma once

#include <random>
#include <vector>

#include "../../Core/GeneratorParams.h"
#include "TrapPhrasePlanner.h"
#include "TrapStyleProfile.h"
#include "../../Core/TrackState.h"

namespace bbg
{
class Trap808Generator
{
public:
    void generateRhythm(TrackState& subTrack,
                        const TrackState& kickTrack,
                        const GeneratorParams& params,
                        const TrapStyleProfile& style,
                        const TrapStyleSpec& spec,
                        float subActivity,
                        const std::vector<TrapPhraseRole>& phrase,
                        std::mt19937& rng) const;

    void assignPitches(TrackState& subTrack,
                       const GeneratorParams& params,
                       const TrapStyleProfile& style,
                       const TrapStyleSpec& spec,
                       const std::vector<TrapPhraseRole>& phrase,
                       std::mt19937& rng) const;

    void applySlides(TrackState& subTrack,
                     const TrapStyleSpec& spec,
                     const std::vector<TrapPhraseRole>& phrase,
                     std::mt19937& rng) const;

    void generate(TrackState& subTrack,
                  const TrackState& kickTrack,
                  const GeneratorParams& params,
                  const TrapStyleProfile& style,
                  const TrapStyleSpec& spec,
                  float subActivity,
                  const std::vector<TrapPhraseRole>& phrase,
                  std::mt19937& rng) const;
};
} // namespace bbg
