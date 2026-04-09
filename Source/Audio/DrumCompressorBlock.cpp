#include "DrumCompressorBlock.h"

#include <algorithm>
#include <cmath>

namespace bbg
{
namespace
{
constexpr float kMinDetectorLevel = 1.0e-6f;

float millisecondsToCoefficient(double sampleRate, float milliseconds)
{
    const auto timeSeconds = std::max(0.0005f, milliseconds * 0.001f);
    return static_cast<float>(std::exp(-1.0 / (sampleRate * timeSeconds)));
}

float computeEffectiveAttackMs(const CompressorState& state)
{
    if (state.character == DrumCompressorCharacter::Glue)
        return std::clamp(state.attackMs * 0.82f, 1.0f, 80.0f);

    return std::clamp(std::max(4.5f, state.attackMs * 1.22f), 1.0f, 80.0f);
}

float computeEffectiveReleaseMs(const CompressorState& state)
{
    if (state.character == DrumCompressorCharacter::Glue)
        return std::clamp(state.releaseMs * 1.12f + 12.0f, 20.0f, 400.0f);

    return std::clamp(state.releaseMs * 0.84f, 20.0f, 400.0f);
}

float computeAutoMakeupDb(const CompressorState& state, float gainReductionDb)
{
    const float characterFactor = state.character == DrumCompressorCharacter::Glue ? 0.62f : 0.46f;
    const float trimCompensation = std::max(0.0f, state.inputTrimDb) * 0.18f;
    const float saturationCompensation = state.saturation * 1.4f;
    return std::clamp(gainReductionDb * characterFactor + trimCompensation + saturationCompensation, 0.0f, 9.0f);
}
} // namespace

void DrumCompressorBlock::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 1000.0 ? spec.sampleRate : 44100.0;
    compressor.reset();
    compressor.prepare(spec);
    dryBuffer.setSize(static_cast<int>(spec.numChannels), static_cast<int>(spec.maximumBlockSize), false, false, true);
    reset();
}

void DrumCompressorBlock::reset()
{
    compressor.reset();
    dryBuffer.clear();
    transientFastEnvelope = 0.0f;
    transientSlowEnvelope = 0.0f;
    smoothedAutoMakeupDb = 0.0f;
    lastGainReductionDb.store(0.0f, std::memory_order_relaxed);
}

void DrumCompressorBlock::ensureScratchCapacity(int numChannels, int numSamples)
{
    if (dryBuffer.getNumChannels() < numChannels || dryBuffer.getNumSamples() < numSamples)
        dryBuffer.setSize(numChannels, numSamples, false, false, true);
}

float DrumCompressorBlock::applySaturationSample(float sample,
                                                 float amount,
                                                 DrumSaturationMode saturationMode,
                                                 DrumCompressorCharacter character) const noexcept
{
    const float characterBias = character == DrumCompressorCharacter::Glue ? 0.94f : 1.08f;
    const float shapedAmount = std::clamp(amount * characterBias, 0.0f, 1.25f);

    if (saturationMode == DrumSaturationMode::Warm)
    {
        const float drive = 1.0f + shapedAmount * 4.8f;
        const float shaped = std::tanh(sample * drive);
        const float normalizer = std::max(0.001f, std::tanh(drive));
        return shaped / normalizer;
    }

    const float drive = 1.0f + shapedAmount * 6.6f;
    const float asymmetrical = sample + 0.18f * sample * std::abs(sample);
    const float softClip = std::tanh(asymmetrical * drive * 1.08f);
    const float edge = juce::jlimit(-1.25f, 1.25f, asymmetrical * (1.0f + shapedAmount * 0.32f));
    const float normalizer = std::max(0.001f, std::tanh(drive * 1.08f));
    return juce::jlimit(-1.25f, 1.25f, 0.72f * (softClip / normalizer) + 0.28f * edge);
}

