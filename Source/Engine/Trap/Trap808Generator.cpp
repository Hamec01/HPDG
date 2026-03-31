#include "Trap808Generator.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <vector>

#include "../../Core/PatternProject.h"
#include "TrapLowEndRoles.h"

namespace bbg
{
namespace
{
enum class TrapBassEventType
{
    Reject = 0,
    StartShort,
    StartMedium,
    StartLong,
    StartPhraseHold,
    RestartShort,
    RestartMedium,
    ContinueSustain
};

enum class PitchClass
{
    Root = 0,
    Octave,
    Fifth,
    Color,
    Count
};

struct TrapBassBudgets
{
    int minStartsPerTwoBars = 3;
    int maxStartsPerTwoBars = 6;
    int maxStartsPerBar = 3;
    int maxRestartsPerBar = 3;
    int maxPitchChangesPerTwoBars = 3;
    int maxSlidesPerTwoBars = 2;

    float anchorStartProbability = 0.78f;
    float supportStartProbability = 0.30f;
    float pickupStartProbability = 0.44f;
    float edgeStartProbability = 0.84f;
    float ghostStartProbability = 0.03f;

    float rootTarget = 0.66f;
    float octaveTarget = 0.16f;
    float fifthTarget = 0.14f;
    float colorTarget = 0.04f;

    float baseSlideGate = 0.16f;
};

struct TrapBassAnchorContext
{
    TrapKickRole kickRole = TrapKickRole::Support;

    bool isStartOfBar = false;
    bool isMidBarStrongPoint = false;
    bool isEndOfBar = false;
    bool isEndOf2BarPhrase = false;
    bool isEndOf4BarPhrase = false;
    bool isPreTransition = false;

    float localHiHatDensity = 0.0f;
    bool hasMajorHatFx = false;
    bool hasOpenHatAccent = false;
    bool hasStrongSnareAccent = false;
    float localKickDensity = 0.0f;

    bool hasOngoingSustain = false;
    bool hasOngoingPhraseHold = false;

    int ticksSincePreviousBassStart = 999999;
    bool previousBassStartWasRestart = false;

    bool goodSlideEntry = false;
    bool goodSlideTarget = false;

