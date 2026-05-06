#include "BoomBapClassicAlgebraGenerator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <numeric>
#include <random>

namespace bbg
{
namespace
{
constexpr int kTicksPerBar = 64;
constexpr int kTicksPerBeat = 16;
constexpr int kTicksPerSixteenth = 4;
constexpr int kTicksPerEighth = 8;

struct WeightedKick
{
    int tick = 0;
    float weight = 0.0f;
};

constexpr std::array<WeightedKick, 12> kKickWeights {{
    { 0, 1.00f },
    { 8, 0.45f },
    { 12, 0.35f },
    { 20, 0.25f },
    { 24, 0.50f },
    { 28, 0.20f },
    { 32, 0.70f },
    { 40, 0.45f },
    { 44, 0.35f },
    { 52, 0.25f },
    { 56, 0.45f },
    { 60, 0.30f }
}};

float clamp01(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

float random01(std::mt19937& rng)
{
    return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng);
}

int randomInt(std::mt19937& rng, int low, int high)
{
    return std::uniform_int_distribution<int>(low, high)(rng);
}

bool chance(std::mt19937& rng, float probability)
{
    return random01(rng) <= clamp01(probability);
}

int swingOffsetTicks(float swing)
{
    return std::clamp(static_cast<int>(std::lround((swing - 0.5f) * 8.0f)), 0, 3);
}

int barStart(int bar)
{
    return bar * kTicksPerBar;
}

int normalizeTickInBar(int tick64)
{
    const int value = tick64 % kTicksPerBar;
    return value < 0 ? value + kTicksPerBar : value;
}

bool isBackbeatTick(int tickInBar)
{
    return tickInBar == 16 || tickInBar == 48;
}

bool containsTick(const std::vector<BoomBapClassicAlgebraNote>& notes, int bar, int tickInBar)
{
    return std::any_of(notes.begin(), notes.end(), [bar, tickInBar](const auto& note)
    {
        return note.barIndex == bar && normalizeTickInBar(note.tick64) == tickInBar;
    });
}

bool laneHasAt(const BoomBapClassicAlgebraPattern& pattern, int lane, int bar, int tickInBar)
{
    return containsTick(pattern.notesByLane[static_cast<size_t>(lane)], bar, tickInBar);
}

void sortLane(std::vector<BoomBapClassicAlgebraNote>& notes)
{
    std::stable_sort(notes.begin(), notes.end(), [](const auto& a, const auto& b)
    {
        if (a.tick64 != b.tick64)
            return a.tick64 < b.tick64;
        return a.velocity > b.velocity;
    });
}

void dedupeLane(std::vector<BoomBapClassicAlgebraNote>& notes)
{
    sortLane(notes);
    notes.erase(std::unique(notes.begin(), notes.end(), [](const auto& a, const auto& b)
    {
        return a.laneIndex == b.laneIndex && a.tick64 == b.tick64;
    }), notes.end());
}

void addNote(BoomBapClassicAlgebraPattern& pattern,
             int lane,
             int bar,
             int tickInBar,
             int velocity,
             int microTimingTicks,
             BoomBapClassicRole role,
             int length = 1)
{
    if (lane < 0 || lane >= BoomBapClassicLanes::Count || bar < 0)
        return;

    BoomBapClassicAlgebraNote note;
    note.laneIndex = lane;
    note.barIndex = bar;
    note.tick64 = barStart(bar) + std::clamp(tickInBar, 0, kTicksPerBar - 1);
    note.length = std::max(1, length);
    note.velocity = std::clamp(velocity, 1, 127);
    note.microTimingTicks = microTimingTicks;
    note.role = role;
    note.roleString = BoomBapClassicAlgebraGenerator::roleToString(role);
    pattern.notesByLane[static_cast<size_t>(lane)].push_back(note);
}

int countLane(const BoomBapClassicAlgebraPattern& pattern, int lane)
{
    return static_cast<int>(pattern.notesByLane[static_cast<size_t>(lane)].size());
}

int countRole(const BoomBapClassicAlgebraPattern& pattern, BoomBapClassicRole role)
{
    int total = 0;
    for (const auto& lane : pattern.notesByLane)
        total += static_cast<int>(std::count_if(lane.begin(), lane.end(), [role](const auto& note) { return note.role == role; }));
    return total;
}

std::vector<int> laneTicksInBar(const BoomBapClassicAlgebraPattern& pattern, int lane, int bar)
{
    std::vector<int> ticks;
    for (const auto& note : pattern.notesByLane[static_cast<size_t>(lane)])
        if (note.barIndex == bar)
            ticks.push_back(normalizeTickInBar(note.tick64));
    std::sort(ticks.begin(), ticks.end());
    return ticks;
}

float barSimilarity(const BoomBapClassicAlgebraPattern& pattern, int lhsBar, int rhsBar)
{
    int compared = 0;
    int same = 0;
    for (int lane = 0; lane < BoomBapClassicLanes::Count; ++lane)
    {
        if (lane == BoomBapClassicLanes::Sub808)
            continue;

        const auto lhs = laneTicksInBar(pattern, lane, lhsBar);
        const auto rhs = laneTicksInBar(pattern, lane, rhsBar);
        compared += std::max(static_cast<int>(lhs.size()), static_cast<int>(rhs.size()));
        for (const int tick : lhs)
            if (std::find(rhs.begin(), rhs.end(), tick) != rhs.end())
                ++same;
    }

    return compared > 0 ? static_cast<float>(same) / static_cast<float>(compared) : 1.0f;
}

float sigmoid(float value)
{
    return 1.0f / (1.0f + std::exp(-value));
}
} // namespace

std::vector<BoomBapClassicAlgebraNote> BoomBapClassicAlgebraPattern::allNotes() const
{
    std::vector<BoomBapClassicAlgebraNote> out;
    for (const auto& lane : notesByLane)
        out.insert(out.end(), lane.begin(), lane.end());

    std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b)
    {
        if (a.tick64 != b.tick64)
            return a.tick64 < b.tick64;
        return a.laneIndex < b.laneIndex;
    });
    return out;
}

