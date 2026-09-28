#include "BoomBapClassicAlgebraGenerator.h"
#include "../GenerationModel/CandidateSelectionEngine.h"

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

GenerationLaneFamily featureFamilyForBoomBapLane(int lane)
{
    if (lane == BoomBapClassicLanes::Kick || lane == BoomBapClassicLanes::KickGhost) return GenerationLaneFamily::Kick;
    if (lane == BoomBapClassicLanes::Snare || lane == BoomBapClassicLanes::ClapGhost) return GenerationLaneFamily::Snare;
    if (lane == BoomBapClassicLanes::HiHat || lane == BoomBapClassicLanes::HatAccent || lane == BoomBapClassicLanes::OpenHat) return GenerationLaneFamily::Hat;
    if (lane == BoomBapClassicLanes::Sub808) return GenerationLaneFamily::Bass;
    return GenerationLaneFamily::Ornament;
}

MusicalRole featureRoleForBoomBap(BoomBapClassicRole role)
{
    switch (role)
    {
        case BoomBapClassicRole::Anchor: return MusicalRole::Anchor;
        case BoomBapClassicRole::Backbeat: return MusicalRole::Backbeat;
        case BoomBapClassicRole::StrongPulse:
        case BoomBapClassicRole::WeakPulse: return MusicalRole::Pulse;
        case BoomBapClassicRole::Pickup:
        case BoomBapClassicRole::PickupToKick: return MusicalRole::Pickup;
        case BoomBapClassicRole::Response:
        case BoomBapClassicRole::ResponseFromKick: return MusicalRole::Response;
        case BoomBapClassicRole::Ghost:
        case BoomBapClassicRole::GhostBeforeSnare:
        case BoomBapClassicRole::GhostAfterSnare: return MusicalRole::Ghost;
        case BoomBapClassicRole::Accent: return MusicalRole::Accent;
        case BoomBapClassicRole::Fill:
        case BoomBapClassicRole::FillKick:
        case BoomBapClassicRole::FillSupport: return MusicalRole::Fill;
        case BoomBapClassicRole::Turnaround:
        case BoomBapClassicRole::Ending: return MusicalRole::Transition;
        case BoomBapClassicRole::Ornament: return MusicalRole::Ornament;
        default: return MusicalRole::Unknown;
    }
}

PatternFeatureVector extractBoomBapFeatures(const BoomBapClassicAlgebraPattern& pattern, int bars)
{
    PatternFeatureInput input;
    input.bars = bars;
    input.ticksPerBar = kTicksPerBar;
    for (const auto& note : pattern.allNotes())
        input.events.push_back({ featureFamilyForBoomBapLane(note.laneIndex), featureRoleForBoomBap(note.role), note.tick64,
                                 note.velocity, note.microTimingTicks });
    return PatternFeatureExtractor::extract(input);
}

StyleTargetProfile boomBapStyleTarget(BoomBapSubstyle substyle)
{
    PatternFeatureVector target { 0.50f, 0.42f, 0.52f, 0.34f, 0.48f, 0.72f, 0.56f, 0.92f };
    switch (substyle)
    {
        case BoomBapSubstyle::Dusty:
            target = { 0.44f, 0.48f, 0.60f, 0.48f, 0.56f, 0.64f, 0.52f, 0.90f };
            break;
        case BoomBapSubstyle::Jazzy:
            target = { 0.52f, 0.58f, 0.66f, 0.52f, 0.44f, 0.54f, 0.68f, 0.90f };
            break;
        case BoomBapSubstyle::Aggressive:
            target = { 0.64f, 0.52f, 0.58f, 0.38f, 0.34f, 0.62f, 0.72f, 0.94f };
            break;
        case BoomBapSubstyle::LaidBack:
            target = { 0.42f, 0.44f, 0.55f, 0.50f, 0.58f, 0.72f, 0.52f, 0.92f };
            break;
        case BoomBapSubstyle::BoomBapGold:
            target = { 0.50f, 0.45f, 0.54f, 0.36f, 0.48f, 0.70f, 0.62f, 0.94f };
            break;
        case BoomBapSubstyle::RussianUnderground:
            target = { 0.45f, 0.56f, 0.62f, 0.50f, 0.56f, 0.58f, 0.64f, 0.90f };
            break;
        case BoomBapSubstyle::LofiRap:
            target = { 0.38f, 0.40f, 0.50f, 0.46f, 0.64f, 0.78f, 0.46f, 0.90f };
            break;
        case BoomBapSubstyle::Classic:
        default:
            break;
    }

    const PatternFeatureVector tolerance { 0.20f, 0.22f, 0.24f, 0.24f, 0.20f, 0.22f, 0.24f, 0.12f };
    const PatternFeatureVector weight { 1.20f, 1.05f, 0.70f, 0.85f, 1.15f, 1.00f, 1.10f, 0.80f };
    return { target, tolerance, weight };
}

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

struct BoomBapAlgebraProfile
{
    const char* name = "Classic";
    BoomBapSubstyle substyle = BoomBapSubstyle::Classic;
    float swingMin = 0.56f;
    float swingMax = 0.60f;
    float hatEighthDropout = 0.02f;
    float hatSixteenthRate = 0.12f;
    float hatAccentRate = 0.18f;
    float ghostRate = 0.16f;
    float kickClusterRate = 0.22f;
    float kickPreSnareBias = 0.12f;
    float openHatRate = 0.08f;
    float rideRate = 0.02f;
    float percRate = 0.18f;
    float sub808Rate = 0.0f;
    float bar4Lift = 0.22f;
    float rawness = 0.30f;
    float softness = 0.20f;
    int snareLateMsMin = 4;
    int snareLateMsMax = 12;
    int hatLateMsMin = 3;
    int hatLateMsMax = 14;
    int kickMsMin = -5;
    int kickMsMax = 4;
    int ghostMsMin = -10;
    int ghostMsMax = 12;
    int mainKickMin = 1;
    int mainKickMax = 3;
};

