#pragma once

#include <juce_core/juce_core.h>

namespace bbg
{
enum class AnalysisMode
{
    Off,
    AnalyzeOnly,
    GenerateFromSample
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

    juce::File audioFile;
};
} // namespace bbg
