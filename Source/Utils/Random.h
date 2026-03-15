#pragma once

#include <random>

namespace bbg
{
inline std::mt19937 makeRng(int seed)
{
    return std::mt19937(static_cast<std::mt19937::result_type>(seed));
}
} // namespace bbg