    bool stylePrefersThisPosition = false;
};

struct TrapBassCandidate
{
    int step = 0;
    TrapKickRole role = TrapKickRole::Support;
    float staticScore = 0.0f;
    bool selected = false;
};

float subBalanceWeight(const StyleInfluenceState& styleInfluence)
{
    return std::clamp(laneBiasFor(styleInfluence, TrackType::Sub808).balanceWeight, 0.65f, 1.6f);
}

float lowEndCouplingWeight(const StyleInfluenceState& styleInfluence)
{
    return std::clamp(styleInfluence.lowEndCouplingWeight, 0.65f, 1.6f);
}

float bounceWeight(const StyleInfluenceState& styleInfluence)
{
    return std::clamp(styleInfluence.bounceWeight, 0.65f, 1.5f);
}

struct ReferenceTrapKickFeel
{
    bool available = false;
    float density = 0.0f;
    float anchorRatio = 0.0f;
    float bounceRatio = 0.0f;
    float tailRatio = 0.0f;
    std::array<float, 16> presence {};
};

ReferenceTrapKickFeel buildReferenceTrapKickFeel(const StyleInfluenceState& styleInfluence, int bar)
{
    ReferenceTrapKickFeel feel;
    if (!styleInfluence.referenceKickCorpus.available || styleInfluence.referenceKickCorpus.variants.empty())
        return feel;

    int contributingBars = 0;
    float totalNotes = 0.0f;
    float anchors = 0.0f;
    float bounces = 0.0f;
    float tails = 0.0f;

    for (const auto& variant : styleInfluence.referenceKickCorpus.variants)
    {
        if (!variant.available || variant.barPatterns.empty())
            continue;

        const int sourceBars = std::max(1, variant.sourceBars > 0 ? variant.sourceBars : static_cast<int>(variant.barPatterns.size()));
        const int normalizedBar = ((bar % sourceBars) + sourceBars) % sourceBars;
        if (normalizedBar < 0 || normalizedBar >= static_cast<int>(variant.barPatterns.size()))
            continue;

        const auto& barPattern = variant.barPatterns[static_cast<size_t>(normalizedBar)];
        ++contributingBars;
        totalNotes += static_cast<float>(barPattern.notes.size());
        for (const auto& note : barPattern.notes)
        {
            const int step = std::clamp(note.step16, 0, 15);
            feel.presence[static_cast<size_t>(step)] += 1.0f;
            if (step == 0 || step == 8 || step == 10)
                anchors += 1.0f;
            if (step == 6 || step == 11 || step == 12 || step == 13)
                bounces += 1.0f;
            if (step >= 14)
                tails += 1.0f;
        }
    }

    if (contributingBars <= 0)
        return feel;

    feel.available = true;
    const float invBars = 1.0f / static_cast<float>(contributingBars);
    for (auto& value : feel.presence)
        value *= invBars;
    feel.density = std::clamp((totalNotes * invBars) / 4.0f, 0.0f, 1.0f);
    feel.anchorRatio = totalNotes > 0.0f ? anchors / totalNotes : 0.0f;
    feel.bounceRatio = totalNotes > 0.0f ? bounces / totalNotes : 0.0f;
    feel.tailRatio = totalNotes > 0.0f ? tails / totalNotes : 0.0f;
    return feel;
}

const std::array<int, 7>& intervalsForScale(int scaleMode)
{
    static const std::array<int, 7> minor { 0, 2, 3, 5, 7, 8, 10 };
    static const std::array<int, 7> major { 0, 2, 4, 5, 7, 9, 11 };
    static const std::array<int, 7> harmonicMinor { 0, 2, 3, 5, 7, 8, 11 };
    if (scaleMode == 1)
        return major;
    if (scaleMode == 2)
        return harmonicMinor;
    return minor;
}

TrapBassBudgets getTrapBassBudgets(TrapSubstyle substyle)
{
    switch (substyle)
    {
        case TrapSubstyle::ATLClassic: return { 3, 6, 3, 3, 3, 2, 0.82f, 0.32f, 0.48f, 0.86f, 0.03f, 0.66f, 0.16f, 0.14f, 0.04f, 0.20f };
        case TrapSubstyle::DarkTrap: return { 2, 5, 2, 2, 2, 1, 0.84f, 0.22f, 0.34f, 0.92f, 0.02f, 0.74f, 0.17f, 0.08f, 0.01f, 0.11f };
        case TrapSubstyle::CloudTrap: return { 2, 5, 2, 2, 3, 2, 0.78f, 0.28f, 0.42f, 0.84f, 0.02f, 0.69f, 0.18f, 0.10f, 0.03f, 0.16f };
        case TrapSubstyle::RageTrap: return { 4, 7, 4, 4, 4, 3, 0.82f, 0.42f, 0.58f, 0.80f, 0.04f, 0.57f, 0.20f, 0.18f, 0.05f, 0.27f };
        case TrapSubstyle::MemphisTrap: return { 4, 7, 4, 3, 3, 2, 0.80f, 0.36f, 0.52f, 0.84f, 0.03f, 0.64f, 0.16f, 0.16f, 0.04f, 0.21f };
        case TrapSubstyle::LuxuryTrap: return { 3, 6, 3, 2, 2, 1, 0.78f, 0.24f, 0.36f, 0.82f, 0.02f, 0.72f, 0.16f, 0.10f, 0.02f, 0.12f };
    }

    return {};
}

float substyleThreshold(TrapSubstyle substyle)
{
    switch (substyle)
    {
        case TrapSubstyle::ATLClassic: return 0.80f;
        case TrapSubstyle::DarkTrap: return 0.88f;
        case TrapSubstyle::CloudTrap: return 0.84f;
        case TrapSubstyle::RageTrap: return 0.72f;
        case TrapSubstyle::MemphisTrap: return 0.80f;
        case TrapSubstyle::LuxuryTrap: return 0.90f;
    }

    return 0.82f;
}

bool isStrongBeat(int stepInBar)
{
    return stepInBar == 0 || stepInBar == 8 || stepInBar == 12;
}

bool isEdgeStep(int stepInBar)
{
    return stepInBar >= 14;
}

int countLocalKickDensity(const std::vector<NoteEvent>& kicks, int step)
{
    int count = 0;
    for (const auto& k : kicks)
        if (std::abs(k.step - step) <= 1)
            ++count;
    return count;
}

int countEventsNear(const TrackState* track, int step, int radius)
{
    if (track == nullptr)
        return 0;

    int count = 0;
    for (const auto& n : track->notes)
        if (std::abs(n.step - step) <= radius)
            ++count;
    return count;
}

float densityNear(const TrackState* track, int step, int radius, float normalize)
{
    const float count = static_cast<float>(countEventsNear(track, step, radius));
    return std::clamp(count / std::max(1.0f, normalize), 0.0f, 1.0f);
}

bool hasAccentNear(const TrackState* track, int step, int radius, int minVelocity)
{
    if (track == nullptr)
        return false;

    for (const auto& n : track->notes)
        if (std::abs(n.step - step) <= radius && n.velocity >= minVelocity)
            return true;
    return false;
}

bool hasLongFxNear(const TrackState* track, int step, int radius)
{
    if (track == nullptr)
        return false;

    int count = 0;
    for (const auto& n : track->notes)
    {
        if (std::abs(n.step - step) <= radius)
        {
            if (n.length >= 2)
                return true;
            ++count;
        }
    }

    return count >= 2;
}

TrapKickRole classifyKickRole(const std::vector<NoteEvent>& kicks,
                              size_t index,
                              TrapPhraseRole phraseRole)
{
    const auto& k = kicks[index];
    const int prevGap = index > 0 ? (k.step - kicks[index - 1].step) : 99;
    const int nextGap = index + 1 < kicks.size() ? (kicks[index + 1].step - k.step) : 99;

    auto role = classifyKickRoleFromNote(k, phraseRole);
    if ((nextGap > 0 && nextGap <= 2) || (prevGap > 0 && prevGap <= 2))
        role = TrapKickRole::Pickup;
    return role;
}

TrapKickRole classifySubRole(int step, TrapPhraseRole phraseRole)
{
    const int stepInBar = ((step % 16) + 16) % 16;
    if (isEdgeStep(stepInBar) || phraseRole == TrapPhraseRole::Ending)
        return TrapKickRole::Tail;
    if (isStrongBeat(stepInBar))
        return TrapKickRole::Anchor;
    if (stepInBar <= 2 || stepInBar == 7 || stepInBar == 11)
        return TrapKickRole::Pickup;
    return TrapKickRole::Support;
}

void sortByTime(std::vector<NoteEvent>& notes)
{
    std::sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b)
    {
        if (a.step != b.step)
            return a.step < b.step;
        return a.velocity > b.velocity;
    });
}

void cleanRhythmMonophonic(std::vector<NoteEvent>& notes)
{
    if (notes.empty())
        return;

    sortByTime(notes);

    std::vector<NoteEvent> filtered;
    filtered.reserve(notes.size());
    for (const auto& n : notes)
    {
        if (!filtered.empty() && filtered.back().step == n.step)
        {
            if (n.velocity > filtered.back().velocity)
                filtered.back() = n;
            continue;
        }
        filtered.push_back(n);
    }

    for (size_t i = 1; i < filtered.size(); ++i)
    {
        auto& prev = filtered[i - 1];
        const auto& cur = filtered[i];
        const int stepGap = std::max(1, cur.step - prev.step);
        prev.length = std::min(prev.length, stepGap);
    }

    notes = std::move(filtered);
}

