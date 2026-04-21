#include "MonstaFxBlock.h"

#include <algorithm>
#include <cmath>

namespace bbg
{
namespace
{
constexpr std::uint32_t kDefaultChaosSeed = 0x4d4f4e53u;
constexpr double kMinimumBpm = 45.0;
constexpr double kMaximumBpm = 240.0;
constexpr double kHistorySeconds = 2.5;
constexpr std::array<double, 6> kSliceDivisionsInQuarterNotes {
    0.5,
    1.0 / 3.0,
    0.25,
    1.0 / 6.0,
    0.125,
    0.0625
};
} // namespace

void MonstaFxBlock::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 1000.0 ? spec.sampleRate : 44100.0;
    dryBuffer.setSize(static_cast<int>(spec.numChannels), static_cast<int>(spec.maximumBlockSize), false, false, true);
    historyCapacitySamples = juce::jmax(2048, static_cast<int>(std::ceil(sampleRate * kHistorySeconds)));
    historyBuffer.setSize(static_cast<int>(spec.numChannels), historyCapacitySamples, false, false, true);
    reset();
}

void MonstaFxBlock::reset()
{
    dryBuffer.clear();
    historyBuffer.clear();
    resetHistoryState();
    lastBpm = 120.0;
    profileValid = false;
    activeChaosSeed = 0;
    acknowledgedChaosRequest = 0;
    activeProfile = {};
}

void MonstaFxBlock::ensureScratchCapacity(int numChannels, int numSamples)
{
    if (dryBuffer.getNumChannels() < numChannels || dryBuffer.getNumSamples() < numSamples)
        dryBuffer.setSize(numChannels, numSamples, false, false, true);
}

void MonstaFxBlock::ensureHistoryCapacity(int numChannels)
{
    const int requiredHistorySamples = juce::jmax(2048, static_cast<int>(std::ceil(sampleRate * kHistorySeconds)));
    if (historyBuffer.getNumChannels() < numChannels || historyBuffer.getNumSamples() != requiredHistorySamples)
    {
        historyCapacitySamples = requiredHistorySamples;
        historyBuffer.setSize(numChannels, historyCapacitySamples, false, false, true);
        resetHistoryState();
    }
}

void MonstaFxBlock::resetHistoryState()
{
    historyWritePosition = 0;
    availableHistorySamples = 0;
    expectedNextBlockSample = std::numeric_limits<std::int64_t>::min();
}

void MonstaFxBlock::maybeRefreshProfile(const MonstaFxState& state)
{
    const auto effectiveSeed = state.chaosSeed != 0 ? state.chaosSeed : kDefaultChaosSeed;
    const auto requestStamp = (static_cast<std::uint64_t>(effectiveSeed) << 1u) | 1u;

    if (!profileValid || effectiveSeed != activeChaosSeed)
    {
        generateProfile(effectiveSeed);
        profileValid = true;
        activeChaosSeed = effectiveSeed;
        return;
    }

    if (state.pendingChaosReseed && requestStamp != acknowledgedChaosRequest)
    {
        generateProfile(effectiveSeed);
        acknowledgedChaosRequest = requestStamp;
        activeChaosSeed = effectiveSeed;
    }
}

