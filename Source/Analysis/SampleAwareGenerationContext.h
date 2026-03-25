#pragma once

#include "AudioFeatureMap.h"

namespace bbg
{
struct SampleAwareGenerationContext
{
    bool enabled = false;
    AudioFeatureMap featureMap;
    float supportVsContrast = 0.5f;
    float reactivity = 0.7f;
};
} // namespace bbg
