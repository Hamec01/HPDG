#pragma once

#include <algorithm>

namespace bbg
{
template <typename T>
inline T clampValue(T value, T lo, T hi)
{
    return std::clamp(value, lo, hi);
}

inline float normalizeRange(float value, float lo, float hi)
{
    if (hi <= lo)
        return 0.0f;

    return std::clamp((value - lo) / (hi - lo), 0.0f, 1.0f);
}
} // namespace bbg
