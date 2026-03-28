#pragma once

#include <array>

#include <juce_audio_basics/juce_audio_basics.h>

#include "LaneSampleBank.h"

namespace bbg
{
class PreviewEngine
{
public:
    struct TriggerOptions
    {
        float playbackRate = 1.0f;
        int maxDurationSamples = -1;
        bool mono = true;
        bool cutItself = true;
        bool legato = false;
        bool glide = false;
        int glideDurationSamples = 0;
    };

    void prepare(double sampleRate);
    void reset();

    void noteOn(TrackType trackType, float gain, const LaneSampleBank& sampleBank);
    void noteOnAtSample(TrackType trackType, float gain, int sampleOffset, const LaneSampleBank& sampleBank);
    void noteOn(TrackType trackType, float gain, const LaneSampleBank& sampleBank, const TriggerOptions& options);
    void noteOnAtSample(TrackType trackType, float gain, int sampleOffset, const LaneSampleBank& sampleBank, const TriggerOptions& options);
    void render(juce::AudioBuffer<float>& buffer, int startSample, int numSamples);
    bool hasActiveVoices() const;

private:
    struct Voice
    {
        const juce::AudioBuffer<float>* sample = nullptr;
        TrackType trackType = TrackType::Kick;
        bool active = false;
        double samplePosition = 0.0;
        int startDelaySamples = 0;
        float velocity = 1.0f;
        float playbackRate = 1.0f;
        float targetPlaybackRate = 1.0f;
        float glideStepPerSample = 0.0f;
        int glideSamplesRemaining = 0;
        int remainingSamples = -1;
        bool hasPendingTransition = false;
        int pendingTransitionDelaySamples = 0;
        float pendingPlaybackRate = 1.0f;
        float pendingVelocity = 1.0f;
        int pendingRemainingSamples = -1;
        bool pendingGlide = false;
        int pendingGlideDurationSamples = 0;
    };

    static constexpr int kMaxVoices = 64;

    Voice* allocateVoice(TrackType incomingTrack);
    Voice* findActiveVoice(TrackType trackType);
    void applyPendingTransition(Voice& voice);

    std::array<Voice, kMaxVoices> voices {};
    double currentSampleRate = 44100.0;
};
} // namespace bbg
