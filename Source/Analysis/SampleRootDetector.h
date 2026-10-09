#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace bbg
{
// Pitch of a bass / 808 one-shot from its sound (docs/audit/SAMPLE_ANALYSIS_STAGE.md, step 9).
// YIN (de Cheveigne & Kawahara 2002) frame by frame over the note; the settled pitch is the
// energy-weighted median of the largest group of frames within +-35 cents of each other, which
// skips the pitch glide an 808 starts with. A softer YIN threshold is tried when the strict one
// finds no periodic frames (synth basses with strong upper harmonics).
struct SampleRoot
{
    bool valid = false;
    double midi = 0.0;        // fractional MIDI note: 24.0 = C1, 24.3 = C1 + 30 cents
    float confidence = 0.0f;  // share of the voiced, weighted frames that agree on this pitch

    int nearestNote() const { return static_cast<int>(std::lround(midi)); }
    int pitchClass() const { return ((nearestNote() % 12) + 12) % 12; }
    double cents() const { return 100.0 * (midi - nearestNote()); }
};

class SampleRootDetector
{
public:
    static SampleRoot detect(const juce::AudioBuffer<float>& buffer, double sampleRate);
    static SampleRoot detect(const float* mono, int numSamples, double sampleRate);
};
} // namespace bbg
