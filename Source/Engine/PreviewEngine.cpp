#include "PreviewEngine.h"

#include <algorithm>
#include <cmath>

namespace bbg
{
namespace
{
bool isCoreLane(TrackType t)
{
    return t == TrackType::Kick || t == TrackType::Snare || t == TrackType::Sub808;
}
}

void PreviewEngine::prepare(double sampleRate)
{
    currentSampleRate = sampleRate > 1000.0 ? sampleRate : 44100.0;
    reset();
}

void PreviewEngine::reset()
{
    for (auto& voice : voices)
    {
        voice.sample = nullptr;
        voice.trackType = TrackType::Kick;
        voice.active = false;
        voice.samplePosition = 0.0;
        voice.startDelaySamples = 0;
        voice.velocity = 1.0f;
        voice.playbackRate = 1.0f;
        voice.targetPlaybackRate = 1.0f;
        voice.glideStepPerSample = 0.0f;
        voice.glideSamplesRemaining = 0;
        voice.remainingSamples = -1;
        voice.hasPendingTransition = false;
        voice.pendingTransitionDelaySamples = 0;
        voice.pendingPlaybackRate = 1.0f;
        voice.pendingVelocity = 1.0f;
        voice.pendingRemainingSamples = -1;
        voice.pendingGlide = false;
        voice.pendingGlideDurationSamples = 0;
    }
}

void PreviewEngine::noteOn(TrackType trackType, float gain, const LaneSampleBank& sampleBank)
{
    noteOnAtSample(trackType, gain, 0, sampleBank);
}

void PreviewEngine::noteOnAtSample(TrackType trackType, float gain, int sampleOffset, const LaneSampleBank& sampleBank)
{
    noteOnAtSample(trackType, gain, sampleOffset, sampleBank, {});
}

void PreviewEngine::noteOn(TrackType trackType, float gain, const LaneSampleBank& sampleBank, const TriggerOptions& options)
{
    noteOnAtSample(trackType, gain, 0, sampleBank, options);
}

void PreviewEngine::noteOnAtSample(TrackType trackType, float gain, int sampleOffset, const LaneSampleBank& sampleBank, const TriggerOptions& options)
{
    const auto* selected = sampleBank.getSelectedBuffer(trackType);
    if ((selected == nullptr || selected->getNumSamples() <= 0) && trackType == TrackType::HatFX)
        selected = sampleBank.getSelectedBuffer(TrackType::HiHat);
    if ((selected == nullptr || selected->getNumSamples() <= 0) && trackType == TrackType::ClapGhostSnare)
        selected = sampleBank.getSelectedBuffer(TrackType::Snare);
    if ((selected == nullptr || selected->getNumSamples() <= 0) && trackType == TrackType::GhostKick)
        selected = sampleBank.getSelectedBuffer(TrackType::Kick);
    if (selected == nullptr || selected->getNumSamples() <= 0)
        return;

    if (trackType == TrackType::Sub808)
    {
        if (options.mono && (options.legato || options.glide))
        {
            if (auto* activeVoice = findActiveVoice(TrackType::Sub808); activeVoice != nullptr)
            {
                activeVoice->hasPendingTransition = true;
                activeVoice->pendingTransitionDelaySamples = std::max(0, sampleOffset);
                activeVoice->pendingPlaybackRate = juce::jmax(0.01f, options.playbackRate);
                activeVoice->pendingVelocity = juce::jlimit(0.0f, 1.0f, gain);
                activeVoice->pendingRemainingSamples = options.maxDurationSamples;
                activeVoice->pendingGlide = options.glide;
                activeVoice->pendingGlideDurationSamples = std::max(0, options.glideDurationSamples);
                return;
            }
        }

        if (options.mono || options.cutItself)
        {
            for (auto& voice : voices)
            {
                if (voice.active && voice.trackType == TrackType::Sub808)
                {
                    voice.active = false;
                    voice.samplePosition = 0.0;
                    voice.hasPendingTransition = false;
                }
            }
        }
    }

    auto* voice = allocateVoice(trackType);
    if (voice == nullptr)
        return;

    voice->active = true;
    voice->sample = selected;
    voice->trackType = trackType;
    voice->samplePosition = 0.0;
    voice->startDelaySamples = std::max(0, sampleOffset);
    voice->velocity = juce::jlimit(0.0f, 1.0f, gain);
    voice->playbackRate = juce::jmax(0.01f, options.playbackRate);
    voice->targetPlaybackRate = voice->playbackRate;
    voice->glideStepPerSample = 0.0f;
    voice->glideSamplesRemaining = 0;
    voice->remainingSamples = options.maxDurationSamples;
    voice->hasPendingTransition = false;
}

void PreviewEngine::applyPendingTransition(Voice& voice)
{
    if (!voice.hasPendingTransition)
        return;

    voice.velocity = juce::jlimit(0.0f, 1.0f, voice.pendingVelocity);
    voice.remainingSamples = voice.pendingRemainingSamples;

    if (voice.pendingGlide && voice.pendingGlideDurationSamples > 0)
    {
        voice.targetPlaybackRate = juce::jmax(0.01f, voice.pendingPlaybackRate);
        voice.glideSamplesRemaining = voice.pendingGlideDurationSamples;
        voice.glideStepPerSample = (voice.targetPlaybackRate - voice.playbackRate)
            / static_cast<float>(juce::jmax(1, voice.glideSamplesRemaining));
    }
    else
    {
        voice.playbackRate = juce::jmax(0.01f, voice.pendingPlaybackRate);
        voice.targetPlaybackRate = voice.playbackRate;
        voice.glideStepPerSample = 0.0f;
        voice.glideSamplesRemaining = 0;
    }

    voice.hasPendingTransition = false;
}

void PreviewEngine::render(juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    if (numSamples <= 0)
        return;

    const auto totalSamples = buffer.getNumSamples();
    const int start = juce::jlimit(0, totalSamples, startSample);
    const int end = juce::jlimit(start, totalSamples, start + numSamples);

    const auto readSampleAt = [](const juce::AudioBuffer<float>& source, double position)
    {
        const int channelCount = source.getNumChannels();
        const int sampleCount = source.getNumSamples();
        const int baseIndex = juce::jlimit(0, juce::jmax(0, sampleCount - 1), static_cast<int>(position));
        const int nextIndex = juce::jlimit(0, juce::jmax(0, sampleCount - 1), baseIndex + 1);
        const float fraction = static_cast<float>(position - static_cast<double>(baseIndex));

        const auto monoAt = [&](int index)
        {
            if (channelCount <= 1)
                return source.getSample(0, index);
            return 0.5f * (source.getSample(0, index) + source.getSample(1, index));
        };

        const float base = monoAt(baseIndex);
        const float next = monoAt(nextIndex);
        return base + (next - base) * fraction;
    };

    for (int sample = start; sample < end; ++sample)
    {
        float mixed = 0.0f;

        for (auto& voice : voices)
        {
            if (!voice.active || voice.sample == nullptr)
                continue;

            if (voice.startDelaySamples > 0)
            {
                --voice.startDelaySamples;
                continue;
            }

            if (voice.hasPendingTransition)
            {
                if (voice.pendingTransitionDelaySamples > 0)
                {
                    --voice.pendingTransitionDelaySamples;
                }
                else
                {
                    applyPendingTransition(voice);
                }
            }

            const auto* bufferRef = voice.sample;
            const int length = bufferRef->getNumSamples();
            if (voice.samplePosition >= static_cast<double>(length))
            {
                voice.active = false;
                continue;
            }

            const float src = readSampleAt(*bufferRef, voice.samplePosition);

            mixed += src * voice.velocity;

            if (voice.glideSamplesRemaining > 0)
            {
                voice.playbackRate += voice.glideStepPerSample;
                --voice.glideSamplesRemaining;
                if (voice.glideSamplesRemaining <= 0)
                    voice.playbackRate = voice.targetPlaybackRate;
            }

            voice.samplePosition += static_cast<double>(voice.playbackRate);

            if (voice.remainingSamples > 0)
            {
                --voice.remainingSamples;
                if (voice.remainingSamples <= 0)
                    voice.active = false;
            }
        }

        const float normalized = std::tanh(mixed * 0.85f);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.addSample(ch, sample, normalized);
    }
}

PreviewEngine::Voice* PreviewEngine::findActiveVoice(TrackType trackType)
{
    for (auto& voice : voices)
    {
        if (voice.active && voice.trackType == trackType)
            return &voice;
    }

    return nullptr;
}

bool PreviewEngine::hasActiveVoices() const
{
    return std::any_of(voices.begin(), voices.end(), [](const Voice& voice)
    {
        return voice.active;
    });
}

PreviewEngine::Voice* PreviewEngine::allocateVoice(TrackType incomingTrack)
{
    for (auto& voice : voices)
    {
        if (!voice.active)
            return &voice;
    }

    Voice* best = nullptr;
    int bestProgress = -1;

    for (auto& voice : voices)
    {
        if (voice.sample == nullptr)
            continue;

        if (isCoreLane(incomingTrack) && isCoreLane(voice.trackType))
            continue;

        const int length = std::max(1, voice.sample->getNumSamples());
        const int progress = static_cast<int>((voice.samplePosition * 1000.0) / static_cast<double>(length));
        if (progress > bestProgress)
        {
            bestProgress = progress;
            best = &voice;
        }
    }

    if (best != nullptr)
        return best;

    // Fallback when only core lanes are active.
    return &voices.front();
}
} // namespace bbg
