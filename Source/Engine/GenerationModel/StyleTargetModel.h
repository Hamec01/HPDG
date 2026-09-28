#pragma once

#include "PatternFeatureVector.h"

namespace bbg
{
struct StyleTargetProfile
{
    PatternFeatureVector target;
    PatternFeatureVector tolerance;
    PatternFeatureVector weight;
};

struct StyleTargetMatch
{
    float fit = 0.0f;
    float weightedDistance = 0.0f;
    PatternFeatureVector featureFit;
};

class StyleTargetModel
{
public:
    static StyleTargetProfile withPerformanceIntent(const StyleTargetProfile& base,
                                                    float density,
                                                    float swing,
                                                    float humanize,
                                                    float variation);

    static StyleTargetMatch evaluate(const PatternFeatureVector& features,
                                     const StyleTargetProfile& profile);
};
} // namespace bbg