const char* BoomBapClassicAlgebraGenerator::roleToString(BoomBapClassicRole role)
{
    switch (role)
    {
        case BoomBapClassicRole::Anchor: return "anchor";
        case BoomBapClassicRole::Support: return "support";
        case BoomBapClassicRole::Ghost: return "ghost";
        case BoomBapClassicRole::Accent: return "accent";
        case BoomBapClassicRole::Fill: return "fill";
        case BoomBapClassicRole::Ending: return "ending";
        default: return "support";
    }
}

int BoomBapClassicAlgebraGenerator::ticksPerBar() { return kTicksPerBar; }
int BoomBapClassicAlgebraGenerator::ticksPerBeat() { return kTicksPerBeat; }
int BoomBapClassicAlgebraGenerator::ticksPerSixteenth() { return kTicksPerSixteenth; }
int BoomBapClassicAlgebraGenerator::ticksPerEighth() { return kTicksPerEighth; }

BoomBapClassicAlgebraPattern BoomBapClassicAlgebraGenerator::generate(const BoomBapClassicAlgebraParams& rawParams) const
{
    BoomBapClassicAlgebraParams params = rawParams;
    params.bars = std::clamp(params.bars, 1, 16);
    params.density = clamp01(params.density);
    params.swing = clamp01(params.swing);
    params.humanize = clamp01(params.humanize);
    params.variation = clamp01(params.variation);
    params.candidateCount = std::max(1, params.candidateCount);

    BoomBapClassicPatternScorer scorer;
    BoomBapClassicAlgebraPattern best;
    float bestScore = -100000.0f;

    for (int candidate = 0; candidate < params.candidateCount; ++candidate)
    {
        auto pattern = generateCandidate(params, candidate);
        validateAndRepair(pattern, params);
        pattern.score = scorer.score(pattern, params);

        if (candidate == 0 || pattern.score.quality > bestScore)
        {
            bestScore = pattern.score.quality;
            best = std::move(pattern);
            best.selectedCandidateIndex = candidate;
        }
    }

    best.debugSummary = buildDebugSummary(best, params);
    juce::Logger::writeToLog(best.debugSummary);
    return best;
}

