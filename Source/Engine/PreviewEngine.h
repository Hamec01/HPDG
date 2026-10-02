#pragma once

#include <array>
#include <memory>

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
    void renderSeparated(std::array<juce::AudioBuffer<float>, kTrackTypeCount>& buffers, int startSample, int numSamples);
    bool hasActiveVoices() const;

private:
    struct Voice
    {
        // Shared ownership: a genre switch can swap the sample bank while this voice rings out.
        std::shared_ptr<const juce::AudioBuffer<float>> sample;
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

        // Choke / release: a cut scheduled at a sample offset (when the next hit of the same
        // lane starts) and a short linear fade so nothing ends with a click.
        int cutDelaySamples = -1;
        double rateScale = 1.0; // the sample file's rate / the device rate
        int fadeSamplesRemaining = 0;
        int fadeTotalSamples = 0;
    };

    static constexpr int kMaxVoices = 64;

    Voice* allocateVoice(TrackType incomingTrack);
    Voice* findActiveVoice(TrackType trackType);
    void applyPendingTransition(Voice& voice);
    void scheduleCut(TrackType trackType, int sampleOffset);
    void startFade(Voice& voice, int fadeSamples) const;
    // Advances cut / delay / fade state for one output sample. Returns false while the voice is
    // silent this sample (not started yet, or just ended); gain is the envelope multiplier.
    bool stepVoice(Voice& voice, float& gain);

    std::array<Voice, kMaxVoices> voices {};
    double currentSampleRate = 44100.0;
};
} // namespace bbg
