#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

namespace bbg
{
// What a sample says about itself: the tempo in its WAV 'acid' chunk (FL Studio renders,
// "acidized" packs) or in its file name ("... - 140 BPM G# Min.wav", "rhh_bass_loop_90_Am.wav"),
// and the key in its file name. These are hints: the analyzers check them against the audio
// (docs/audit/SAMPLE_ANALYSIS_STAGE.md, step 6) and fall back to the audio when the two disagree.
struct SampleLabels
{
    double bpm = 0.0;        // 0 = no tempo label
    juce::String bpmSource;  // "acid" or "name"
    int keyRoot = -1;        // -1 = no key label, 0 = C ... 11 = B
    int keyMode = -1;        // -1 = not given, 0 = minor, 1 = major
    juce::String keySource;  // "name"

    bool hasBpm() const { return bpm > 20.0; }
    bool hasKey() const { return keyRoot >= 0; }
    juce::String describe() const;
};

class SampleLabelReader
{
public:
    // Tempo and key from a file name (with or without extension / folders).
    static SampleLabels fromName(const juce::String& fileName);
    // The 'acid' tempo from reader metadata (juce::WavAudioFormat keys) wins over the name's.
    static SampleLabels read(const juce::File& file, const juce::StringPairArray& metadata);
};
} // namespace bbg
