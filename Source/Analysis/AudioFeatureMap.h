#pragma once

#include <vector>

namespace bbg
{
struct StepFeature
{
    float energy = 0.0f;
    float accent = 0.0f;
    float onset = 0.0f;
    float low = 0.0f;
    float mid = 0.0f;
    float high = 0.0f;

    bool isStrongBeat = false;
    bool isWeakBeat = false;
    bool nearPhraseBoundary = false;
};

struct AudioFeatureMap
{
    int bars = 0;
    int stepsPerBar = 16;
    std::vector<StepFeature> steps;
    std::vector<float> barDensity;
};
} // namespace bbg