bool hasStartNear(const std::vector<NoteEvent>& notes, int step, int maxDistance)
{
    for (const auto& n : notes)
        if (std::abs(n.step - step) <= maxDistance)
            return true;
    return false;
}

int twoBarWindowIndex(int step)
{
    return std::max(0, step / 32);
}

bool isNoteActiveAtStep(const std::vector<NoteEvent>& notes, int step)
{
    for (const auto& n : notes)
        if (step >= n.step && step < (n.step + n.length))
            return true;
    return false;
}

bool isPhraseHoldActiveAtStep(const std::vector<NoteEvent>& notes, int step)
{
    for (const auto& n : notes)
        if (n.length >= 6 && step >= n.step && step < (n.step + n.length))
            return true;
    return false;
}

bool stylePrefersPosition(TrapSubstyle substyle, const TrapBassAnchorContext& ctx)
{
    switch (substyle)
    {
        case TrapSubstyle::ATLClassic:
            return ctx.kickRole == TrapKickRole::Anchor || ctx.isEndOfBar || ctx.isPreTransition;
        case TrapSubstyle::DarkTrap:
            return ctx.isEndOfBar || ctx.isEndOf2BarPhrase || (ctx.localHiHatDensity < 0.32f && ctx.localKickDensity < 0.4f);
        case TrapSubstyle::CloudTrap:
            return ctx.isStartOfBar || ctx.isMidBarStrongPoint || (ctx.goodSlideEntry || ctx.goodSlideTarget);
        case TrapSubstyle::RageTrap:
            return ctx.kickRole == TrapKickRole::Anchor || ctx.kickRole == TrapKickRole::Pickup || ctx.isPreTransition;
        case TrapSubstyle::MemphisTrap:
            return ctx.kickRole == TrapKickRole::Tail || ctx.isEndOfBar || ctx.isEndOf2BarPhrase;
        case TrapSubstyle::LuxuryTrap:
            return ctx.kickRole == TrapKickRole::Anchor || ctx.isEndOf2BarPhrase || ctx.isEndOf4BarPhrase;
    }

    return false;
}

float scoreTrapBassAnchor(const TrapStyleSpec& spec,
                          const TrapStyleProfile& style,
                          const TrapBassBudgets& budgets,
                          const TrapBassAnchorContext& ctx)
{
    (void) budgets;
    float score = 0.0f;

    switch (ctx.kickRole)
    {
        case TrapKickRole::Anchor: score += 1.00f * std::clamp(spec.followKickProbability, 0.0f, 1.2f); break;
        case TrapKickRole::Tail: score += 0.82f * std::clamp(spec.phraseEdgeAnchorProbability, 0.0f, 1.2f); break;
        case TrapKickRole::Pickup: score += 0.46f * std::clamp(0.45f + spec.counterGapProbability, 0.0f, 1.2f); break;
        case TrapKickRole::Support: score += 0.34f * std::clamp(0.35f + spec.followKickProbability * 0.5f, 0.0f, 1.2f); break;
        case TrapKickRole::GhostLike: score += 0.08f; break;
    }

    if (ctx.isStartOfBar) score += 0.22f;
    if (ctx.isMidBarStrongPoint) score += 0.10f;
    if (ctx.isEndOfBar) score += 0.28f;
    if (ctx.isEndOf2BarPhrase) score += 0.40f;
    if (ctx.isEndOf4BarPhrase) score += 0.50f;
    if (ctx.isPreTransition) score += 0.32f;

    const float crowd = std::max(ctx.localHiHatDensity, ctx.localKickDensity);
    if (crowd < 0.25f) score += 0.24f;
    else if (crowd < 0.45f) score += 0.12f;
    else if (crowd > 0.85f) score -= 0.32f;
    else if (crowd > 0.65f) score -= 0.16f;

    float topPenalty = 0.0f;
    if (ctx.localHiHatDensity > 0.75f) topPenalty += 0.18f;
    if (ctx.hasMajorHatFx) topPenalty += 0.30f;
    if (ctx.hasOpenHatAccent) topPenalty += 0.22f;
    if (ctx.hasStrongSnareAccent) topPenalty += 0.16f;

    float denseTopSensitivity = 1.0f;
    if (style.substyle == TrapSubstyle::DarkTrap || style.substyle == TrapSubstyle::LuxuryTrap)
        denseTopSensitivity = 1.18f;
    if (style.substyle == TrapSubstyle::RageTrap)
        denseTopSensitivity = 0.82f;
    score -= topPenalty * denseTopSensitivity;

    if (ctx.localKickDensity > 0.75f)
        score -= 0.20f;

    if (ctx.hasOngoingPhraseHold) score -= 0.44f;
    else if (ctx.hasOngoingSustain) score -= 0.22f;

    if (ctx.ticksSincePreviousBassStart < 120) score -= 0.50f;
    else if (ctx.ticksSincePreviousBassStart < 240) score -= 0.28f;

    float restartPenalty = ctx.previousBassStartWasRestart ? 0.18f : 0.0f;
    if (style.substyle == TrapSubstyle::RageTrap)
        restartPenalty *= 0.55f;
    if (style.substyle == TrapSubstyle::DarkTrap)
        restartPenalty += 0.08f;
    score -= restartPenalty;

    if (ctx.stylePrefersThisPosition)
        score += 0.14f;

    if (ctx.goodSlideEntry && spec.slideMode != TrapSlideMode::Sparse) score += 0.12f;
    if (ctx.goodSlideTarget && spec.slideMode != TrapSlideMode::Sparse) score += 0.10f;

    switch (style.substyle)
    {
        case TrapSubstyle::ATLClassic:
            if (ctx.kickRole == TrapKickRole::Anchor) score += 0.08f;
            if (ctx.isEndOfBar) score += 0.06f;
            if (ctx.isPreTransition) score += 0.04f;
            break;
        case TrapSubstyle::DarkTrap:
            if (ctx.isEndOfBar) score += 0.08f;
            if (ctx.isEndOf2BarPhrase) score += 0.12f;
            if (crowd < 0.2f) score += 0.10f;
            break;
        case TrapSubstyle::CloudTrap:
            if (ctx.isStartOfBar) score += 0.04f;
            if (ctx.isMidBarStrongPoint) score += 0.04f;
            if (ctx.goodSlideEntry || ctx.goodSlideTarget) score += 0.06f;
            break;
        case TrapSubstyle::RageTrap:
            if (ctx.kickRole == TrapKickRole::Anchor) score += 0.10f;
            if (ctx.isPreTransition) score += 0.10f;
            if (ctx.kickRole == TrapKickRole::Pickup) score += 0.05f;
            break;
        case TrapSubstyle::MemphisTrap:
            if (ctx.kickRole == TrapKickRole::Tail) score += 0.10f;
            if (ctx.isEndOfBar || ctx.isEndOf2BarPhrase) score += 0.10f;
            if (ctx.isPreTransition) score += 0.08f;
            break;
        case TrapSubstyle::LuxuryTrap:
            if (ctx.kickRole == TrapKickRole::Anchor) score += 0.06f;
            if (ctx.isEndOfBar || ctx.isEndOf2BarPhrase || ctx.isEndOf4BarPhrase) score += 0.10f;
            if (crowd < 0.2f) score += 0.06f;
            break;
    }

    // Keep score from exploding while preserving ordering.
    return std::clamp(score, -2.0f, 3.0f);
}

