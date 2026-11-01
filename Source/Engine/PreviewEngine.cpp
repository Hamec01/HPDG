#include "PreviewEngine.h"

#include <algorithm>
#include <cmath>

#include "Bass808/BassEngine.h"
#include "Bass808/Sub808Sound.h"

namespace bbg
{
namespace
{
bool isCoreLane(TrackType t)
{
    return t == TrackType::Kick || t == TrackType::Snare || t == TrackType::Sub808;
}
}

PreviewEngine::PreviewEngine() : bass(std::make_unique<BassEngine>())
{
    bassEvents.reserve(512);
    kickHits.reserve(64);
}

PreviewEngine::~PreviewEngine() = default;

void PreviewEngine::prepare(double sampleRate)
{
    currentSampleRate = sampleRate > 1000.0 ? sampleRate : 44100.0;
    bass->prepare(currentSampleRate, 512);
    if (bassSound != nullptr)
    {
        bass->setSettings(bassSound->settings);
        bass->setSample(bassSound->sample);
    }
    bassBuffer.assign(8192, 0.0f);
    reset();
}

void PreviewEngine::setSub808Sound(std::shared_ptr<const Sub808Sound> sound, double bpm)
{
    bass->setTempo(bpm);
    if (sound == bassSound)
        return;
    bassSound = std::move(sound);
    if (bassSound != nullptr)
    {
        bass->setSettings(bassSound->settings); // a note already sounding takes the new settings at once
        bass->setSample(bassSound->sample);
    }
}

bool PreviewEngine::bassPlaysLane(const LaneSampleBank& sampleBank) const
{
    if (bassSound == nullptr)
        return false;
    return bassSound->bankSource == nullptr || bassSound->bankSource == sampleBank.getSelectedBufferShared(TrackType::Sub808);
}

int PreviewEngine::sub808LeadSamples(const LaneSampleBank& sampleBank) const
{
    return bassPlaysLane(sampleBank) ? bass->latencySamples() : 0;
}

void PreviewEngine::queueBassNote(float gain, int sampleOffset, const TriggerOptions& options)
{
    const int at = std::max(0, sampleOffset);
    const int note = juce::jlimit(0, 127, options.midiNote);
    BassEvent on;
    on.at = at;
    on.note = note;
    on.velocity = juce::jlimit(0.0f, 1.0f, gain);
    on.mode = 0; // BassSettings::Retrigger
    if (options.mono && options.glide && options.glideDurationSamples > 0)
    {
        on.mode = 2; // Glide
        on.glideMs = static_cast<float>(1000.0 * options.glideDurationSamples / currentSampleRate);
    }
    else if (options.mono && (options.legato || options.glide))
    {
        on.mode = 1; // Legato
    }

    // a release of this key still to come belongs to the note this one replaces
    bassEvents.erase(std::remove_if(bassEvents.begin(), bassEvents.end(), [&](const BassEvent& e)
    {
        return !e.on && e.note == note && e.at >= at;
    }), bassEvents.end());

    if (bassEvents.size() + 2 > bassEvents.capacity())
        return; // never allocate here
    bassEvents.push_back(on);
    if (options.maxDurationSamples > 0)
    {
        BassEvent off = on;
        off.on = false;
        off.at = at + options.maxDurationSamples;
        bassEvents.push_back(off);
    }
}

void PreviewEngine::duckBass(int numSamples)
{
    const float depth = bassSound != nullptr ? juce::jlimit(0.0f, 1.0f, bassSound->sidechain) : 0.0f;
    if (kickHits.empty() && duck < 1.0e-4)
    {
        duck = 0.0;
        return;
    }
    // down by up to -30 dB at full depth; recovery 60 ms (light) .. 220 ms (deep)
    const double floor = std::pow(10.0, -30.0 * depth / 20.0);
    const double release = std::exp(-1.0 / ((0.06 + 0.16 * depth) * currentSampleRate));
    const int attackSamples = std::max(1, static_cast<int>(0.002 * currentSampleRate));
    std::sort(kickHits.begin(), kickHits.end());
    size_t next = 0;
    for (int i = 0; i < numSamples; ++i)
    {
        while (next < kickHits.size() && kickHits[next] <= i)
        {
            duckAttack = attackSamples;
            ++next;
        }
        if (duckAttack > 0)
        {
            duck += (1.0 - duck) / static_cast<double>(duckAttack);
            --duckAttack;
        }
        else
        {
            duck *= release;
        }
        bassBuffer[static_cast<size_t>(i)] *= static_cast<float>(1.0 - (1.0 - floor) * duck);
    }
    kickHits.erase(kickHits.begin(), kickHits.begin() + static_cast<std::ptrdiff_t>(next));
    for (auto& hit : kickHits)
        hit -= numSamples;
}

void PreviewEngine::renderBass(int numSamples)
{
    if (static_cast<int>(bassBuffer.size()) < numSamples)
        bassBuffer.resize(static_cast<size_t>(numSamples)); // only a host block above 8192
    std::fill(bassBuffer.begin(), bassBuffer.begin() + numSamples, 0.0f);
    struct DuckAtEnd
    {
        PreviewEngine& engine;
        int n;
        ~DuckAtEnd() { engine.duckBass(n); }
    } duckAtEnd { *this, numSamples };
    // the engine delays its output (the saturator's latency): after the last voice ends it keeps
    // running that long, or the end of every note's release would be cut off (a click per note)
    if (!bass->isActive() && bassEvents.empty() && bassTail <= 0)
        return;

    // in time order, a note's start before a release at the same sample (so a slide joins the note it
    // follows instead of starting after its release)
    for (size_t i = 1; i < bassEvents.size(); ++i)
    {
        const auto e = bassEvents[i];
        size_t j = i;
        while (j > 0 && (bassEvents[j - 1].at > e.at || (bassEvents[j - 1].at == e.at && !bassEvents[j - 1].on && e.on)))
        {
            bassEvents[j] = bassEvents[j - 1];
            --j;
        }
        bassEvents[j] = e;
    }

    int position = 0;
    size_t applied = 0;
    for (; applied < bassEvents.size() && bassEvents[applied].at < numSamples; ++applied)
    {
        const auto& e = bassEvents[applied];
        if (e.at > position)
        {
            bass->render(bassBuffer.data() + position, e.at - position);
            position = e.at;
        }
        if (e.on)
            bass->noteOn(e.note, e.velocity, e.mode, e.glideMs);
        else
            bass->noteOff(e.note);
    }
    if (position < numSamples)
        bass->render(bassBuffer.data() + position, numSamples - position);
    bassTail = bass->isActive() || !bassEvents.empty() ? bass->latencySamples() + 64 : bassTail - numSamples;

    bassEvents.erase(bassEvents.begin(), bassEvents.begin() + static_cast<std::ptrdiff_t>(applied));
    for (auto& e : bassEvents)
        e.at -= numSamples;
}

void PreviewEngine::startFade(Voice& voice, int fadeSamples) const
{
    if (voice.fadeTotalSamples > 0 && voice.fadeSamplesRemaining <= fadeSamples)
        return; // already fading out at least this fast
    voice.fadeTotalSamples = juce::jmax(1, fadeSamples);
    voice.fadeSamplesRemaining = voice.fadeTotalSamples;
}

void PreviewEngine::scheduleCut(TrackType trackType, int sampleOffset)
{
    for (auto& voice : voices)
    {
        if (!voice.active || voice.trackType != trackType)
            continue;
        const int at = std::max(0, sampleOffset);
        if (voice.cutDelaySamples < 0 || at < voice.cutDelaySamples)
            voice.cutDelaySamples = at;
    }
}

bool PreviewEngine::stepVoice(Voice& voice, float& gain)
{
    // The cut clock runs from the start of the block, like startDelaySamples, so a cut lands
    // exactly where the next hit begins even if this voice itself started inside the block.
    if (voice.cutDelaySamples > 0)
    {
        --voice.cutDelaySamples;
    }
    else if (voice.cutDelaySamples == 0)
    {
        voice.cutDelaySamples = -1;
        if (voice.startDelaySamples > 0)
        {
            voice.active = false; // cut before it ever sounded
            return false;
        }
        startFade(voice, static_cast<int>(0.003 * currentSampleRate)); // 3 ms choke
    }

    if (voice.startDelaySamples > 0)
    {
        --voice.startDelaySamples;
        return false;
    }

    gain = 1.0f;
    if (voice.fadeTotalSamples > 0)
    {
        if (voice.fadeSamplesRemaining <= 0)
        {
            voice.active = false;
            return false;
        }
        gain = static_cast<float>(voice.fadeSamplesRemaining) / static_cast<float>(voice.fadeTotalSamples);
        --voice.fadeSamplesRemaining;
    }
    return true;
}

void PreviewEngine::reset()
{
    bass->allNotesOff();
    bassEvents.clear();
    kickHits.clear();
    duck = 0.0;
    duckAttack = 0;
    bassPrimed = false;
    bassTail = bass->latencySamples() + 64; // the 3 ms choke of allNotesOff plays out
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
        voice.cutDelaySamples = -1;
        voice.fadeSamplesRemaining = 0;
        voice.fadeTotalSamples = 0;
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
    if (trackType == TrackType::Kick && bassSound != nullptr && bassSound->sidechain > 0.001f
        && kickHits.size() < kickHits.capacity())
        kickHits.push_back(std::max(0, sampleOffset));

    if (trackType == TrackType::Sub808 && options.midiNote >= 0 && bassPlaysLane(sampleBank))
    {
        scheduleCut(TrackType::Sub808, sampleOffset); // a plain-sample note from before the switch
        queueBassNote(gain, sampleOffset, options);
        return;
    }

    auto sourceTrack = trackType;
    auto selected = sampleBank.getSelectedBufferShared(trackType);
    const auto fallBackTo = [&](TrackType from, TrackType to)
    {
        if ((selected == nullptr || selected->getNumSamples() <= 0) && trackType == from)
        {
            selected = sampleBank.getSelectedBufferShared(to);
            sourceTrack = to;
        }
    };
    fallBackTo(TrackType::HatFX, TrackType::HiHat);
    fallBackTo(TrackType::ClapGhostSnare, TrackType::Snare);
    fallBackTo(TrackType::GhostKick, TrackType::Kick);
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
            scheduleCut(TrackType::Sub808, sampleOffset);
    }
    else
    {
        // Drums behave like FL's "cut itself": a new hit of the same lane chokes the previous
        // one if it is still ringing (short fade, no click). A closed hat also chokes the open hat.
        scheduleCut(trackType, sampleOffset);
        if (trackType == TrackType::HiHat)
            scheduleCut(TrackType::OpenHat, sampleOffset);
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
    voice->cutDelaySamples = -1;
    voice->fadeSamplesRemaining = 0;
    voice->fadeTotalSamples = 0;
    voice->rateScale = sampleBank.getSelectedSampleRate(sourceTrack) / currentSampleRate;
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
    voice.fadeTotalSamples = 0; // a legato / glide continuation cancels the previous note's release
    voice.fadeSamplesRemaining = 0;
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

    renderBass(end - start);

    for (int sample = start; sample < end; ++sample)
    {
        float mixed = bassBuffer[static_cast<size_t>(sample - start)];

        for (auto& voice : voices)
        {
            if (!voice.active || voice.sample == nullptr)
                continue;

            float envelope = 1.0f;
            if (!stepVoice(voice, envelope))
                continue;

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

            const auto* bufferRef = voice.sample.get();
            const int length = bufferRef->getNumSamples();
            if (voice.samplePosition >= static_cast<double>(length))
            {
                voice.active = false;
                continue;
            }

            const float src = readSampleAt(*bufferRef, voice.samplePosition);

            mixed += src * voice.velocity * envelope;

            if (voice.glideSamplesRemaining > 0)
            {
                voice.playbackRate += voice.glideStepPerSample;
                --voice.glideSamplesRemaining;
                if (voice.glideSamplesRemaining <= 0)
                    voice.playbackRate = voice.targetPlaybackRate;
            }

            voice.samplePosition += static_cast<double>(voice.playbackRate) * voice.rateScale;

            if (voice.remainingSamples > 0)
            {
                --voice.remainingSamples;
                if (voice.remainingSamples <= 0)
                    startFade(voice, static_cast<int>(0.006 * currentSampleRate)); // note end: 6 ms release
            }
        }

        const float normalized = std::tanh(mixed * 0.85f);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.addSample(ch, sample, normalized);
    }
}

void PreviewEngine::renderSeparated(std::array<juce::AudioBuffer<float>, kTrackTypeCount>& buffers,
                                    int startSample,
                                    int numSamples)
{
    if (numSamples <= 0 || buffers.empty())
        return;

    const auto totalSamples = buffers.front().getNumSamples();
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

    renderBass(end - start);

    for (int sample = start; sample < end; ++sample)
    {
        std::array<float, kTrackTypeCount> mixedByTrack {};
        mixedByTrack[static_cast<size_t>(trackTypeIndex(TrackType::Sub808))] = bassBuffer[static_cast<size_t>(sample - start)];

        for (auto& voice : voices)
        {
            if (!voice.active || voice.sample == nullptr)
                continue;

            float envelope = 1.0f;
            if (!stepVoice(voice, envelope))
                continue;

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

            const auto* bufferRef = voice.sample.get();
            const int length = bufferRef->getNumSamples();
            if (voice.samplePosition >= static_cast<double>(length))
            {
                voice.active = false;
                continue;
            }

            const float src = readSampleAt(*bufferRef, voice.samplePosition);
            mixedByTrack[static_cast<size_t>(trackTypeIndex(voice.trackType))] += src * voice.velocity * envelope;

            if (voice.glideSamplesRemaining > 0)
            {
                voice.playbackRate += voice.glideStepPerSample;
                --voice.glideSamplesRemaining;
                if (voice.glideSamplesRemaining <= 0)
                    voice.playbackRate = voice.targetPlaybackRate;
            }

            voice.samplePosition += static_cast<double>(voice.playbackRate) * voice.rateScale;

            if (voice.remainingSamples > 0)
            {
                --voice.remainingSamples;
                if (voice.remainingSamples <= 0)
                    startFade(voice, static_cast<int>(0.006 * currentSampleRate)); // note end: 6 ms release
            }
        }

        for (int trackIndex = 0; trackIndex < kTrackTypeCount; ++trackIndex)
        {
            auto& trackBuffer = buffers[static_cast<size_t>(trackIndex)];
            const float value = mixedByTrack[static_cast<size_t>(trackIndex)];
            if (std::abs(value) <= 1.0e-8f)
                continue;

            for (int channel = 0; channel < trackBuffer.getNumChannels(); ++channel)
                trackBuffer.addSample(channel, sample, value);
        }
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
    if (bass->isActive() || !bassEvents.empty() || bassTail > 0)
        return true;
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