BoomBapClassicAlgebraPattern BoomBapClassicAlgebraGenerator::generateCandidate(const BoomBapClassicAlgebraParams& params,
                                                                               int candidateIndex) const
{
    BoomBapClassicAlgebraPattern pattern;
    std::mt19937 rng(static_cast<std::mt19937::result_type>(params.seed * 1103515245u + candidateIndex * 2654435761u));

    generateBar(pattern, params, candidateIndex, 0, rng);

    BoomBapClassicPatternScorer scorer;
    auto oneBar = pattern;
    oneBar.notesByLane[BoomBapClassicLanes::Sub808].clear();
    auto oneBarParams = params;
    oneBarParams.bars = 1;
    const bool cloneGoodBar = scorer.score(oneBar, oneBarParams).quality > 2.6f && chance(rng, 0.68f);
    if (params.bars > 1)
    {
        if (cloneGoodBar)
            cloneBarWithSmallMutation(pattern, params, rng);
        else
            generateBar(pattern, params, candidateIndex, 1, rng);
    }

    const auto barOneKickSkeleton = laneTicksInBar(pattern, BoomBapClassicLanes::Kick, 0);
    if (params.bars > 2)
        generateBar(pattern, params, candidateIndex, 2, rng, &barOneKickSkeleton);
    if (params.bars > 3)
        generateBar(pattern, params, candidateIndex, 3, rng, &barOneKickSkeleton);
    for (int bar = 4; bar < params.bars; ++bar)
        generateBar(pattern, params, candidateIndex, bar, rng, &barOneKickSkeleton);

    for (auto& lane : pattern.notesByLane)
        dedupeLane(lane);
    return pattern;
}

