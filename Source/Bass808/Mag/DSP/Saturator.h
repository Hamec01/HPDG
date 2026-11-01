#pragma once

#include <array>
#include <memory>

#include <juce_dsp/juce_dsp.h>

#include "DSP/HamloMag.h"
#include "DSP/MagSettings.h"

namespace hamlo::dsp
{
// Copied from HPEQ (Source/DSP/ProcessorsM6.h, SaturatorProcessor) - see docs/SYNC_WITH_HPEQ.md.
// Saturator (insert). y = (f(g x + b) - f(b)) / n, then a 10 Hz DC blocker and the output trim, with
//   g = 10^(drive/20), b = bias, f = tanh | tube (tanh above 0, tanh(1.4u)/1.4 below) | hard clip,
//   n = g f'(b) when auto gain is on (small signals keep unity gain), else 1.
// Oversampling 2x / 4x: JUCE half-band FIR (equiripple, max quality) with integer latency, so the
// node can align its dry path exactly. Latency per setting: latencySamples().
// Model 0 is that law ("Classic", bit-exact for projects saved before the HamloMag pass); models 1..5
// run HamloMag (HamloMag.h) around the same oversamplers.
class SaturatorProcessor
{
public:
    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept;
    void process(float* const* channels, int numChannels, int numSamples, const FxSlotParams&) noexcept;

    // Integer latency at the base rate for oversampling index 0 (1x) / 1 (2x) / 2 (4x). The first call
    // builds the table (allocates): it is made from prepare() / the message thread.
    static int latencySamples(int oversamplingIndex);
    // The whole slot: oversampling + the HamloMag wow delay (TAPE, VINYL).
    static int latencySamples(const FxSlotParams&, double sampleRate);
    float inputLevelDb() const noexcept { return mag.inputLevelDb(); }
    float outputLevelDb() const noexcept { return mag.outputLevelDb(); }
    // Oversampled processing runs in sub-blocks of this size, so the oversampler buffers stay small
    // (M8: 49 KB -> about 1/8 per slot); the FIR stages stream, so the output does not change.
    static constexpr int kOversamplingChunk = 64;

private:
    void shape(float* data, int n, float g, float bias, float norm, int curve) noexcept;

    double sampleRate = 48000.0;
    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 2> oversamplers; // 2x, 4x
    int activeOversampling = 0;
    std::array<double, kMaxChannels> dcIn {}, dcOut {};
    double dcCoefficient = 0.999;
    MagProcessor mag;
    int magFactor = 0; // oversampling factor the HamloMag cores are prepared for
};
} // namespace hamlo::dsp