const BoomBapAlgebraProfile& algebraProfile(BoomBapSubstyle substyle)
{
    static const std::array<BoomBapAlgebraProfile, 6> profiles {{
        { "Classic", BoomBapSubstyle::Classic, 0.56f, 0.61f, 0.0f, 0.12f, 0.18f, 0.04f, 0.18f, 0.10f, 0.06f, 0.01f, 0.14f, 0.0f, 0.18f, 0.18f, 0.12f, 4, 12, 3, 12, -4, 4, -8, 10, 1, 3 },
        { "Dusty", BoomBapSubstyle::Dusty, 0.58f, 0.64f, 0.08f, 0.10f, 0.16f, 0.12f, 0.18f, 0.16f, 0.05f, 0.02f, 0.20f, 0.02f, 0.24f, 0.74f, 0.24f, 8, 18, 5, 18, -5, 5, -12, 14, 1, 3 },
        { "Jazzy", BoomBapSubstyle::Jazzy, 0.60f, 0.66f, 0.06f, 0.08f, 0.12f, 0.18f, 0.14f, 0.10f, 0.10f, 0.18f, 0.30f, 0.01f, 0.28f, 0.45f, 0.28f, 5, 15, 4, 16, -4, 5, -14, 18, 1, 3 },
        { "BoomBapGold", BoomBapSubstyle::BoomBapGold, 0.57f, 0.61f, 0.02f, 0.11f, 0.14f, 0.16f, 0.16f, 0.12f, 0.06f, 0.01f, 0.12f, 0.0f, 0.18f, 0.16f, 0.10f, 4, 11, 3, 11, -4, 4, -8, 10, 1, 3 },
        { "RussianUnderground", BoomBapSubstyle::RussianUnderground, 0.59f, 0.65f, 0.16f, 0.07f, 0.11f, 0.16f, 0.26f, 0.22f, 0.025f, 0.0f, 0.10f, 0.0f, 0.34f, 0.86f, 0.18f, 8, 18, 5, 16, -6, 5, -12, 16, 1, 3 },
        { "LofiRap", BoomBapSubstyle::LofiRap, 0.56f, 0.60f, 0.12f, 0.045f, 0.08f, 0.07f, 0.10f, 0.08f, 0.02f, 0.0f, 0.08f, 0.0f, 0.12f, 0.62f, 0.78f, 4, 13, 3, 14, -4, 4, -10, 12, 1, 3 }
    }};

    for (const auto& profile : profiles)
        if (profile.substyle == substyle)
            return profile;
    return profiles.front();
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

float profiledSwing(float requestedSwing, const BoomBapAlgebraProfile& profile)
{
    if (requestedSwing <= 0.0f)
        return profile.swingMin;
    return std::clamp(requestedSwing, profile.swingMin, profile.swingMax);
}

int microMsToPpq(float ms, float bpm)
{
    const float safeBpm = std::clamp(bpm, 40.0f, 220.0f);
    return static_cast<int>(std::lround(ms * 960.0f * safeBpm / 60000.0f));
}

BoomBapTiming::TimingBreakdown makeTiming(std::mt19937& rng,
                                         const BoomBapClassicAlgebraParams& params,
                                         const BoomBapStyleProfile& style,
                                         int lane,
                                         BoomBapClassicRole role,
                                         int tick64)
{
    BoomBapTiming::TimingBreakdown out;
    const int localTick = ((tick64 % kTicksPerBar) + kTicksPerBar) % kTicksPerBar;
    const bool weakSixteenth = localTick % 8 == 4;
    float swingMultiplier = 0.0f;
    if (weakSixteenth)
    {
        if (lane == BoomBapClassicLanes::HiHat || lane == BoomBapClassicLanes::HatAccent) swingMultiplier = 0.90f;
        else if (lane == BoomBapClassicLanes::Ride) swingMultiplier = 1.0f;
        else if (lane == BoomBapClassicLanes::OpenHat) swingMultiplier = 0.90f;
        else if (lane == BoomBapClassicLanes::ClapGhost) swingMultiplier = 0.65f;
        else if (lane == BoomBapClassicLanes::Perc) swingMultiplier = 0.70f;
        else if (lane == BoomBapClassicLanes::Kick && role != BoomBapClassicRole::Anchor) swingMultiplier = 0.22f;
    }
    out.structuralSwingPPQ = static_cast<int>(std::lround(BoomBapTiming::getSixteenthSwingOffsetPPQ(params.swing) * swingMultiplier));

    BoomBapTiming::TimingDistribution stylePocket = style.pocket.percussion;
    if (lane == BoomBapClassicLanes::Snare)
    {
        stylePocket = localTick == 16 ? style.pocket.snareBeat2 : style.pocket.snareBeat4;
        stylePocket.meanPPQ = static_cast<float>(localTick == 16 ? style.snareLateBeat2Ticks : style.snareLateBeat4Ticks);
        stylePocket.minPPQ = std::max(0.0f, stylePocket.meanPPQ - 7.0f);
        stylePocket.maxPPQ = stylePocket.meanPPQ + 7.0f;
    }
    else if (lane == BoomBapClassicLanes::Kick)
        stylePocket = role == BoomBapClassicRole::Anchor ? style.pocket.kickAnchor : style.pocket.kickSyncopated;
    else if (lane == BoomBapClassicLanes::KickGhost)
        stylePocket = style.pocket.kickPickup;
    else if (lane == BoomBapClassicLanes::ClapGhost)
        stylePocket = localTick < 16 || (localTick > 16 && localTick < 48)
            ? style.pocket.ghostBeforeSnare : style.pocket.ghostAfterSnare;
    else if (lane == BoomBapClassicLanes::HiHat || lane == BoomBapClassicLanes::HatAccent)
        stylePocket = weakSixteenth ? style.pocket.hatWeak : style.pocket.hatStrong;
    else if (lane == BoomBapClassicLanes::OpenHat)
        stylePocket = style.pocket.openHat;
    else if (lane == BoomBapClassicLanes::Ride)
        stylePocket = style.pocket.ride;

    auto pocketDistribution = stylePocket;
    pocketDistribution.sigmaPPQ *= std::clamp(params.humanize, 0.0f, 1.0f);
    out.pocketPPQ = BoomBapTiming::sampleTruncatedGaussianPPQ(rng, pocketDistribution);
    BoomBapTiming::TimingDistribution jitter { 0.0f,
        style.pocket.humanJitterSigmaPPQ * params.humanize,
        -style.pocket.humanJitterLimitPPQ,
        style.pocket.humanJitterLimitPPQ };
    out.humanJitterPPQ = BoomBapTiming::sampleTruncatedGaussianPPQ(rng, jitter);
    return out;
}

int randomMicroMs(std::mt19937& rng, float bpm, int minMs, int maxMs, float humanize)
{
    const float ticksPerMs = 960.0f * std::clamp(bpm, 40.0f, 220.0f) / 60000.0f;
    BoomBapTiming::TimingDistribution d {
        0.5f * (minMs + maxMs) * ticksPerMs,
        std::max(0.5f, (maxMs - minMs) * ticksPerMs / 6.0f) * humanize,
        minMs * ticksPerMs,
        maxMs * ticksPerMs
    };
    return BoomBapTiming::sampleTruncatedGaussianPPQ(rng, d);
}

int swungHatMicro(std::mt19937& rng, const BoomBapClassicAlgebraParams& params, const BoomBapAlgebraProfile&, bool offbeat)
{
    const auto timing = makeTiming(rng, params, getBoomBapProfile(params.substyle), BoomBapClassicLanes::HiHat,
                                   offbeat ? BoomBapClassicRole::WeakPulse : BoomBapClassicRole::StrongPulse,
                                   offbeat ? 4 : 0);
    return timing.total();
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
    note.timing.pocketPPQ = microTimingTicks;
    note.role = role;
    note.roleString = BoomBapClassicAlgebraGenerator::roleToString(role);
    pattern.notesByLane[static_cast<size_t>(lane)].push_back(note);
}

void addTimedNote(BoomBapClassicAlgebraPattern& pattern,
                  const BoomBapClassicAlgebraParams& params,
                  const BoomBapStyleProfile& style,
                  std::mt19937& rng,
                  int lane, int bar, int tickInBar, int velocity,
                  BoomBapClassicRole role, int length = 1)
{
    const auto timing = makeTiming(rng, params, style, lane, role, tickInBar);
    addNote(pattern, lane, bar, tickInBar, velocity, timing.total(), role, length);
    auto& note = pattern.notesByLane[static_cast<size_t>(lane)].back();
    note.timing = timing;
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
        case BoomBapClassicRole::Backbeat: return "backbeat";
        case BoomBapClassicRole::Syncopated: return "syncopated";
        case BoomBapClassicRole::Pickup: return "pickup";
        case BoomBapClassicRole::Response: return "response";
        case BoomBapClassicRole::Support: return "support";
        case BoomBapClassicRole::Ghost: return "ghost";
        case BoomBapClassicRole::GhostBeforeSnare: return "ghost_before_snare";
        case BoomBapClassicRole::GhostAfterSnare: return "ghost_after_snare";
        case BoomBapClassicRole::ClapLayer: return "clap_layer";
        case BoomBapClassicRole::FillSupport: return "fill_support";
        case BoomBapClassicRole::PickupToKick: return "pickup_to_kick";
        case BoomBapClassicRole::ResponseFromKick: return "response_from_kick";
        case BoomBapClassicRole::FillKick: return "fill_kick";
        case BoomBapClassicRole::Accent: return "accent";
        case BoomBapClassicRole::WeakPulse: return "weak_pulse";
        case BoomBapClassicRole::StrongPulse: return "strong_pulse";
        case BoomBapClassicRole::Turnaround: return "turnaround";
        case BoomBapClassicRole::Ornament: return "ornament";
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
    params.candidateCount = std::clamp(params.candidateCount, 24, 64);

    const auto& style = getBoomBapProfile(params.substyle);
    const auto styleTarget = StyleTargetModel::withPerformanceIntent(boomBapStyleTarget(params.substyle),
                                                                     params.density, params.swing,
                                                                     params.humanize, params.variation);
    auto context = makeBoomBapGenerationContext(params.seed, params.bars, style);
    if (params.forcedArchetype) context.archetype=*params.forcedArchetype;
    if (params.forcedRareEvent) context.requestedEvent=*params.forcedRareEvent;
    BoomBapClassicPatternScorer scorer;
    std::vector<BoomBapClassicAlgebraPattern> candidates;
    candidates.reserve(static_cast<size_t>(params.candidateCount));

    for (int candidate = 0; candidate < params.candidateCount; ++candidate)
    {
        auto pattern = generateCandidate(params, context, candidate);
        validateAndRepair(pattern, params);
        pattern.score = scorer.score(pattern, params, style);
        pattern.features = extractBoomBapFeatures(pattern, params.bars);
        pattern.styleMatch = StyleTargetModel::evaluate(pattern.features, styleTarget);
        pattern.selectionQuality = pattern.score.quality + 0.16f * (pattern.styleMatch.fit - 0.5f);
        pattern.selectedCandidateIndex=candidate;
        candidates.push_back(std::move(pattern));
    }
    std::vector<CandidateSelectionEntry> selectionEntries;
    selectionEntries.reserve(candidates.size());
    for (const auto& candidate : candidates)
        selectionEntries.push_back({ candidate.selectionQuality,
                                     0.72f * candidate.score.novelty + 0.28f * candidate.features.surfaceNovelty(),
                                     !candidate.score.hardTrapLeak });
    const auto selection = CandidateSelectionEngine::select(selectionEntries,
        { style.qualityFloor, style.nearBestTolerance, style.selectionTemperature, style.noveltyWeight,
          static_cast<std::uint32_t>(context.selectionSeed) });
    auto best=std::move(candidates[static_cast<size_t>(selection.selectedIndex)]);
    best.nearBestPoolSize=selection.nearBestPoolSize;
    best.bestCandidateQuality=selection.bestQuality;
    best.selectedCandidateQuality=selection.selectedQuality;
    best.debugSummary = buildDebugSummary(best, params);
    return best;
}

BoomBapClassicAlgebraPattern BoomBapClassicAlgebraGenerator::generateCandidate(const BoomBapClassicAlgebraParams& params,
                                                                               const BoomBapGenerationContext& context,
                                                                               int candidateIndex) const
{
    BoomBapClassicAlgebraPattern pattern;
    pattern.context=context;
    std::mt19937 rng(static_cast<std::mt19937::result_type>(params.seed * 1103515245u + candidateIndex * 2654435761u));

    generateBar(pattern, params, candidateIndex, 0, rng, context);

    BoomBapClassicPatternScorer scorer;
    auto oneBar = pattern;
    oneBar.notesByLane[BoomBapClassicLanes::Sub808].clear();
    auto oneBarParams = params;
    oneBarParams.bars = 1;
    const bool cloneGoodBar = scorer.score(oneBar, oneBarParams, getBoomBapProfile(params.substyle)).quality > 2.6f && chance(rng, 0.82f);
    if (params.bars > 1)
    {
        if (cloneGoodBar)
            cloneBarWithSmallMutation(pattern, params, rng, context);
        else
            generateBar(pattern, params, candidateIndex, 1, rng, context);
    }

    if (params.bars > 2)
        deriveBarFromStatement(pattern, params, rng, 2, context);
    if (params.bars > 3)
        deriveBarFromStatement(pattern, params, rng, 3, context);
    for (int bar = 4; bar < params.bars; ++bar)
        deriveBarFromStatement(pattern, params, rng, bar, context);

    applyRarePhraseEvent(pattern, params, rng);

    // Product decision: Ghost Snare/Clap and Perc are retired lanes. Keep their
    // stable numeric slots for project compatibility, but never emit events.
    pattern.notesByLane[BoomBapClassicLanes::ClapGhost].clear();
    pattern.notesByLane[BoomBapClassicLanes::Perc].clear();
    if (pattern.realizedEvent == RarePhraseEvent::SnareFill
        || pattern.realizedEvent == RarePhraseEvent::GhostPickup
        || pattern.realizedEvent == RarePhraseEvent::PercEnding)
    {
        pattern.realizedEvent = RarePhraseEvent::None;
        pattern.fillType = BoomBapFillType::None;
    }

    for (auto& lane : pattern.notesByLane)
        dedupeLane(lane);
    return pattern;
}

void BoomBapClassicAlgebraGenerator::generateBar(BoomBapClassicAlgebraPattern& pattern,
                                                 const BoomBapClassicAlgebraParams& params,
                                                 int candidateIndex,
                                                 int barIndex,
                                                 std::mt19937& rng,
                                                 const BoomBapGenerationContext& context,
                                                 const std::vector<int>* kickSkeletonToAnswer) const
{
    const bool ending = (barIndex % 4) == 3;
    const auto& profile = algebraProfile(params.substyle);
    const auto& style = getBoomBapProfile(params.substyle);
    const std::array<float,5> densityScale{{1.0f,1.08f,.72f,.98f,1.02f}};
    const float den = std::clamp(params.density*densityScale[static_cast<size_t>(context.archetype)],0.0f,1.0f);
    const float hum = params.humanize;
    const float var = params.variation;

    for (const int snareTick : { 16, 48 })
    {
        const int mainSnareVelocity = randomInt(rng, 100, 120);
        addTimedNote(pattern, params, style, rng, BoomBapClassicLanes::Snare, barIndex, snareTick,
                     mainSnareVelocity, BoomBapClassicRole::Backbeat);
        auto& mainSnare=pattern.notesByLane[BoomBapClassicLanes::Snare].back(); mainSnare.priority=100;

        const float archetypeGhost = context.archetype==BoomBapGrooveArchetype::BreakHeavy?1.35f
            : context.archetype==BoomBapGrooveArchetype::Sparse?.45f:1.0f;
        const float roleBoost = (barIndex % 4 == 1 || ending) ? 0.32f : 0.0f;
        const float ghostProbability = std::clamp(profile.ghostRate * archetypeGhost
            * (0.75f + den * 0.50f + var * 0.30f + roleBoost), 0.0f, 0.28f);
        if (chance(rng, ghostProbability))
        {
            static constexpr std::array<int,6> offsets{{-8,-4,-2,2,4,8}};
            static constexpr std::array<float,6> weights{{.08f,.30f,.12f,.12f,.30f,.08f}};
            float draw=random01(rng),acc=0;int offset=-4;
            for(size_t i=0;i<offsets.size();++i){acc+=weights[i];if(draw<=acc){offset=offsets[i];break;}}
            const float ratio=std::uniform_real_distribution<float>(style.clapGhostVelocityMinRatio,style.clapGhostVelocityMaxRatio)(rng);
            const int velocity=std::clamp(static_cast<int>(std::lround(mainSnareVelocity*ratio)),1,mainSnareVelocity-1);
            const auto role=offset<0?BoomBapClassicRole::GhostBeforeSnare:BoomBapClassicRole::GhostAfterSnare;
            addTimedNote(pattern,params,style,rng,BoomBapClassicLanes::ClapGhost,barIndex,snareTick+offset,velocity,role);
            auto& ghost=pattern.notesByLane[BoomBapClassicLanes::ClapGhost].back();
            ghost.anchorLane=BoomBapClassicLanes::Snare; ghost.anchorTick64=barStart(barIndex)+snareTick; ghost.priority=50;
        }
    }

    std::vector<int> selectedKickTicks;
    selectedKickTicks.reserve(4);
    // Select a relational motif first; per-position probabilities only articulate that motif.
    static const std::array<std::array<int,4>,5> kickMotifs{{
        {{0,24,40,-1}}, {{0,12,32,56}}, {{0,28,-1,-1}}, {{0,8,40,56}}, {{0,24,52,60}}
    }};
    const auto& motif=kickMotifs[(static_cast<size_t>(context.archetype)+static_cast<size_t>(candidateIndex))%kickMotifs.size()];
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
            + (0.7f + profile.kickPreSnareBias) * (preSnare ? 1.0f : 0.0f)
            + 0.6f * (postSnare ? 1.0f : 0.0f)
            + 0.5f * ((ending || answerTick) ? 1.0f : 0.0f)
            - 1.2f * (crowded ? 1.0f : 0.0f)
            - 0.9f * (collision ? 1.0f : 0.0f);
        float probability = sigmoid(score - 1.72f) * candidate.weight * (0.64f + den * 0.52f + var * 0.14f + profile.kickClusterRate * 0.18f);
        const bool motifMember=std::find(motif.begin(),motif.end(),candidate.tick)!=motif.end();
        probability=motifMember?std::max(probability,0.74f+den*.12f):probability*.28f;

        if (barIndex == 0 && candidate.tick == 0)
            probability = 0.98f;
        else if (candidate.tick == 0)
            probability = std::max(probability, 0.32f);
        if (ending && candidate.tick >= 52)
            probability += 0.18f;
        if (answerTick && barIndex % 4 == 2)
            probability += 0.16f;

        if (chance(rng, probability))
            selectedKickTicks.push_back(candidate.tick);
    }

    const int maxMainKicks = ending ? std::min(4, profile.mainKickMax + 1) : profile.mainKickMax;
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
    if (static_cast<int>(selectedKickTicks.size()) < profile.mainKickMin
        && std::find(selectedKickTicks.begin(), selectedKickTicks.end(), 32) == selectedKickTicks.end())
    {
        selectedKickTicks.push_back(32);
    }
    std::sort(selectedKickTicks.begin(), selectedKickTicks.end());

    for (const int tick : selectedKickTicks)
        addTimedNote(pattern, params, style, rng,
                BoomBapClassicLanes::Kick,
                barIndex,
                tick,
                randomInt(rng, 85, 118),
                (tick == 0 || tick == 32) ? BoomBapClassicRole::Anchor
                                          : (tick >= 40 ? BoomBapClassicRole::Response : BoomBapClassicRole::Syncopated));

    for (const int tick : { 4, 28, 36, 60 })
    {
        const bool tooClose = std::any_of(selectedKickTicks.begin(), selectedKickTicks.end(), [tick](int mainTick) { return std::abs(mainTick - tick) < 4; });
        if (!tooClose && chance(rng, 0.02f + profile.kickClusterRate * 0.10f + den * 0.05f + var * 0.05f + (ending ? 0.05f : 0.0f)))
        {
            const auto nearest=std::min_element(selectedKickTicks.begin(),selectedKickTicks.end(),[tick](int a,int b){return std::abs(a-tick)<std::abs(b-tick);});
            if(nearest==selectedKickTicks.end() || std::abs(*nearest-tick)>12) continue;
            const auto mainIt=std::find_if(pattern.notesByLane[BoomBapClassicLanes::Kick].begin(),pattern.notesByLane[BoomBapClassicLanes::Kick].end(),[&](const auto&n){return n.barIndex==barIndex&&normalizeTickInBar(n.tick64)==*nearest;});
            const int anchorVelocity=mainIt!=pattern.notesByLane[BoomBapClassicLanes::Kick].end()?mainIt->velocity:96;
            const float ratio=std::uniform_real_distribution<float>(style.ghostKickVelocityMinRatio,style.ghostKickVelocityMaxRatio)(rng);
            addTimedNote(pattern, params, style, rng,
                    BoomBapClassicLanes::KickGhost,
                    barIndex,
                    tick,
                    static_cast<int>(std::lround(anchorVelocity*ratio)), tick<*nearest?BoomBapClassicRole::PickupToKick:BoomBapClassicRole::ResponseFromKick);
            auto& ghost=pattern.notesByLane[BoomBapClassicLanes::KickGhost].back();ghost.anchorLane=BoomBapClassicLanes::Kick;ghost.anchorTick64=barStart(barIndex)+*nearest;ghost.priority=45;
        }
    }

    for (const int tick : { 0, 8, 16, 24, 32, 40, 48, 56 })
    {
        if (chance(rng, profile.hatEighthDropout * (0.35f + var) * (barIndex % 4 == 1 || ending ? 1.5f : 1.0f)))
            continue;

        const bool offbeat = (tick % 16) == 8;
        const int baseVelocity = (tick % 16) == 0 ? randomInt(rng, 72, 88) : randomInt(rng, 48, 68);
        addTimedNote(pattern, params, style, rng, BoomBapClassicLanes::HiHat, barIndex, tick, baseVelocity,
                     offbeat ? BoomBapClassicRole::WeakPulse : BoomBapClassicRole::StrongPulse);
    }

    for (const int tick : { 4, 12, 20, 28, 36, 44, 52, 60 })
    {
        const bool beforeSnare = tick == 12 || tick == 44;
        const float probability = profile.hatSixteenthRate + den * 0.10f + var * 0.04f + (beforeSnare ? 0.04f : 0.0f) + (ending && tick >= 52 ? profile.bar4Lift * 0.18f : 0.0f);
        if (chance(rng, probability))
        {
            const bool accent = chance(rng, profile.hatAccentRate + den * 0.08f + (beforeSnare ? 0.12f : 0.0f));
            addTimedNote(pattern, params, style, rng,
                    accent ? BoomBapClassicLanes::HatAccent : BoomBapClassicLanes::HiHat,
                    barIndex,
                    tick,
                    accent ? randomInt(rng, 85, 105) : randomInt(rng, 48, 68),
                    accent ? BoomBapClassicRole::Accent : BoomBapClassicRole::Support);
        }
    }

    if (chance(rng, profile.openHatRate + den * 0.025f + (ending ? profile.bar4Lift * 0.25f : 0.0f)))
    {
        const int tick = ending ? (chance(rng, 0.5f) ? 56 : 60) : (chance(rng, 0.5f) ? 44 : 52);
        addNote(pattern,
                BoomBapClassicLanes::OpenHat,
                barIndex,
                tick,
                randomInt(rng, 70, 105),
                randomMicroMs(rng, params.bpm, 0, profile.hatLateMsMax, hum),
                ending ? BoomBapClassicRole::Ending : BoomBapClassicRole::Accent,
                2);
    }

    if ((barIndex == 0 && candidateIndex % 5 == 0 && chance(rng, 0.35f)) || (ending && chance(rng, 0.35f + den * 0.20f)))
        addNote(pattern, BoomBapClassicLanes::Cymbal, barIndex, ending ? 60 : 0, randomInt(rng, 78, 112), randomMicroMs(rng, params.bpm, 0, 8, hum), ending ? BoomBapClassicRole::Ending : BoomBapClassicRole::Accent, 4);

    if (chance(rng, profile.percRate + den * 0.10f + (barIndex % 4 == 2 ? 0.06f : 0.0f)))
        addNote(pattern,
                BoomBapClassicLanes::Perc,
                barIndex,
                randomInt(rng, 0, 7) * 8 + (chance(rng, 0.45f) ? 4 : 0),
                randomInt(rng, 52, 94),
                randomMicroMs(rng, params.bpm, -10, 10, hum),
                BoomBapClassicRole::Support);

    if (chance(rng, profile.rideRate + den * 0.015f) && den > 0.72f)
        addNote(pattern, BoomBapClassicLanes::Ride, barIndex, randomInt(rng, 0, 7) * 8, randomInt(rng, 62, 92), randomMicroMs(rng, params.bpm, 0, 10, hum), BoomBapClassicRole::Support);

    if (profile.sub808Rate > 0.0f && chance(rng, profile.sub808Rate * den) && laneHasAt(pattern, BoomBapClassicLanes::Kick, barIndex, 0))
        addNote(pattern, BoomBapClassicLanes::Sub808, barIndex, 0, randomInt(rng, 58, 82), randomMicroMs(rng, params.bpm, -2, 4, hum), BoomBapClassicRole::Support, 4);
}

