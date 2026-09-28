#include "TrapAlgebraEngine.h"
#include "../GenerationModel/CandidateSelectionEngine.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace bbg
{
std::pair<float, float> couplingBand(TrapAlgebraSubstyle substyle);

namespace
{
constexpr int kTicksPerBar = 64;
constexpr int kPpqPerQuarter = 960;

GenerationLaneFamily featureFamilyForTrapLane(int lane)
{
    if (lane == TrapAlgebraLanes::Kick || lane == TrapAlgebraLanes::KickGhost) return GenerationLaneFamily::Kick;
    if (lane == TrapAlgebraLanes::Snare || lane == TrapAlgebraLanes::ClapGhost) return GenerationLaneFamily::Snare;
    if (lane == TrapAlgebraLanes::HiHat || lane == TrapAlgebraLanes::HatAccent || lane == TrapAlgebraLanes::OpenHat) return GenerationLaneFamily::Hat;
    if (lane == TrapAlgebraLanes::Sub808) return GenerationLaneFamily::Bass;
    return GenerationLaneFamily::Ornament;
}

MusicalRole featureRoleForTrap(TrapAlgebraRole role)
{
    switch (role)
    {
        case TrapAlgebraRole::Anchor:
        case TrapAlgebraRole::BassAnchor: return MusicalRole::Anchor;
        case TrapAlgebraRole::Support:
        case TrapAlgebraRole::BassHold: return MusicalRole::Pulse;
        case TrapAlgebraRole::BassPickup: return MusicalRole::Pickup;
        case TrapAlgebraRole::BassAnswer: return MusicalRole::Response;
        case TrapAlgebraRole::Ghost: return MusicalRole::Ghost;
        case TrapAlgebraRole::Accent: return MusicalRole::Accent;
        case TrapAlgebraRole::Roll: return MusicalRole::Ornament;
        case TrapAlgebraRole::Fill: return MusicalRole::Fill;
        case TrapAlgebraRole::Ending: return MusicalRole::Transition;
        case TrapAlgebraRole::Bass: return MusicalRole::Anchor;
        default: return MusicalRole::Unknown;
    }
}

PatternFeatureVector extractTrapFeatures(const TrapPatternMatrix& matrix)
{
    PatternFeatureInput input;
    input.bars = matrix.getBars();
    input.ticksPerBar = kTicksPerBar;
    for (const auto& note : matrix.allNotes())
        input.events.push_back({ featureFamilyForTrapLane(note.laneIndex), featureRoleForTrap(note.role), note.tick64,
                                 note.velocity, note.microTimingTicks });
    return PatternFeatureExtractor::extract(input);
}

StyleTargetProfile trapStyleTarget(TrapAlgebraSubstyle substyle)
{
    PatternFeatureVector target { 0.58f, 0.56f, 0.56f, 0.18f, 0.42f, 0.58f, 0.70f, 0.94f };
    switch (substyle)
    {
        case TrapAlgebraSubstyle::DarkTrap:
            target = { 0.48f, 0.58f, 0.48f, 0.14f, 0.54f, 0.64f, 0.76f, 0.94f };
            break;
        case TrapAlgebraSubstyle::CloudTrap:
            target = { 0.42f, 0.52f, 0.52f, 0.16f, 0.62f, 0.60f, 0.60f, 0.92f };
            break;
        case TrapAlgebraSubstyle::RageTrap:
            target = { 0.76f, 0.64f, 0.72f, 0.22f, 0.24f, 0.48f, 0.78f, 0.92f };
            break;
        case TrapAlgebraSubstyle::MemphisTrap:
            target = { 0.62f, 0.68f, 0.64f, 0.20f, 0.38f, 0.52f, 0.72f, 0.90f };
            break;
        case TrapAlgebraSubstyle::LuxuryTrap:
            target = { 0.48f, 0.50f, 0.58f, 0.14f, 0.56f, 0.66f, 0.66f, 0.96f };
            break;
        case TrapAlgebraSubstyle::ATLClassic:
        default:
            break;
    }

    const PatternFeatureVector tolerance { 0.20f, 0.22f, 0.24f, 0.18f, 0.20f, 0.24f, 0.22f, 0.10f };
    const PatternFeatureVector weight { 1.15f, 1.05f, 0.75f, 0.55f, 1.20f, 0.90f, 1.25f, 0.85f };
    return { target, tolerance, weight };
}

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

float sigmoid(float value)
{
    return 1.0f / (1.0f + std::exp(-value));
}

int barStart(int bar)
{
    return bar * kTicksPerBar;
}

int tickInBar(int tick64)
{
    const int tick = tick64 % kTicksPerBar;
    return tick < 0 ? tick + kTicksPerBar : tick;
}

int swingOffsetTicks(float swing)
{
    return std::clamp(static_cast<int>(std::lround((swing - 0.5f) * 8.0f)), 0, 2);
}

int microMsToPpq(float milliseconds, const TrapAlgebraParams& params)
{
    const float safeBpm = std::clamp(params.bpm, 40.0f, 240.0f);
    const float ppq = milliseconds * static_cast<float>(kPpqPerQuarter) * safeBpm / 60000.0f;
    return static_cast<int>(std::lround(ppq));
}

int randomMicroPpq(std::mt19937& rng,
                   const TrapAlgebraParams& params,
                   float minMs,
                   float maxMs)
{
    std::uniform_real_distribution<float> distribution(minMs, maxMs);
    const float scaledMs = distribution(rng) * std::clamp(0.35f + params.humanize * 0.90f, 0.0f, 1.0f);
    return microMsToPpq(scaledMs, params);
}

float barEnergy(int bar)
{
    switch (bar % 4)
    {
        case 0: return 0.20f;
        case 1: return 0.32f;
        case 2: return 0.52f;
        default: return 0.82f;
    }
}

float barVariationAmount(int bar)
{
    switch (bar % 4)
    {
        case 0: return 0.08f;
        case 1: return 0.14f;
        case 2: return 0.28f;
        default: return 0.46f;
    }
}

bool isLegalMainKickLocalTick(int tick, bool /*bar4*/)
{
    return tick == 0
        || tick == 16
        || tick == 24
        || tick == 36
        || tick == 40
        || tick == 56;
}

bool isStumblingMainKickLocalTick(int tick)
{
    return tick == 12
        || tick == 28
        || tick == 44
        || tick == 52
        || tick == 60;
}

int fallbackKickLocalTickForBar(int bar)
{
    switch (bar % 4)
    {
        case 0: return 0;
        case 1: return 24;
        case 2: return 56;
        default: return 24;
    }
}

int microTimingForLane(int lane, int tick, const TrapAlgebraParams& params, std::mt19937& rng)
{
    switch (lane)
    {
        case TrapAlgebraLanes::Kick:
        case TrapAlgebraLanes::Sub808:
            return 0;
        case TrapAlgebraLanes::Snare:
            return randomMicroPpq(rng, params, 0.0f, 4.0f);
        case TrapAlgebraLanes::ClapGhost:
            return randomMicroPpq(rng, params, -3.0f, 5.0f);
        case TrapAlgebraLanes::HiHat:
        case TrapAlgebraLanes::HatAccent:
        {
            const bool offbeat = tickInBar(tick) == 8 || tickInBar(tick) == 24 || tickInBar(tick) == 40 || tickInBar(tick) == 56;
            const float swingMs = static_cast<float>(swingOffsetTicks(params.swing)) * 2.2f;
            return randomMicroPpq(rng, params, offbeat ? swingMs : -1.2f, offbeat ? swingMs + 3.0f : 1.8f);
        }
        case TrapAlgebraLanes::Perc:
            return randomMicroPpq(rng, params, -5.0f, 5.0f);
        default:
            return randomMicroPpq(rng, params, -2.0f, 2.0f);
    }
}

int hatVelocity(std::mt19937& rng, int tick, bool accent)
{
    if (accent)
        return randomInt(rng, 86, 112);

    const int phase = tickInBar(tick) % 16;
    if (phase == 0)
        return randomInt(rng, 68, 96);
    if (phase == 8)
        return randomInt(rng, 54, 82);
    return randomInt(rng, 45, 78);
}

float velocityVariance(const std::vector<TrapAlgebraNote>& notes)
{
    if (notes.size() < 2)
        return 0.0f;

    const float mean = std::accumulate(notes.begin(), notes.end(), 0.0f, [](float sum, const auto& note)
    {
        return sum + static_cast<float>(note.velocity);
    }) / static_cast<float>(notes.size());

    float variance = 0.0f;
    for (const auto& note : notes)
    {
        const float delta = static_cast<float>(note.velocity) - mean;
        variance += delta * delta;
    }

    return variance / static_cast<float>(notes.size());
}

bool inRollZone(int tick, bool bar4)
{
    const int local = tickInBar(tick);
    if (local >= 24 && local <= 31)
        return true;
    if (local >= 34 && local <= 40)
        return true;
    if (local >= 56 && local <= 63)
        return true;
    return bar4 && local >= 48 && local <= 63;
}

int nearestRollZoneStart(int tick, bool bar4)
{
    const int local = tickInBar(tick);
    struct Zone { int start; int end; };
    std::vector<Zone> zones { { 24, 31 }, { 34, 40 }, { 56, 63 } };
    if (bar4)
        zones.push_back({ 48, 63 });

    int best = zones.front().start;
    int bestDistance = 1000;
    for (const auto& zone : zones)
    {
        const int clamped = std::clamp(local, zone.start, zone.end);
        const int distance = std::abs(local - clamped);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = clamped;
        }
    }
    return best;
}

} // namespace

