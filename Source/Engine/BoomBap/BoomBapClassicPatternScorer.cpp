#include "BoomBapClassicAlgebraGenerator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <set>

namespace bbg
{
namespace
{
constexpr int kTicksPerBar = 64;

int tickInBar(const BoomBapClassicAlgebraNote& note)
{
    const int value = note.tick64 % kTicksPerBar;
    return value < 0 ? value + kTicksPerBar : value;
}

bool hasLaneTick(const BoomBapClassicAlgebraPattern& pattern, int lane, int bar, int tick)
{
    const auto& notes = pattern.notesByLane[static_cast<size_t>(lane)];
    return std::any_of(notes.begin(), notes.end(), [bar, tick](const auto& note)
    {
        return note.barIndex == bar && tickInBar(note) == tick;
    });
}

int countLane(const BoomBapClassicAlgebraPattern& pattern, int lane)
{
    return static_cast<int>(pattern.notesByLane[static_cast<size_t>(lane)].size());
}

float closenessScore(int value, int ideal, int tolerance)
{
    if (tolerance <= 0)
        return value == ideal ? 1.0f : 0.0f;
    return std::clamp(1.0f - std::abs(value - ideal) / static_cast<float>(tolerance), 0.0f, 1.0f);
}

std::set<int> barSignature(const BoomBapClassicAlgebraPattern& pattern, int bar)
{
    std::set<int> signature;
    for (int lane = 0; lane < BoomBapClassicLanes::Count; ++lane)
    {
        if (lane == BoomBapClassicLanes::Sub808)
            continue;

        for (const auto& note : pattern.notesByLane[static_cast<size_t>(lane)])
            if (note.barIndex == bar)
                signature.insert(lane * 1000 + tickInBar(note));
    }
    return signature;
}

float velocityVarianceScore(const std::vector<BoomBapClassicAlgebraNote>& notes)
{
    if (notes.size() < 2)
        return 0.5f;

    const float mean = std::accumulate(notes.begin(), notes.end(), 0.0f, [](float sum, const auto& note)
    {
        return sum + static_cast<float>(note.velocity);
    }) / static_cast<float>(notes.size());

    float variance = 0.0f;
    for (const auto& note : notes)
        variance += (static_cast<float>(note.velocity) - mean) * (static_cast<float>(note.velocity) - mean);
    variance /= static_cast<float>(notes.size());

    return std::clamp(std::sqrt(variance) / 18.0f, 0.0f, 1.0f);
}
} // namespace

BoomBapClassicScoreBreakdown BoomBapClassicPatternScorer::score(const BoomBapClassicAlgebraPattern& pattern,
                                                                const BoomBapClassicAlgebraParams& params) const
{
    BoomBapClassicScoreBreakdown out;
    const int bars = std::max(1, params.bars);

    int backbeats = 0;
    for (int bar = 0; bar < bars; ++bar)
    {
        if (hasLaneTick(pattern, BoomBapClassicLanes::Snare, bar, 16))
            ++backbeats;
        if (hasLaneTick(pattern, BoomBapClassicLanes::Snare, bar, 48))
            ++backbeats;
    }
    out.backbeatScore = static_cast<float>(backbeats) / static_cast<float>(bars * 2);

    int kickAnchorHits = 0;
    for (int bar = 0; bar < bars; ++bar)
    {
        if (hasLaneTick(pattern, BoomBapClassicLanes::Kick, bar, 0))
            kickAnchorHits += 2;
        if (hasLaneTick(pattern, BoomBapClassicLanes::Kick, bar, 32))
            ++kickAnchorHits;
    }
    out.kickAnchorScore = std::clamp(static_cast<float>(kickAnchorHits) / static_cast<float>(bars * 3), 0.0f, 1.0f);

    const int kickCount = countLane(pattern, BoomBapClassicLanes::Kick);
    const int hats = countLane(pattern, BoomBapClassicLanes::HiHat) + countLane(pattern, BoomBapClassicLanes::HatAccent);
    const int ghosts = countLane(pattern, BoomBapClassicLanes::ClapGhost) + countLane(pattern, BoomBapClassicLanes::KickGhost);
    const int idealKicks = std::clamp(static_cast<int>(std::lround(bars * (2.0f + params.density * 1.0f))), bars, bars * 4);
    out.grooveScore = 0.45f * closenessScore(kickCount, idealKicks, bars * 2)
        + 0.35f * closenessScore(hats, bars * 8 + static_cast<int>(params.density * bars * 3.0f), bars * 4)
        + 0.20f * closenessScore(ghosts, static_cast<int>(params.density * bars * 1.5f), bars * 2);

    std::vector<std::set<int>> signatures;
    for (int bar = 0; bar < bars; ++bar)
        signatures.push_back(barSignature(pattern, bar));
    int identicalPairs = 0;
    int pairs = 0;
    for (int a = 0; a < bars; ++a)
    {
        for (int b = a + 1; b < bars; ++b)
        {
            ++pairs;
            if (signatures[static_cast<size_t>(a)] == signatures[static_cast<size_t>(b)])
                ++identicalPairs;
        }
    }
    out.variationScore = pairs > 0 ? 1.0f - static_cast<float>(identicalPairs) / static_cast<float>(pairs) : 0.65f;
    if (bars >= 2 && signatures[0] == signatures[1])
        out.variationScore = std::max(0.55f, out.variationScore);

    const int totalNotes = static_cast<int>(pattern.allNotes().size());
    const int idealTotal = bars * (10 + static_cast<int>(params.density * 6.0f));
    out.densityBalanceScore = closenessScore(totalNotes, idealTotal, bars * 8);
    out.velocityHumanityScore = 0.65f * velocityVarianceScore(pattern.notesByLane[BoomBapClassicLanes::HiHat])
        + 0.35f * velocityVarianceScore(pattern.allNotes());

    int collisions = 0;
    for (int bar = 0; bar < bars; ++bar)
    {
        for (const int tick : { 16, 48 })
            if (hasLaneTick(pattern, BoomBapClassicLanes::Kick, bar, tick))
                ++collisions;
    }
    const int openHats = countLane(pattern, BoomBapClassicLanes::OpenHat);
    const int cymbals = countLane(pattern, BoomBapClassicLanes::Cymbal);
    const int perc = countLane(pattern, BoomBapClassicLanes::Perc);
    out.conflictPenalty = std::clamp(collisions / static_cast<float>(bars) + std::max(0, openHats - 2) * 0.4f + std::max(0, cymbals - 2) * 0.4f, 0.0f, 2.0f);
    out.spamPenalty = std::clamp(std::max(0, kickCount - bars * 4) * 0.25f
                                     + std::max(0, hats - bars * 13) * 0.08f
                                     + std::max(0, perc - 5) * 0.25f,
                                 0.0f,
                                 2.0f);

    out.quality = 1.4f * out.backbeatScore
        + 1.2f * out.kickAnchorScore
        + 1.3f * out.grooveScore
        + 0.9f * out.variationScore
        + 0.8f * out.densityBalanceScore
        + 0.7f * out.velocityHumanityScore
        - 1.2f * out.conflictPenalty
        - 1.0f * out.spamPenalty;
    return out;
}
} // namespace bbg