void BoomBapClassicAlgebraGenerator::cloneBarWithSmallMutation(BoomBapClassicAlgebraPattern& pattern,
                                                               const BoomBapClassicAlgebraParams& params,
                                                               std::mt19937& rng,
                                                               const BoomBapGenerationContext& context) const
{
    juce::ignoreUnused(context);
    const auto& profile = algebraProfile(params.substyle);

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
        addNote(pattern,
                BoomBapClassicLanes::ClapGhost,
                1,
                chance(rng, 0.5f) ? 44 : 52,
                randomInt(rng, 35, 70),
                randomMicroMs(rng, params.bpm, profile.ghostMsMin, profile.ghostMsMax, params.humanize),
                BoomBapClassicRole::Ghost);
    if (chance(rng, 0.18f + params.variation * 0.10f))
        addNote(pattern,
                BoomBapClassicLanes::HatAccent,
                1,
                chance(rng, 0.5f) ? 12 : 44,
                randomInt(rng, 85, 105),
                swungHatMicro(rng, params, profile, true),
                BoomBapClassicRole::Accent);
}

void BoomBapClassicAlgebraGenerator::deriveBarFromStatement(BoomBapClassicAlgebraPattern& pattern,
                                                             const BoomBapClassicAlgebraParams& params,
                                                             std::mt19937& rng,
                                                             int targetBar,
                                                             const BoomBapGenerationContext& context) const
{
    const bool development = targetBar % 4 == 2;
    const bool turnaround = targetBar % 4 == 3;
    const auto& style = getBoomBapProfile(params.substyle);
    for (int lane = 0; lane < BoomBapClassicLanes::Count; ++lane)
    {
        const auto source = pattern.notesByLane[static_cast<size_t>(lane)];
        for (const auto& original : source)
        {
            if (original.barIndex != 0 || lane == BoomBapClassicLanes::Sub808)
                continue;
            auto copy = original;
            const bool protectedBackbeat = lane == BoomBapClassicLanes::Snare
                && (normalizeTickInBar(copy.tick64) == 16 || normalizeTickInBar(copy.tick64) == 48);
            const float archetypeRemoval = context.archetype==BoomBapGrooveArchetype::Sparse?.10f:context.archetype==BoomBapGrooveArchetype::BreakHeavy?.02f:0.0f;
            const float removeChance = protectedBackbeat ? 0.0f
                : archetypeRemoval+(development ? 0.10f + params.variation * 0.08f : turnaround ? 0.16f + params.variation * 0.10f : 0.05f);
            if (chance(rng, removeChance))
                continue; // HatDropout / RemovePickup / RemoveGhost
            copy.barIndex = targetBar;
            int local = normalizeTickInBar(copy.tick64);
            if (lane == BoomBapClassicLanes::Kick && local % 16 != 0 && chance(rng, development ? 0.28f : 0.16f))
            {
                local = std::clamp(local + (chance(rng, 0.5f) ? 4 : -4), 0, 63); // ShiftSyncopatedKick
                copy.role = turnaround ? BoomBapClassicRole::Turnaround : BoomBapClassicRole::Response;
                copy.roleString = roleToString(copy.role);
                copy.timing = makeTiming(rng, params, style, lane, copy.role, local);
                copy.microTimingTicks = copy.timing.total();
            }
            copy.tick64 = targetBar * kTicksPerBar + local;
            pattern.notesByLane[static_cast<size_t>(lane)].push_back(copy);
        }
    }
    if (development && chance(rng, 0.45f + params.variation * 0.2f))
        addTimedNote(pattern, params, style, rng, BoomBapClassicLanes::ClapGhost, targetBar,
                     chance(rng, .5f) ? 12 : 44, randomInt(rng, 36, 62), BoomBapClassicRole::Ghost);
    if (turnaround)
    {
        if (chance(rng, 0.62f))
            addTimedNote(pattern, params, style, rng, BoomBapClassicLanes::HatAccent, targetBar, 60,
                         randomInt(rng, 84, 104), BoomBapClassicRole::Turnaround);
        if (chance(rng, 0.38f + params.variation * .2f))
            addTimedNote(pattern, params, style, rng, BoomBapClassicLanes::Kick, targetBar, 56,
                         randomInt(rng, 82, 108), BoomBapClassicRole::Turnaround);
    }
}