TrapPatternMatrix::TrapPatternMatrix(int barsToUse)
{
    reset(barsToUse);
}

void TrapPatternMatrix::reset(int newBars)
{
    bars = std::clamp(newBars, 1, 16);
    for (auto& lane : lanes)
        lane.assign(static_cast<size_t>(bars * kTicksPerBar), {});
}

int TrapPatternMatrix::getBars() const
{
    return bars;
}

int TrapPatternMatrix::getTotalTicks() const
{
    return bars * kTicksPerBar;
}

bool TrapPatternMatrix::setNote(int lane,
                                int tick64,
                                int velocity,
                                int durationTicks,
                                int microTimingTicks,
                                TrapAlgebraRole role)
{
    if (lane < 0 || lane >= TrapAlgebraLanes::Count || tick64 < 0 || tick64 >= getTotalTicks())
        return false;

    auto& cell = lanes[static_cast<size_t>(lane)][static_cast<size_t>(tick64)];
    cell.active = true;
    cell.velocity = std::clamp(velocity, 1, 127);
    cell.durationTicks = std::clamp(durationTicks, 1, getTotalTicks() - tick64);
    // +/-30 PPQ is required to preserve exact double-time 1/64 positions on
    // the existing 1/16 + microOffset event representation.
    cell.microTimingTicks = std::clamp(microTimingTicks, -30, 30);
    cell.role = role;
    cell.roleString = TrapAlgebraEngine::roleToString(role);
    return true;
}

void TrapPatternMatrix::clearNote(int lane, int tick64)
{
    if (lane < 0 || lane >= TrapAlgebraLanes::Count || tick64 < 0 || tick64 >= getTotalTicks())
        return;

    lanes[static_cast<size_t>(lane)][static_cast<size_t>(tick64)] = {};
}

bool TrapPatternMatrix::hasNote(int lane, int tick64) const
{
    const auto* cell = cellAt(lane, tick64);
    return cell != nullptr && cell->active;
}

bool TrapPatternMatrix::isLaneActiveAt(int lane, int tick64) const
{
    if (lane < 0 || lane >= TrapAlgebraLanes::Count || tick64 < 0 || tick64 >= getTotalTicks())
        return false;

    const auto& laneCells = lanes[static_cast<size_t>(lane)];
    if (laneCells[static_cast<size_t>(tick64)].active)
        return true;

    for (int start = std::max(0, tick64 - 64); start < tick64; ++start)
    {
        const auto& cell = laneCells[static_cast<size_t>(start)];
        if (cell.active && start + cell.durationTicks > tick64)
            return true;
    }

    return false;
}

const TrapPatternCell* TrapPatternMatrix::cellAt(int lane, int tick64) const
{
    if (lane < 0 || lane >= TrapAlgebraLanes::Count || tick64 < 0 || tick64 >= getTotalTicks())
        return nullptr;

    return &lanes[static_cast<size_t>(lane)][static_cast<size_t>(tick64)];
}

TrapPatternCell* TrapPatternMatrix::cellAt(int lane, int tick64)
{
    if (lane < 0 || lane >= TrapAlgebraLanes::Count || tick64 < 0 || tick64 >= getTotalTicks())
        return nullptr;

    return &lanes[static_cast<size_t>(lane)][static_cast<size_t>(tick64)];
}

int TrapPatternMatrix::activeLaneCountAt(int tick64) const
{
    int count = 0;
    for (int lane = 0; lane < TrapAlgebraLanes::Count; ++lane)
        if (isLaneActiveAt(lane, tick64))
            ++count;
    return count;
}

int TrapPatternMatrix::active808Ticks() const
{
    int count = 0;
    for (int tick = 0; tick < getTotalTicks(); ++tick)
        if (isLaneActiveAt(TrapAlgebraLanes::Sub808, tick))
            ++count;
    return count;
}

int TrapPatternMatrix::countLane(int lane) const
{
    if (lane < 0 || lane >= TrapAlgebraLanes::Count)
        return 0;
    return static_cast<int>(std::count_if(lanes[static_cast<size_t>(lane)].begin(), lanes[static_cast<size_t>(lane)].end(), [](const auto& cell)
    {
        return cell.active;
    }));
}

int TrapPatternMatrix::countLaneInBar(int lane, int bar) const
{
    if (lane < 0 || lane >= TrapAlgebraLanes::Count || bar < 0 || bar >= bars)
        return 0;

    int count = 0;
    for (int tick = barStart(bar); tick < barStart(bar + 1); ++tick)
        if (hasNote(lane, tick))
            ++count;
    return count;
}

std::vector<TrapAlgebraNote> TrapPatternMatrix::notesForLane(int lane) const
{
    std::vector<TrapAlgebraNote> out;
    if (lane < 0 || lane >= TrapAlgebraLanes::Count)
        return out;

    const auto& laneCells = lanes[static_cast<size_t>(lane)];
    for (int tick = 0; tick < getTotalTicks(); ++tick)
    {
        const auto& cell = laneCells[static_cast<size_t>(tick)];
        if (!cell.active)
            continue;

        out.push_back({ lane,
                        tick / kTicksPerBar,
                        tick,
                        cell.durationTicks,
                        cell.velocity,
                        cell.microTimingTicks,
                        cell.role,
                        cell.roleString });
    }
    return out;
}

std::vector<TrapAlgebraNote> TrapPatternMatrix::notesInBar(int bar) const
{
    std::vector<TrapAlgebraNote> out;
    if (bar < 0 || bar >= bars)
        return out;

    for (int lane = 0; lane < TrapAlgebraLanes::Count; ++lane)
    {
        for (const auto& note : notesForLane(lane))
            if (note.barIndex == bar)
                out.push_back(note);
    }

    std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b)
    {
        if (a.tick64 != b.tick64)
            return a.tick64 < b.tick64;
        return a.laneIndex < b.laneIndex;
    });
    return out;
}

