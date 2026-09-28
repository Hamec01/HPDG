#include "PatternFeatureVector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <set>

namespace bbg
{
namespace
{
float clamp01(float value) { return std::clamp(value, 0.0f, 1.0f); }
}

float PatternFeatureVector::surfaceNovelty() const
{
    return clamp01(0.30f * syncopation
                 + 0.24f * (1.0f - repetition)
                 + 0.18f * timingActivity
                 + 0.16f * velocityLife
                 + 0.12f * interlock);
}

PatternFeatureVector PatternFeatureExtractor::extract(const PatternFeatureInput& input)
{
    PatternFeatureVector out;
    const int bars = std::max(1, input.bars);
    const int ticksPerBar = std::max(1, input.ticksPerBar);
    const int totalTicks = bars * ticksPerBar;
    if (input.events.empty())
    {
        out.negativeSpace = 1.0f;
        return out;
    }

    out.density = clamp01(static_cast<float>(input.events.size()) / static_cast<float>(bars * 32));

    float syncTotal = 0.0f;
    float timingTotal = 0.0f;
    float velocityMean = 0.0f;
    int explainedRoles = 0;
    for (const auto& event : input.events)
    {
        const int local = ((event.tick % ticksPerBar) + ticksPerBar) % ticksPerBar;
        const int sixteenth = std::max(1, ticksPerBar / 16);
        const int eighth = std::max(1, ticksPerBar / 8);
        const int quarter = std::max(1, ticksPerBar / 4);
        syncTotal += local % quarter == 0 ? 0.0f : local % eighth == 0 ? 0.35f : local % sixteenth == 0 ? 0.68f : 1.0f;
        timingTotal += std::min(1.0f, std::abs(static_cast<float>(event.microTimingPpq)) / 24.0f);
        velocityMean += static_cast<float>(event.velocity);
        if (event.role != MusicalRole::Unknown)
            ++explainedRoles;
    }
    out.syncopation = syncTotal / static_cast<float>(input.events.size());
    out.timingActivity = timingTotal / static_cast<float>(input.events.size());
    out.roleClarity = static_cast<float>(explainedRoles) / static_cast<float>(input.events.size());

    velocityMean /= static_cast<float>(input.events.size());
    float velocityVariance = 0.0f;
    for (const auto& event : input.events)
    {
        const float delta = static_cast<float>(event.velocity) - velocityMean;
        velocityVariance += delta * delta;
    }
    velocityVariance /= static_cast<float>(input.events.size());
    out.velocityLife = clamp01(std::sqrt(velocityVariance) / 22.0f);

    constexpr int windowTicks = 4;
    int emptyWindows = 0;
    int windowCount = 0;
    for (int start = 0; start < totalTicks; start += windowTicks)
    {
        const bool occupied = std::any_of(input.events.begin(), input.events.end(), [=](const auto& event)
        {
            return event.tick >= start && event.tick < start + windowTicks;
        });
        emptyWindows += occupied ? 0 : 1;
        ++windowCount;
    }
    out.negativeSpace = static_cast<float>(emptyWindows) / static_cast<float>(std::max(1, windowCount));

    float similarityTotal = 0.0f;
    int comparisons = 0;
    std::set<std::pair<int, int>> firstBarSignature;
    for (const auto& event : input.events)
        if (event.tick < ticksPerBar)
            firstBarSignature.insert({ static_cast<int>(event.lane), event.tick });
    for (int bar = 1; bar < bars; ++bar)
    {
        std::set<std::pair<int, int>> signature;
        for (const auto& event : input.events)
            if (event.tick / ticksPerBar == bar)
                signature.insert({ static_cast<int>(event.lane), event.tick % ticksPerBar });
        int intersection = 0;
        for (const auto& item : signature)
            intersection += firstBarSignature.count(item) > 0 ? 1 : 0;
        const int unionSize = static_cast<int>(firstBarSignature.size() + signature.size()) - intersection;
        similarityTotal += unionSize > 0 ? static_cast<float>(intersection) / static_cast<float>(unionSize) : 1.0f;
        ++comparisons;
    }
    out.repetition = comparisons > 0 ? similarityTotal / static_cast<float>(comparisons) : 1.0f;

    int relatedEvents = 0;
    for (size_t i = 0; i < input.events.size(); ++i)
    {
        const bool related = std::any_of(input.events.begin(), input.events.end(), [&](const auto& other)
        {
            return &other != &input.events[i]
                && other.lane != input.events[i].lane
                && std::abs(other.tick - input.events[i].tick) <= 2;
        });
        relatedEvents += related ? 1 : 0;
    }
    out.interlock = static_cast<float>(relatedEvents) / static_cast<float>(input.events.size());
    return out;
}
} // namespace bbg
