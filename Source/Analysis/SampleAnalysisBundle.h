#pragma once

#include "AudioFeatureMap.h"
#include "DrumBreakTranscriber.h"
#include "SampleHarmonyAnalyzer.h"
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
    DrumBreakAnalysis breakAnalysis;
    SampleHarmony harmony;
    double harmonyBpm = 0.0;        // tempo the bass segments were cut with
    double harmonyOriginSeconds = 0.0;
};
} // namespace bbg