std::vector<TrapAlgebraNote> TrapPatternMatrix::allNotes() const
{
    std::vector<TrapAlgebraNote> out;
    for (int lane = 0; lane < TrapAlgebraLanes::Count; ++lane)
    {
        auto laneNotes = notesForLane(lane);
        out.insert(out.end(), laneNotes.begin(), laneNotes.end());
    }

    std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b)
    {
        if (a.tick64 != b.tick64)
            return a.tick64 < b.tick64;
        return a.laneIndex < b.laneIndex;
    });
    return out;
}

TrapAlgebraPattern TrapAlgebraEngine::generate(const TrapAlgebraParams& rawParams) const
{
    TrapAlgebraParams params = rawParams;
    params.bars = std::clamp(params.bars, 1, 16);
    params.density = clamp01(params.density);
    params.swing = std::clamp(params.swing, 0.50f, 0.60f);
    params.humanize = clamp01(params.humanize);
    params.variation = clamp01(params.variation);
    params.temperature = std::clamp(params.temperature, 0.20f, 0.80f);
    params.qMin = clamp01(params.qMin);
    params.candidateCount = std::clamp(params.candidateCount, 8, 128);
    params.tempoContext = resolveTrapTempo(params.bpm, params.bars);

    const auto weights = weightsForSubstyle(params.substyle);
    const auto styleTarget = StyleTargetModel::withPerformanceIntent(trapStyleTarget(params.substyle),
                                                                     params.density, params.swing,
                                                                     params.humanize, params.variation);
    TrapQualityScorer scorer;

    std::vector<TrapAlgebraPattern> candidates;
    candidates.reserve(static_cast<size_t>(params.candidateCount));

    for (int candidate = 0; candidate < params.candidateCount; ++candidate)
    {
        auto pattern = generateCandidate(params, candidate);
        repair(pattern, params, weights);
        shape808Durations(pattern.matrix, params);
        pattern.score = scorer.score(pattern.matrix, params, weights);
        pattern.features = extractTrapFeatures(pattern.matrix);
        pattern.styleMatch = StyleTargetModel::evaluate(pattern.features, styleTarget);
        pattern.selectionQuality = pattern.score.quality + 0.06f * (pattern.styleMatch.fit - 0.5f);
        pattern.selectedCandidateIndex = candidate;

        const float boltzmann = std::exp(-pattern.score.energy / params.temperature);
        const bool accepted = pattern.score.quality >= params.qMin || boltzmann > 0.20f;
        pattern.acceptedByThreshold = accepted;

        candidates.push_back(std::move(pattern));
    }

    std::vector<CandidateSelectionEntry> selectionEntries;
    selectionEntries.reserve(candidates.size());
    for (const auto& candidate : candidates)
        selectionEntries.push_back({ candidate.selectionQuality, candidate.features.surfaceNovelty(), candidate.acceptedByThreshold });
    const auto selection = CandidateSelectionEngine::select(selectionEntries,
        { params.qMin, 0.08f, std::max(0.12f, params.temperature), 0.10f,
          static_cast<std::uint32_t>(params.seed * 2654435761u + 0x9e3779b9u) });
    auto selected = std::move(candidates[static_cast<size_t>(selection.selectedIndex)]);
    selected.nearBestPoolSize = selection.nearBestPoolSize;
    selected.bestCandidateQuality = selection.bestQuality;
    selected.selectedCandidateQuality = selection.selectedQuality;

    selected.debugSummary = buildDebugSummary(selected, params, weights);
    selected.tempoContext = params.tempoContext;
    return selected;
}

TrapAlgebraPattern TrapAlgebraEngine::generateCandidate(const TrapAlgebraParams& params, int candidateIndex) const
{
    TrapAlgebraPattern pattern;
    pattern.matrix.reset(params.bars);
    pattern.tempoContext = params.tempoContext;

    std::mt19937 rng(static_cast<std::mt19937::result_type>(params.seed * 1664525u + candidateIndex * 1013904223u));
    generateSkeleton(pattern.matrix, params, rng);

    for (int bar = 0; bar < params.bars; ++bar)
        generateBarAdditions(pattern.matrix, params, bar, candidateIndex, rng);

    shape808Durations(pattern.matrix, params);
    return pattern;
}

void TrapAlgebraEngine::generateSkeleton(TrapPatternMatrix& matrix,
                                         const TrapAlgebraParams& params,
                                         std::mt19937& rng) const
{
    const int swingTicks = swingOffsetTicks(params.swing);

    for (int bar = 0; bar < params.bars; ++bar)
    {
        const int start = barStart(bar);
        const std::array<int, 2> doubleTimeBackbeats { 16, 48 };
        const std::array<int, 1> normalBackbeat { 32 };
        if (params.tempoContext.doubleTime)
        {
            for (const int tick : doubleTimeBackbeats)
                matrix.setNote(TrapAlgebraLanes::Snare, start + tick, randomInt(rng, 100, 124), 2,
                               microTimingForLane(TrapAlgebraLanes::Snare, start + tick, params, rng),
                               TrapAlgebraRole::Anchor);
        }
        else
        {
            for (const int tick : normalBackbeat)
                matrix.setNote(TrapAlgebraLanes::Snare, start + tick, randomInt(rng, 100, 124), 2,
                               microTimingForLane(TrapAlgebraLanes::Snare, start + tick, params, rng),
                               TrapAlgebraRole::Anchor);
        }

        const int hatPulse = params.tempoContext.doubleTime ? 4 : 8;
        for (int tick = 0; tick < 64; tick += hatPulse)
        {
            matrix.setNote(TrapAlgebraLanes::HiHat,
                           start + tick,
                           hatVelocity(rng, tick, false),
                           1,
                           (tick == 8 || tick == 24 || tick == 40 || tick == 56) ? swingTicks : 0,
                           TrapAlgebraRole::Support);
        }

        if (bar == 0)
        {
            const int kickTick = start;
            matrix.setNote(TrapAlgebraLanes::Kick, kickTick, randomInt(rng, 96, 123), 2, 0, TrapAlgebraRole::Anchor);
            matrix.setNote(TrapAlgebraLanes::Sub808, kickTick, randomInt(rng, 92, 118), randomInt(rng, 7, 11), 0, TrapAlgebraRole::Bass);
        }
    }
}