void BoomBapClassicAlgebraGenerator::applyRarePhraseEvent(BoomBapClassicAlgebraPattern& pattern,
                                                           const BoomBapClassicAlgebraParams& params,
                                                           std::mt19937& rng) const
{
    if (params.bars < 2 || pattern.context.requestedEvent == RarePhraseEvent::None)
        return;
    const auto& style=getBoomBapProfile(params.substyle); const int bar=params.bars-1, start=barStart(bar), zone=start+44;
    const auto eraseZone=[&](int lane,int from)
    {
        auto& notes=pattern.notesByLane[lane];
        notes.erase(std::remove_if(notes.begin(),notes.end(),[&](const auto&n){return n.tick64>=start+from && !(lane==BoomBapClassicLanes::Snare&&normalizeTickInBar(n.tick64)==48);}),notes.end());
    };
    auto addRelated=[&](int lane,int local,int velocity,BoomBapClassicRole role,int anchorLane,int anchorLocal,int priority)
    {
        addTimedNote(pattern,params,style,rng,lane,bar,local,velocity,role);
        auto& n=pattern.notesByLane[lane].back();n.anchorLane=anchorLane;n.anchorTick64=start+anchorLocal;n.priority=priority;
    };
    switch(pattern.context.requestedEvent)
    {
        case RarePhraseEvent::SnareFill:
        {
            eraseZone(BoomBapClassicLanes::HiHat,48); eraseZone(BoomBapClassicLanes::HatAccent,48);
            const auto forced=params.forcedFillType.value_or(BoomBapFillType::None);
            pattern.fillType=forced!=BoomBapFillType::None?forced:static_cast<BoomBapFillType>(1+(boomBapHash(pattern.context.motifSeed,0x46494c4cULL)%5));
            const std::array<int,3> ticks{{52,56,60}}; const std::array<float,3> contour{{.38f,.48f,.62f}};
            const auto main=std::find_if(pattern.notesByLane[BoomBapClassicLanes::Snare].begin(),pattern.notesByLane[BoomBapClassicLanes::Snare].end(),[&](const auto&n){return n.barIndex==bar&&normalizeTickInBar(n.tick64)==48;});
            const int mainVel=main==pattern.notesByLane[BoomBapClassicLanes::Snare].end()?108:main->velocity;
            int count=pattern.fillType==BoomBapFillType::SnareAcceleration?3:2;
            for(int i=0;i<count;++i)addRelated(BoomBapClassicLanes::ClapGhost,ticks[i],std::min(mainVel-1,(int)std::lround(mainVel*contour[i])),BoomBapClassicRole::FillSupport,BoomBapClassicLanes::Snare,48,70+i);
            pattern.realizedEvent=RarePhraseEvent::SnareFill; break;
        }
        case RarePhraseEvent::KickTurnaround:
            eraseZone(BoomBapClassicLanes::HiHat,56); addRelated(BoomBapClassicLanes::Kick,56,102,BoomBapClassicRole::Turnaround,BoomBapClassicLanes::Snare,48,80); pattern.realizedEvent=RarePhraseEvent::KickTurnaround; break;
        case RarePhraseEvent::HatDropout:
            eraseZone(BoomBapClassicLanes::HiHat,48);eraseZone(BoomBapClassicLanes::HatAccent,48);eraseZone(BoomBapClassicLanes::OpenHat,48);pattern.realizedEvent=RarePhraseEvent::HatDropout;break;
        case RarePhraseEvent::GhostPickup:
            addRelated(BoomBapClassicLanes::ClapGhost,44,48,BoomBapClassicRole::GhostBeforeSnare,BoomBapClassicLanes::Snare,48,50);pattern.realizedEvent=RarePhraseEvent::GhostPickup;break;
        case RarePhraseEvent::OpenHatLift:
            eraseZone(BoomBapClassicLanes::HiHat,60);addRelated(BoomBapClassicLanes::OpenHat,60,88,BoomBapClassicRole::Turnaround,BoomBapClassicLanes::Snare,48,62);pattern.realizedEvent=RarePhraseEvent::OpenHatLift;break;
        case RarePhraseEvent::PercEnding:
            addRelated(BoomBapClassicLanes::Perc,60,76,BoomBapClassicRole::Turnaround,BoomBapClassicLanes::Snare,48,55);pattern.realizedEvent=RarePhraseEvent::PercEnding;break;
        case RarePhraseEvent::BreakStop:
            for(int lane=0;lane<BoomBapClassicLanes::Count;++lane)if(lane!=BoomBapClassicLanes::Snare)eraseZone(lane,52);pattern.realizedEvent=RarePhraseEvent::BreakStop;break;
        default: break;
    }
    // Local density budget: preserve anchors/fill gesture and remove weakest decoration first.
    std::vector<std::pair<int,size_t>> local;
    for(int lane=0;lane<BoomBapClassicLanes::Count;++lane)for(size_t i=0;i<pattern.notesByLane[lane].size();++i)if(pattern.notesByLane[lane][i].tick64>=zone)local.emplace_back(lane,i);
    while((int)local.size()>style.maxFillEvents+2)
    {
        auto weakest=std::min_element(local.begin(),local.end(),[&](auto a,auto b){return pattern.notesByLane[a.first][a.second].priority<pattern.notesByLane[b.first][b.second].priority;});
        if(weakest==local.end()||pattern.notesByLane[weakest->first][weakest->second].priority>=95)break;
        pattern.notesByLane[weakest->first].erase(pattern.notesByLane[weakest->first].begin()+static_cast<std::ptrdiff_t>(weakest->second));
        local.clear();for(int lane=0;lane<BoomBapClassicLanes::Count;++lane)for(size_t i=0;i<pattern.notesByLane[lane].size();++i)if(pattern.notesByLane[lane][i].tick64>=zone)local.emplace_back(lane,i);
    }
}