void MonstaFxBlock::generateProfile(std::uint32_t chaosSeed)
{
    auto randomState = mixSeed(chaosSeed, 0x9e3779b9u);
    static constexpr std::array<int, 10> divisionChoices { 2, 2, 0, 1, 2, 3, 4, 2, 1, 5 };
    static constexpr std::array<int, 5> gateChoices { 2, 4, 4, 8, 8 };
    static constexpr std::array<int, 5> crushHoldChoices { 2, 3, 4, 6, 8 };

    activeProfile.divisionIndex = divisionChoices[static_cast<size_t>(randomInt(randomState, 0, static_cast<int>(divisionChoices.size()) - 1))];
    activeProfile.activityChance = 0.42f + random01(randomState) * 0.34f;
    activeProfile.repeatWeight = 0.32f + random01(randomState) * 0.26f;
    activeProfile.microLoopWeight = 0.18f + random01(randomState) * 0.20f;
    activeProfile.reverseWeight = 0.10f + random01(randomState) * 0.18f;
    activeProfile.skipWeight = 0.05f + random01(randomState) * 0.10f;
    activeProfile.crushChance = 0.12f + random01(randomState) * 0.30f;
    activeProfile.gateChance = 0.18f + random01(randomState) * 0.34f;
    activeProfile.maxRepeatSubdivision = random01(randomState) > 0.55f ? 8 : 4;
    activeProfile.gateSteps = gateChoices[static_cast<size_t>(randomInt(randomState, 0, static_cast<int>(gateChoices.size()) - 1))];
    activeProfile.crushHoldBase = crushHoldChoices[static_cast<size_t>(randomInt(randomState,
                                                                                  0,
                                                                                  static_cast<int>(crushHoldChoices.size()) - 1))];
    activeProfile.crushBitDepth = randomInt(randomState, 5, 9);
    activeProfile.outputTrim = 0.84f + random01(randomState) * 0.12f;
}

MonstaFxBlock::SliceDecision MonstaFxBlock::makeSliceDecision(std::int64_t sliceIndex, int sliceSamples) const
{
    SliceDecision decision;
    if (sliceSamples <= 0)
        return decision;

    auto randomState = mixSeed(activeChaosSeed,
                               hash32(static_cast<std::uint32_t>(sliceIndex)
                                      ^ hash32(static_cast<std::uint32_t>(sliceIndex >> 32))));

    if (random01(randomState) < activeProfile.activityChance)
    {
        const std::array<float, 4> weights {
            activeProfile.repeatWeight,
            activeProfile.microLoopWeight,
            activeProfile.reverseWeight,
            activeProfile.skipWeight
        };

        switch (chooseWeightedIndex(randomState, weights))
        {
            case 0:
                decision.baseMode = BaseMode::Repeat;
                break;
            case 1:
                decision.baseMode = BaseMode::MicroLoop;
                break;
            case 2:
                decision.baseMode = BaseMode::Reverse;
                break;
            case 3:
            default:
                decision.baseMode = BaseMode::Skip;
                break;
        }
    }

    decision.crush = random01(randomState) < activeProfile.crushChance;
    decision.gate = random01(randomState) < activeProfile.gateChance;
    decision.outputTrim = juce::jlimit(0.70f, 1.0f, activeProfile.outputTrim - random01(randomState) * 0.05f);

    static constexpr std::array<int, 3> repeatChoices { 2, 4, 8 };
    decision.repeatSubdivision = repeatChoices[static_cast<size_t>(randomInt(randomState, 0, static_cast<int>(repeatChoices.size()) - 1))];
    while (decision.repeatSubdivision > 2 && sliceSamples / decision.repeatSubdivision < 12)
        decision.repeatSubdivision /= 2;

    const int minLoopLength = juce::jmax(8, sliceSamples / 10);
    const int maxLoopLength = juce::jmax(minLoopLength, sliceSamples / 3);
    decision.loopLengthSamples = randomInt(randomState, minLoopLength, maxLoopLength);
    decision.loopStartOffsetSamples = randomInt(randomState,
                                                0,
                                                juce::jmax(0, sliceSamples - decision.loopLengthSamples - 1));
    decision.jumpOffsetSamples = randomInt(randomState,
                                           juce::jmax(1, sliceSamples / 8),
                                           juce::jmax(1, sliceSamples / 2));
    decision.crushHoldSamples = juce::jmax(1, activeProfile.crushHoldBase + randomInt(randomState, 0, 3));
    decision.crushBitDepth = juce::jlimit(4, 10, activeProfile.crushBitDepth - randomInt(randomState, 0, 2));
    decision.gateSteps = activeProfile.gateSteps;

    if (decision.gate)
    {
        decision.gateMask = 0;
        for (int step = 0; step < decision.gateSteps; ++step)
        {
            if (random01(randomState) > 0.38f)
                decision.gateMask |= (1u << step);
        }

        if (decision.gateMask == 0)
            decision.gateMask = 1u << randomInt(randomState, 0, juce::jmax(0, decision.gateSteps - 1));
    }

    return decision;
}

