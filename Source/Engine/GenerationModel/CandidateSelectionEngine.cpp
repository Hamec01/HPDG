#include "CandidateSelectionEngine.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace bbg
{
CandidateSelectionResult CandidateSelectionEngine::select(const std::vector<CandidateSelectionEntry>& entries,
                                                           const CandidateSelectionConfig& config)
{
    CandidateSelectionResult result;
    if (entries.empty())
        return result;

    std::vector<int> validIndices;
    for (int index = 0; index < static_cast<int>(entries.size()); ++index)
        if (entries[static_cast<size_t>(index)].hardValid)
            validIndices.push_back(index);
    if (validIndices.empty())
        for (int index = 0; index < static_cast<int>(entries.size()); ++index)
            validIndices.push_back(index);

    result.bestIndex = *std::max_element(validIndices.begin(), validIndices.end(), [&](int lhs, int rhs)
    {
        return entries[static_cast<size_t>(lhs)].quality < entries[static_cast<size_t>(rhs)].quality;
    });
    result.bestQuality = entries[static_cast<size_t>(result.bestIndex)].quality;

    std::vector<int> pool;
    for (int index = 0; index < static_cast<int>(entries.size()); ++index)
    {
        const auto& entry = entries[static_cast<size_t>(index)];
        if (entry.hardValid
            && entry.quality >= config.qualityFloor
            && entry.quality >= result.bestQuality - std::max(0.0f, config.nearBestTolerance))
            pool.push_back(index);
    }
    if (pool.empty())
        pool.push_back(result.bestIndex);

    const float temperature = std::max(0.01f, config.temperature);
    std::vector<double> weights;
    double totalWeight = 0.0;
    for (const int index : pool)
    {
        const auto& entry = entries[static_cast<size_t>(index)];
        const double qualityWeight = std::exp(static_cast<double>(entry.quality - result.bestQuality) / temperature);
        const double noveltyWeight = 1.0 + std::max(0.0f, config.noveltyWeight) * std::clamp(entry.novelty, 0.0f, 1.0f);
        weights.push_back(std::max(0.000001, qualityWeight * noveltyWeight));
        totalWeight += weights.back();
    }

    std::mt19937 rng(config.seed);
    std::uniform_real_distribution<double> distribution(0.0, totalWeight);
    double pick = distribution(rng);
    result.selectedIndex = pool.back();
    for (size_t index = 0; index < pool.size(); ++index)
    {
        pick -= weights[index];
        if (pick <= 0.0)
        {
            result.selectedIndex = pool[index];
            break;
        }
    }

    result.nearBestPoolSize = static_cast<int>(pool.size());
    result.selectedQuality = entries[static_cast<size_t>(result.selectedIndex)].quality;
    return result;
}
} // namespace bbg
