#pragma once

#include <random>

#include "TechnoTypes.h"

namespace bbg
{
// One techno drum candidate built by the rules of docs/techno-engine.md, section 3:
// the four-on-the-floor axis first, then rumble, clap, hats (accent profile, choke under the
// offbeat open hat), ride, Euclidean / polymeter percussion kept off the axis, rolls and crash,
// arranged as a phrase of A / A' / F bars.
class TechnoGrammar
{
public:
    static TechnoPattern generateCandidate(const TechnoGenerationParams& params, const TechnoStyleProfile& style, std::mt19937& rng);
};

// Section 4: anchor integrity, interlock, polyphonic syncopation, density and repetition fits.
class TechnoScorer
{
public:
    static TechnoScore score(const TechnoPattern& pattern, const TechnoStyleProfile& style);
    static float syncopation(const TechnoPattern& pattern); // S, per bar
};
} // namespace bbg
