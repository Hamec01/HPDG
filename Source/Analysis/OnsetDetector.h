#pragma once

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

#include "STFTAnalyzer.h"

namespace bbg
{
struct OnsetFrame
{
    float broadband = 0.0f;
    float low = 0.0f;
    float mid = 0.0f;
    float high = 0.0f;
    bool transient = false;
};

struct OnsetDetectionResult
{
    std::vector<OnsetFrame> frames;
    float transientThreshold = 0.0f;
};

class OnsetDetector
{
public:
    OnsetDetectionResult detect(const SpectralAnalysis& spectral) const
    {
        OnsetDetectionResult result;
        result.frames.resize(spectral.frames.size());

        if (spectral.frames.empty())
            return result;

        float maxBroadband = 0.0f;
        float maxLow = 0.0f;
        float maxMid = 0.0f;
        float maxHigh = 0.0f;

        for (size_t index = 0; index < spectral.frames.size(); ++index)
        {
            const auto& frame = spectral.frames[index];
            const auto& prev = spectral.frames[index > 0 ? index - 1 : index];

            auto& onset = result.frames[index];
            onset.low = std::max(0.0f, frame.lowEnergy - prev.lowEnergy);
            onset.mid = std::max(0.0f, frame.midEnergy - prev.midEnergy);
            onset.high = std::max(0.0f, frame.highEnergy - prev.highEnergy);

            const float rmsDelta = std::max(0.0f, frame.rms - prev.rms);
            onset.broadband = 0.50f * frame.spectralFlux + 0.30f * rmsDelta + 0.20f * (onset.low + onset.mid + onset.high) / 3.0f;

            maxBroadband = std::max(maxBroadband, onset.broadband);
            maxLow = std::max(maxLow, onset.low);
            maxMid = std::max(maxMid, onset.mid);
            maxHigh = std::max(maxHigh, onset.high);
        }

        const float invBroadband = maxBroadband > 1.0e-6f ? 1.0f / maxBroadband : 1.0f;
        const float invLow = maxLow > 1.0e-6f ? 1.0f / maxLow : 1.0f;
        const float invMid = maxMid > 1.0e-6f ? 1.0f / maxMid : 1.0f;
        const float invHigh = maxHigh > 1.0e-6f ? 1.0f / maxHigh : 1.0f;

        std::vector<float> broadbandValues;
        broadbandValues.reserve(result.frames.size());

        for (auto& onset : result.frames)
        {
            onset.broadband *= invBroadband;
            onset.low *= invLow;
            onset.mid *= invMid;
            onset.high *= invHigh;
            broadbandValues.push_back(onset.broadband);
        }

        const float mean = broadbandValues.empty()
            ? 0.0f
            : std::accumulate(broadbandValues.begin(), broadbandValues.end(), 0.0f) / static_cast<float>(broadbandValues.size());

        float variance = 0.0f;
        for (const float value : broadbandValues)
        {
            const float delta = value - mean;
            variance += delta * delta;
        }

        const float deviation = broadbandValues.size() > 1
            ? std::sqrt(variance / static_cast<float>(broadbandValues.size()))
            : 0.0f;
        result.transientThreshold = std::clamp(mean + 0.35f * deviation, 0.18f, 0.72f);

        for (auto& onset : result.frames)
            onset.transient = onset.broadband >= result.transientThreshold;

        return result;
    }
};
} // namespace bbg