#pragma once

#include <algorithm>
#include <cmath>
#include <random>

namespace bbg::BoomBapTiming
{
constexpr double PPQ = 960.0;

struct TimingDistribution
{
    float meanPPQ = 0.0f;
    float sigmaPPQ = 1.0f;
    float minPPQ = -3.0f;
    float maxPPQ = 3.0f;
};

struct TimingBreakdown
{
    int structuralSwingPPQ = 0;
    int pocketPPQ = 0;
    int humanJitterPPQ = 0;
    int total() const noexcept { return structuralSwingPPQ + pocketPPQ + humanJitterPPQ; }
};

inline double parameterPercentToSwingRatio(double percent)
{
    return std::clamp(percent / 100.0, 0.50, 0.75);
}

inline int getSixteenthSwingOffsetPPQ(double swingRatio)
{
    const auto ratio = std::clamp(swingRatio, 0.50, 0.75);
    return static_cast<int>(std::lround(PPQ * (2.0 * ratio - 1.0) * 0.25));
}

inline double ppqToMilliseconds(int ticks, double bpm)
{
    return static_cast<double>(ticks) * 60000.0 / (PPQ * std::clamp(bpm, 40.0, 220.0));
}

inline int sampleTruncatedGaussianPPQ(std::mt19937& rng, const TimingDistribution& distribution)
{
    if (distribution.sigmaPPQ <= 0.0f)
        return static_cast<int>(std::lround(std::clamp(distribution.meanPPQ, distribution.minPPQ, distribution.maxPPQ)));
    std::normal_distribution<float> gaussian(distribution.meanPPQ, distribution.sigmaPPQ);
    return static_cast<int>(std::lround(std::clamp(gaussian(rng), distribution.minPPQ, distribution.maxPPQ)));
}
} // namespace bbg::BoomBapTiming
