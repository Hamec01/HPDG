#pragma once

#include <juce_core/juce_core.h>

namespace bbg
{
enum class AnalysisMode
{
    Off,
    AnalyzeOnly,
    GenerateFromSample,
    ExtractFromSample
};

struct SampleAnalysisRequest
{
    enum class TempoHandling
    {
        Auto,
        PreferHalfTime,
        PreferDoubleTime,
        KeepDetected
    };

    enum class SourceType
    {
        None,
        LiveInput,
        AudioFile
    };

    SourceType source = SourceType::None;

    int barsToCapture = 8;
    bool useHostTempoIfAvailable = true;
    bool detectTempoFromFile = true;
    TempoHandling tempoHandling = TempoHandling::Auto;
    bool detectPhraseHints = true;
    bool downmixToMono = true;
    bool buildLaneEvidence = true;
    bool buildTranscription = true;
    bool buildGenerationHints = true;
    bool detectBassline = true;
    bool detectDrumEvents = true;
    bool usePercussiveHarmonicSeparation = true;

    juce::File audioFile;
};
} // namespace bbg