void DrumCompressorBlock::process(juce::AudioBuffer<float>& buffer, const CompressorState& rawState)
{
    if (buffer.getNumSamples() <= 0 || buffer.getNumChannels() <= 0)
        return;

    CompressorState state = rawState;
    sanitizeCompressorState(state);

    if (!state.enabled)
    {
        lastGainReductionDb.store(0.0f, std::memory_order_relaxed);
        smoothedAutoMakeupDb = 0.0f;
        return;
    }

    ensureScratchCapacity(buffer.getNumChannels(), buffer.getNumSamples());
    dryBuffer.makeCopyOf(buffer, true);

    buffer.applyGain(juce::Decibels::decibelsToGain(state.inputTrimDb));

    compressor.setThreshold(state.thresholdDb);
    compressor.setRatio(state.ratio);
    const float effectiveAttackMs = computeEffectiveAttackMs(state);
    const float effectiveReleaseMs = computeEffectiveReleaseMs(state);
    compressor.setAttack(effectiveAttackMs);
    compressor.setRelease(effectiveReleaseMs);

    const float transientFastCoeff = millisecondsToCoefficient(sampleRate,
                                                               state.character == DrumCompressorCharacter::Punch ? 1.6f : 2.4f);
    const float transientSlowCoeff = millisecondsToCoefficient(sampleRate,
                                                               state.character == DrumCompressorCharacter::Punch ? 18.0f : 26.0f);
    const float fastAttackBias = std::clamp((12.0f - effectiveAttackMs) / 10.0f, 0.0f, 1.0f);
    const float transientProtectBase = state.character == DrumCompressorCharacter::Punch ? 0.24f : 0.11f;

    float blockPeakGainReductionDb = 0.0f;

    for (int sampleIndex = 0; sampleIndex < buffer.getNumSamples(); ++sampleIndex)
    {
        float detectorSample = 0.0f;
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            detectorSample += std::abs(buffer.getSample(channel, sampleIndex));
        detectorSample /= static_cast<float>(buffer.getNumChannels());

        transientFastEnvelope = detectorSample + transientFastCoeff * (transientFastEnvelope - detectorSample);
        transientSlowEnvelope = detectorSample + transientSlowCoeff * (transientSlowEnvelope - detectorSample);
        const float onset = std::max(0.0f, transientFastEnvelope - transientSlowEnvelope);
        const float transientStrength = detectorSample > kMinDetectorLevel
            ? std::clamp(onset / (detectorSample + 0.001f), 0.0f, 1.0f)
            : 0.0f;
        const float transientProtection = transientProtectBase * fastAttackBias * transientStrength;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const float inputSample = buffer.getSample(channel, sampleIndex);
            const float protectedInput = inputSample * (1.0f - transientProtection);
            const float compressedSample = compressor.processSample(channel, protectedInput);
            buffer.setSample(channel, sampleIndex, compressedSample);

            const float inAbs = std::abs(protectedInput);
            const float outAbs = std::abs(compressedSample);
            if (inAbs > kMinDetectorLevel && outAbs <= inAbs)
            {
                const float sampleGainReductionDb = juce::Decibels::gainToDecibels(inAbs, -96.0f)
                    - juce::Decibels::gainToDecibels(outAbs, -96.0f);
                blockPeakGainReductionDb = std::max(blockPeakGainReductionDb, sampleGainReductionDb);
            }
        }
    }

    const float compressionDrivenSaturation = std::clamp(blockPeakGainReductionDb / 18.0f, 0.0f, 0.30f);
    const float saturationAmount = std::clamp(state.saturation * (1.0f + compressionDrivenSaturation), 0.0f, 1.0f);
    if (saturationAmount > 0.001f)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            auto* channelData = buffer.getWritePointer(channel);
            for (int sampleIndex = 0; sampleIndex < buffer.getNumSamples(); ++sampleIndex)
                channelData[sampleIndex] = applySaturationSample(channelData[sampleIndex],
                                                                 saturationAmount,
                                                                 state.saturationMode,
                                                                 state.character);
        }
    }

    if (state.autoMakeup)
    {
        const float targetMakeupDb = computeAutoMakeupDb(state, blockPeakGainReductionDb);
        const float smoothingCoeff = millisecondsToCoefficient(sampleRate, 45.0f);
        smoothedAutoMakeupDb = targetMakeupDb + smoothingCoeff * (smoothedAutoMakeupDb - targetMakeupDb);
        buffer.applyGain(juce::Decibels::decibelsToGain(smoothedAutoMakeupDb));
    }
    else
    {
        smoothedAutoMakeupDb = 0.0f;
        buffer.applyGain(juce::Decibels::decibelsToGain(state.outputTrimDb));
    }

    const float wetMix = std::clamp(state.mix, 0.0f, 1.0f);
    const float dryMix = 1.0f - wetMix;
    if (dryMix > 0.0f)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const float* dryData = dryBuffer.getReadPointer(channel);
            float* wetData = buffer.getWritePointer(channel);
            for (int sampleIndex = 0; sampleIndex < buffer.getNumSamples(); ++sampleIndex)
                wetData[sampleIndex] = dryData[sampleIndex] * dryMix + wetData[sampleIndex] * wetMix;
        }
    }

    const float previousGainReductionDb = lastGainReductionDb.load(std::memory_order_relaxed);
    const float displayedGainReductionDb = std::max(blockPeakGainReductionDb, previousGainReductionDb * 0.82f);
    lastGainReductionDb.store(displayedGainReductionDb, std::memory_order_relaxed);
}
} // namespace bbg