void BoomBapClassicAlgebraGenerator::generateBar(BoomBapClassicAlgebraPattern& pattern,
                                                 const BoomBapClassicAlgebraParams& params,
                                                 int candidateIndex,
                                                 int barIndex,
                                                 std::mt19937& rng,
                                                 const std::vector<int>* kickSkeletonToAnswer) const
{
    const bool ending = (barIndex % 4) == 3;
    const int swingTicks = swingOffsetTicks(params.swing);
    const float den = params.density;
    const float hum = params.humanize;
    const float var = params.variation;

    for (const int snareTick : { 16, 48 })
    {
        const int delay = std::clamp(static_cast<int>(std::lround(params.swing * 1.5f * hum)) + randomInt(rng, 0, hum > 0.55f ? 1 : 0), 0, 2);
        addNote(pattern, BoomBapClassicLanes::Snare, barIndex, snareTick, randomInt(rng, 100, 120), delay, BoomBapClassicRole::Anchor);

        for (const int offset : { -4, -2, 2, 4 })
        {
            const float roleBoost = (barIndex % 4 == 1 || ending) ? 0.12f : 0.0f;
            const float nearBoost = (std::abs(offset) == 2) ? 0.08f : 0.0f;
            if (chance(rng, 0.07f + den * 0.13f + hum * 0.11f + var * 0.08f + roleBoost + nearBoost))
            {
                const int velocity = randomInt(rng, 35, std::min(75, 88));
                addNote(pattern, BoomBapClassicLanes::ClapGhost, barIndex, snareTick + offset, velocity, randomInt(rng, -2, 3), BoomBapClassicRole::Ghost);
            }
        }
    }

    std::vector<int> selectedKickTicks;
    selectedKickTicks.reserve(4);
    for (const auto& candidate : kKickWeights)
    {
        const bool downbeat = candidate.tick == 0 || candidate.tick == 32;
        const bool syncopated = candidate.tick == 8 || candidate.tick == 24 || candidate.tick == 40 || candidate.tick == 56;
        const bool preSnare = candidate.tick == 12 || candidate.tick == 44;
        const bool postSnare = candidate.tick == 20 || candidate.tick == 52;
        const bool answerTick = kickSkeletonToAnswer != nullptr
            && std::find(kickSkeletonToAnswer->begin(), kickSkeletonToAnswer->end(), candidate.tick) == kickSkeletonToAnswer->end()
            && (candidate.tick == 24 || candidate.tick == 40 || candidate.tick == 56);
        const bool collision = isBackbeatTick(candidate.tick);
        const bool crowded = std::any_of(selectedKickTicks.begin(), selectedKickTicks.end(), [&](int tick) { return std::abs(tick - candidate.tick) < 5; });

        const float score = 1.4f * (downbeat ? 1.0f : 0.0f)
            + 0.9f * (syncopated ? 1.0f : 0.0f)
            + 0.7f * (preSnare ? 1.0f : 0.0f)
            + 0.6f * (postSnare ? 1.0f : 0.0f)
            + 0.5f * ((ending || answerTick) ? 1.0f : 0.0f)
            - 1.2f * (crowded ? 1.0f : 0.0f)
            - 0.9f * (collision ? 1.0f : 0.0f);
        float probability = sigmoid(score - 1.65f) * candidate.weight * (0.72f + den * 0.65f + var * 0.18f);

        if (barIndex == 0 && candidate.tick == 0)
            probability = 0.98f;
        else if (candidate.tick == 0)
            probability = std::max(probability, 0.62f);
        if (ending && candidate.tick >= 52)
            probability += 0.18f;
        if (answerTick && barIndex % 4 == 2)
            probability += 0.16f;

        if (chance(rng, probability))
            selectedKickTicks.push_back(candidate.tick);
    }

    const int maxMainKicks = ending ? 4 : 3;
    if (selectedKickTicks.empty())
        selectedKickTicks.push_back(0);
    std::stable_sort(selectedKickTicks.begin(), selectedKickTicks.end(), [](int a, int b)
    {
        const auto priority = [](int tick)
        {
            if (tick == 0) return 100;
            if (tick == 32) return 80;
            if (tick == 24 || tick == 56) return 70;
            if (tick == 8 || tick == 40) return 60;
            return 40;
        };
        return priority(a) > priority(b);
    });
    if (static_cast<int>(selectedKickTicks.size()) > maxMainKicks)
        selectedKickTicks.resize(static_cast<size_t>(maxMainKicks));
    std::sort(selectedKickTicks.begin(), selectedKickTicks.end());

    for (const int tick : selectedKickTicks)
        addNote(pattern, BoomBapClassicLanes::Kick, barIndex, tick, randomInt(rng, 85, 118), randomInt(rng, -1, 1), BoomBapClassicRole::Anchor);

    for (const int tick : { 4, 28, 36, 60 })
    {
        const bool tooClose = std::any_of(selectedKickTicks.begin(), selectedKickTicks.end(), [tick](int mainTick) { return std::abs(mainTick - tick) < 4; });
        if (!tooClose && chance(rng, 0.04f + den * 0.09f + var * 0.08f + (ending ? 0.06f : 0.0f)))
            addNote(pattern, BoomBapClassicLanes::KickGhost, barIndex, tick, randomInt(rng, 35, 70), randomInt(rng, -1, 2), BoomBapClassicRole::Ghost);
    }

    for (const int tick : { 0, 8, 16, 24, 32, 40, 48, 56 })
    {
        const bool offbeat = (tick % 16) == 8 || (tick % 16) == 24;
        const int baseVelocity = (tick % 16) == 0 ? randomInt(rng, 72, 88) : randomInt(rng, 48, 68);
        const int micro = offbeat ? std::clamp(swingTicks + randomInt(rng, 0, hum > 0.35f ? 1 : 0), 1, 3)
                                  : randomInt(rng, 0, hum > 0.60f ? 1 : 0);
        addNote(pattern, BoomBapClassicLanes::HiHat, barIndex, tick, baseVelocity, micro, BoomBapClassicRole::Support);
    }

    for (const int tick : { 4, 12, 20, 28, 36, 44, 52, 60 })
    {
        const bool beforeSnare = tick == 12 || tick == 44;
        const float probability = 0.04f + den * 0.18f + var * 0.08f + (beforeSnare ? 0.05f : 0.0f) + (ending && tick >= 52 ? 0.07f : 0.0f);
        if (chance(rng, probability))
        {
            const bool accent = chance(rng, 0.18f + den * 0.12f + (beforeSnare ? 0.16f : 0.0f));
            addNote(pattern,
                    accent ? BoomBapClassicLanes::HatAccent : BoomBapClassicLanes::HiHat,
                    barIndex,
                    tick,
                    accent ? randomInt(rng, 85, 105) : randomInt(rng, 48, 68),
                    std::clamp(swingTicks + randomInt(rng, 0, 1), 1, 3),
                    accent ? BoomBapClassicRole::Accent : BoomBapClassicRole::Support);
        }
    }

    if (chance(rng, 0.03f + den * 0.05f + (ending ? 0.10f : 0.0f)))
    {
        const int tick = ending ? (chance(rng, 0.5f) ? 56 : 60) : (chance(rng, 0.5f) ? 44 : 52);
        addNote(pattern, BoomBapClassicLanes::OpenHat, barIndex, tick, randomInt(rng, 70, 105), randomInt(rng, 0, 2), ending ? BoomBapClassicRole::Ending : BoomBapClassicRole::Accent, 2);
    }

    if ((barIndex == 0 && candidateIndex % 5 == 0 && chance(rng, 0.35f)) || (ending && chance(rng, 0.35f + den * 0.20f)))
        addNote(pattern, BoomBapClassicLanes::Cymbal, barIndex, ending ? 60 : 0, randomInt(rng, 78, 112), randomInt(rng, 0, 2), ending ? BoomBapClassicRole::Ending : BoomBapClassicRole::Accent, 4);

    if (chance(rng, 0.10f + den * 0.16f + (barIndex % 4 == 2 ? 0.08f : 0.0f)))
        addNote(pattern, BoomBapClassicLanes::Perc, barIndex, randomInt(rng, 0, 7) * 8 + (chance(rng, 0.45f) ? 4 : 0), randomInt(rng, 52, 94), randomInt(rng, -2, 2), BoomBapClassicRole::Support);

    if (chance(rng, 0.01f + den * 0.03f) && den > 0.72f)
        addNote(pattern, BoomBapClassicLanes::Ride, barIndex, randomInt(rng, 0, 7) * 8, randomInt(rng, 62, 92), randomInt(rng, 0, 2), BoomBapClassicRole::Support);
}

