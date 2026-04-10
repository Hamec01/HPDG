#pragma once

#include "AudioFeatureMap.h"
#include "GenerationHints.h"
#include "LaneEvidenceMap.h"
#include "SampleApplyMode.h"
#include "SampleApplyWeights.h"
#include "SampleTranscription.h"

namespace bbg
{
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
};
} // namespace bbg
