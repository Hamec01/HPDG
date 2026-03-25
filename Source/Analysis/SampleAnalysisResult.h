#pragma once

#include <vector>

namespace bbg
{
struct SampleAnalysisResult
{
    bool valid = false;

    double sampleRate = 44100.0;
    double detectedBpm = 0.0;
    bool bpmFromHost = false;
    bool bpmReliable = false;

    int analyzedBars = 0;
    int stepsPerBar = 16;
    int totalSteps = 0;

    std::vector<float> energyPerStep;
    std::vector<float> accentPerStep;
    std::vector<float> onsetStrengthPerStep;
    std::vector<float> lowBandPerStep;
    std::vector<float> midBandPerStep;
    std::vector<float> highBandPerStep;

    std::vector<float> densityPerBar;
    std::vector<int> phraseBoundaryBars;

    bool sparse = false;
    bool dense = false;
    bool transientRich = false;
    bool loopLike = false;
};
} // namespace bbg