TrapBassEventType decideTrapBassEventType(const TrapStyleSpec& spec,
                                          const TrapStyleProfile& style,
                                          const TrapBassAnchorContext& ctx,
                                          float score,
                                          float threshold)
{
    const float weakThreshold = std::max(0.62f, threshold - 0.20f);
    if (score < weakThreshold)
        return TrapBassEventType::Reject;

    if (ctx.hasOngoingPhraseHold && score < 1.15f)
        return TrapBassEventType::ContinueSustain;

    if (ctx.hasOngoingSustain && score < 0.95f)
        return TrapBassEventType::ContinueSustain;

    if ((ctx.isEndOf2BarPhrase || ctx.isEndOf4BarPhrase) && score >= 1.10f && spec.sustainProbability > 0.20f)
        return TrapBassEventType::StartPhraseHold;

    const float restartPreference =
        style.substyle == TrapSubstyle::RageTrap ? 0.62f :
        (style.substyle == TrapSubstyle::MemphisTrap ? 0.44f :
        (style.substyle == TrapSubstyle::ATLClassic ? 0.36f : 0.22f));

    if (score >= 1.00f)
    {
        if (restartPreference > 0.35f && !ctx.hasOngoingPhraseHold)
            return TrapBassEventType::RestartMedium;

        if (spec.sustainProbability > 0.45f)
            return TrapBassEventType::StartLong;

        return TrapBassEventType::StartMedium;
    }

    if (score >= std::max(0.82f, threshold - 0.06f))
        return TrapBassEventType::StartMedium;

    return TrapBassEventType::StartShort;
}

int lengthForEventType(TrapBassEventType type,
                       int step,
                       const TrapStyleSpec& spec,
                       const TrapStyleProfile& style,
                       std::mt19937& rng)
{
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    const int stepInBar = ((step % 16) + 16) % 16;

    if (type == TrapBassEventType::StartShort || type == TrapBassEventType::RestartShort)
        return chance(rng) < 0.35f ? 2 : 1;

    if (type == TrapBassEventType::StartMedium || type == TrapBassEventType::RestartMedium)
    {
        if (style.substyle == TrapSubstyle::RageTrap)
            return chance(rng) < 0.65f ? 2 : 3;
        return chance(rng) < 0.52f ? 3 : 2;
    }

    if (type == TrapBassEventType::StartLong)
    {
        if (style.substyle == TrapSubstyle::DarkTrap)
            return chance(rng) < 0.58f ? 5 : 4;
        return chance(rng) < 0.5f ? 4 : 5;
    }

    if (type == TrapBassEventType::StartPhraseHold)
    {
        const int toBarEnd = std::max(3, 16 - stepInBar);
        const int bonus = (spec.preferSparseSpace || style.substyle == TrapSubstyle::DarkTrap) ? 2 : 1;
        return std::clamp(toBarEnd + bonus, 4, 8);
    }

    return 1;
}

int chooseWeightedPitchClass(const std::array<float, static_cast<size_t>(PitchClass::Count)>& weights,
                             std::mt19937& rng)
{
    float sum = 0.0f;
    for (float w : weights)
        sum += std::max(0.0f, w);
    if (sum <= 0.0001f)
        return static_cast<int>(PitchClass::Root);

    std::uniform_real_distribution<float> pick(0.0f, sum);
    const float needle = pick(rng);
    float accum = 0.0f;
    for (size_t i = 0; i < weights.size(); ++i)
    {
        accum += std::max(0.0f, weights[i]);
        if (needle <= accum)
            return static_cast<int>(i);
    }

    return static_cast<int>(PitchClass::Root);
}

bool scaleContains(const std::array<int, 7>& scale, int semitone)
{
    for (int v : scale)
        if (v == semitone)
            return true;
    return false;
}