void BoomBapClassicAlgebraGenerator::cloneBarWithSmallMutation(BoomBapClassicAlgebraPattern& pattern,
                                                               const BoomBapClassicAlgebraParams& params,
                                                               std::mt19937& rng) const
{
    for (int lane = 0; lane < BoomBapClassicLanes::Count; ++lane)
    {
        const auto source = pattern.notesByLane[static_cast<size_t>(lane)];
        for (const auto& note : source)
        {
            if (note.barIndex != 0)
                continue;

            auto copy = note;
            copy.barIndex = 1;
            copy.tick64 = kTicksPerBar + normalizeTickInBar(note.tick64);
            if (lane == BoomBapClassicLanes::HiHat)
                copy.velocity = std::clamp(copy.velocity + randomInt(rng, -5, 5), 1, 127);
            if ((lane == BoomBapClassicLanes::ClapGhost || lane == BoomBapClassicLanes::OpenHat) && chance(rng, 0.18f + params.variation * 0.12f))
                continue;
            pattern.notesByLane[static_cast<size_t>(lane)].push_back(copy);
        }
    }

    if (chance(rng, 0.24f + params.variation * 0.18f))
        addNote(pattern, BoomBapClassicLanes::ClapGhost, 1, chance(rng, 0.5f) ? 44 : 52, randomInt(rng, 35, 70), randomInt(rng, -2, 3), BoomBapClassicRole::Ghost);
    if (chance(rng, 0.18f + params.variation * 0.10f))
        addNote(pattern, BoomBapClassicLanes::HatAccent, 1, chance(rng, 0.5f) ? 12 : 44, randomInt(rng, 85, 105), std::max(1, swingOffsetTicks(params.swing)), BoomBapClassicRole::Accent);
}

