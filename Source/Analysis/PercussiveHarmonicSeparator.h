#pragma once

#include <algorithm>
#include <vector>

#include "OnsetDetector.h"

namespace bbg
{
struct SeparationFrame
{
    float percussive = 0.0f;
    float harmonic = 0.0f;
    float sustain = 0.0f;
};

struct PercussiveHarmonicSeparation
{
    std::vector<SeparationFrame> frames;
};

class PercussiveHarmonicSeparator
{
public:
    PercussiveHarmonicSeparation separate(const SpectralAnalysis& spectral,
                                          const OnsetDetectionResult& onsets) const
    {
        PercussiveHarmonicSeparation result;
        result.frames.resize(spectral.frames.size());

        if (spectral.frames.empty() || onsets.frames.size() != spectral.frames.size())
            return result;

        float maxPercussive = 0.0f;
        float maxHarmonic = 0.0f;
        float maxSustain = 0.0f;

        for (size_t index = 0; index < spectral.frames.size(); ++index)
        {
            const auto start = index > 2 ? index - 2 : 0;
            const auto end = std::min(spectral.frames.size(), index + 3);

            float smoothRms = 0.0f;
            float smoothLowMid = 0.0f;
            int count = 0;
            for (size_t frameIndex = start; frameIndex < end; ++frameIndex)
            {
                smoothRms += spectral.frames[frameIndex].rms;
                smoothLowMid += 0.5f * (spectral.frames[frameIndex].lowEnergy + spectral.frames[frameIndex].midEnergy);
                ++count;
            }

            smoothRms /= static_cast<float>(std::max(1, count));
            smoothLowMid /= static_cast<float>(std::max(1, count));

            const auto& spectralFrame = spectral.frames[index];
            const auto& onsetFrame = onsets.frames[index];
            auto& separated = result.frames[index];

            separated.percussive = std::clamp(0.55f * onsetFrame.broadband
                                                  + 0.20f * onsetFrame.high
                                                  + 0.15f * spectralFrame.spectralFlux
                                                  + 0.10f * std::max(0.0f, spectralFrame.rms - smoothRms),
                                              0.0f,
                                              1.0f);

            separated.sustain = std::clamp(0.60f * std::max(0.0f, smoothRms - 0.35f * onsetFrame.broadband)
                                               + 0.40f * smoothLowMid,
                                           0.0f,
                                           1.0f);

            separated.harmonic = std::clamp(0.42f * separated.sustain
                                                + 0.32f * (1.0f - spectralFrame.flatness)
                                                + 0.26f * smoothLowMid,
                                            0.0f,
                                            1.0f);

            maxPercussive = std::max(maxPercussive, separated.percussive);
            maxHarmonic = std::max(maxHarmonic, separated.harmonic);
            maxSustain = std::max(maxSustain, separated.sustain);
        }

        const float invPercussive = maxPercussive > 1.0e-6f ? 1.0f / maxPercussive : 1.0f;
        const float invHarmonic = maxHarmonic > 1.0e-6f ? 1.0f / maxHarmonic : 1.0f;
        const float invSustain = maxSustain > 1.0e-6f ? 1.0f / maxSustain : 1.0f;

        for (auto& separated : result.frames)
        {
            separated.percussive *= invPercussive;
            separated.harmonic *= invHarmonic;
            separated.sustain *= invSustain;
        }

        return result;
    }
};
} // namespace bbg