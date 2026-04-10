#pragma once

#include "AudioFeatureMap.h"
#include "GenerationHints.h"
#include "LaneEvidenceMap.h"
#include "SampleAnalysisResult.h"
#include "SampleTranscription.h"

namespace bbg
{
struct SampleAnalysisBundle
{
    SampleAnalysisResult summary;
    AudioFeatureMap featureMap;
    LaneEvidenceMap laneEvidence;
    SampleTranscription transcription;
    GenerationHints hints;
};
} // namespace bbg