float MonstaFxBlock::readOperationSample(int channel,
                                         float liveSample,
                                         std::int64_t currentSample,
                                         std::int64_t sliceStartSample,
                                         int sliceSamples,
                                         int relativeSample,
                                         const SliceDecision& decision) const
{
    if (decision.baseMode == BaseMode::Pass || sliceSamples <= 0)
        return liveSample;

    const auto previousSliceStartSample = sliceStartSample - sliceSamples;
    if (previousSliceStartSample < 0)
        return liveSample;

    int sourceRelativeSample = relativeSample;
    switch (decision.baseMode)
    {
        case BaseMode::Repeat:
        {
            const int segmentLength = juce::jmax(1, sliceSamples / juce::jmax(1, decision.repeatSubdivision));
            sourceRelativeSample = relativeSample % segmentLength;
            break;
        }
        case BaseMode::MicroLoop:
            sourceRelativeSample = decision.loopStartOffsetSamples
                + (relativeSample % juce::jmax(1, decision.loopLengthSamples));
            break;
        case BaseMode::Reverse:
            sourceRelativeSample = (sliceSamples - 1) - relativeSample;
            break;
        case BaseMode::Skip:
            sourceRelativeSample = (relativeSample + decision.jumpOffsetSamples) % sliceSamples;
            break;
        case BaseMode::Pass:
        default:
            return liveSample;
    }

    sourceRelativeSample = juce::jlimit(0, sliceSamples - 1, sourceRelativeSample);
    const auto sourceSample = previousSliceStartSample + sourceRelativeSample;
    if (sourceSample >= currentSample)
        return liveSample;

    if ((currentSample - 1 - sourceSample) >= availableHistorySamples)
        return liveSample;

    return readHistorySample(channel, currentSample, sourceSample);
}

float MonstaFxBlock::readHistorySample(int channel, std::int64_t currentSample, std::int64_t sourceSample) const
{
    if (historyCapacitySamples <= 0 || availableHistorySamples <= 0)
        return 0.0f;

    const auto samplesBack = currentSample - 1 - sourceSample;
    if (samplesBack < 0 || samplesBack >= availableHistorySamples || samplesBack >= historyCapacitySamples)
        return 0.0f;

    int historyIndex = historyWritePosition - 1 - static_cast<int>(samplesBack);
    while (historyIndex < 0)
        historyIndex += historyCapacitySamples;

    historyIndex %= historyCapacitySamples;
    return historyBuffer.getSample(channel, historyIndex);
}

float MonstaFxBlock::applyGateEnvelope(int relativeSample, int sliceSamples, const SliceDecision& decision) const
{
    if (!decision.gate || decision.gateSteps <= 0 || sliceSamples <= 0)
        return 1.0f;

    const int stepLength = juce::jmax(1, sliceSamples / decision.gateSteps);
    const int stepIndex = juce::jlimit(0, decision.gateSteps - 1, relativeSample / stepLength);
    const auto stepGain = [&](int index)
    {
        return ((decision.gateMask >> index) & 1u) != 0u ? 1.0f : 0.0f;
    };

    float gain = stepGain(stepIndex);
    const int fadeSamples = juce::jlimit(2, 24, stepLength / 3);
    const int localPosition = relativeSample - stepIndex * stepLength;

    if (stepIndex > 0 && localPosition < fadeSamples)
    {
        const float t = static_cast<float>(localPosition) / static_cast<float>(fadeSamples);
        gain = juce::jmap(t, stepGain(stepIndex - 1), gain);
    }
    else if (stepIndex + 1 < decision.gateSteps && localPosition > stepLength - fadeSamples)
    {
        const float t = static_cast<float>(localPosition - (stepLength - fadeSamples)) / static_cast<float>(fadeSamples);
        gain = juce::jmap(t, gain, stepGain(stepIndex + 1));
    }

    return juce::jlimit(0.0f, 1.0f, gain);
}