void BoomBapClassicAlgebraGenerator::validateAndRepair(BoomBapClassicAlgebraPattern& pattern,
                                                       const BoomBapClassicAlgebraParams& params) const
{
    const auto& profile = algebraProfile(params.substyle);
    const auto& style = getBoomBapProfile(params.substyle);

    for (auto& lane : pattern.notesByLane)
        dedupeLane(lane);

    for (int bar = 0; bar < params.bars; ++bar)
    {
        for (const int snareTick : { 16, 48 })
        {
            if (!laneHasAt(pattern, BoomBapClassicLanes::Snare, bar, snareTick))
            {
                addNote(pattern,
                        BoomBapClassicLanes::Snare,
                        bar,
                        snareTick,
                        108,
                        microMsToPpq(static_cast<float>((profile.snareLateMsMin + profile.snareLateMsMax) / 2), params.bpm),
                        BoomBapClassicRole::Anchor);
                pattern.repairsApplied.add("add_missing_snare");
            }
        }

        const bool intentionalHatSilence = bar == params.bars - 1
            && (pattern.realizedEvent == RarePhraseEvent::HatDropout || pattern.realizedEvent == RarePhraseEvent::BreakStop);
        int carrierCount = intentionalHatSilence ? 4 : 0;
        for (const auto& note : pattern.notesByLane[BoomBapClassicLanes::HiHat])
            carrierCount += note.barIndex == bar;
        for (const int carrierTick : { 0, 16, 32, 48 })
        {
            if (carrierCount >= 4)
                break;
            if (!laneHasAt(pattern, BoomBapClassicLanes::HiHat, bar, carrierTick))
            {
                addNote(pattern, BoomBapClassicLanes::HiHat, bar, carrierTick, 72, 0, BoomBapClassicRole::StrongPulse);
                ++carrierCount;
                pattern.repairsApplied.add("restore_readable_hat_carrier");
            }
        }

        // deriveBarFromStatement's per-note removeChance (development/turnaround bars) applies
        // uniformly across lanes with no anchor protection for Kick, unlike Snare's backbeat.
        // At low density that can roll every kick out of a derived bar. Every bar needs at
        // least one kick to stay a usable foundation track, so restore a plain anchor if the
        // whole bar was stripped (not just tick 0), mirroring the bar-0 fallback this replaces.
        const bool barHasKick = std::any_of(pattern.notesByLane[BoomBapClassicLanes::Kick].begin(),
                                            pattern.notesByLane[BoomBapClassicLanes::Kick].end(),
                                            [bar](const auto& note) { return note.barIndex == bar; });
        if (!barHasKick)
        {
            addNote(pattern, BoomBapClassicLanes::Kick, bar, 0, 108, 0, BoomBapClassicRole::Anchor);
            pattern.repairsApplied.add("add_missing_kick_anchor");
        }
    }

    auto& supportSnares=pattern.notesByLane[BoomBapClassicLanes::ClapGhost];
    supportSnares.erase(std::remove_if(supportSnares.begin(),supportSnares.end(),[&](auto& ghost)
    {
        if(ghost.role==BoomBapClassicRole::FillSupport || ghost.role==BoomBapClassicRole::ClapLayer) return false;
        auto& mains=pattern.notesByLane[BoomBapClassicLanes::Snare];
        auto nearest=mains.end();int distance=999;
        for(auto it=mains.begin();it!=mains.end();++it)if(it->barIndex==ghost.barIndex&&std::abs(it->tick64-ghost.tick64)<distance){nearest=it;distance=std::abs(it->tick64-ghost.tick64);}
        if(nearest==mains.end()||distance>static_cast<int>(style.ghostAnchorWindow64)){++pattern.orphanGhostCount;++pattern.repairedGhostCount;pattern.repairsApplied.add("remove_orphan_snare_ghost");return true;}
        ghost.anchorLane=BoomBapClassicLanes::Snare;ghost.anchorTick64=nearest->tick64;
        ghost.role=ghost.tick64<nearest->tick64?BoomBapClassicRole::GhostBeforeSnare:BoomBapClassicRole::GhostAfterSnare;ghost.roleString=roleToString(ghost.role);
        const int maxVelocity=std::max(1,static_cast<int>(std::floor(nearest->velocity*style.clapGhostVelocityMaxRatio)));
        if(ghost.velocity>maxVelocity){ghost.velocity=maxVelocity;++pattern.repairedGhostCount;pattern.repairsApplied.add("reduce_clap_ghost_relative_velocity");}
        pattern.maxGhostVelocityRatio=std::max(pattern.maxGhostVelocityRatio,ghost.velocity/static_cast<float>(std::max(1,nearest->velocity)));return false;
    }),supportSnares.end());

    auto& kickGhosts=pattern.notesByLane[BoomBapClassicLanes::KickGhost];
    kickGhosts.erase(std::remove_if(kickGhosts.begin(),kickGhosts.end(),[&](auto& ghost)
    {
        if(ghost.role==BoomBapClassicRole::FillKick) return false;
        auto& mains=pattern.notesByLane[BoomBapClassicLanes::Kick];auto nearest=mains.end();int distance=999;
        for(auto it=mains.begin();it!=mains.end();++it)if(it->barIndex==ghost.barIndex&&std::abs(it->tick64-ghost.tick64)<distance){nearest=it;distance=std::abs(it->tick64-ghost.tick64);}
        if(nearest==mains.end()||distance>12){++pattern.orphanKickGhostCount;pattern.repairsApplied.add("remove_orphan_kick_ghost");return true;}
        ghost.anchorLane=BoomBapClassicLanes::Kick;ghost.anchorTick64=nearest->tick64;ghost.role=ghost.tick64<nearest->tick64?BoomBapClassicRole::PickupToKick:BoomBapClassicRole::ResponseFromKick;ghost.roleString=roleToString(ghost.role);
        ghost.velocity=std::min(ghost.velocity,std::max(1,static_cast<int>(std::floor(nearest->velocity*style.ghostKickVelocityMaxRatio))));return false;
    }),kickGhosts.end());

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
    reduceLane(BoomBapClassicLanes::Sub808, profile.sub808Rate <= 0.001f ? 0 : std::max(1, params.bars / 4), "reduce_sub808_reinforcement");

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
        addNote(pattern, BoomBapClassicLanes::HatAccent, 3, 60, 92, microMsToPpq(8.0f, params.bpm), BoomBapClassicRole::Ending);
        pattern.repairsApplied.add("vary_bar_4_identical_phrase");
    }

    for (auto& note : pattern.notesByLane[BoomBapClassicLanes::Snare])
    {
        if ((note.role == BoomBapClassicRole::Anchor || note.role == BoomBapClassicRole::Backbeat) && note.microTimingTicks < 0)
        {
            note.microTimingTicks = 0;
            pattern.repairsApplied.add("prevent_early_main_snare");
        }
    }

    const int maxPocketPpq = microMsToPpq(26.0f, params.bpm);
    for (auto& lane : pattern.notesByLane)
    {
        for (auto& note : lane)
        {
            if (std::abs(note.microTimingTicks) > maxPocketPpq)
            {
                note.microTimingTicks = std::clamp(note.microTimingTicks, -maxPocketPpq, maxPocketPpq);
                pattern.repairsApplied.add("clamp_overhumanized_micro");
            }
        }
    }

    for (auto& lane : pattern.notesByLane)
        dedupeLane(lane);
}

