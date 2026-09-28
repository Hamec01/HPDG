#pragma once

#include <cstdint>
#include <vector>

namespace bbg
{
struct CandidateSelectionEntry
{
    float quality = 0.0f;
    float novelty = 0.0f;
    bool hardValid = true;
};

struct CandidateSelectionConfig
{
    float qualityFloor = 0.0f;
    float nearBestTolerance = 0.05f;
    float temperature = 0.15f;
    float noveltyWeight = 0.08f;
    std::uint32_t seed = 1;
};

struct CandidateSelectionResult
{
    int selectedIndex = 0;
    int bestIndex = 0;
    int nearBestPoolSize = 0;
    float bestQuality = 0.0f;
    float selectedQuality = 0.0f;
};

class CandidateSelectionEngine
{
public:
    static CandidateSelectionResult select(const std::vector<CandidateSelectionEntry>& entries,
                                           const CandidateSelectionConfig& config);
};
} // namespace bbg