float MonstaFxBlock::applyEdgeWindow(int relativeSample, int segmentLength) const
{
    if (segmentLength <= 2)
        return 1.0f;

    const int fadeSamples = juce::jlimit(2, 32, segmentLength / 5);
    const float fadeIn = juce::jlimit(0.0f,
                                      1.0f,
                                      static_cast<float>(relativeSample) / static_cast<float>(juce::jmax(1, fadeSamples)));
    const float fadeOut = juce::jlimit(0.0f,
                                       1.0f,
                                       static_cast<float>((segmentLength - 1) - relativeSample)
                                           / static_cast<float>(juce::jmax(1, fadeSamples)));
    return juce::jlimit(0.0f, 1.0f, juce::jmin(fadeIn, fadeOut));
}

std::uint32_t MonstaFxBlock::hash32(std::uint32_t value) noexcept
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

std::uint32_t MonstaFxBlock::mixSeed(std::uint32_t base, std::uint32_t salt) noexcept
{
    return hash32(base ^ (salt + 0x9e3779b9u + (base << 6u) + (base >> 2u)));
}

float MonstaFxBlock::random01(std::uint32_t& state) noexcept
{
    state = hash32(state + 0x9e3779b9u);
    return static_cast<float>(state & 0x00ffffffu) / static_cast<float>(0x01000000u);
}

int MonstaFxBlock::randomInt(std::uint32_t& state, int minValue, int maxValue) noexcept
{
    if (maxValue <= minValue)
        return minValue;

    const int span = maxValue - minValue + 1;
    return minValue + static_cast<int>(std::floor(random01(state) * static_cast<float>(span)));
}

int MonstaFxBlock::chooseWeightedIndex(std::uint32_t& state, const std::array<float, 4>& weights) noexcept
{
    float total = 0.0f;
    for (const auto weight : weights)
        total += juce::jmax(0.0f, weight);

    if (total <= 0.0f)
        return 0;

    const float target = random01(state) * total;
    float accumulated = 0.0f;
    for (int index = 0; index < static_cast<int>(weights.size()); ++index)
    {
        accumulated += juce::jmax(0.0f, weights[static_cast<size_t>(index)]);
        if (target <= accumulated)
            return index;
    }

    return static_cast<int>(weights.size()) - 1;
}

float MonstaFxBlock::quantizeToBitDepth(float sample, int bitDepth) noexcept
{
    const int safeBits = juce::jlimit(4, 12, bitDepth);
    const float steps = static_cast<float>(1 << safeBits);
    const float normalized = juce::jlimit(-1.0f, 1.0f, sample);
    return std::round(normalized * steps) / steps;
}