void TrapAlgebraEngine::generateBarAdditions(TrapPatternMatrix& matrix,
                                             const TrapAlgebraParams& params,
                                             int bar,
                                             int candidateIndex,
                                             std::mt19937& rng) const
{
    const int start = barStart(bar);
    const auto style = weightsForSubstyle(params.substyle);
    const float density = std::clamp(params.density * (0.72f + style.hatRate * 0.56f), 0.0f, 1.0f);
    const float energy = barEnergy(bar);
    const float variation = barVariationAmount(bar) * (0.65f + params.variation * 0.70f);
    const bool bar4 = (bar % 4) == 3;

    std::vector<int> selectedKicks;
    for (int tick = 0; tick < 64; ++tick)
        if (matrix.hasNote(TrapAlgebraLanes::Kick, start + tick))
            selectedKicks.push_back(tick);

    const std::array<std::array<std::array<int, 4>, 4>, 9> phraseMotifs {{
        std::array<std::array<int, 4>, 4> {{ {{ 0, 24, -1, -1 }}, {{ 24, 56, -1, -1 }}, {{ 56, -1, -1, -1 }}, {{ 24, -1, -1, -1 }} }},
        std::array<std::array<int, 4>, 4> {{ {{ 0, 36, -1, -1 }}, {{ 24, -1, -1, -1 }}, {{ 16, 40, -1, -1 }}, {{ 24, 56, -1, -1 }} }},
        std::array<std::array<int, 4>, 4> {{ {{ 0, 24, -1, -1 }}, {{ 36, 56, -1, -1 }}, {{ 24, -1, -1, -1 }}, {{ 16, 40, -1, -1 }} }},
        std::array<std::array<int, 4>, 4> {{ {{ 0, 16, -1, -1 }}, {{ 24, 40, -1, -1 }}, {{ 56, -1, -1, -1 }}, {{ 24, 56, -1, -1 }} }},
        std::array<std::array<int, 4>, 4> {{ {{ 0, 40, -1, -1 }}, {{ 24, -1, -1, -1 }}, {{ 36, 56, -1, -1 }}, {{ 16, 40, -1, -1 }} }},
        std::array<std::array<int, 4>, 4> {{ {{ 0, 24, 56, -1 }}, {{ 24, -1, -1, -1 }}, {{ 16, 36, -1, -1 }}, {{ 56, -1, -1, -1 }} }},
        std::array<std::array<int, 4>, 4> {{ {{ 0, -1, -1, -1 }}, {{ 24, 56, -1, -1 }}, {{ 24, 40, -1, -1 }}, {{ 16, 56, -1, -1 }} }},
        std::array<std::array<int, 4>, 4> {{ {{ 0, 36, -1, -1 }}, {{ 56, -1, -1, -1 }}, {{ 24, 56, -1, -1 }}, {{ 24, 40, -1, -1 }} }},
        std::array<std::array<int, 4>, 4> {{ {{ 0, 16, 40, -1 }}, {{ 24, -1, -1, -1 }}, {{ 56, -1, -1, -1 }}, {{ 24, 56, -1, -1 }} }}
    }};

    const auto& motif = phraseMotifs[static_cast<size_t>((candidateIndex + params.seed) % static_cast<int>(phraseMotifs.size()))]
                                    [static_cast<size_t>(bar % 4)];

    const int motifKickCount = static_cast<int>(std::count_if(motif.begin(), motif.end(), [](int tick) { return tick >= 0; }));
    const int styleKickLimit = style.kickIrregularity >= 0.54f ? 3 : 2;
    const int targetKicks = std::clamp(motifKickCount, 1, bar4 ? std::max(2, styleKickLimit) : styleKickLimit);

    for (const int candidateTick : motif)
    {
        if (static_cast<int>(selectedKicks.size()) >= targetKicks)
            break;
        if (candidateTick < 0 || matrix.hasNote(TrapAlgebraLanes::Kick, start + candidateTick))
            continue;
        if (!isLegalMainKickLocalTick(candidateTick, bar4))
            continue;
        if (std::find(selectedKicks.begin(), selectedKicks.end(), candidateTick) != selectedKicks.end())
            continue;
        if (std::any_of(selectedKicks.begin(), selectedKicks.end(), [candidateTick](int tick) { return std::abs(tick - candidateTick) < 8; }))
            continue;
        if (candidateTick == 0 && bar != 0 && selectedKicks.empty() && chance(rng, 0.74f))
            continue;

        matrix.setNote(TrapAlgebraLanes::Kick,
                       start + candidateTick,
                       randomInt(rng, 92, 123),
                       2,
                       0,
                       TrapAlgebraRole::Anchor);
        selectedKicks.push_back(candidateTick);
    }

    if (selectedKicks.empty())
    {
        const int fallbackTick = fallbackKickLocalTickForBar(bar);
        matrix.setNote(TrapAlgebraLanes::Kick,
                       start + fallbackTick,
                       randomInt(rng, 92, 118),
                       2,
                       0,
                       TrapAlgebraRole::Anchor);
        selectedKicks.push_back(fallbackTick);
    }

    std::sort(selectedKicks.begin(), selectedKicks.end());

    if (params.substyle == TrapAlgebraSubstyle::RageTrap && bar4 && chance(rng, 0.04f + density * 0.04f))
    {
        const int tick = 48;
        const bool nearMainKick = std::any_of(selectedKicks.begin(), selectedKicks.end(), [tick](int mainTick)
        {
            return std::abs(mainTick - tick) < 8;
        });
        if (!nearMainKick)
        {
            matrix.setNote(TrapAlgebraLanes::KickGhost,
                           start + tick,
                           randomInt(rng, 36, 58),
                           1,
                           0,
                           TrapAlgebraRole::Ghost);
        }
    }

    const auto [couplingLow, couplingHigh] = couplingBand(params.substyle);
    const float couplingTarget = 0.5f * (couplingLow + couplingHigh);
    for (const int kickTick : selectedKicks)
    {
        const auto nextIt = std::find_if(selectedKicks.begin(), selectedKicks.end(), [kickTick](int other) { return other > kickTick; });
        const int nextKickTick = nextIt != selectedKicks.end() ? *nextIt : 64;
        int endLimit = nextKickTick - 2;
        if (kickTick < 32)
            endLimit = std::min(endLimit, 30);
        if (kickTick >= 32 && kickTick < 56)
            endLimit = std::min(endLimit, 54);

        const int styleExtra = static_cast<int>(std::lround(style.bassLegato * 5.0f));
        const int maxDuration = std::clamp(endLimit - kickTick, 4, (bar4 ? 10 : 8) + styleExtra);
        const int minDuration = std::min(maxDuration, kickTick == 0 ? 6 + static_cast<int>(style.bassLegato * 3.0f) : 4);
        const int duration = randomInt(rng, minDuration, maxDuration);
        auto* subCell = matrix.cellAt(TrapAlgebraLanes::Sub808, start + kickTick);
        const bool phraseAnchor = kickTick == 0 || (bar4 && kickTick >= 56);
        const bool shouldCouple = subCell != nullptr && subCell->active
            ? true
            : chance(rng, std::clamp(couplingTarget + (phraseAnchor ? 0.20f : 0.0f)
                                                   - ((kickTick == 24 || kickTick == 40) ? 0.06f : 0.0f),
                                     0.30f,
                                     0.96f));
        if (subCell != nullptr && subCell->active)
        {
            subCell->velocity = std::clamp(subCell->velocity + static_cast<int>(style.bassDistortion * 5.0f), 88, 120);
            subCell->durationTicks = duration;
            subCell->microTimingTicks = 0;
            subCell->role = TrapAlgebraRole::BassAnchor;
            subCell->roleString = roleToString(TrapAlgebraRole::BassAnchor);
        }
        else if (shouldCouple)
        {
            matrix.setNote(TrapAlgebraLanes::Sub808,
                           start + kickTick,
                           randomInt(rng, 88, 118),
                           duration,
                           0,
                           phraseAnchor ? TrapAlgebraRole::BassAnchor : TrapAlgebraRole::Bass);
        }

        if (kickTick != 0
            && (kickTick % 4) == 0
            && !matrix.hasNote(TrapAlgebraLanes::HatAccent, start + kickTick)
            && chance(rng, 0.18f + density * 0.14f + (kickTick >= 56 ? 0.10f : 0.0f)))
        {
            matrix.setNote(TrapAlgebraLanes::HatAccent,
                           start + kickTick,
                           hatVelocity(rng, kickTick, true),
                           1,
                           microTimingForLane(TrapAlgebraLanes::HatAccent, start + kickTick, params, rng),
                           kickTick >= 56 ? TrapAlgebraRole::Ending : TrapAlgebraRole::Accent);
        }
    }

    // An occasional independent 808 answer keeps the low end conversational.
    if (!selectedKicks.empty() && chance(rng, 0.10f + params.variation * 0.12f + style.kickIrregularity * 0.10f))
    {
        const int sourceKick = selectedKicks[static_cast<size_t>(randomInt(rng, 0, static_cast<int>(selectedKicks.size()) - 1))];
        const int answerTick = sourceKick + (chance(rng, 0.68f) ? 8 : 12);
        const bool validAnswer = answerTick < 64
            && answerTick != 16 && answerTick != 32 && answerTick != 48
            && !matrix.hasNote(TrapAlgebraLanes::Kick, start + answerTick)
            && !matrix.hasNote(TrapAlgebraLanes::Sub808, start + answerTick);
        if (validAnswer)
        {
            matrix.setNote(TrapAlgebraLanes::Sub808,
                           start + answerTick,
                           randomInt(rng, 82, 108),
                           randomInt(rng, 4, 8),
                           0,
                           answerTick >= 56 ? TrapAlgebraRole::BassPickup : TrapAlgebraRole::BassAnswer);
        }
    }

    for (const int tick : { 4, 12, 20, 28, 36, 44, 52, 60 })
    {
        const bool preSnare = tick == 28;
        const bool ending = bar4 && tick >= 52;
        float probability = 0.05f + style.hatRate * 0.28f + density * 0.18f + variation * 0.16f + (preSnare ? 0.08f : 0.0f) + (ending ? 0.10f : 0.0f);
        if (params.substyle == TrapAlgebraSubstyle::CloudTrap || params.substyle == TrapAlgebraSubstyle::LuxuryTrap)
            probability *= 0.80f;
        if (chance(rng, probability))
        {
            const bool accent = chance(rng, 0.18f + energy * 0.18f + (preSnare || ending ? 0.18f : 0.0f));
            matrix.setNote(accent ? TrapAlgebraLanes::HatAccent : TrapAlgebraLanes::HiHat,
                           start + tick,
                           hatVelocity(rng, tick, accent),
                           1,
                           microTimingForLane(accent ? TrapAlgebraLanes::HatAccent : TrapAlgebraLanes::HiHat, start + tick, params, rng),
                           accent ? TrapAlgebraRole::Accent : TrapAlgebraRole::Support);
        }
    }

    const int desiredRolls = bar4
                                 ? (chance(rng, 0.10f + style.rollRate * 0.80f + params.variation * 0.10f) ? 1 : 0)
                                 : (chance(rng, style.rollRate * 0.40f + density * 0.05f) ? 1 : 0);
    for (int i = 0; i < desiredRolls; ++i)
        addRoll(matrix, params, bar, bar4, rng);

    if (chance(rng, 0.06f + density * 0.10f + (bar4 ? 0.10f : 0.0f)))
        matrix.setNote(TrapAlgebraLanes::OpenHat, start + (bar4 ? randomInt(rng, 52, 60) : (chance(rng, 0.5f) ? 24 : 56)), randomInt(rng, 72, 110), 3, microTimingForLane(TrapAlgebraLanes::OpenHat, start, params, rng), TrapAlgebraRole::Accent);

    if ((bar == 0 && candidateIndex % 7 == 0 && chance(rng, 0.28f)) || (bar4 && chance(rng, 0.36f)))
        matrix.setNote(TrapAlgebraLanes::Cymbal, start + (bar4 ? 60 : 0), randomInt(rng, 78, 116), 5, 0, bar4 ? TrapAlgebraRole::Ending : TrapAlgebraRole::Accent);

    if (chance(rng, 0.04f + density * 0.08f + style.cowbell * 0.22f + (bar == 2 ? 0.05f : 0.0f)))
        matrix.setNote(TrapAlgebraLanes::Perc, start + randomInt(rng, 0, 15) * 4, randomInt(rng, 48, 94), 1, microTimingForLane(TrapAlgebraLanes::Perc, start, params, rng), TrapAlgebraRole::Support);

    if (chance(rng, 0.08f + params.humanize * 0.08f + (bar == 1 || bar4 ? 0.05f : 0.0f)))
        matrix.setNote(TrapAlgebraLanes::ClapGhost, start + randomInt(rng, 29, 35), randomInt(rng, 38, 76), 1, microTimingForLane(TrapAlgebraLanes::ClapGhost, start + 32, params, rng), TrapAlgebraRole::Ghost);
}

