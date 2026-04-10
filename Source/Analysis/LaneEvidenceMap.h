#pragma once

#include <vector>

namespace bbg
{
struct StepEvidence
{
    float kickProbability = 0.0f;
    float snareProbability = 0.0f;
    float hatProbability = 0.0f;
    float openHatProbability = 0.0f;
    float percProbability = 0.0f;
    float bassProbability = 0.0f;

    float onsetBroadband = 0.0f;
    float onsetLow = 0.0f;
    float onsetMid = 0.0f;
    float onsetHigh = 0.0f;

    float lowEnergy = 0.0f;
    float midEnergy = 0.0f;
    float highEnergy = 0.0f;

    float transientSharpness = 0.0f;
    float sustain = 0.0f;
    float harmonicStrength = 0.0f;
    float percussiveStrength = 0.0f;

    bool nearPhraseBoundary = false;
    bool isStrongBeat = false;
    bool isWeakBeat = false;
};

struct LaneEvidenceMap
{
    int bars = 0;
    int stepsPerBar = 16;
    std::vector<StepEvidence> steps;
};
} // namespace bbg