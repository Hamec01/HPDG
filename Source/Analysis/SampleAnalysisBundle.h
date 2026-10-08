#pragma once

#include "AudioFeatureMap.h"
#include "DrumBreakTranscriber.h"
#include "SampleHarmonyAnalyzer.h"
#include "SampleLabelReader.h"
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
    SampleLabels labels;            // tempo / key the file states (name, acid chunk)
    SampleHarmony harmony;
    double sampleBpm = 0.0;         // the sample's own tempo when trusted, 0 when unknown
    double harmonyBpm = 0.0;        // tempo the bass segments were cut with
    double harmonyOriginSeconds = 0.0;
    // true: harmonyBpm / harmonyOriginSeconds were measured from the sample (a confident drum
    // loop); false: the sample is assumed to play at session tempo with beat 1 at its start.
    bool harmonyTempoFromSample = false;
};
} // namespace bbg