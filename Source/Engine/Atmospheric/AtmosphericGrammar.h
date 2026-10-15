#pragma once

#include <random>

#include "AtmosphericTypes.h"

namespace bbg
{
// One Atmospheric drum candidate (ATMOS_ENGINE_SPEC.md, section 3):
// Pulse   - kick bar types "four" / "half" by sections, clap 2 / 4 by sections, off-beat 8th hats;
// Two-Step - kick 1 + 11, snare 5 + 13 with pickups, a hat motif on the 8ths;
// Broken  - a motif bar re-placed bar by bar around the 2 / 4 anchor.
class AtmosphericGrammar
{
public:
    static AtmosPattern generateCandidate(const AtmosGenerationParams& params, const AtmosStyleProfile& style, std::mt19937& rng);
};

// Trait fits against the passport (kicks / snares / hats per bar, off-beat shares, repetition)
// plus the hard gates of each substyle.
class AtmosphericScorer
{
public:
    static AtmosTraits measure(const AtmosPattern& pattern);
    // userDensity < 0: the style's own hat target (no user intent).
    static AtmosScore score(const AtmosPattern& pattern, const AtmosStyleProfile& style, float userDensity = -1.0f);
};
} // namespace bbg
