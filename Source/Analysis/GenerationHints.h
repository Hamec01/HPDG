#pragma once

#include <vector>

namespace bbg
{
struct GenerationHints
{
    std::vector<float> kickStepWeights;
    std::vector<float> snareStepWeights;
    std::vector<float> hatStepWeights;
    std::vector<float> openHatStepWeights;
    std::vector<float> percStepWeights;
    std::vector<float> bassStepWeights;

    std::vector<int> preferredKickAnchors;
    std::vector<int> preferredSnareAnchors;
    std::vector<int> phraseBoundaries;

    bool drumsLikelyPresent = false;
    bool bassLikelyPresent = false;
};
} // namespace bbg