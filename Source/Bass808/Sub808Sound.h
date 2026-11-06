#pragma once

#include <memory>

#include <juce_audio_basics/juce_audio_basics.h>

#include "Bass808/BassSample.h"
#include "Bass808/BassSettings.h"
#include "Core/Sub808Types.h"

namespace bbg
{
// What HPDG's audio plays on the Sub808 lane: the engine settings (the lane's sound with the strip on top)
// and the sample layer. Built on the message thread, swapped in under the project lock, never changed.
struct Sub808Sound
{
    BassSettings settings;
    std::shared_ptr<const BassSample> sample;
    // A lane on its bank sample: the buffer this sound was made from. When the bank's selection no longer
    // matches it, the audio falls back to the plain sample player until the sound is rebuilt.
    std::shared_ptr<const juce::AudioBuffer<float>> bankSource;
    float sidechain = 0.0f; // the strip's SIDECHAIN: how deep a kick ducks the bass
};

// Voices retain the sample separately from its sound wrapper. Keep the wrapper alive
// until both are unused, so a later note cannot free sample memory on the audio thread.
inline bool canRetireSub808Sound(const std::shared_ptr<const Sub808Sound>& sound)
{
    return sound.use_count() == 1 && (sound->sample == nullptr || sound->sample.use_count() == 1);
}

// The bank sample as HPDG always played it: a raw sample (no envelope shaping, no filter, velocity
// straight to level), released in 6 ms at the note end.
BassSettings sub808SampleOnlySettings();

// The strip (filter, drive, length, gain) applied to a sound's settings; also forces velocity
// sensitivity to 1, as HPDG's note gain already carries the lane volume.
BassSettings applySub808Strip(BassSettings, const Sub808SoundState&);

// The lane's sound. bankBuffer / bankRate / bankRootHz: the lane's selected bank sample (used when the
// sound is not custom; nullptr: none). nullptr when there is nothing to play.
std::shared_ptr<const Sub808Sound> buildSub808Sound(const Sub808SoundState&,
                                                    std::shared_ptr<const juce::AudioBuffer<float>> bankBuffer,
                                                    double bankRate,
                                                    double bankRootHz,
                                                    const juce::String& bankName);
} // namespace bbg
