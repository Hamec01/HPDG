#pragma once

#include <memory>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>

namespace bbg
{
// A sample loaded into HPDG 808's sample layer (a kick to make an 808 from, a bass one-shot, any WAV):
// mono, at its own rate, with the pitch it plays at (rootHz) so a note can be played from it.
// Immutable once made: the audio thread holds it by shared_ptr while a note plays it.
struct BassSample
{
    static constexpr double kMaxSeconds = 20.0;

    std::vector<float> data;
    double sampleRate = 48000.0;
    double rootHz = 0.0;            // detected pitch of the body; 0: unpitched (played as if it were C2)
    juce::String name, path;

    double lengthSeconds() const noexcept { return static_cast<double>(data.size()) / sampleRate; }

    // Reads WAV / AIFF / FLAC / MP3 (mixed to mono, at most kMaxSeconds). nullptr and `error` on failure.
    static std::shared_ptr<const BassSample> fromFile(const juce::File& file, juce::String& error);
    // The copy kept in the plugin state (a 24-bit WAV in memory).
    static std::shared_ptr<const BassSample> fromWavMemory(const juce::MemoryBlock& wav, const juce::String& name, const juce::String& path);
    juce::MemoryBlock toWavMemory() const;
    static std::shared_ptr<const BassSample> fromBuffer(std::vector<float> mono, double sampleRate, const juce::String& name);

    // The pitch of the body: the normalised square difference (McLeod) over the part after the attack,
    // 25-400 Hz. 0 when nothing periodic is found (a noise hit).
    static double detectRootHz(const std::vector<float>& mono, double sampleRate);
};
} // namespace bbg
