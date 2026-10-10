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

    // Tempo range the user picked for this sample (0 / 0 = automatic), as FL Studio's
    // "Detect tempo" ranges (50-100, 75-150, 100-200, 150-300): the analyzer only considers tempos
    // inside it, which settles a sample the automatic reading got wrong (80 vs 120 / 160).
    double tempoRangeMin = 0.0;
    double tempoRangeMax = 0.0;
    bool hasTempoRange() const { return tempoRangeMin > 20.0 && tempoRangeMax > tempoRangeMin; }

    // Key the user picked for this sample (root 0-11 = C..B, mode 0 minor / 1 major; -1 =
    // detect). Audio-only key sits near its limit on short loops (~0.65 MIREX), so a one-click
    // correction replaces the detected key as it is (no check against the audio).
    int manualKeyRoot = -1;
    int manualKeyMode = -1;
    bool hasManualKey() const { return manualKeyRoot >= 0 && manualKeyRoot < 12 && (manualKeyMode == 0 || manualKeyMode == 1); }

    // What the sample states about itself (SampleLabelReader: WAV acid chunk / file name), filled
    // by SampleAnalyzer::analyzeAudioFileExtended. Hints checked against the audio.
    double labelBpm = 0.0;
    int labelKeyRoot = -1;
    int labelKeyMode = -1;

    // Set by SampleAnalyzer::analyzeAudioFileExtended when the file (or the trimmed fragment) is
    // longer than the analysis window and was cut: the analysed length is then no loop length.
    bool audioCut = false;
    // Set by SampleAnalyzer for a whole song when a second window read the tempo more
    // confidently (DrumBreakTranscriber::preferSecondWindowTempo): the analysed window takes it.
    double songWindowBpm = 0.0;
    float songWindowConfidence = 0.0f;

    bool hasTrim() const { return trimEndSeconds > trimStartSeconds; }
};
} // namespace bbg