void TrapAlgebraEngine::shape808Durations(TrapPatternMatrix& matrix,
                                          const TrapAlgebraParams& params) const
{
    auto bassNotes = matrix.notesForLane(TrapAlgebraLanes::Sub808);
    std::stable_sort(bassNotes.begin(), bassNotes.end(), [](const auto& a, const auto& b)
    {
        return a.tick64 < b.tick64;
    });

    for (size_t index = 0; index < bassNotes.size(); ++index)
    {
        const auto& note = bassNotes[index];
        auto* cell = matrix.cellAt(TrapAlgebraLanes::Sub808, note.tick64);
        if (cell == nullptr || !cell->active)
            continue;

        const int nextTick = index + 1 < bassNotes.size() ? bassNotes[index + 1].tick64 : matrix.getTotalTicks();
        const int available = std::max(1, nextTick - note.tick64);
        float articulation = 0.76f;
        if (note.role == TrapAlgebraRole::BassAnchor)
            articulation = 0.90f;
        else if (note.role == TrapAlgebraRole::BassAnswer)
            articulation = 0.62f;
        else if (note.role == TrapAlgebraRole::BassPickup)
            articulation = 0.42f;

        if (params.substyle == TrapAlgebraSubstyle::RageTrap)
            articulation += 0.04f;
        articulation = std::clamp(articulation, 0.35f, 0.94f);

        const int roleDuration = std::max(2, static_cast<int>(std::floor(static_cast<float>(available) * articulation)));
        cell->durationTicks = std::clamp(std::min(cell->durationTicks, roleDuration), 2, available);
    }
}

