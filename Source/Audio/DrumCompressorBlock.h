#pragma once

#include <atomic>

#include <juce_dsp/juce_dsp.h>

#include "../Core/SoundLayerState.h"

namespace bbg
{
class DrumCompressorBlock
{
public:
    void prepare(const juce::dsp::ProcessSpec& spec);
    void reset();

    void process(juce::AudioBuffer<float>& buffer, const CompressorState& state);

    float getLastGainReductionDb() const noexcept
    {
        return lastGainReductionDb.load(std::memory_order_relaxed);
    }

private:
    void ensureScratchCapacity(int numChannels, int numSamples);
    float applySaturationSample(float sample,
                                float amount,
                                DrumSaturationMode saturationMode,
                                DrumCompressorCharacter character) const noexcept;

    juce::dsp::Compressor<float> compressor;
    juce::AudioBuffer<float> dryBuffer;
    double sampleRate = 44100.0;
    float transientFastEnvelope = 0.0f;
    float transientSlowEnvelope = 0.0f;
    float smoothedAutoMakeupDb = 0.0f;
    std::atomic<float> lastGainReductionDb { 0.0f };
};
} // namespace bbg