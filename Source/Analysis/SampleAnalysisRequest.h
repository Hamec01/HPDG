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

    // Drum break / drum loop copy: transcribe Kick / Snare / HiHat with real timing (PPQ 960)
    // instead of the step-grid transcription. 0 = keep the original groove, 1 = hard quantize.
    bool transcribeDrumBreak = false;
    float breakQuantizeAmount = 0.0f;

    juce::File audioFile;

    // Fragment of the file the analyzer reads (seconds). trimEnd <= trimStart = the whole file
    // (still capped at SampleAnalyzer's maximum). The file itself is never modified.
    double trimStartSeconds = 0.0;
    double trimEndSeconds = 0.0;

    // Tempo typed by the user for this sample (> 0): detection is skipped and this tempo is
    // used as-is (and trusted for key analysis and generation).
    double manualBpm = 0.0;

    // What the sample states about itself (SampleLabelReader: WAV acid chunk / file name), filled
    // by SampleAnalyzer::analyzeAudioFileExtended. Hints checked against the audio.
    double labelBpm = 0.0;
    int labelKeyRoot = -1;
    int labelKeyMode = -1;

    bool hasTrim() const { return trimEndSeconds > trimStartSeconds; }
};
} // namespace bbg
