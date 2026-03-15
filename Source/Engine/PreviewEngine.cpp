#include "PreviewEngine.h"

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
        voice.samplePosition = 0;
        voice.startDelaySamples = 0;
        voice.velocity = 1.0f;
    }
}

void PreviewEngine::noteOn(TrackType trackType, float gain, const LaneSampleBank& sampleBank)
{
    noteOnAtSample(trackType, gain, 0, sampleBank);
}

void PreviewEngine::noteOnAtSample(TrackType trackType, float gain, int sampleOffset, const LaneSampleBank& sampleBank)
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
        // Mono choke: retriggering sub immediately cuts previous sub tail.
        for (auto& voice : voices)
        {
            if (voice.active && voice.trackType == TrackType::Sub808)
            {
                voice.active = false;
                voice.samplePosition = 0;
            }
        }
    }

    auto* voice = allocateVoice(trackType);
    if (voice == nullptr)
        return;

    voice->active = true;
    voice->sample = selected;
    voice->trackType = trackType;
    voice->samplePosition = 0;
    voice->startDelaySamples = std::max(0, sampleOffset);
    voice->velocity = juce::jlimit(0.0f, 1.0f, gain);
}

void PreviewEngine::render(juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    if (numSamples <= 0)
        return;

    const auto totalSamples = buffer.getNumSamples();
    const int start = juce::jlimit(0, totalSamples, startSample);
    const int end = juce::jlimit(start, totalSamples, start + numSamples);

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

            const auto* bufferRef = voice.sample;
            const int length = bufferRef->getNumSamples();
            if (voice.samplePosition >= length)
            {
                voice.active = false;
                continue;
            }

            const int chCount = bufferRef->getNumChannels();
            float src = 0.0f;
            if (chCount == 1)
                src = bufferRef->getSample(0, voice.samplePosition);
            else
                src = 0.5f * (bufferRef->getSample(0, voice.samplePosition) + bufferRef->getSample(1, voice.samplePosition));

            mixed += src * voice.velocity;
            ++voice.samplePosition;
        }

        const float normalized = std::tanh(mixed * 0.85f);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.addSample(ch, sample, normalized);
    }
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
        const int progress = (voice.samplePosition * 1000) / length;
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
