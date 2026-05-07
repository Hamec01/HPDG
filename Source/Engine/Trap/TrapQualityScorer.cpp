#include "TrapAlgebraEngine.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace bbg
{
namespace
{
constexpr int kTicksPerBar = 64;

int tickInBar(int tick64)
{
    const int tick = tick64 % kTicksPerBar;
    return tick < 0 ? tick + kTicksPerBar : tick;
}

float clamp01(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

float distanceToRange(float value, float low, float high)
{
    if (value >= low && value <= high)
        return 0.0f;
    return value < low ? low - value : value - high;
}

float rangeScore(float value, float low, float high, float tolerance)
{
    return clamp01(1.0f - distanceToRange(value, low, high) / std::max(0.0001f, tolerance));
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

float normalizedBarDistance(const TrapPatternMatrix& matrix, int lhsBar, int rhsBar)
{
    if (lhsBar < 0 || rhsBar < 0 || lhsBar >= matrix.getBars() || rhsBar >= matrix.getBars())
        return 0.0f;

    int diff = 0;
    for (int lane = 0; lane < TrapAlgebraLanes::Count; ++lane)
    {
        for (int tick = 0; tick < kTicksPerBar; ++tick)
        {
            const bool lhs = matrix.hasNote(lane, lhsBar * kTicksPerBar + tick);
            const bool rhs = matrix.hasNote(lane, rhsBar * kTicksPerBar + tick);
            if (lhs != rhs)
                ++diff;
        }
    }
    return static_cast<float>(diff) / static_cast<float>(TrapAlgebraLanes::Count * kTicksPerBar);
}

struct RollSegment
{
    std::vector<TrapAlgebraNote> notes;
};

std::vector<RollSegment> collectRolls(const TrapPatternMatrix& matrix)
{
    std::vector<TrapAlgebraNote> rolls;
    for (const auto lane : { TrapAlgebraLanes::HiHat, TrapAlgebraLanes::HatAccent })
    {
        for (const auto& note : matrix.notesForLane(lane))
            if (note.role == TrapAlgebraRole::Roll)
                rolls.push_back(note);
    }

    std::stable_sort(rolls.begin(), rolls.end(), [](const auto& a, const auto& b)
    {
        return a.tick64 < b.tick64;
    });

    std::vector<RollSegment> segments;
    for (const auto& note : rolls)
    {
        if (segments.empty() || note.tick64 - segments.back().notes.back().tick64 > 3 || note.barIndex != segments.back().notes.back().barIndex)
            segments.push_back({});
        segments.back().notes.push_back(note);
    }
    return segments;
}

float zoneScore(const RollSegment& segment)
{
    if (segment.notes.empty())
        return 0.0f;

    const int bar = segment.notes.front().barIndex;
    const bool bar4 = (bar % 4) == 3;
    const float center = std::accumulate(segment.notes.begin(), segment.notes.end(), 0.0f, [](float sum, const auto& note)
    {
        return sum + static_cast<float>(tickInBar(note.tick64));
    }) / static_cast<float>(segment.notes.size());

    struct Zone { float start; float end; };
    std::vector<Zone> zones { { 24.0f, 31.0f }, { 34.0f, 40.0f }, { 56.0f, 63.0f } };
    if (bar4)
        zones.push_back({ 48.0f, 63.0f });

    float bestDistance = 64.0f;
    for (const auto& zone : zones)
    {
        const float clamped = std::clamp(center, zone.start, zone.end);
        bestDistance = std::min(bestDistance, std::abs(center - clamped));
    }
    return std::exp(-(bestDistance * bestDistance) / (2.0f * 4.0f * 4.0f));
}

float lengthScore(const RollSegment& segment)
{
    const int length = static_cast<int>(segment.notes.size());
    if (length <= 0)
        return 0.0f;

    const bool bar4 = (segment.notes.front().barIndex % 4) == 3;
    const float target = bar4 ? 7.0f : 4.0f;
    const float sigma = bar4 ? 3.0f : 2.0f;
    return std::exp(-((static_cast<float>(length) - target) * (static_cast<float>(length) - target)) / (2.0f * sigma * sigma));
}

float velocityContourScore(const RollSegment& segment)
{
    const int count = static_cast<int>(segment.notes.size());
    if (count < 2)
        return 0.35f;

    float meanIndex = 0.0f;
    float meanVelocity = 0.0f;
    for (int index = 0; index < count; ++index)
    {
        meanIndex += static_cast<float>(index);
        meanVelocity += static_cast<float>(segment.notes[static_cast<size_t>(index)].velocity);
    }
    meanIndex /= static_cast<float>(count);
    meanVelocity /= static_cast<float>(count);

    float numerator = 0.0f;
    float denominator = 0.0f;
    float stutter = 0.0f;
    for (int index = 0; index < count; ++index)
    {
        const float x = static_cast<float>(index) - meanIndex;
        const float y = static_cast<float>(segment.notes[static_cast<size_t>(index)].velocity) - meanVelocity;
        numerator += x * y;
        denominator += x * x;
        stutter += ((index % 2) == 0 ? 1.0f : -1.0f) * y;
    }

    const float slope = denominator > 0.0f ? numerator / denominator : 0.0f;
    const float rising = 1.0f / (1.0f + std::exp(-slope * 0.10f));
    const float falling = 1.0f / (1.0f + std::exp(slope * 0.10f));
    const float stutterScore = clamp01(0.5f + stutter / (static_cast<float>(count) * 50.0f));
    const float varianceScore = clamp01(velocityVariance(segment.notes) / 90.0f);
    return std::max({ rising, falling, stutterScore, varianceScore });
}

float anchorRelationScore(const TrapPatternMatrix& matrix, const RollSegment& segment)
{
    if (segment.notes.empty())
        return 0.0f;

    const int center = segment.notes[segment.notes.size() / 2].tick64;
    float best = 0.0f;
    for (int offset = -12; offset <= 12; ++offset)
    {
        if (matrix.hasNote(TrapAlgebraLanes::Snare, center + offset)
            || matrix.hasNote(TrapAlgebraLanes::Kick, center + offset)
            || tickInBar(center) >= 56)
        {
            best = std::max(best, 1.0f - std::abs(offset) / 12.0f);
        }
    }
    return best;
}

float gridScore(const RollSegment& segment)
{
    if (segment.notes.size() < 2)
        return 0.5f;

    int valid = 0;
    for (size_t i = 1; i < segment.notes.size(); ++i)
    {
        const int gap = segment.notes[i].tick64 - segment.notes[i - 1].tick64;
        if (gap == 1 || gap == 2)
            ++valid;
    }
    return static_cast<float>(valid) / static_cast<float>(segment.notes.size() - 1);
}

float closenessScore(float value, float target, float tolerance)
{
    return clamp01(1.0f - std::abs(value - target) / std::max(0.0001f, tolerance));
}

float lowEndPocketScore(const TrapPatternMatrix& matrix)
{
    const int bars = std::max(1, matrix.getBars());
    float total = 0.0f;

    for (int bar = 0; bar < bars; ++bar)
    {
        const auto barStart = bar * kTicksPerBar;
        std::vector<int> ticks;
        for (const auto& kick : matrix.notesForLane(TrapAlgebraLanes::Kick))
            if (kick.barIndex == bar)
                ticks.push_back(tickInBar(kick.tick64));

        std::sort(ticks.begin(), ticks.end());
        ticks.erase(std::unique(ticks.begin(), ticks.end()), ticks.end());

        const int count = static_cast<int>(ticks.size());
        const bool bar4 = (bar % 4) == 3;
        float countScore = rangeScore(static_cast<float>(count), 1.0f, bar4 ? 4.0f : 3.0f, 1.0f);
        float anchorScore = matrix.hasNote(TrapAlgebraLanes::Kick, barStart) ? 1.0f : 0.25f;

        float positionScore = 0.0f;
        int closePairs = 0;
        int stumbleHits = 0;
        for (size_t i = 0; i < ticks.size(); ++i)
        {
            const int tick = ticks[i];
            const bool downbeat = tick == 0;
            const bool preSnare = tick == 16 || tick == 24;
            const bool postSnare = tick == 36 || tick == 40;
            const bool ending = tick == 56;
            const bool legal = downbeat || preSnare || postSnare || ending;
            positionScore += legal ? 1.0f : 0.0f;

            if (tick == 12 || tick == 28 || tick == 44 || tick == 52 || tick == 60)
                ++stumbleHits;
            if (tick == 32)
                ++stumbleHits;
            if (i > 0 && tick - ticks[i - 1] < 7)
                ++closePairs;
        }
        positionScore = ticks.empty() ? 0.0f : positionScore / static_cast<float>(ticks.size());

        const bool hasPreSnare = std::any_of(ticks.begin(), ticks.end(), [](int tick) { return tick == 16 || tick == 24; });
        const bool hasPostSnare = std::any_of(ticks.begin(), ticks.end(), [](int tick) { return tick == 36 || tick == 40; });
        const bool hasEnding = std::any_of(ticks.begin(), ticks.end(), [](int tick) { return tick == 56; });
        const float snareRelation = (hasPreSnare ? 0.42f : 0.0f)
                                  + (hasPostSnare ? 0.42f : 0.0f)
                                  + ((bar4 && hasEnding) ? 0.16f : 0.08f);

        float hatGlue = 0.0f;
        for (const int tick : ticks)
        {
            const bool hatPulse = matrix.hasNote(TrapAlgebraLanes::HiHat, barStart + tick)
                || matrix.hasNote(TrapAlgebraLanes::HatAccent, barStart + tick)
                || (tick % 8) == 0
                || (tick % 4) == 0;
            hatGlue += hatPulse ? 1.0f : 0.25f;
        }
        hatGlue = ticks.empty() ? 0.0f : hatGlue / static_cast<float>(ticks.size());

        const float stumblePenalty = std::min(0.75f, static_cast<float>(closePairs) * 0.22f + static_cast<float>(stumbleHits) * 0.28f);
        total += clamp01(0.22f * countScore
                       + 0.20f * anchorScore
                       + 0.24f * positionScore
                       + 0.20f * clamp01(snareRelation)
                       + 0.14f * hatGlue
                       - stumblePenalty);
    }

    return total / static_cast<float>(bars);
}
} // namespace

float TrapEnergyModel::energy(const TrapQualityBreakdown& score, const TrapSubstyleWeights& weights) const
{
    const float eSnare = 1.0f - score.snareBackboneScore;
    const float eKick808 = 1.0f - score.kick808CouplingScore;
    const float eHat = 1.0f - score.hiHatMovementScore;
    const float eRoll = 1.0f - score.rollQualityScore;
    const float eSpace = 1.0f - score.negativeSpaceScore;
    const float eVar = 1.0f - score.barVariationScore;
    const float eSpam = score.spamPenalty + score.overloadPenalty + score.mudPenalty;

    return weights.snare * eSnare
        + weights.kick808 * eKick808
        + weights.hat * eHat
        + weights.roll * eRoll
        + weights.negativeSpace * eSpace
        + weights.variation * eVar
        + weights.overload * eSpam;
}

TrapQualityBreakdown TrapQualityScorer::score(const TrapPatternMatrix& matrix,
                                              const TrapAlgebraParams& params,
                                              const TrapSubstyleWeights& weights) const
{
    juce::ignoreUnused(params);

    TrapQualityBreakdown out;
    const int bars = std::max(1, matrix.getBars());
    const int totalTicks = matrix.getTotalTicks();

    int missingSnare = 0;
    for (int bar = 0; bar < bars; ++bar)
        if (!matrix.hasNote(TrapAlgebraLanes::Snare, bar * kTicksPerBar + 32))
            ++missingSnare;
    out.snareMissingCount = missingSnare;
    out.snareBackboneScore = 1.0f - static_cast<float>(missingSnare) / static_cast<float>(bars);

    const auto kicks = matrix.notesForLane(TrapAlgebraLanes::Kick);
    int coupledKicks = 0;
    for (const auto& kick : kicks)
    {
        for (int offset = -2; offset <= 2; ++offset)
        {
            if (matrix.hasNote(TrapAlgebraLanes::Sub808, kick.tick64 + offset))
            {
                ++coupledKicks;
                break;
            }
        }
    }
    out.kick808CouplingRatio = kicks.empty() ? 0.0f : static_cast<float>(coupledKicks) / static_cast<float>(kicks.size());
    out.kick808CouplingScore = 0.64f * out.kick808CouplingRatio + 0.36f * lowEndPocketScore(matrix);

    out.sub808Density = totalTicks > 0 ? static_cast<float>(matrix.active808Ticks()) / static_cast<float>(totalTicks) : 0.0f;
    const float mud = std::max(0.0f, out.sub808Density - 0.65f);
    const float empty = std::max(0.0f, 0.18f - out.sub808Density);
    out.mudPenalty = clamp01((mud * mud + empty * empty) * 8.0f);

    const auto hats = matrix.notesForLane(TrapAlgebraLanes::HiHat);
    const auto hatAccents = matrix.notesForLane(TrapAlgebraLanes::HatAccent);
    std::vector<TrapAlgebraNote> allHats = hats;
    allHats.insert(allHats.end(), hatAccents.begin(), hatAccents.end());
    out.hatVelocityVariance = velocityVariance(allHats);
    const float hatDensity = totalTicks > 0 ? static_cast<float>(allHats.size()) / static_cast<float>(totalTicks) : 0.0f;
    const float densityScore = rangeScore(hatDensity, std::max(0.03f, weights.targetHatDensity - 0.07f), weights.targetHatDensity + 0.07f, 0.14f);
    const float varianceScore = clamp01(out.hatVelocityVariance / std::max(1.0f, weights.hatVarianceMin));
    const float restScore = allHats.size() < static_cast<size_t>(totalTicks / 2) ? 1.0f : 0.2f;
    const float accentScore = hatAccents.empty() ? 0.62f : rangeScore(static_cast<float>(hatAccents.size()) / std::max(1.0f, static_cast<float>(allHats.size())), 0.04f, 0.22f, 0.18f);
    out.hiHatMovementScore = 0.34f * densityScore + 0.34f * varianceScore + 0.18f * restScore + 0.14f * accentScore;

    const auto rolls = collectRolls(matrix);
    out.rollCount = static_cast<int>(rolls.size());
    if (rolls.empty())
        out.averageRollQuality = weights.rollRate < 0.24f ? 0.82f : 0.55f;
    else
    {
        float totalRollQuality = 0.0f;
        for (const auto& roll : rolls)
        {
            const float quality = zoneScore(roll)
                * lengthScore(roll)
                * velocityContourScore(roll)
                * std::max(0.35f, anchorRelationScore(matrix, roll))
                * gridScore(roll);
            totalRollQuality += clamp01(quality);
        }
        out.averageRollQuality = totalRollQuality / static_cast<float>(rolls.size());
    }
    const float rollCountPenalty = std::max(0, out.rollCount - bars * 2) * 0.12f;
    out.rollQualityScore = clamp01(out.averageRollQuality - rollCountPenalty);

    float overload = 0.0f;
    int airyTicks = 0;
    for (int tick = 0; tick < totalTicks; ++tick)
    {
        const int active = matrix.activeLaneCountAt(tick);
        overload += static_cast<float>(std::max(0, active - weights.activeLaneMax) * std::max(0, active - weights.activeLaneMax));
        if (active <= 2)
            ++airyTicks;
    }
    out.overloadPenalty = clamp01(overload / static_cast<float>(std::max(1, totalTicks)));
    out.negativeSpaceScore = clamp01(0.70f * (1.0f - out.overloadPenalty) + 0.30f * (static_cast<float>(airyTicks) / static_cast<float>(std::max(1, totalTicks))));

    out.d01 = normalizedBarDistance(matrix, 0, 1);
    out.d02 = normalizedBarDistance(matrix, 0, 2);
    out.d03 = normalizedBarDistance(matrix, 0, 3);
    if (bars >= 4)
    {
        out.barVariationScore = (rangeScore(out.d01, 0.05f, 0.15f, 0.10f)
                              + rangeScore(out.d02, 0.10f, 0.25f, 0.14f)
                              + rangeScore(out.d03, 0.18f, 0.40f, 0.20f)) / 3.0f;
    }
    else
    {
        out.barVariationScore = 0.65f;
    }

    const auto allNotes = matrix.allNotes();
    int tastefulMicro = 0;
    for (const auto& note : allNotes)
    {
        const bool laneAllowsTiming = note.laneIndex == TrapAlgebraLanes::HiHat
            || note.laneIndex == TrapAlgebraLanes::HatAccent
            || note.laneIndex == TrapAlgebraLanes::ClapGhost
            || note.laneIndex == TrapAlgebraLanes::Perc;
        const int limit = laneAllowsTiming ? 18 : 10;
        if (std::abs(note.microTimingTicks) <= limit)
            ++tastefulMicro;
    }
    out.grooveMicrotimingScore = allNotes.empty() ? 0.0f : static_cast<float>(tastefulMicro) / static_cast<float>(allNotes.size());

    const int openHats = matrix.countLane(TrapAlgebraLanes::OpenHat);
    const int cymbals = matrix.countLane(TrapAlgebraLanes::Cymbal);
    const int perc = matrix.countLane(TrapAlgebraLanes::Perc);
    const int hatCount = static_cast<int>(allHats.size());
    out.spamPenalty = clamp01(std::max(0, hatCount - totalTicks / 2) * 0.04f
                            + std::max(0, openHats - bars) * 0.12f
                            + std::max(0, cymbals - 2) * 0.18f
                            + std::max(0, perc - bars * 2) * 0.08f);

    const float syncopationScore = lowEndPocketScore(matrix);
    const float bassWeightScore = rangeScore(out.sub808Density, 0.18f, 0.65f, 0.18f);
    const float coreScore = clamp01((1.22f * out.snareBackboneScore
                                  + 1.24f * out.kick808CouplingScore
                                  + 1.00f * out.hiHatMovementScore
                                  + 0.86f * syncopationScore
                                  + 0.96f * bassWeightScore
                                  + 0.90f * out.negativeSpaceScore
                                  + 0.82f * out.barVariationScore
                                  + 0.55f * out.grooveMicrotimingScore) / 7.55f);

    const float rollDensity = bars > 0 ? static_cast<float>(out.rollCount) / static_cast<float>(bars) : 0.0f;
    const float percDensity = bars > 0 ? static_cast<float>(perc) / static_cast<float>(bars) : 0.0f;
    const float styleScore = clamp01((closenessScore(hatDensity, std::max(0.05f, weights.hatRate * 0.55f), 0.18f)
                                   + closenessScore(rollDensity, weights.rollRate * 0.75f, 0.75f)
                                   + closenessScore(out.sub808Density, std::clamp(0.22f + weights.bassLegato * 0.18f, 0.18f, 0.65f), 0.22f)
                                   + closenessScore(percDensity, weights.cowbell * 1.25f, 1.60f)
                                   + closenessScore(1.0f - out.negativeSpaceScore, weights.kickIrregularity * 0.38f, 0.42f)
                                   + closenessScore(out.overloadPenalty, (1.0f - weights.drumDryness) * 0.05f, 0.10f)) / 6.0f);

    const float weightedPenalty = weights.overload * (out.overloadPenalty + out.spamPenalty)
        + weights.mud * out.mudPenalty;
    out.quality = clamp01(0.68f * coreScore + 0.32f * styleScore - weightedPenalty * 0.18f);
    out.energy = TrapEnergyModel().energy(out, weights);
    return out;
}

juce::StringArray TrapConstraintValidator::validate(const TrapPatternMatrix& matrix,
                                                    const TrapAlgebraParams& params,
                                                    const TrapQualityBreakdown& score) const
{
    juce::StringArray issues;
    const int bars = std::max(1, matrix.getBars());

    for (int bar = 0; bar < bars; ++bar)
        if (!matrix.hasNote(TrapAlgebraLanes::Snare, bar * kTicksPerBar + 32))
            issues.add("missing_snare_backbone");

    if (matrix.countLane(TrapAlgebraLanes::Sub808) == 0)
        issues.add("missing_808_anchor");
    if (score.sub808Density > 0.65f)
        issues.add("constant_808_wall");
    if (score.sub808Density < 0.18f)
        issues.add("empty_808");
    if (score.kick808CouplingRatio < 0.45f)
        issues.add("low_kick_808_coupling");
    if (score.kick808CouplingScore < 0.60f)
        issues.add("low_trap_core_low_end");
    if (score.hiHatMovementScore < 0.60f)
        issues.add("weak_hat_driver");
    if (score.negativeSpaceScore < 0.35f)
        issues.add("lost_negative_space");
    for (const auto& note : matrix.allNotes())
    {
        if (std::abs(note.microTimingTicks) >= 60)
        {
            issues.add("whole_tick_microtiming");
            break;
        }
    }
    if (score.hatVelocityVariance < TrapAlgebraEngine::weightsForSubstyle(params.substyle).hatVarianceMin)
        issues.add("flat_hats");
    if (score.d01 < 0.001f && score.d02 < 0.001f && score.d03 < 0.001f && bars >= 4)
        issues.add("all_bars_identical");
    if (score.spamPenalty > 0.2f || score.overloadPenalty > 0.2f)
        issues.add("spam_or_overload");

    return issues;
}
} // namespace bbg
