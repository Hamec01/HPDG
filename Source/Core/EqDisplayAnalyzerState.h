#pragma once

#include <array>

namespace bbg
{
inline constexpr int kEqDisplayAnalyzerBinCount = 48;

struct EqDisplayAnalyzerState
{
    std::array<float, kEqDisplayAnalyzerBinCount> magnitudes {};
    float rms = 0.0f;
    bool active = false;
};
} // namespace bbg