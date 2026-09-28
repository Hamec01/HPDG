#include "StyleTargetModel.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace bbg
{
namespace
{
float clamp01(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

float gaussianFit(float value, float target, float tolerance)
{
    const float z = (value - target) / std::max(0.025f, tolerance);
    return std::exp(-0.5f * z * z);
}
}

StyleTargetProfile StyleTargetModel::withPerformanceIntent(const StyleTargetProfile& base,
                                                           float density,
                                                           float swing,
                                                           float humanize,
                                                           float variation)
{
    auto result = base;
    const float densityIntent = clamp01(density) - 0.5f;
    const float swingIntent = std::clamp((swing - 0.50f) / 0.25f, 0.0f, 1.0f);
    const float humanIntent = clamp01(humanize) - 0.5f;
    const float variationIntent = clamp01(variation) - 0.5f;

    result.target.density = clamp01(base.target.density + densityIntent * 0.48f);
    result.target.negativeSpace = clamp01(base.target.negativeSpace - densityIntent * 0.34f);
    result.target.syncopation = clamp01(base.target.syncopation + variationIntent * 0.16f);
    result.target.repetition = clamp01(base.target.repetition - variationIntent * 0.30f);
    result.target.timingActivity = clamp01(base.target.timingActivity
                                           + swingIntent * 0.16f
                                           + humanIntent * 0.12f);
    result.target.velocityLife = clamp01(base.target.velocityLife + humanIntent * 0.18f);
    return result;
}

StyleTargetMatch StyleTargetModel::evaluate(const PatternFeatureVector& features,
                                            const StyleTargetProfile& profile)
{
    StyleTargetMatch result;
    const std::array<float, 8> values {
        features.density, features.syncopation, features.velocityLife, features.timingActivity,
        features.negativeSpace, features.repetition, features.interlock, features.roleClarity
    };
    const std::array<float, 8> targets {
        profile.target.density, profile.target.syncopation, profile.target.velocityLife, profile.target.timingActivity,
        profile.target.negativeSpace, profile.target.repetition, profile.target.interlock, profile.target.roleClarity
    };
    const std::array<float, 8> tolerances {
        profile.tolerance.density, profile.tolerance.syncopation, profile.tolerance.velocityLife, profile.tolerance.timingActivity,
        profile.tolerance.negativeSpace, profile.tolerance.repetition, profile.tolerance.interlock, profile.tolerance.roleClarity
    };
    const std::array<float, 8> weights {
        profile.weight.density, profile.weight.syncopation, profile.weight.velocityLife, profile.weight.timingActivity,
        profile.weight.negativeSpace, profile.weight.repetition, profile.weight.interlock, profile.weight.roleClarity
    };

    std::array<float, 8> fits {};
    float weightedFit = 0.0f;
    float weightedDistance = 0.0f;
    float weightSum = 0.0f;
    for (size_t index = 0; index < values.size(); ++index)
    {
        fits[index] = gaussianFit(values[index], targets[index], tolerances[index]);
        const float weight = std::max(0.0f, weights[index]);
        const float normalizedDistance = std::abs(values[index] - targets[index]) / std::max(0.025f, tolerances[index]);
        weightedFit += weight * fits[index];
        weightedDistance += weight * normalizedDistance;
        weightSum += weight;
    }

    const float divisor = std::max(0.0001f, weightSum);
    result.fit = clamp01(weightedFit / divisor);
    result.weightedDistance = weightedDistance / divisor;
    result.featureFit = { fits[0], fits[1], fits[2], fits[3], fits[4], fits[5], fits[6], fits[7] };
    return result;
}
} // namespace bbg
