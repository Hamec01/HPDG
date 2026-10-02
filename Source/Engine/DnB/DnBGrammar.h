#pragma once

#include <random>

#include "DnBTypes.h"

namespace bbg
{
struct DnBGenerationParams
{
    int seed = 1;
    int bars = 4;
    float bpm = 174.0f;
    float density = 0.5f;
    float swingPercent = 51.0f;
    float humanize = 0.25f;
    float variation = 0.5f;
    int substyle = 0;
    int candidateCount = 64;
    float nearBestTolerance = 0.03f;
    float temperature = 0.012f;
};

// The relational grammar: phrase planner -> snare backbone -> kick/snare conversation ->
// ghosts -> carrier -> accents / air -> fills -> velocity hierarchy -> microtiming -> repair.
// One call builds one candidate; the engine scores many and selects inside the quality zone.
class DnBGrammar
{
public:
    static DnBPattern generateCandidate(const DnBGenerationParams& params,
                                        const DnBStyleProfile& style,
                                        std::mt19937& rng);

    // Hard constraints: no orphan ghosts, no kick on the backbone, ghost < anchor velocity,
    // no machine-gun runs, the backbone never rushes. Also used after pruning.
    static void repair(DnBPattern& pattern);
};
} // namespace bbg
