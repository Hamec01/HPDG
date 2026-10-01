#pragma once

#include "AudioFeatureMap.h"
#include "GenerationHints.h"
#include "LaneEvidenceMap.h"
#include "SampleApplyMode.h"
#include "SampleApplyWeights.h"
#include "SampleTranscription.h"

namespace bbg
{
// The character of the loaded sample, for parts that should fit its mood (the bass).
struct SampleMood
{
    bool valid = false;
    float busyness = 0.5f;  // 0 = a hit per beat or less, 1 = four or more hits per beat
    float sustain = 0.0f;   // 0 = dry drums, 1 = pads / chords that never decay
    bool minor = false;
    float swing = 0.0f;     // 0 = straight, 1 = heavy shuffle (~66%)
    float bpm = 0.0f;

    // 0 = busy, bright, fast; 1 = calm, sustained, sad: spacious bass, little swing.
    float calmness() const noexcept
    {
        if (!valid)
            return 0.0f;
        const float slow = bpm > 0.0f && bpm < 88.0f ? 1.0f : 0.0f;
        const float value = 0.35f * sustain + 0.35f * (1.0f - busyness) + 0.15f * (minor ? 1.0f : 0.0f) + 0.15f * slow;
        return value < 0.0f ? 0.0f : value > 1.0f ? 1.0f : value;
    }
};

struct SampleAwareGenerationContext
{
    bool enabled = false;
    AudioFeatureMap featureMap;
    LaneEvidenceMap laneEvidence;
    SampleTranscription transcription;
    GenerationHints hints;
    float supportVsContrast = 0.5f;
    float reactivity = 0.7f;
    SampleApplyMode applyMode = SampleApplyMode::Blend;
    SampleApplyWeights applyWeights;
    bool preferCopyDrums = false;
    bool preferCopyBass = false;
    SampleMood mood;
};
} // namespace bbg