juce::String BoomBapClassicAlgebraGenerator::buildDebugSummary(const BoomBapClassicAlgebraPattern& pattern,
                                                               const BoomBapClassicAlgebraParams& params) const
{
    const auto& profile = algebraProfile(params.substyle);
    juce::StringArray lines;
    lines.add("style: Boom Bap Classic Algebra");
    lines.add("substyle: " + juce::String(profile.name));
    lines.add("seed: " + juce::String(params.seed));
    lines.add("bars: " + juce::String(params.bars));
    lines.add("swing: " + juce::String(profiledSwing(params.swing, profile), 3));
    lines.add("density: " + juce::String(params.density, 3));
    lines.add("groove archetype: " + juce::String(toString(pattern.context.archetype)));
    lines.add("rare phrase event requested/realized: " + juce::String(toString(pattern.context.requestedEvent)) + "/" + juce::String(toString(pattern.realizedEvent)));
    lines.add("fill type: " + juce::String(toString(pattern.fillType)));
    lines.add("selected candidate index: " + juce::String(pattern.selectedCandidateIndex) + "/" + juce::String(params.candidateCount));
    lines.add("near-best pool/best/selected/tolerance: " + juce::String(pattern.nearBestPoolSize) + "/" + juce::String(pattern.bestCandidateQuality,3) + "/" + juce::String(pattern.selectedCandidateQuality,3) + "/" + juce::String(getBoomBapProfile(params.substyle).nearBestTolerance,3));
    lines.add("shared features density/sync/velocity/timing/space/repeat/interlock/roles: "
              + juce::String(pattern.features.density, 3) + "/" + juce::String(pattern.features.syncopation, 3) + "/"
              + juce::String(pattern.features.velocityLife, 3) + "/" + juce::String(pattern.features.timingActivity, 3) + "/"
              + juce::String(pattern.features.negativeSpace, 3) + "/" + juce::String(pattern.features.repetition, 3) + "/"
              + juce::String(pattern.features.interlock, 3) + "/" + juce::String(pattern.features.roleClarity, 3));
    lines.add("style target fit/distance/selection score: " + juce::String(pattern.styleMatch.fit, 3) + "/"
              + juce::String(pattern.styleMatch.weightedDistance, 3) + "/"
              + juce::String(pattern.selectionQuality, 3));
    lines.add("final score: " + juce::String(pattern.score.quality, 3));
    lines.add("core scores backbeat/kick/break/micro/space: "
              + juce::String(pattern.score.backbeatScore, 3) + "/"
              + juce::String(pattern.score.kickAnchorScore, 3) + "/"
              + juce::String(pattern.score.breakResemblanceScore, 3) + "/"
              + juce::String(pattern.score.microPlausibilityScore, 3) + "/"
              + juce::String(pattern.score.negativeSpaceScore, 3));
    lines.add("penalties trapLeak/earlySnare/overMicro/sub808: "
              + juce::String(pattern.score.trapLeakPenalty, 3) + "/"
              + juce::String(pattern.score.earlySnarePenalty, 3) + "/"
              + juce::String(pattern.score.overHumanizePenalty, 3) + "/"
              + juce::String(pattern.score.sub808OverusePenalty, 3));
    lines.add("kick count: " + juce::String(countLane(pattern, BoomBapClassicLanes::Kick)));
    lines.add("snare count: " + juce::String(countLane(pattern, BoomBapClassicLanes::Snare)));
    lines.add("hat count: " + juce::String(countLane(pattern, BoomBapClassicLanes::HiHat) + countLane(pattern, BoomBapClassicLanes::HatAccent)));
    lines.add("ghost count: " + juce::String(countRole(pattern, BoomBapClassicRole::Ghost)));
    lines.add("new scores ghostContext/ghostVelocity/kickConversation/hatMotif/rare/novelty: "
              +juce::String(pattern.score.ghostContextQuality,3)+"/"+juce::String(pattern.score.ghostVelocityQuality,3)+"/"
              +juce::String(pattern.score.kickConversationQuality,3)+"/"+juce::String(pattern.score.hatMotifCoherence,3)+"/"
              +juce::String(pattern.score.rareEventQuality,3)+"/"+juce::String(pattern.score.novelty,3));
    lines.add("ghost diagnostics orphan/repaired/maxRatio: "+juce::String(pattern.orphanGhostCount)+"/"+juce::String(pattern.repairedGhostCount)+"/"+juce::String(pattern.maxGhostVelocityRatio,3));
    lines.add("kick ghost orphan count: "+juce::String(pattern.orphanKickGhostCount));
    lines.add("repairs applied: " + (pattern.repairsApplied.isEmpty() ? juce::String("none") : pattern.repairsApplied.joinIntoString(",")));
    lines.add("phrase roles:");
    lines.add("  bar 1 statement");
    lines.add("  bar 2 confirmation");
    lines.add("  bar 3 development");
    lines.add("  bar 4 turnaround");
    return lines.joinIntoString("\n");
}
} // namespace bbg