void addCandidate(std::vector<TrapBassCandidate>& candidates, int step, TrapKickRole role)
{
    for (auto& c : candidates)
    {
        if (c.step != step)
            continue;

        auto rank = [](TrapKickRole r)
        {
            switch (r)
            {
                case TrapKickRole::Anchor: return 5;
                case TrapKickRole::Tail: return 4;
                case TrapKickRole::Pickup: return 3;
                case TrapKickRole::Support: return 2;
                case TrapKickRole::GhostLike: return 1;
            }
            return 0;
        };

        if (rank(role) > rank(c.role))
            c.role = role;
        return;
    }

    candidates.push_back({ step, role, 0.0f, false });
}
} // namespace

void Trap808Generator::generateRhythm(TrackState& subTrack,
                                      const TrackState& kickTrack,
                                      const TrackState* hiHatTrack,
                                      const TrackState* hatFxTrack,
                                      const TrackState* openHatTrack,
                                      const TrackState* snareTrack,
                                      const GeneratorParams& params,
                                      const TrapStyleProfile& style,
                                      const TrapStyleSpec& spec,
                                      const StyleInfluenceState& styleInfluence,
                                      float subActivity,
                                      const std::vector<TrapPhraseRole>& phrase,
                                      std::mt19937& rng) const
{
    subTrack.notes.clear();
    const int bars = std::max(1, params.bars);
    const int totalSteps = bars * 16;

    std::vector<NoteEvent> kicks;
    kicks.reserve(kickTrack.notes.size());
    for (const auto& k : kickTrack.notes)
        if (k.step >= 0 && k.step < totalSteps)
            kicks.push_back(k);
    sortByTime(kicks);

    const auto budgets = getTrapBassBudgets(style.substyle);
    const float threshold = substyleThreshold(style.substyle);
    std::uniform_int_distribution<int> vel(style.sub808VelocityMin, style.sub808VelocityMax);

    const float activity = std::clamp(subActivity * style.sub808Activity * subBalanceWeight(styleInfluence) * lowEndCouplingWeight(styleInfluence), 0.1f, 1.0f);
    const float bounce = bounceWeight(styleInfluence);

    const int twoBarWindows = std::max(1, (totalSteps + 31) / 32);
    std::vector<int> startsPerWindow(static_cast<size_t>(twoBarWindows), 0);
    std::vector<int> startsPerBar(static_cast<size_t>(bars), 0);
    std::vector<int> restartsPerBar(static_cast<size_t>(bars), 0);
    std::vector<ReferenceTrapKickFeel> referenceFeels(static_cast<size_t>(bars));
    for (int bar = 0; bar < bars; ++bar)
        referenceFeels[static_cast<size_t>(bar)] = buildReferenceTrapKickFeel(styleInfluence, bar);

    int maxStartsPerBar = budgets.maxStartsPerBar;
    if (activity < 0.4f)
        maxStartsPerBar = std::max(1, maxStartsPerBar - 1);
    else if (activity > 0.95f)
        maxStartsPerBar = std::min(maxStartsPerBar + 1, budgets.maxStartsPerBar + 1);

    std::vector<TrapBassCandidate> candidates;
    candidates.reserve(static_cast<size_t>(bars * 8 + kicks.size()));

    for (size_t i = 0; i < kicks.size(); ++i)
    {
        const auto& k = kicks[i];
        const int bar = std::clamp(k.step / 16, 0, bars - 1);
        const auto phraseRole = bar < static_cast<int>(phrase.size()) ? phrase[static_cast<size_t>(bar)] : TrapPhraseRole::Base;
        const auto role = classifyKickRole(kicks, i, phraseRole);
        addCandidate(candidates, k.step, role);
    }

    // Phrase-edge candidates and silence-recovery anchors.
    for (int bar = 0; bar < bars; ++bar)
    {
        addCandidate(candidates, bar * 16, TrapKickRole::Support);
        addCandidate(candidates, bar * 16 + 15, TrapKickRole::Tail);
    }

    for (size_t i = 1; i < kicks.size(); ++i)
    {
        const int gap = kicks[i].step - kicks[i - 1].step;
        if (gap >= 6)
            addCandidate(candidates, kicks[i - 1].step + gap / 2, TrapKickRole::Pickup);
    }

    std::sort(candidates.begin(), candidates.end(), [](const TrapBassCandidate& a, const TrapBassCandidate& b)
    {
        if (a.step != b.step)
            return a.step < b.step;
        return static_cast<int>(a.role) < static_cast<int>(b.role);
    });

    int lastStartStep = std::numeric_limits<int>::min();
    bool lastStartWasRestart = false;

    for (auto& candidate : candidates)
    {
        if (candidate.step < 0 || candidate.step >= totalSteps)
            continue;

        const int bar = std::clamp(candidate.step / 16, 0, bars - 1);
        const int stepInBar = candidate.step % 16;
        const int window = twoBarWindowIndex(candidate.step);
        if (window < 0 || window >= twoBarWindows)
            continue;
        const auto& referenceFeel = referenceFeels[static_cast<size_t>(bar)];
        const float referencePresence = referenceFeel.presence[static_cast<size_t>(stepInBar)];

        TrapBassAnchorContext ctx;
        ctx.kickRole = candidate.role;

        ctx.isStartOfBar = stepInBar == 0;
        ctx.isMidBarStrongPoint = (stepInBar == 8 || stepInBar == 12);
        ctx.isEndOfBar = stepInBar >= 14;
        ctx.isEndOf2BarPhrase = (bar % 2 == 1) && ctx.isEndOfBar;
        ctx.isEndOf4BarPhrase = (bar % 4 == 3) && ctx.isEndOfBar;
        ctx.isPreTransition = stepInBar == 15 || stepInBar == 14;

        ctx.localHiHatDensity = densityNear(hiHatTrack, candidate.step, 1, 6.0f);
        ctx.hasMajorHatFx = hasLongFxNear(hatFxTrack, candidate.step, 1);
        ctx.hasOpenHatAccent = hasAccentNear(openHatTrack, candidate.step, 0, 70);
        ctx.hasStrongSnareAccent = hasAccentNear(snareTrack, candidate.step, 1, std::max(108, style.snareVelocityMin + 8));
        ctx.localKickDensity = std::clamp(static_cast<float>(countLocalKickDensity(kicks, candidate.step)) / 3.0f, 0.0f, 1.0f);

        ctx.hasOngoingSustain = isNoteActiveAtStep(subTrack.notes, candidate.step);
        ctx.hasOngoingPhraseHold = isPhraseHoldActiveAtStep(subTrack.notes, candidate.step);

        if (lastStartStep <= std::numeric_limits<int>::min() / 2)
            ctx.ticksSincePreviousBassStart = 999999;
        else
            ctx.ticksSincePreviousBassStart = std::max(0, (candidate.step - lastStartStep) * 120);

        ctx.previousBassStartWasRestart = lastStartWasRestart;

        ctx.goodSlideEntry = (candidate.role == TrapKickRole::Anchor || candidate.role == TrapKickRole::Tail)
                             && (stepInBar >= 13 || stepInBar == 0 || stepInBar == 8);
        ctx.goodSlideTarget = (candidate.role == TrapKickRole::Anchor || candidate.role == TrapKickRole::Tail)
                              && !ctx.hasMajorHatFx;

        ctx.stylePrefersThisPosition = stylePrefersPosition(style.substyle, ctx);

        candidate.staticScore = scoreTrapBassAnchor(spec, style, budgets, ctx);
        if (referenceFeel.available)
        {
            float referenceBias = referencePresence * (ctx.kickRole == TrapKickRole::Anchor ? 0.38f : 0.3f);
            referenceBias += referenceFeel.anchorRatio * (ctx.isStartOfBar || ctx.isMidBarStrongPoint ? 0.16f : 0.04f);
            referenceBias += referenceFeel.bounceRatio * (ctx.kickRole == TrapKickRole::Pickup || ctx.kickRole == TrapKickRole::Support ? 0.2f : 0.06f);
            referenceBias += referenceFeel.tailRatio * (ctx.isEndOfBar ? 0.18f : 0.0f);
            referenceBias += (lowEndCouplingWeight(styleInfluence) - 1.0f) * 0.12f;
            referenceBias += (bounce - 1.0f) * (ctx.kickRole == TrapKickRole::Pickup ? 0.14f : 0.06f);

            if (referenceFeel.density < 0.3f && ctx.kickRole == TrapKickRole::Support)
                referenceBias -= 0.12f;

            candidate.staticScore = std::clamp(candidate.staticScore + referenceBias, -2.0f, 3.2f);
        }
        const TrapBassEventType eventType = decideTrapBassEventType(spec, style, ctx, candidate.staticScore, threshold);

        if (eventType == TrapBassEventType::Reject || eventType == TrapBassEventType::ContinueSustain)
            continue;

        const bool isRestart = eventType == TrapBassEventType::RestartShort || eventType == TrapBassEventType::RestartMedium;
        const bool isStart = eventType != TrapBassEventType::Reject && eventType != TrapBassEventType::ContinueSustain;

        if (!isStart)
            continue;

        if (startsPerBar[static_cast<size_t>(bar)] >= maxStartsPerBar)
            continue;
        if (startsPerWindow[static_cast<size_t>(window)] >= budgets.maxStartsPerTwoBars)
            continue;
        if (isRestart && restartsPerBar[static_cast<size_t>(bar)] >= budgets.maxRestartsPerBar)
            continue;

        if (hasStartNear(subTrack.notes, candidate.step, 1) && !isRestart)
            continue;

        if (ctx.hasOngoingSustain && !isRestart)
            continue;

        if (isRestart)
        {
            for (auto& existing : subTrack.notes)
            {
                if (candidate.step > existing.step && candidate.step < (existing.step + existing.length))
                    existing.length = std::max(1, candidate.step - existing.step);
            }
        }

        int length = lengthForEventType(eventType, candidate.step, spec, style, rng);
        if (referenceFeel.available)
        {
            if (referencePresence >= 0.42f && (ctx.kickRole == TrapKickRole::Anchor || ctx.kickRole == TrapKickRole::Tail))
                length = std::max(length, ctx.isEndOfBar ? 4 : 3);
            if (referenceFeel.tailRatio > 0.16f && ctx.isEndOfBar)
                length = std::max(length, 4);
            if (referenceFeel.bounceRatio > 0.2f && isRestart)
                length = std::min(length, 2);
        }
        int velocityBonus = 0;
        if (candidate.role == TrapKickRole::Anchor) velocityBonus += 6;
        if (candidate.role == TrapKickRole::GhostLike) velocityBonus -= 18;
        if (isRestart) velocityBonus += 3;
        const int velocity = std::clamp(vel(rng) + velocityBonus, style.sub808VelocityMin, style.sub808VelocityMax);
        subTrack.notes.push_back({ 36, candidate.step, length, velocity, 0, false });

        startsPerBar[static_cast<size_t>(bar)] += 1;
        startsPerWindow[static_cast<size_t>(window)] += 1;
        if (isRestart)
            restartsPerBar[static_cast<size_t>(bar)] += 1;

        lastStartStep = candidate.step;
        lastStartWasRestart = isRestart;
        candidate.selected = true;
    }

    // Guarantee minimum starts by promoting strongest unselected candidates per window.
    for (int window = 0; window < twoBarWindows; ++window)
    {
        while (startsPerWindow[static_cast<size_t>(window)] < budgets.minStartsPerTwoBars)
        {
            int bestIndex = -1;
            float bestScore = -1000.0f;
            for (size_t i = 0; i < candidates.size(); ++i)
            {
                const auto& c = candidates[i];
                if (c.selected)
                    continue;
                if (twoBarWindowIndex(c.step) != window)
                    continue;
                if (hasStartNear(subTrack.notes, c.step, 1))
                    continue;

                const int bar = std::clamp(c.step / 16, 0, bars - 1);
                if (startsPerBar[static_cast<size_t>(bar)] >= maxStartsPerBar)
                    continue;

                if (c.staticScore > bestScore)
                {
                    bestScore = c.staticScore;
                    bestIndex = static_cast<int>(i);
                }
            }

            if (bestIndex < 0)
                break;

            auto& chosen = candidates[static_cast<size_t>(bestIndex)];
            const int bar = std::clamp(chosen.step / 16, 0, bars - 1);
            const int length = std::clamp(lengthForEventType(TrapBassEventType::StartMedium, chosen.step, spec, style, rng), 2, 4);
            const int velocity = std::clamp(vel(rng) + (chosen.role == TrapKickRole::Anchor ? 5 : 0), style.sub808VelocityMin, style.sub808VelocityMax);
            subTrack.notes.push_back({ 36, chosen.step, length, velocity, 0, false });
            startsPerBar[static_cast<size_t>(bar)] += 1;
            startsPerWindow[static_cast<size_t>(window)] += 1;
            chosen.selected = true;
        }
    }

    cleanRhythmMonophonic(subTrack.notes);
}

