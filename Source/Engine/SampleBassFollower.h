#pragma once

#include <juce_core/juce_core.h>

#include "../Analysis/SampleHarmonyAnalyzer.h"
#include "../Core/PatternProject.h"

namespace bbg
{
struct SampleBassFollowReport
{
    bool keyApplied = false;
    int notesFollowed = 0;   // 808 notes re-pitched to the sample's bass
    int notesKept = 0;       // no clear sample bass there: the engine's (in-key) pitch stays
};

// Guide mode, genres with an 808 (Trap for now): the engine keeps the 808 rhythm, the sample
// decides the pitch. Each 808 note takes the sample's bass note sounding at that moment,
// placed in the 808 register around the key root (octave jumps chosen by the engine survive).
class SampleBassFollower
{
public:
    static constexpr float kMinKeyConfidence = 0.4f;
    static constexpr float kMinBassConfidence = 0.35f;

    static bool shouldApplyKey(const SampleHarmony& harmony) { return harmony.valid && harmony.keyConfidence >= kMinKeyConfidence; }

    static SampleBassFollowReport apply(PatternProject& project,
                                        const SampleHarmony& harmony,
                                        double bpm,
                                        double originSeconds);
};

juce::String describeSampleBassFollowReport(const SampleBassFollowReport& report, const SampleHarmony& harmony);
} // namespace bbg
