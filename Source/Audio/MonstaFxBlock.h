#pragma once

#include <array>
#include <cstdint>
#include <limits>

#include <juce_dsp/juce_dsp.h>

#include "../Core/SoundLayerState.h"

namespace bbg
{
struct MonstaFxTimelineContext
{
    double bpm = 120.0;
    std::int64_t blockStartSample = 0;
    double blockStartQuarter = 0.0;
};

class MonstaFxBlock
{
public:
    void prepare(const juce::dsp::ProcessSpec& spec);
    void reset();

    void process(juce::AudioBuffer<float>& buffer,
                 const MonstaFxState& state,
                 const MonstaFxTimelineContext& timeline);

private:
    enum class BaseMode
    {
        Pass = 0,
        Repeat,
        MicroLoop,
        Reverse,
        Skip
    };

    struct Profile
    {
        MonstaFxFlavor flavor = MonstaFxFlavor::SyncGlitch;
        int divisionIndex = 2;
        float activityChance = 0.56f;
        float repeatWeight = 0.38f;
        float microLoopWeight = 0.24f;
        float reverseWeight = 0.18f;
        float skipWeight = 0.10f;
        float crushChance = 0.24f;
        float gateChance = 0.34f;
        int maxRepeatSubdivision = 8;
        int gateSteps = 4;
        int crushHoldBase = 3;
        int crushBitDepth = 7;
        float outputTrim = 0.90f;
        float dropoutChance = 0.0f;
        float dropoutDepth = 0.0f;
        float trashDrive = 1.0f;
        float wowDepthSamples = 0.0f;
        float wowRateHz = 0.0f;
        float flutterDepthSamples = 0.0f;
        float flutterRateHz = 0.0f;
        float smearMix = 0.0f;
        float jitterChance = 0.0f;
        int jitterRangeSamples = 0;
    };

    struct SliceDecision
    {
        BaseMode baseMode = BaseMode::Pass;
        bool crush = false;
        bool gate = false;
        int repeatSubdivision = 4;
        int loopStartOffsetSamples = 0;
        int loopLengthSamples = 0;
        int jumpOffsetSamples = 0;
        int crushHoldSamples = 1;
        int crushBitDepth = 8;
        int gateSteps = 4;
        std::uint32_t gateMask = 0;
        float outputTrim = 1.0f;
        bool dropout = false;
        float dropoutDepth = 0.0f;
        float trashDrive = 1.0f;
        float wowDepthSamples = 0.0f;
        float wowRateHz = 0.0f;
        float flutterDepthSamples = 0.0f;
        float flutterRateHz = 0.0f;
        float smearMix = 0.0f;
        int jitterRangeSamples = 0;
    };

    void ensureScratchCapacity(int numChannels, int numSamples);
    void ensureHistoryCapacity(int numChannels);
    void resetHistoryState();
    void maybeRefreshProfile(const MonstaFxState& state);
    void generateProfile(std::uint32_t chaosSeed);
    SliceDecision makeSliceDecision(std::int64_t sliceIndex, int sliceSamples) const;
    float readOperationSample(int channel,
                              float liveSample,
                              std::int64_t currentSample,
                              std::int64_t sliceStartSample,
                              int sliceSamples,
                              int relativeSample,
                              const SliceDecision& decision) const;
    float readHistorySample(int channel, std::int64_t currentSample, std::int64_t sourceSample) const;
    float applyGateEnvelope(int relativeSample, int sliceSamples, const SliceDecision& decision) const;
    float applyEdgeWindow(int relativeSample, int segmentLength) const;
    static std::uint32_t hash32(std::uint32_t value) noexcept;
    static std::uint32_t mixSeed(std::uint32_t base, std::uint32_t salt) noexcept;
    static float random01(std::uint32_t& state) noexcept;
    static int randomInt(std::uint32_t& state, int minValue, int maxValue) noexcept;
    static int chooseWeightedIndex(std::uint32_t& state, const std::array<float, 4>& weights) noexcept;
    static float quantizeToBitDepth(float sample, int bitDepth) noexcept;
    static float softClip(float sample, float drive) noexcept;

    double sampleRate = 44100.0;
    juce::AudioBuffer<float> dryBuffer;
    juce::AudioBuffer<float> historyBuffer;
    int historyCapacitySamples = 0;
    int historyWritePosition = 0;
    int availableHistorySamples = 0;
    std::int64_t expectedNextBlockSample = std::numeric_limits<std::int64_t>::min();
    double lastBpm = 120.0;
    bool profileValid = false;
    std::uint32_t activeChaosSeed = 0;
    std::uint64_t acknowledgedChaosRequest = 0;
    Profile activeProfile;
};
} // namespace bbg