void Trap808Generator::assignPitches(TrackState& subTrack,
                                     const GeneratorParams& params,
                                     const TrapStyleProfile& style,
                                     const TrapStyleSpec& spec,
                                     const std::vector<TrapPhraseRole>& phrase,
                                     std::mt19937& rng) const
{
    (void) spec;
    if (subTrack.notes.empty())
        return;

    sortByTime(subTrack.notes);
    const auto budgets = getTrapBassBudgets(style.substyle);

    const auto& scale = intervalsForScale(params.scaleMode);
    const int root = std::clamp(params.keyRoot, 0, 11);
    const int baseRootPitch = std::clamp(36 + root, 24, 60);

    const bool allowFlat7 = scaleContains(scale, 10);
    const bool allowMinor3 = scaleContains(scale, 3);

    const int twoBarWindows = std::max(1, ((std::max(1, params.bars) * 16) + 31) / 32);
    std::vector<std::array<int, static_cast<size_t>(PitchClass::Count)>> classCounts(static_cast<size_t>(twoBarWindows));
    std::vector<int> windowTotals(static_cast<size_t>(twoBarWindows), 0);
    std::vector<int> pitchChanges(static_cast<size_t>(twoBarWindows), 0);

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    int previousPitch = -1;

    for (auto& note : subTrack.notes)
    {
        const int bar = std::max(0, note.step / 16);
        const auto phraseRole = bar < static_cast<int>(phrase.size()) ? phrase[static_cast<size_t>(bar)] : TrapPhraseRole::Base;
        const auto role = classifySubRole(note.step, phraseRole);
        const int window = std::clamp(twoBarWindowIndex(note.step), 0, twoBarWindows - 1);

        std::array<float, static_cast<size_t>(PitchClass::Count)> weights {
            budgets.rootTarget,
            budgets.octaveTarget,
            budgets.fifthTarget,
            budgets.colorTarget
        };

        if (!(role == TrapKickRole::Tail || role == TrapKickRole::Pickup || style.substyle == TrapSubstyle::RageTrap))
            weights[static_cast<size_t>(PitchClass::Color)] = 0.0f;
        if (!allowFlat7 && !allowMinor3)
            weights[static_cast<size_t>(PitchClass::Color)] = 0.0f;

        const int observed = windowTotals[static_cast<size_t>(window)] + 1;
        const float expectedRoot = budgets.rootTarget * observed;
        const float expectedOct = budgets.octaveTarget * observed;
        const float expectedFifth = budgets.fifthTarget * observed;
        const float expectedColor = budgets.colorTarget * observed;

        const auto& counts = classCounts[static_cast<size_t>(window)];
        weights[static_cast<size_t>(PitchClass::Root)] *= 1.0f + std::max(0.0f, expectedRoot - static_cast<float>(counts[static_cast<size_t>(PitchClass::Root)]));
        weights[static_cast<size_t>(PitchClass::Octave)] *= 1.0f + std::max(0.0f, expectedOct - static_cast<float>(counts[static_cast<size_t>(PitchClass::Octave)]));
        weights[static_cast<size_t>(PitchClass::Fifth)] *= 1.0f + std::max(0.0f, expectedFifth - static_cast<float>(counts[static_cast<size_t>(PitchClass::Fifth)]));
        weights[static_cast<size_t>(PitchClass::Color)] *= 1.0f + std::max(0.0f, expectedColor - static_cast<float>(counts[static_cast<size_t>(PitchClass::Color)]));

        if (role == TrapKickRole::Anchor)
            weights[static_cast<size_t>(PitchClass::Root)] *= 1.28f;
        if (role == TrapKickRole::Support)
            weights[static_cast<size_t>(PitchClass::Color)] *= 0.5f;

        const int pickedClass = chooseWeightedPitchClass(weights, rng);

        const int rootPitch = baseRootPitch;
        const int octavePitch = std::clamp(baseRootPitch + 12, 24, 60);
        const int fifthPitch = std::clamp(baseRootPitch + 7, 24, 60);
        int colorPitch = fifthPitch;
        if (allowFlat7 && allowMinor3)
        {
            const bool preferMinorThird = style.substyle == TrapSubstyle::DarkTrap || style.substyle == TrapSubstyle::MemphisTrap;
            colorPitch = std::clamp(baseRootPitch + (preferMinorThird ? 3 : (chance(rng) < 0.5f ? 3 : 10)), 24, 60);
        }
        else if (allowMinor3)
        {
            colorPitch = std::clamp(baseRootPitch + 3, 24, 60);
        }
        else if (allowFlat7)
        {
            colorPitch = std::clamp(baseRootPitch + 10, 24, 60);
        }

        int chosenPitch = rootPitch;
        if (pickedClass == static_cast<int>(PitchClass::Octave))
            chosenPitch = octavePitch;
        else if (pickedClass == static_cast<int>(PitchClass::Fifth))
            chosenPitch = fifthPitch;
        else if (pickedClass == static_cast<int>(PitchClass::Color))
            chosenPitch = colorPitch;

        if (previousPitch > 0)
        {
            const bool changed = chosenPitch != previousPitch;
            if (changed && pitchChanges[static_cast<size_t>(window)] >= budgets.maxPitchChangesPerTwoBars)
            {
                chosenPitch = previousPitch;
            }
            else if (changed && (role == TrapKickRole::Anchor || role == TrapKickRole::Support) && chance(rng) < 0.58f)
            {
                chosenPitch = previousPitch;
            }
        }

        if (previousPitch > 0 && chosenPitch != previousPitch)
            pitchChanges[static_cast<size_t>(window)] += 1;

        note.pitch = std::clamp(chosenPitch, 24, 60);
        classCounts[static_cast<size_t>(window)][static_cast<size_t>(pickedClass)] += 1;
        windowTotals[static_cast<size_t>(window)] += 1;
        previousPitch = note.pitch;
    }
}

