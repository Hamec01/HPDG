#pragma once

#include <algorithm>
#include <array>

#include <juce_core/juce_core.h>

namespace bbg::Sub808Scale
{
inline const std::array<int, 7>& intervalsForScale(int scaleMode)
{
    static const std::array<int, 7> minor { 0, 2, 3, 5, 7, 8, 10 };
    static const std::array<int, 7> major { 0, 2, 4, 5, 7, 9, 11 };
    static const std::array<int, 7> harmonicMinor { 0, 2, 3, 5, 7, 8, 11 };

    if (scaleMode == 1)
        return major;
    if (scaleMode == 2)
        return harmonicMinor;
    return minor;
}

inline bool pitchClassInScale(int pitchClass, int keyRoot, int scaleMode)
{
    const int normalizedPitchClass = ((pitchClass % 12) + 12) % 12;
    const int normalizedRoot = ((keyRoot % 12) + 12) % 12;
    const int intervalFromRoot = (normalizedPitchClass - normalizedRoot + 12) % 12;
    const auto& scale = intervalsForScale(scaleMode);
    return std::find(scale.begin(), scale.end(), intervalFromRoot) != scale.end();
}

inline bool isPitchInScale(int pitch, int keyRoot, int scaleMode)
{
    return pitchClassInScale(pitch, keyRoot, scaleMode);
}

inline bool isRootPitch(int pitch, int keyRoot)
{
    return (((pitch - keyRoot) % 12) + 12) % 12 == 0;
}

inline int snapPitchToScale(int pitch, int keyRoot, int scaleMode, int minPitch, int maxPitch)
{
    const int clamped = juce::jlimit(minPitch, maxPitch, pitch);
    if (isPitchInScale(clamped, keyRoot, scaleMode))
        return clamped;

    for (int distance = 1; distance <= 12; ++distance)
    {
        const int down = juce::jmax(minPitch, clamped - distance);
        if (isPitchInScale(down, keyRoot, scaleMode))
            return down;

        const int up = juce::jmin(maxPitch, clamped + distance);
        if (isPitchInScale(up, keyRoot, scaleMode))
            return up;
    }

    return clamped;
}
} // namespace bbg::Sub808Scale