void TrapAlgebraEngine::addRoll(TrapPatternMatrix& matrix,
                                const TrapAlgebraParams& params,
                                int bar,
                                bool barFill,
                                std::mt19937& rng) const
{
    constexpr int ppqPerTick64 = kPpqPerQuarter / 16;
    const int start = barStart(bar);
    const int zonePick = randomInt(rng, 0, barFill ? 3 : 2);
    int zoneStart = 24;
    int zoneEnd = 31;
    if (zonePick == 1)
    {
        zoneStart = 34;
        zoneEnd = 40;
    }
    else if (zonePick == 2)
    {
        zoneStart = 56;
        zoneEnd = 63;
    }
    else if (zonePick == 3)
    {
        zoneStart = 48;
        zoneEnd = 63;
    }

    const auto style = weightsForSubstyle(params.substyle);
    HatBurst burst;
    burst.role = barFill ? HatBurstRole::PhraseEnding
                         : (zonePick == 0 ? HatBurstRole::PreSnareTension
                                          : (zonePick == 1 ? HatBurstRole::PostSnareRelease
                                                           : HatBurstRole::BarTransition));
    burst.strength = clamp01(0.35f + style.rollRate * 0.45f + params.density * 0.20f);

    const float tripletProbability = clamp01(style.tripletBias * 0.62f);
    if (chance(rng, tripletProbability))
        burst.rate = chance(rng, 0.72f) ? HatRate::SixteenthTriplet : HatRate::ThirtySecondTriplet;
    else
    {
        const float extremeProbability = std::pow(params.density, 2.5f)
            * (params.substyle == TrapAlgebraSubstyle::RageTrap ? 0.34f : 0.12f);
        burst.rate = chance(rng, extremeProbability) ? HatRate::SixtyFourth : HatRate::ThirtySecond;
    }

    // Geometric-like short prior: 2/3 dominate, long runs require an ending role.
    burst.noteCount = 2;
    const int hardMax = barFill ? 7 : (burst.rate == HatRate::SixtyFourth ? 5 : 6);
    while (burst.noteCount < hardMax && chance(rng, 0.43f))
        ++burst.noteCount;
    burst.contour = static_cast<HatVelocityContour>(randomInt(rng, 0, 4));

    const auto intervalForRate = [&](HatRate rate)
    {
        switch (rate)
        {
            case HatRate::Eighth: return params.tempoContext.straightSubdivisionPpq(8);
            case HatRate::Sixteenth: return params.tempoContext.straightSubdivisionPpq(16);
            case HatRate::ThirtySecond: return params.tempoContext.straightSubdivisionPpq(32);
            case HatRate::SixtyFourth: return params.tempoContext.straightSubdivisionPpq(64);
            case HatRate::EighthTriplet: return params.tempoContext.tripletSubdivisionPpq(8);
            case HatRate::SixteenthTriplet: return params.tempoContext.tripletSubdivisionPpq(16);
            case HatRate::ThirtySecondTriplet: return params.tempoContext.tripletSubdivisionPpq(32);
            default: return params.tempoContext.straightSubdivisionPpq(32);
        }
    };

    const int intervalPpq = intervalForRate(burst.rate);
    const int zoneStartPpq = (start + zoneStart) * ppqPerTick64;
    const int zoneEndPpq = (start + zoneEnd) * ppqPerTick64;
    const int burstSpanPpq = std::max(0, burst.noteCount - 1) * intervalPpq;
    const int latestStartPpq = std::max(zoneStartPpq, zoneEndPpq - burstSpanPpq);
    const int startQuantum = std::max(1, ppqPerTick64);
    const int startChoices = std::max(0, (latestStartPpq - zoneStartPpq) / startQuantum);
    burst.startPpq = zoneStartPpq + randomInt(rng, 0, startChoices) * startQuantum;

    for (int i = 0; i < burst.noteCount; ++i)
    {
        const int exactPpq = burst.startPpq + i * intervalPpq;
        if (exactPpq > zoneEndPpq)
            break;

        int velocity = 72;
        if (burst.contour == HatVelocityContour::Rising)
            velocity = 48 + i * 7 + randomInt(rng, -3, 4);
        else if (burst.contour == HatVelocityContour::Falling)
            velocity = 96 - i * 6 + randomInt(rng, -3, 4);
        else if (burst.contour == HatVelocityContour::Wave)
            velocity = 68 + static_cast<int>(std::sin(static_cast<float>(i) * 1.7f) * 18.0f) + randomInt(rng, -2, 3);
        else if (burst.contour == HatVelocityContour::StrongWeak)
            velocity = (i % 2 == 0 ? 92 : 56) + randomInt(rng, -3, 3);
        else
            velocity = (i == burst.noteCount - 1 ? 102 : 58 + i * 3) + randomInt(rng, -3, 3);

        const int nearestTick64 = static_cast<int>(std::lround(static_cast<double>(exactPpq)
                                                                / static_cast<double>(ppqPerTick64)));
        const int structuralRemainderPpq = exactPpq - nearestTick64 * ppqPerTick64;
        const int lane = (i == burst.noteCount - 1 && chance(rng, 0.22f))
            ? TrapAlgebraLanes::HatAccent
            : TrapAlgebraLanes::HiHat;

        matrix.setNote(lane,
                       nearestTick64,
                       std::clamp(velocity, 42, 112),
                       1,
                       structuralRemainderPpq,
                       TrapAlgebraRole::Roll);
    }
}

std::pair<float, float> couplingBand(TrapAlgebraSubstyle substyle)
{
    switch (substyle)
    {
        case TrapAlgebraSubstyle::RageTrap: return { 0.72f, 0.90f };
        case TrapAlgebraSubstyle::CloudTrap: return { 0.50f, 0.72f };
        case TrapAlgebraSubstyle::DarkTrap: return { 0.58f, 0.78f };
        case TrapAlgebraSubstyle::MemphisTrap: return { 0.60f, 0.80f };
        case TrapAlgebraSubstyle::LuxuryTrap: return { 0.56f, 0.76f };
        case TrapAlgebraSubstyle::ATLClassic:
        default: return { 0.62f, 0.82f };
    }
}

std::pair<float, float> occupancyBand(TrapAlgebraSubstyle substyle)
{
    switch (substyle)
    {
        case TrapAlgebraSubstyle::RageTrap: return { 0.34f, 0.55f };
        case TrapAlgebraSubstyle::CloudTrap: return { 0.22f, 0.42f };
        case TrapAlgebraSubstyle::DarkTrap: return { 0.24f, 0.43f };
        case TrapAlgebraSubstyle::MemphisTrap: return { 0.26f, 0.46f };
        case TrapAlgebraSubstyle::LuxuryTrap: return { 0.25f, 0.44f };
        case TrapAlgebraSubstyle::ATLClassic:
        default: return { 0.28f, 0.48f };
    }
}