void Trap808Generator::applySlides(TrackState& subTrack,
                                   const TrapStyleProfile& style,
                                   const TrapStyleSpec& spec,
                                   const std::vector<TrapPhraseRole>& phrase,
                                   std::mt19937& rng) const
{
    if (subTrack.notes.size() < 2)
        return;

    sortByTime(subTrack.notes);
    const auto budgets = getTrapBassBudgets(style.substyle);
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);

    float slideProbability = budgets.baseSlideGate;
    if (spec.slideMode == TrapSlideMode::Moderate)
        slideProbability *= 1.12f;
    else if (spec.slideMode == TrapSlideMode::Aggressive)
        slideProbability *= 1.35f;

    const int totalSteps = std::max(1, (subTrack.notes.back().step / 16) + 1) * 16;
    const int twoBarWindows = std::max(1, (totalSteps + 31) / 32);
    std::vector<int> slidesPerWindow(static_cast<size_t>(twoBarWindows), 0);

    for (size_t i = 0; i + 1 < subTrack.notes.size(); ++i)
    {
        auto& current = subTrack.notes[i];
        auto& next = subTrack.notes[i + 1];
        const int gap = next.step - current.step;
        if (gap <= 0 || gap > 3)
            continue;
        if (current.pitch == next.pitch)
            continue;

        const int window = std::clamp(twoBarWindowIndex(current.step), 0, twoBarWindows - 1);
        if (slidesPerWindow[static_cast<size_t>(window)] >= budgets.maxSlidesPerTwoBars)
            continue;

        const int bar = std::max(0, current.step / 16);
        const int nextBar = std::max(0, next.step / 16);
        const int stepInBar = current.step % 16;
        const auto phraseRole = bar < static_cast<int>(phrase.size()) ? phrase[static_cast<size_t>(bar)] : TrapPhraseRole::Base;
        const auto nextPhraseRole = nextBar < static_cast<int>(phrase.size()) ? phrase[static_cast<size_t>(nextBar)] : TrapPhraseRole::Base;
        const auto currentRole = classifySubRole(current.step, phraseRole);
        const auto nextRole = classifySubRole(next.step, nextPhraseRole);

        const bool meaningfulPhraseMove = (nextRole == TrapKickRole::Anchor || nextRole == TrapKickRole::Tail || phraseRole == TrapPhraseRole::Ending);
        if (!meaningfulPhraseMove)
            continue;
        if (current.length < 2 && gap < 2)
            continue;

        int localDensity = 0;
        for (const auto& n : subTrack.notes)
            if (std::abs(n.step - current.step) <= 2)
                ++localDensity;
        if (localDensity > 3)
            continue;

        float gate = slideProbability;
        if (phraseRole == TrapPhraseRole::Ending || stepInBar >= 13 || nextRole == TrapKickRole::Anchor)
            gate += 0.1f;
        if (style.substyle == TrapSubstyle::RageTrap)
            gate += 0.08f;
        if (spec.preferSparseSpace && localDensity >= 2)
            gate *= 0.8f;
        if (currentRole == TrapKickRole::Support && nextRole == TrapKickRole::Support)
            gate *= 0.72f;
        if (current.length >= 4 && (phraseRole == TrapPhraseRole::Ending || stepInBar >= 12))
            gate *= 1.08f;
        if (current.length <= 2 && gap == 1)
            gate *= 0.9f;

        if (chance(rng) < std::clamp(gate, 0.02f, 0.86f))
        {
            current.length = std::max(current.length, gap + 1);
            slidesPerWindow[static_cast<size_t>(window)] += 1;
        }
    }
}

void Trap808Generator::generate(TrackState& subTrack,
                                const TrackState& kickTrack,
                                const TrackState* hiHatTrack,
                                const TrackState* hatFxTrack,
                                const TrackState* openHatTrack,
                                const TrackState* snareTrack,
                                const GeneratorParams& params,
                                const TrapStyleProfile& style,
                                const TrapStyleSpec& spec,
                                const StyleInfluenceState& styleInfluence,
                                float subActivity,
                                const std::vector<TrapPhraseRole>& phrase,
                                std::mt19937& rng) const
{
    generateRhythm(subTrack,
                   kickTrack,
                   hiHatTrack,
                   hatFxTrack,
                   openHatTrack,
                   snareTrack,
                   params,
                   style,
                   spec,
                   styleInfluence,
                   subActivity,
                   phrase,
                   rng);
    assignPitches(subTrack, params, style, spec, phrase, rng);
    applySlides(subTrack, style, spec, phrase, rng);
}
} // namespace bbg
