#pragma once

#include <unordered_set>

#include <juce_core/juce_core.h>

#include "../../Core/PatternProject.h"
#include "../ExtractPatternBuilder.h"

namespace bbg
{
struct TrapSampleBlendReport
{
    bool applied = false;
    int kickBarsFromSample = 0; // style bars whose kicks follow the sample
    int kickBarsKept = 0;       // style bars where the sample offered no legal kick
    int sampleKicksDropped = 0; // sample kicks on positions Trap does not play
    int hatBarsFromSample = 0;
    int hatBarsKept = 0;
    int bassAdded = 0;
    int bassRemoved = 0;
    std::unordered_set<TrackType> changedTracks;
};

// Guide mode, Trap over a drum loop: the sample leads, Trap's rules decide. The generic blend
// layered the loop's kicks and hats over the engine's own (+13-45 % kicks, two hat grids at once:
// "the kicks pop out, the hats turn to mush", maintainer 2026-10-27). Here each bar takes ONE
// source per lane: the loop's kicks where they sit on positions the Trap engine plays (else the
// engine's), the loop's hats where they fit the 1/32 / triplet grid (else the engine's), and the
// 808 follows the kicks that remain.
class TrapSampleBlend
{
public:
    // sampleBpm: the tempo the extracted ticks are written in; patternBpm: the beat's tempo (the
    // loop is mapped by the octave between them and repeats under a longer pattern).
    static TrapSampleBlendReport apply(PatternProject& project,
                                       const ExtractedPatternData& extracted,
                                       double sampleBpm,
                                       double patternBpm);
};

juce::String describeTrapSampleBlendReport(const TrapSampleBlendReport& report);

// ATLClassic / DarkTrap: the sample-led 808 (bass composer, follower, drum-loop blend) folded into the
// style's low register (TrapAlgebraEngine::styleWindow808Pitch). True when a note moved.
bool applyTrapStyle808Window(PatternProject& project);
} // namespace bbg
