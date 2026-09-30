#pragma once

#include <array>
#include <vector>

#include <juce_core/juce_core.h>

namespace bbg
{
// Key and bass-line estimate for a musical sample, so a generated 808 can play "in the
// sample's key and on its bass notes".
//   - key: chroma of the whole sample against Krumhansl-Kessler major / minor profiles
//   - bass: per beat, the strongest low note (30-260 Hz) scored with its 2nd-4th harmonics,
//     which keeps sub / 808 fundamentals (poorly resolved by any FFT) unambiguous
// Times are seconds from the start of the audio; callers map them to ticks with the tempo
// they trust (sample tempo or project tempo).
struct SampleBassSegment
{
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    int midiNote = -1;        // -1: no clear bass note in this segment
    float confidence = 0.0f;  // 0..1: how much this note dominates the low register
    float lowEnergy = 0.0f;   // relative low-register energy (0..1 of the loudest segment)
};

struct SampleHarmony
{
    bool valid = false;
    int keyRoot = 0;          // 0 = C ... 11 = B
    int scaleMode = 0;        // matches GeneratorParams::scaleMode: 0 = minor, 1 = major
    float keyConfidence = 0.0f;
    std::array<float, 12> chroma {};
    std::vector<SampleBassSegment> bass;

    int confidentBassNotes(float minConfidence = 0.35f) const;
    const SampleBassSegment* segmentAt(double seconds) const;
    juce::String keyName() const;
    juce::String describe() const;
};

class SampleHarmonyAnalyzer
{
public:
    // segmentSeconds: bass segmentation step (normally one beat); originSeconds: where beat 1
    // is, so segments line up with the musical grid.
    SampleHarmony analyze(const std::vector<float>& mono,
                          double sampleRate,
                          double segmentSeconds,
                          double originSeconds) const;
};
} // namespace bbg
