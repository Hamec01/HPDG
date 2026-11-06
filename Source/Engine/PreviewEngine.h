#pragma once

#include <array>
#include <memory>
#include <utility>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

#include "LaneSampleBank.h"

namespace bbg
{
class BassEngine;
struct BassSample;
struct Sub808Sound;

// Plays the lanes' samples. The Sub808 lane is played by the HPDG 808 engine (Source/Bass808) once a
// sound is set: its bank sample as a plain sample, or an HPDG 808 sound.
class PreviewEngine
{
public:
    PreviewEngine();
    ~PreviewEngine();

    struct TriggerOptions
    {
        float playbackRate = 1.0f;
        int maxDurationSamples = -1;
        bool mono = true;
        bool cutItself = true;
        bool legato = false;
        bool glide = false;
        int glideDurationSamples = 0;
        int midiNote = -1; // the Sub808 note (the bass engine plays notes, not playback rates)
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

    // The Sub808 lane's sound (under the project lock, before the block's notes). nullptr: the plain
    // sample player, as before.
    void setSub808Sound(std::shared_ptr<const Sub808Sound> sound, double bpm);
    void noteOffSub808(int pitch);
    // How much earlier Sub808 notes must be sent so the bass engine's output lands on time (its
    // saturator's latency); 0 while the plain sample player plays the lane.
    int sub808LeadSamples(const LaneSampleBank& sampleBank) const;
    // True once after each reset(): the first block of playback, whose early Sub808 window has
    // already passed the notes right at its start.
    bool takeSub808CatchUp() noexcept { return !std::exchange(bassPrimed, true); }

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

    struct BassEvent
    {
        int at = 0;          // samples from the start of the next render
        int note = 36;
        float velocity = 1.0f;
        int mode = 0;        // BassSettings::PlayMode
        float glideMs = 0.0f;
        bool on = true;
    };

    bool bassPlaysLane(const LaneSampleBank& sampleBank) const;
    void queueBassNote(float gain, int sampleOffset, const TriggerOptions& options);
    // Renders the bass engine for the next numSamples into bassBuffer, applying the queued notes on time.
    void renderBass(int numSamples);
    // The kick's sidechain on bassBuffer: each kick hit pulls the bass down (2 ms in), it comes back
    // exponentially. Runs while the bass is silent too, so a hit still ducks the note that follows.
    void duckBass(int numSamples);

    std::array<Voice, kMaxVoices> voices {};
    std::unique_ptr<BassEngine> bass;
    std::shared_ptr<const Sub808Sound> bassSound;
    std::vector<BassEvent> bassEvents;
    std::vector<float> bassBuffer;
    bool bassPrimed = false;
    int bassTail = 0; // samples the engine still renders after its last voice ended (its output delay)
    std::vector<int> kickHits; // kick starts, samples from the start of the next render
    double duck = 0.0;         // 0 .. 1 how far down the bass is now
    int duckAttack = 0;
    double currentSampleRate = 44100.0;
};
} // namespace bbg