void MonstaFxBlock::process(juce::AudioBuffer<float>& buffer,
                            const MonstaFxState& rawState,
                            const MonstaFxTimelineContext& rawTimeline)
{
    if (buffer.getNumSamples() <= 0 || buffer.getNumChannels() <= 0)
        return;

    MonstaFxState state = rawState;
    sanitizeMonstaFxState(state);

    const float dryGain = state.dry;
    const float wetGain = state.wet;
    const bool hasWet = wetGain > 0.001f;
    const bool touchesSignal = state.enabled || std::abs(dryGain - 1.0f) > 0.001f || hasWet;
    if (!touchesSignal)
        return;

    ensureScratchCapacity(buffer.getNumChannels(), buffer.getNumSamples());
    ensureHistoryCapacity(buffer.getNumChannels());
    dryBuffer.makeCopyOf(buffer, true);

    if (!hasWet)
    {
        buffer.applyGain(dryGain);
        return;
    }

    const double bpm = juce::jlimit(kMinimumBpm,
                                    kMaximumBpm,
                                    rawTimeline.bpm > 0.0 ? rawTimeline.bpm : 120.0);
    const bool blockDiscontinuity = expectedNextBlockSample != std::numeric_limits<std::int64_t>::min()
        && rawTimeline.blockStartSample != expectedNextBlockSample;
    const bool quarterInvalid = !std::isfinite(rawTimeline.blockStartQuarter);
    const bool bpmChanged = std::abs(bpm - lastBpm) > 0.05;

    if (blockDiscontinuity || quarterInvalid || bpmChanged)
    {
        historyBuffer.clear();
        resetHistoryState();
    }

    lastBpm = bpm;
    maybeRefreshProfile(state);

    const double samplesPerQuarter = (60.0 / bpm) * sampleRate;
    const double sliceQuarterLength = kSliceDivisionsInQuarterNotes[static_cast<size_t>(juce::jlimit(0,
                                                                                                        static_cast<int>(kSliceDivisionsInQuarterNotes.size()) - 1,
                                                                                                        activeProfile.divisionIndex))];

    std::int64_t activeSliceIndex = std::numeric_limits<std::int64_t>::min();
    std::int64_t activeSliceStartSample = 0;
    int activeSliceSamples = 0;
    SliceDecision activeDecision;
    int activeCrushGroup = -1;
    std::vector<float> heldCrushSamples(static_cast<size_t>(buffer.getNumChannels()), 0.0f);

    for (int sampleIndex = 0; sampleIndex < buffer.getNumSamples(); ++sampleIndex)
    {
        const auto currentSample = rawTimeline.blockStartSample + sampleIndex;
        const double quarterPosition = rawTimeline.blockStartQuarter + static_cast<double>(sampleIndex) / samplesPerQuarter;
        const auto sliceIndex = static_cast<std::int64_t>(std::floor(quarterPosition / sliceQuarterLength + 1.0e-9));

        if (sliceIndex != activeSliceIndex)
        {
            activeSliceIndex = sliceIndex;
            activeSliceSamples = juce::jmax(1, static_cast<int>(std::llround(sliceQuarterLength * samplesPerQuarter)));
            activeSliceStartSample = static_cast<std::int64_t>(std::llround(static_cast<double>(sliceIndex)
                                                                            * sliceQuarterLength
                                                                            * samplesPerQuarter));
            activeDecision = makeSliceDecision(sliceIndex, activeSliceSamples);
            activeCrushGroup = -1;
            std::fill(heldCrushSamples.begin(), heldCrushSamples.end(), 0.0f);
        }

        const int relativeSample = juce::jlimit(0,
                                                juce::jmax(0, activeSliceSamples - 1),
                                                static_cast<int>(currentSample - activeSliceStartSample));
        const bool transformsSlice = activeDecision.baseMode != BaseMode::Pass;
        const float gateEnvelope = applyGateEnvelope(relativeSample, activeSliceSamples, activeDecision);
        const float sliceWindow = applyEdgeWindow(relativeSample, activeSliceSamples);

        if (activeDecision.crush)
        {
            const int crushGroup = relativeSample / juce::jmax(1, activeDecision.crushHoldSamples);
            if (crushGroup != activeCrushGroup)
            {
                activeCrushGroup = crushGroup;
                for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                {
                    const float liveSample = dryBuffer.getSample(channel, sampleIndex);
                    const float baseSample = readOperationSample(channel,
                                                                 liveSample,
                                                                 currentSample,
                                                                 activeSliceStartSample,
                                                                 activeSliceSamples,
                                                                 relativeSample,
                                                                 activeDecision);
                    heldCrushSamples[static_cast<size_t>(channel)] = quantizeToBitDepth(baseSample, activeDecision.crushBitDepth);
                }
            }
        }

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const float liveSample = dryBuffer.getSample(channel, sampleIndex);
            float wetSample = readOperationSample(channel,
                                                  liveSample,
                                                  currentSample,
                                                  activeSliceStartSample,
                                                  activeSliceSamples,
                                                  relativeSample,
                                                  activeDecision);

            if (activeDecision.crush)
                wetSample = heldCrushSamples[static_cast<size_t>(channel)];

            wetSample *= gateEnvelope;

            if (transformsSlice || activeDecision.gate)
                wetSample = liveSample + (wetSample - liveSample) * sliceWindow;

            wetSample *= activeDecision.outputTrim;
            buffer.setSample(channel, sampleIndex, liveSample * dryGain + wetSample * wetGain);
        }

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            historyBuffer.setSample(channel, historyWritePosition, dryBuffer.getSample(channel, sampleIndex));

        historyWritePosition = (historyWritePosition + 1) % historyCapacitySamples;
        availableHistorySamples = juce::jmin(availableHistorySamples + 1, historyCapacitySamples);
    }

    expectedNextBlockSample = rawTimeline.blockStartSample + buffer.getNumSamples();
}
} // namespace bbg