#pragma once

#include <algorithm>

#include "TimingHelpers.h"

namespace bbg
{
inline int clampVelocity(int velocity)
{
    return std::max(1, std::min(127, velocity));
}
} // namespace bbg
