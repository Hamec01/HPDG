#pragma once

#include <array>

#include <juce_audio_basics/juce_audio_basics.h>

#include "LaneSampleBank.h"

namespace bbg
{
class PreviewEngine
{
public:
    void prepare(double sampleRate);
    void reset();

    void noteOn(TrackType trackType, float gain, const LaneSampleBank& sampleBank);
    void noteOnAtSample(TrackType trackType, float gain, int sampleOffset, const LaneSampleBank& sampleBank);
    void render(juce::AudioBuffer<float>& buffer, int startSample, int numSamples);

private:
    struct Voice
    {
        const juce::AudioBuffer<float>* sample = nullptr;
        TrackType trackType = TrackType::Kick;
        bool active = false;
        int samplePosition = 0;
        int startDelaySamples = 0;
        float velocity = 1.0f;
    };

    static constexpr int kMaxVoices = 64;

    Voice* allocateVoice(TrackType incomingTrack);

    std::array<Voice, kMaxVoices> voices {};
    double currentSampleRate = 44100.0;
};
} // namespace bbg
