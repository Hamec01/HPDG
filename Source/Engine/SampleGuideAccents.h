#pragma once

#include <juce_core/juce_core.h>

#include "../Analysis/DrumBreakTranscriber.h"
#include "../Core/PatternProject.h"

namespace bbg
{
struct SampleGuideAccentReport
{
    bool usedSampleTempo = false;
    double bpm = 0.0;
    double originSeconds = 0.0;
    int candidatePeaks = 0;
    int addedKicks = 0;
};

// Guide mode for a musical (non-drum) sample: the genre engine writes the beat, and strong
// low transients of the sample ("peaks you would put a kick on") may add a kick - only on slots
// the genre allows, never on the snare's backbeat or where a snare already sits, and at most
// one or two per bar. It is a nudge, not a rule: the genre grammar stays in charge.
class SampleGuideAccents
{
public:
    static SampleGuideAccentReport apply(PatternProject& project,
                                         const DrumBreakAnalysis& analysis,
                                         double gridBpm,
                                         double gridOriginSeconds,
                                         float reactivity);
};

juce::String describeSampleGuideAccentReport(const SampleGuideAccentReport& report);
} // namespace bbg