void BoomBapClassicAlgebraGenerator::validateAndRepair(BoomBapClassicAlgebraPattern& pattern,
                                                       const BoomBapClassicAlgebraParams& params) const
{
    for (auto& lane : pattern.notesByLane)
        dedupeLane(lane);

    for (int bar = 0; bar < params.bars; ++bar)
    {
        for (const int snareTick : { 16, 48 })
        {
            if (!laneHasAt(pattern, BoomBapClassicLanes::Snare, bar, snareTick))
            {
                addNote(pattern, BoomBapClassicLanes::Snare, bar, snareTick, 108, std::clamp(swingOffsetTicks(params.swing) / 2, 0, 2), BoomBapClassicRole::Anchor);
                pattern.repairsApplied.add("add_missing_snare");
            }
        }
    }

    if (!laneHasAt(pattern, BoomBapClassicLanes::Kick, 0, 0))
    {
        addNote(pattern, BoomBapClassicLanes::Kick, 0, 0, 108, 0, BoomBapClassicRole::Anchor);
        pattern.repairsApplied.add("add_missing_first_bar_kick_anchor");
    }

    auto& hats = pattern.notesByLane[BoomBapClassicLanes::HiHat];
    const bool flatHats = hats.size() > 1 && std::all_of(hats.begin() + 1, hats.end(), [&](const auto& note) { return note.velocity == hats.front().velocity; });
    if (flatHats)
    {
        for (size_t i = 0; i < hats.size(); ++i)
            hats[i].velocity = std::clamp(hats[i].velocity + static_cast<int>((i % 4) * 4) - 4, 1, 127);
        pattern.repairsApplied.add("vary_flat_hat_velocity");
    }

    int maxMainSnareVelocity = 100;
    for (const auto& note : pattern.notesByLane[BoomBapClassicLanes::Snare])
        maxMainSnareVelocity = std::max(maxMainSnareVelocity, note.velocity);
    for (auto& lane : { std::ref(pattern.notesByLane[BoomBapClassicLanes::ClapGhost]), std::ref(pattern.notesByLane[BoomBapClassicLanes::KickGhost]) })
    {
        for (auto& note : lane.get())
        {
            if (note.velocity >= maxMainSnareVelocity)
            {
                note.velocity = std::max(1, maxMainSnareVelocity - 18);
                pattern.repairsApplied.add("lower_ghost_velocity");
            }
        }
    }

    const auto reduceLane = [&](int lane, int maxCount, const char* repair)
    {
        auto& notes = pattern.notesByLane[static_cast<size_t>(lane)];
        if (static_cast<int>(notes.size()) <= maxCount)
            return;
        std::stable_sort(notes.begin(), notes.end(), [](const auto& a, const auto& b) { return a.velocity > b.velocity; });
        notes.resize(static_cast<size_t>(maxCount));
        sortLane(notes);
        pattern.repairsApplied.add(repair);
    };

    reduceLane(BoomBapClassicLanes::OpenHat, 2, "reduce_open_hats");
    reduceLane(BoomBapClassicLanes::Cymbal, 2, "reduce_cymbals");
    reduceLane(BoomBapClassicLanes::Ride, 4, "reduce_rides");
    reduceLane(BoomBapClassicLanes::Perc, std::clamp(static_cast<int>(1 + params.density * 4.0f), 1, 5), "reduce_perc");

    const int maxTotal = params.bars * (15 + static_cast<int>(params.density * 8.0f));
    while (static_cast<int>(pattern.allNotes().size()) > maxTotal)
    {
        int targetLane = BoomBapClassicLanes::Perc;
        for (const int lane : { BoomBapClassicLanes::Perc, BoomBapClassicLanes::Ride, BoomBapClassicLanes::OpenHat, BoomBapClassicLanes::KickGhost, BoomBapClassicLanes::ClapGhost, BoomBapClassicLanes::HatAccent })
        {
            if (!pattern.notesByLane[static_cast<size_t>(lane)].empty())
            {
                targetLane = lane;
                break;
            }
        }
        auto& lane = pattern.notesByLane[static_cast<size_t>(targetLane)];
        if (lane.empty())
            break;
        lane.erase(std::min_element(lane.begin(), lane.end(), [](const auto& a, const auto& b) { return a.velocity < b.velocity; }));
        pattern.repairsApplied.add("remove_low_priority_density_note");
    }

    if (params.bars >= 4
        && barSimilarity(pattern, 0, 1) > 0.98f
        && barSimilarity(pattern, 0, 2) > 0.98f
        && barSimilarity(pattern, 0, 3) > 0.98f)
    {
        addNote(pattern, BoomBapClassicLanes::HatAccent, 3, 60, 92, std::max(1, swingOffsetTicks(params.swing)), BoomBapClassicRole::Ending);
        pattern.repairsApplied.add("vary_bar_4_identical_phrase");
    }

    for (auto& lane : pattern.notesByLane)
        dedupeLane(lane);
}

juce::String BoomBapClassicAlgebraGenerator::buildDebugSummary(const BoomBapClassicAlgebraPattern& pattern,
                                                               const BoomBapClassicAlgebraParams& params) const
{
    juce::StringArray lines;
    lines.add("style: Boom Bap Classic Algebra");
    lines.add("seed: " + juce::String(params.seed));
    lines.add("bars: " + juce::String(params.bars));
    lines.add("swing: " + juce::String(params.swing, 3));
    lines.add("density: " + juce::String(params.density, 3));
    lines.add("selected candidate index: " + juce::String(pattern.selectedCandidateIndex));
    lines.add("final score: " + juce::String(pattern.score.quality, 3));
    lines.add("kick count: " + juce::String(countLane(pattern, BoomBapClassicLanes::Kick)));
    lines.add("snare count: " + juce::String(countLane(pattern, BoomBapClassicLanes::Snare)));
    lines.add("hat count: " + juce::String(countLane(pattern, BoomBapClassicLanes::HiHat) + countLane(pattern, BoomBapClassicLanes::HatAccent)));
    lines.add("ghost count: " + juce::String(countRole(pattern, BoomBapClassicRole::Ghost)));
    lines.add("repairs applied: " + (pattern.repairsApplied.isEmpty() ? juce::String("none") : pattern.repairsApplied.joinIntoString(",")));
    lines.add("phrase roles:");
    lines.add("  bar 1 statement");
    lines.add("  bar 2 repeat_or_small_variation");
    lines.add("  bar 3 answer");
    lines.add("  bar 4 ending_or_fill");
    return lines.joinIntoString("\n");
}
} // namespace bbg