void TrapAlgebraEngine::repair(TrapAlgebraPattern& pattern,
                               const TrapAlgebraParams& params,
                               const TrapSubstyleWeights& weights) const
{
    auto& matrix = pattern.matrix;

    for (int bar = 0; bar < params.bars; ++bar)
    {
        const std::array<int, 2> expectedBackbeats { params.tempoContext.doubleTime ? 16 : 32,
                                                     params.tempoContext.doubleTime ? 48 : -1 };
        for (const int localTick : expectedBackbeats)
        {
            if (localTick < 0)
                continue;
            const int tick = barStart(bar) + localTick;
            if (!matrix.hasNote(TrapAlgebraLanes::Snare, tick))
            {
                matrix.setNote(TrapAlgebraLanes::Snare, tick, 112, 2, 0, TrapAlgebraRole::Anchor);
                pattern.repairsApplied.add("add_missing_snare_backbone");
            }
        }
    }

    if (matrix.countLane(TrapAlgebraLanes::Sub808) == 0)
    {
        int anchorTick = 0;
        const auto kicks = matrix.notesForLane(TrapAlgebraLanes::Kick);
        if (!kicks.empty())
            anchorTick = kicks.front().tick64;
        matrix.setNote(TrapAlgebraLanes::Sub808, anchorTick, 106, 14, 0, TrapAlgebraRole::Bass);
        pattern.repairsApplied.add("add_missing_808_anchor");
    }

    for (int bar = 0; bar < params.bars; ++bar)
    {
        for (const auto& kick : matrix.notesForLane(TrapAlgebraLanes::Kick))
        {
            if (kick.barIndex != bar)
                continue;

            const int local = tickInBar(kick.tick64);
            const bool bar4 = (bar % 4) == 3;
            if (!isLegalMainKickLocalTick(local, bar4) || isStumblingMainKickLocalTick(local))
            {
                matrix.clearNote(TrapAlgebraLanes::Kick, kick.tick64);
                matrix.clearNote(TrapAlgebraLanes::Sub808, kick.tick64);
                pattern.repairsApplied.add("remove_illegal_main_kick_position");
            }
        }

        const int kickCount = matrix.countLaneInBar(TrapAlgebraLanes::Kick, bar);
        const int minKicks = 1;
        const int maxKicks = (bar % 4) == 3 ? 4 : 3;
        if (kickCount < minKicks)
        {
            const int tick = barStart(bar) + fallbackKickLocalTickForBar(bar);
            matrix.setNote(TrapAlgebraLanes::Kick, tick, 108, 2, 0, TrapAlgebraRole::Anchor);
            if (!matrix.hasNote(TrapAlgebraLanes::Sub808, tick))
                matrix.setNote(TrapAlgebraLanes::Sub808, tick, 106, 12, 0, TrapAlgebraRole::Bass);
            pattern.repairsApplied.add("add_missing_bar_kick");
        }
        else if (kickCount > maxKicks)
        {
            auto kicks = matrix.notesForLane(TrapAlgebraLanes::Kick);
            std::vector<TrapAlgebraNote> barKicks;
            for (const auto& kick : kicks)
                if (kick.barIndex == bar)
                    barKicks.push_back(kick);
            std::stable_sort(barKicks.begin(), barKicks.end(), [](const auto& a, const auto& b)
            {
                const auto priority = [](const auto& note)
                {
                    const int local = tickInBar(note.tick64);
                    if (local == 0) return 1000 + note.velocity;
                    if (local == 24 || local == 36 || local == 40) return 800 + note.velocity;
                    if ((note.barIndex % 4) == 3 && local == 56) return 700 + note.velocity;
                    if (local == 16) return 650 + note.velocity;
                    return note.velocity;
                };
                return priority(a) > priority(b);
            });
            for (size_t index = static_cast<size_t>(maxKicks); index < barKicks.size(); ++index)
            {
                matrix.clearNote(TrapAlgebraLanes::Kick, barKicks[index].tick64);
                matrix.clearNote(TrapAlgebraLanes::Sub808, barKicks[index].tick64);
            }
            pattern.repairsApplied.add("reduce_excess_kicks");
        }
    }

    const int totalTicks = matrix.getTotalTicks();
    const auto [occupancyLow, occupancyHigh] = occupancyBand(params.substyle);
    if (matrix.active808Ticks() < static_cast<int>(occupancyLow * static_cast<float>(totalTicks)))
    {
        auto subs = matrix.notesForLane(TrapAlgebraLanes::Sub808);
        std::stable_sort(subs.begin(), subs.end(), [](const auto& a, const auto& b)
        {
            return a.velocity > b.velocity;
        });
        for (int pass = 0; pass < 6 && matrix.active808Ticks() < static_cast<int>(occupancyLow * static_cast<float>(totalTicks)); ++pass)
        {
            for (const auto& sub : subs)
            {
                auto* cell = matrix.cellAt(TrapAlgebraLanes::Sub808, sub.tick64);
                if (cell != nullptr && cell->active)
                    cell->durationTicks = std::min(32, cell->durationTicks + 4);
                if (matrix.active808Ticks() >= static_cast<int>(occupancyLow * static_cast<float>(totalTicks)))
                    break;
            }
        }
        pattern.repairsApplied.add("restore_808_bass_weight");
    }

    if (matrix.active808Ticks() > static_cast<int>(occupancyHigh * static_cast<float>(totalTicks)))
    {
        auto subs = matrix.notesForLane(TrapAlgebraLanes::Sub808);
        std::stable_sort(subs.begin(), subs.end(), [](const auto& a, const auto& b)
        {
            return a.durationTicks > b.durationTicks;
        });
        for (const auto& sub : subs)
        {
            auto* cell = matrix.cellAt(TrapAlgebraLanes::Sub808, sub.tick64);
            if (cell != nullptr && cell->active)
                cell->durationTicks = std::max(4, cell->durationTicks - 4);
            if (matrix.active808Ticks() <= static_cast<int>(occupancyHigh * static_cast<float>(totalTicks)))
                break;
        }
        pattern.repairsApplied.add("reduce_constant_808_wall");
    }

    auto hats = matrix.notesForLane(TrapAlgebraLanes::HiHat);
    const bool flatHats = hats.size() > 1 && velocityVariance(hats) < weights.hatVarianceMin;
    if (flatHats)
    {
        int index = 0;
        for (const auto& hat : hats)
        {
            auto* cell = matrix.cellAt(TrapAlgebraLanes::HiHat, hat.tick64);
            if (cell != nullptr && cell->active)
                cell->velocity = std::clamp(cell->velocity + ((index % 5) - 2) * 6, 1, 127);
            ++index;
        }
        pattern.repairsApplied.add("add_hat_velocity_variance");
    }

    for (int tick = 0; tick < totalTicks; ++tick)
    {
        while (matrix.activeLaneCountAt(tick) > weights.activeLaneMax)
        {
            bool removed = false;
            for (const int lane : { TrapAlgebraLanes::Perc, TrapAlgebraLanes::Cymbal, TrapAlgebraLanes::OpenHat, TrapAlgebraLanes::ClapGhost, TrapAlgebraLanes::HatAccent, TrapAlgebraLanes::KickGhost })
            {
                if (matrix.hasNote(lane, tick))
                {
                    matrix.clearNote(lane, tick);
                    removed = true;
                    pattern.repairsApplied.add("reduce_simultaneous_overload");
                    break;
                }
            }
            if (!removed)
                break;
        }
    }

    for (const auto& hat : matrix.notesForLane(TrapAlgebraLanes::HiHat))
    {
        if (hat.role != TrapAlgebraRole::Roll)
            continue;

        const bool bar4 = (hat.barIndex % 4) == 3;
        if (!inRollZone(hat.tick64, bar4))
        {
            const int newTick = barStart(hat.barIndex) + nearestRollZoneStart(hat.tick64, bar4);
            matrix.clearNote(TrapAlgebraLanes::HiHat, hat.tick64);
            matrix.setNote(TrapAlgebraLanes::HiHat, newTick, hat.velocity, 1, hat.microTimingTicks, TrapAlgebraRole::Roll);
            pattern.repairsApplied.add("move_roll_to_valid_zone");
        }
    }

    if (params.bars >= 4)
    {
        const TrapQualityScorer scorer;
        const auto score = scorer.score(matrix, params, weights);
        if (score.d02 < 0.04f)
        {
            matrix.setNote(TrapAlgebraLanes::HatAccent, barStart(2) + 28, 96, 1, swingOffsetTicks(params.swing), TrapAlgebraRole::Accent);
            matrix.setNote(TrapAlgebraLanes::HiHat, barStart(2) + 60, 72, 1, swingOffsetTicks(params.swing), TrapAlgebraRole::Support);
            pattern.repairsApplied.add("add_bar_3_answer_variation");
        }

        if (score.d03 < 0.08f)
        {
            const int tick = barStart(3) + 60;
            matrix.setNote(TrapAlgebraLanes::HatAccent, tick, 102, 1, swingOffsetTicks(params.swing), TrapAlgebraRole::Ending);
            matrix.setNote(TrapAlgebraLanes::OpenHat, barStart(3) + 56, 92, 3, swingOffsetTicks(params.swing), TrapAlgebraRole::Ending);
            pattern.repairsApplied.add("add_bar_4_ending_variation");
        }
    }
}

TrapSubstyleWeights TrapAlgebraEngine::weightsForSubstyle(TrapAlgebraSubstyle substyle)
{
    TrapSubstyleWeights weights;
    switch (substyle)
    {
        case TrapAlgebraSubstyle::DarkTrap:
            weights = { "DarkTrap", 1.50f, 1.60f, 0.90f, 0.50f, 1.70f, 0.70f, 0.80f, 1.80f, 1.60f, 0.18f, 45.0f, 4,
                        0.40f, 0.22f, 0.20f, 0.42f, 0.62f, 0.68f, 0.82f, 0.20f, 0.15f, 0.20f, 0.08f, 0.10f, 0.18f, 0.12f };
            break;
        case TrapAlgebraSubstyle::CloudTrap:
            weights = { "CloudTrap", 1.35f, 1.45f, 1.00f, 0.80f, 1.45f, 0.90f, 0.80f, 1.45f, 1.20f, 0.20f, 50.0f, 4,
                        0.34f, 0.18f, 0.12f, 0.30f, 0.42f, 0.20f, 0.22f, 0.95f, 0.60f, 0.10f, 0.12f, 0.05f, 0.12f, 0.28f };
            break;
        case TrapAlgebraSubstyle::RageTrap:
            weights = { "RageTrap", 1.30f, 1.45f, 1.45f, 1.35f, 0.82f, 0.95f, 0.65f, 1.65f, 1.25f, 0.38f, 75.0f, 5,
                        0.60f, 0.70f, 0.10f, 0.58f, 0.45f, 0.80f, 0.75f, 0.65f, 0.90f, 0.05f, 0.95f, 0.05f, 0.06f, 0.18f };
            break;
        case TrapAlgebraSubstyle::MemphisTrap:
            weights = { "MemphisTrap", 1.45f, 1.50f, 1.25f, 1.15f, 1.10f, 0.85f, 0.70f, 1.60f, 1.35f, 0.30f, 65.0f, 4,
                        0.52f, 0.40f, 0.95f, 0.44f, 0.50f, 0.55f, 0.80f, 0.15f, 0.20f, 0.10f, 0.05f, 0.90f, 0.70f, 0.08f };
            break;
        case TrapAlgebraSubstyle::LuxuryTrap:
            weights = { "LuxuryTrap", 1.45f, 1.45f, 1.05f, 0.70f, 1.40f, 0.85f, 0.80f, 1.65f, 1.20f, 0.22f, 55.0f, 4,
                        0.42f, 0.20f, 0.08f, 0.36f, 0.48f, 0.25f, 0.70f, 0.70f, 0.65f, 0.25f, 0.08f, 0.05f, 0.05f, 0.95f };
            break;
        case TrapAlgebraSubstyle::ATLClassic:
        default:
            weights = { "ATLClassic", 1.40f, 1.50f, 1.20f, 0.90f, 1.10f, 0.80f, 0.70f, 1.60f, 1.40f, 0.25f, 55.0f, 4,
                        0.48f, 0.25f, 0.15f, 0.48f, 0.35f, 0.35f, 0.90f, 0.25f, 0.45f, 0.90f, 0.05f, 0.05f, 0.05f, 0.35f };
            break;
    }
    return weights;
}

const char* TrapAlgebraEngine::roleToString(TrapAlgebraRole role)
{
    switch (role)
    {
        case TrapAlgebraRole::Anchor: return "anchor";
        case TrapAlgebraRole::Support: return "support";
        case TrapAlgebraRole::Ghost: return "ghost";
        case TrapAlgebraRole::Accent: return "accent";
        case TrapAlgebraRole::Roll: return "roll";
        case TrapAlgebraRole::Fill: return "fill";
        case TrapAlgebraRole::Bass: return "bass";
        case TrapAlgebraRole::BassAnchor: return "bass_anchor";
        case TrapAlgebraRole::BassAnswer: return "bass_answer";
        case TrapAlgebraRole::BassPickup: return "bass_pickup";
        case TrapAlgebraRole::BassHold: return "bass_hold";
        case TrapAlgebraRole::Ending: return "ending";
        default: return "support";
    }
}

const char* TrapAlgebraEngine::substyleToString(TrapAlgebraSubstyle substyle)
{
    switch (substyle)
    {
        case TrapAlgebraSubstyle::ATLClassic: return "ATLClassic";
        case TrapAlgebraSubstyle::DarkTrap: return "DarkTrap";
        case TrapAlgebraSubstyle::CloudTrap: return "CloudTrap";
        case TrapAlgebraSubstyle::RageTrap: return "RageTrap";
        case TrapAlgebraSubstyle::MemphisTrap: return "MemphisTrap";
        case TrapAlgebraSubstyle::LuxuryTrap: return "LuxuryTrap";
        default: return "ATLClassic";
    }
}

int TrapAlgebraEngine::ticksPerBar()
{
    return kTicksPerBar;
}

int TrapAlgebraEngine::ticksForBars(int bars)
{
    return std::clamp(bars, 1, 16) * kTicksPerBar;
}

juce::String TrapAlgebraEngine::buildDebugSummary(const TrapAlgebraPattern& pattern,
                                                  const TrapAlgebraParams& params,
                                                  const TrapSubstyleWeights& weights) const
{
    juce::ignoreUnused(weights);

    juce::StringArray lines;
    lines.add("style: Trap Algebra Engine");
    lines.add("substyle: " + juce::String(substyleToString(params.substyle)));
    lines.add("tempo host/style/multiplier: " + juce::String(params.tempoContext.hostBpm, 2) + "/"
              + juce::String(params.tempoContext.styleBpm, 2) + "/"
              + juce::String(params.tempoContext.clockMultiplier, 1));
    lines.add("macro host bars / micro style bars: " + juce::String(params.tempoContext.hostBars) + "/"
              + juce::String(params.tempoContext.styleBars));
    lines.add("seed: " + juce::String(params.seed));
    lines.add("temperature: " + juce::String(params.temperature, 3));
    lines.add("candidate count: " + juce::String(params.candidateCount));
    lines.add("selected candidate index: " + juce::String(pattern.selectedCandidateIndex));
    lines.add("near-best pool/best: " + juce::String(pattern.nearBestPoolSize) + "/" + juce::String(pattern.bestCandidateQuality, 3));
    lines.add("shared features density/sync/velocity/timing/space/repeat/interlock/roles: "
              + juce::String(pattern.features.density, 3) + "/" + juce::String(pattern.features.syncopation, 3) + "/"
              + juce::String(pattern.features.velocityLife, 3) + "/" + juce::String(pattern.features.timingActivity, 3) + "/"
              + juce::String(pattern.features.negativeSpace, 3) + "/" + juce::String(pattern.features.repetition, 3) + "/"
              + juce::String(pattern.features.interlock, 3) + "/" + juce::String(pattern.features.roleClarity, 3));
    lines.add("style target fit/distance/selection Q: " + juce::String(pattern.styleMatch.fit, 3) + "/"
              + juce::String(pattern.styleMatch.weightedDistance, 3) + "/"
              + juce::String(pattern.selectedCandidateQuality, 3));
    lines.add("final Q score: " + juce::String(pattern.score.quality, 3));
    lines.add("components S/K/H/R/N/V/G/O/M: "
              + juce::String(pattern.score.snareBackboneScore, 3) + "/"
              + juce::String(pattern.score.kick808CouplingScore, 3) + "/"
              + juce::String(pattern.score.hiHatMovementScore, 3) + "/"
              + juce::String(pattern.score.rollQualityScore, 3) + "/"
              + juce::String(pattern.score.negativeSpaceScore, 3) + "/"
              + juce::String(pattern.score.barVariationScore, 3) + "/"
              + juce::String(pattern.score.grooveMicrotimingScore, 3) + "/"
              + juce::String(pattern.score.overloadPenalty, 3) + "/"
              + juce::String(pattern.score.mudPenalty, 3));
    lines.add("snare missing count: " + juce::String(pattern.score.snareMissingCount));
    lines.add("kick/808 coupling ratio: " + juce::String(pattern.score.kick808CouplingRatio, 3));
    lines.add("808 density: " + juce::String(pattern.score.sub808Density, 3));
    lines.add("hat velocity variance: " + juce::String(pattern.score.hatVelocityVariance, 3));
    lines.add("roll count and average roll quality: " + juce::String(pattern.score.rollCount) + " / " + juce::String(pattern.score.averageRollQuality, 3));
    lines.add("negative space score: " + juce::String(pattern.score.negativeSpaceScore, 3));
    lines.add("bar distances D01/D02/D03: "
              + juce::String(pattern.score.d01, 3) + "/"
              + juce::String(pattern.score.d02, 3) + "/"
              + juce::String(pattern.score.d03, 3));
    lines.add("repair actions applied: " + (pattern.repairsApplied.isEmpty() ? juce::String("none") : pattern.repairsApplied.joinIntoString(",")));
    return lines.joinIntoString("\n");
}
} // namespace bbg
