#include "DnBGrammar.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace bbg
{
namespace
{
using Bar = std::vector<DnBEvent>;

float uniform01(std::mt19937& rng)
{
    return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng);
}

bool chance(std::mt19937& rng, float probability)
{
    return uniform01(rng) < probability;
}

int randomInt(std::mt19937& rng, int lo, int hi)
{
    return std::uniform_int_distribution<int>(lo, hi)(rng);
}

float gaussianBump(float x, float mu, float sigma)
{
    const float z = (x - mu) / sigma;
    return std::exp(-0.5f * z * z);
}

float sigmoid(float x)
{
    return 1.0f / (1.0f + std::exp(-x));
}

// Beta(a, b) via two gammas: most draws small, the occasional louder one.
float betaSample(std::mt19937& rng, float a, float b)
{
    const float x = std::gamma_distribution<float>(a, 1.0f)(rng);
    const float y = std::gamma_distribution<float>(b, 1.0f)(rng);
    return x + y > 0.0f ? x / (x + y) : 0.5f;
}

template <typename T>
T pickWeighted(std::mt19937& rng, const std::vector<std::pair<T, float>>& options)
{
    float total = 0.0f;
    for (const auto& option : options)
        total += std::max(0.0f, option.second);
    if (total <= 0.0f)
        return options.front().first;
    float roll = uniform01(rng) * total;
    for (const auto& option : options)
    {
        roll -= std::max(0.0f, option.second);
        if (roll <= 0.0f)
            return option.first;
    }
    return options.back().first;
}

bool barHas(const Bar& bar, TrackType lane, int tick)
{
    return std::any_of(bar.begin(), bar.end(), [&](const DnBEvent& e) { return e.lane == lane && e.tick == tick; });
}

bool isBackbone(const DnBEvent& e)
{
    return e.lane == TrackType::Snare && e.role == DnBRole::SnareBackbeat;
}

DnBEvent makeEvent(TrackType lane, int tick, int velocity, DnBRole role, bool ghost = false, int length = 2)
{
    DnBEvent e;
    e.lane = lane;
    e.tick = tick;
    e.velocity = juce::jlimit(1, 127, velocity);
    e.role = role;
    e.ghost = ghost;
    e.length = length;
    return e;
}

// Distance from tick to the next / previous backbone snare (16, 48 and their neighbours in
// the adjacent bars), on the 64 lattice.
int distanceToNextSnare(int tick)
{
    for (const int s : { 16, 48, 80 })
        if (s > tick)
            return s - tick;
    return 64;
}

int distanceFromPreviousSnare(int tick)
{
    int best = 64;
    for (const int s : { -16, 16, 48 })
        if (s < tick)
            best = std::min(best, tick - s);
    return best;
}

//------------------------------------------------------------------------------
// Pattern-level choices shared by every bar of a candidate.
struct CandidateContext
{
    const DnBStyleProfile& style;
    const DnBGenerationParams& params;
    DnBCarrierMode carrier = DnBCarrierMode::EighthRolling;
    std::array<bool, 16> brokenMask {};
    std::vector<int> hatPickups; // 16th pickups of an eighth carrier (ticks 12 / 36), fixed per pattern
    float hatPhase = 0.0f;
};

//------------------------------------------------------------------------------
// 1. Snare backbone.
void addSnareBackbone(Bar& bar, const CandidateContext& ctx, std::mt19937& rng)
{
    bar.push_back(makeEvent(TrackType::Snare, DnBGrid::kSnare2, randomInt(rng, 112, 122), DnBRole::SnareBackbeat));
    bar.push_back(makeEvent(TrackType::Snare, DnBGrid::kSnare4, randomInt(rng, 112, 122), DnBRole::SnareBackbeat));

    // Break-derived styles: an extra mid-level snare hit that belongs to the backbone.
    if (chance(rng, (1.0f - ctx.style.snareRigidity) * 0.6f))
    {
        const int tick = pickWeighted<int>(rng, { { 26, 0.25f }, { 28, 0.30f }, { 58, 0.30f }, { 40, 0.15f } });
        auto e = makeEvent(TrackType::Snare, tick, randomInt(rng, 78, 92), DnBRole::BreakDetail, true);
        e.anchorBar = 0;
        e.anchorTick = tick < 32 ? DnBGrid::kSnare2 : DnBGrid::kSnare4;
        bar.push_back(e);
    }
}

// The 16th right after a backbone snare (ticks 20 / 52 on the 64 lattice).
bool isStumbleAfterSnare(int tick)
{
    return tick == DnBGrid::kSnare2 + 4 || tick == DnBGrid::kSnare4 + 4;
}

// 2. Kick answers the snare: anticipation / response fields + meter + syncopation drive.
void addKicks(Bar& bar, const CandidateContext& ctx, std::mt19937& rng)
{
    const auto& style = ctx.style;
    const float density = ctx.params.density;
    std::vector<int> kicks { 0 };
    bar.push_back(makeEvent(TrackType::Kick, 0, randomInt(rng, 114, 124), DnBRole::DownbeatAnchor));

    if (chance(rng, style.twoStepAnchor))
    {
        // The 2-step anchor always sits on the 16th grid (a 32nd-off anchor sounds like a miss).
        const int anchor = pickWeighted<int>(rng, { { 40, 0.64f }, { 24, 0.15f }, { 36, 0.08f }, { 44, 0.07f }, { 32, 0.06f } });
        kicks.push_back(anchor);
        bar.push_back(makeEvent(TrackType::Kick, anchor, randomInt(rng, 110, 122), DnBRole::KickAnchor));
    }

    // Played loops sit at ~2 kicks a bar (downbeat + 2-step); density opens up the rest. Rounding the
    // extra kicks down gave every bar of a density the same count (Modern / Liquid / Jump-Up: 2 in
    // 77-89 % of bars; played loops spread 1-5, docs/audit/DNB_STAGE.md step 1): the part rounded away
    // becomes the chance of one more kick. Never fewer kicks than before.
    const float extraKicks = (style.maxKicks - style.minKicks) * std::clamp((density - 0.25f) * 1.3f, 0.0f, 1.0f);
    const int roundedExtra = static_cast<int>(std::lround(extraKicks));
    const float roundedAway = extraKicks - static_cast<float>(roundedExtra);
    const int budget = std::clamp(style.minKicks + roundedExtra + (roundedAway > 0.0f && chance(rng, roundedAway) ? 1 : 0),
                                  style.minKicks, style.maxKicks);

    std::vector<std::pair<int, float>> scored;
    for (const int t : { 4, 8, 10, 12, 20, 22, 24, 26, 28, 30, 32, 36, 38, 40, 44, 52, 54, 56, 58, 60 })
    {
        if (t == DnBGrid::kSnare2 || t == DnBGrid::kSnare4)
            continue;
        // 32nd-position kicks only in Breakbeat (chopped breaks). At 160-180 BPM a kick a 32nd
        // off the 16th grid reads as out of time in every other style.
        if (t % 4 != 0 && style.substyle != DnBSubstyle::Breakbeat)
            continue;
        // A kick a 16th right after the backbone snare stumbles the 2-step (chopped-break move only).
        if (isStumbleAfterSnare(t) && style.substyle != DnBSubstyle::Breakbeat)
            continue;

        const float m = DnBGrid::metricStrength(t);
        const float anticipation = std::max(gaussianBump(static_cast<float>(distanceToNextSnare(t)), 8.0f, 2.5f),
                                            0.6f * gaussianBump(static_cast<float>(64 - t), 4.0f, 2.0f)); // pickup into the next downbeat
        const float response = gaussianBump(static_cast<float>(distanceFromPreviousSnare(t)), 7.0f, 3.0f);
        const float logit = -2.9f + 1.3f * m + 1.6f * anticipation + 1.2f * response
                          + 1.1f * style.kickSyncopation * (1.0f - m) + 1.6f * (density - 0.5f);
        scored.emplace_back(t, logit);
    }

    // Walk forward in time: each kick depends on what came before it (spacing, budget).
    for (const auto& [t, logit] : scored)
    {
        if (static_cast<int>(kicks.size()) >= budget)
            break;
        int gap = 64;
        for (const int k : kicks)
            gap = std::min(gap, std::abs(k - t));
        if (gap < 4)
            continue;
        const float doublePenalty = gap == 4 ? (style.kickSyncopation > 0.7f ? 0.5f : 1.4f) : 0.0f;
        if (!chance(rng, sigmoid(logit - doublePenalty)))
            continue;

        const float anticipation = gaussianBump(static_cast<float>(distanceToNextSnare(t)), 8.0f, 2.5f);
        const float response = gaussianBump(static_cast<float>(distanceFromPreviousSnare(t)), 7.0f, 3.0f);
        const DnBRole role = anticipation >= response && anticipation > 0.45f ? DnBRole::KickPickup
                           : response > 0.45f ? DnBRole::KickResponse
                                              : DnBRole::KickSyncopation;
        const int velocity = role == DnBRole::KickResponse ? randomInt(rng, 100, 112) : randomInt(rng, 94, 108);
        kicks.push_back(t);
        bar.push_back(makeEvent(TrackType::Kick, t, velocity, role));
    }

    // At least the style's minimum: take the best remaining candidate(s).
    std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    for (const auto& [t, logit] : scored)
    {
        if (static_cast<int>(kicks.size()) >= style.minKicks)
            break;
        bool free = true;
        for (const int k : kicks)
            free = free && std::abs(k - t) >= 6;
        if (!free)
            continue;
        kicks.push_back(t);
        bar.push_back(makeEvent(TrackType::Kick, t, randomInt(rng, 100, 112), DnBRole::KickResponse));
    }
}

// 3. Ghost kick atom: K -> Kghost -> S.
void addGhostKicks(Bar& bar, const CandidateContext& ctx, std::mt19937& rng)
{
    std::vector<DnBEvent> added;
    for (const auto& e : bar)
    {
        if (e.lane != TrackType::Kick || e.tick == 0)
            continue;
        const int next = e.tick + distanceToNextSnare(e.tick);
        const int ghostTick = e.tick + 4;
        if (next - ghostTick < 2 || next - ghostTick > 6 || ghostTick >= 64 || barHas(bar, TrackType::Kick, ghostTick))
            continue;
        if (!chance(rng, ctx.style.ghostKickRate * (0.6f + ctx.params.density)))
            continue;
        auto ghost = makeEvent(TrackType::GhostKick, ghostTick,
                               static_cast<int>(e.velocity * (0.35f + 0.15f * uniform01(rng))), DnBRole::GhostKick, true);
        ghost.anchorBar = 0;
        ghost.anchorTick = next < 64 ? next : -1;
        if (ghost.anchorTick >= 0)
            added.push_back(ghost);
    }
    bar.insert(bar.end(), added.begin(), added.end());
}

// 4. Ghost snares belong to a backbone snare (pre / post offsets on the 64 lattice).
void addGhostSnares(Bar& bar, const CandidateContext& ctx, std::mt19937& rng)
{
    const auto& style = ctx.style;
    std::vector<DnBEvent> added;
    for (const auto& snare : bar)
    {
        if (!isBackbone(snare))
            continue;
        const float expected = style.ghostAmount * (0.5f + ctx.params.density) * 1.3f;
        int count = static_cast<int>(expected) + (chance(rng, expected - std::floor(expected)) ? 1 : 0);
        count = std::clamp(count, 0, 3);

        // Ghosts a 16th / eighth from their snare. A 32nd from it (+-2, +-6) is a flam / drag:
        // right for chopped breaks, a sloppy double snare everywhere else.
        const bool drags = style.substyle == DnBSubstyle::Breakbeat;
        std::vector<std::pair<int, float>> offsets {
            { -8, 0.45f * style.ghostPreBias }, { -4, 0.25f * style.ghostPreBias },
            { 4, 0.25f * (1.0f - style.ghostPreBias) }, { 8, 0.50f * (1.0f - style.ghostPreBias) }
        };
        if (drags)
        {
            offsets.push_back({ -6, 0.10f * style.ghostPreBias });
            offsets.push_back({ -2, 0.20f * style.ghostPreBias });
            offsets.push_back({ 2, 0.15f * (1.0f - style.ghostPreBias) });
            offsets.push_back({ 6, 0.10f * (1.0f - style.ghostPreBias) });
        }
        for (int n = 0; n < count && !offsets.empty(); ++n)
        {
            const int offset = pickWeighted<int>(rng, offsets);
            offsets.erase(std::remove_if(offsets.begin(), offsets.end(), [offset](const auto& o) { return o.first == offset; }), offsets.end());
            const int tick = snare.tick + offset;
            if (tick < 0 || tick >= 64 || barHas(bar, TrackType::Kick, tick) || barHas(bar, TrackType::Snare, tick)
                || std::any_of(added.begin(), added.end(), [tick](const DnBEvent& g) { return g.tick == tick; }))
                continue;
            const float ratio = style.ghostRatioMin + (style.ghostRatioMax - style.ghostRatioMin) * betaSample(rng, 2.0f, 4.0f);
            auto ghost = makeEvent(TrackType::Snare, tick, static_cast<int>(std::lround(snare.velocity * ratio)),
                                   offset < 0 ? DnBRole::GhostPreSnare : DnBRole::GhostPostSnare, true);
            ghost.anchorBar = 0;
            ghost.anchorTick = snare.tick;
            added.push_back(ghost);
        }
    }
    bar.insert(bar.end(), added.begin(), added.end());
}

// 5. Carrier: chosen once per pattern, then given an accent contour and made to yield to the backbone.
void addCarrier(Bar& bar, const CandidateContext& ctx, std::mt19937& rng)
{
    std::vector<int> ticks;
    TrackType lane = TrackType::HiHat;
    DnBRole role = DnBRole::HatCarrier;
    switch (ctx.carrier)
    {
        case DnBCarrierMode::EighthOffbeat:
            ticks = { 8, 24, 40, 56 };
            for (const int t : ctx.hatPickups) ticks.push_back(t);
            break;
        case DnBCarrierMode::EighthRolling:
            for (int t = 0; t < 64; t += 8) ticks.push_back(t);
            for (const int t : ctx.hatPickups) ticks.push_back(t);
            break;
        case DnBCarrierMode::SixteenthShaker: for (int t = 0; t < 64; t += 4) ticks.push_back(t); break;
        case DnBCarrierMode::BrokenSixteenth:
            for (int i = 0; i < 16; ++i)
                if (ctx.brokenMask[static_cast<size_t>(i)])
                    ticks.push_back(i * 4);
            break;
        case DnBCarrierMode::RideDriven:
            for (int t = 0; t < 64; t += 8) ticks.push_back(t);
            lane = TrackType::Ride;
            role = DnBRole::RideCarrier;
            break;
        case DnBCarrierMode::SparseBreakHat:
            for (const int t : { 8, 24, 40, 56 })
                if (!chance(rng, 0.25f))
                    ticks.push_back(t);
            for (int n = 0; n < 2; ++n)
                ticks.push_back(randomInt(rng, 0, 15) * 4);
            break;
        default: break;
    }
    std::sort(ticks.begin(), ticks.end());
    ticks.erase(std::unique(ticks.begin(), ticks.end()), ticks.end());

    const float contrast = ctx.style.accentContrast;
    for (const int t : ticks)
    {
        // Accent contour: offbeat eighths strong, a slower wave on top, a little noise.
        constexpr float twoPi = 6.2831853f;
        float v = 70.0f + 16.0f * contrast * std::cos(twoPi * static_cast<float>(t - 8) / 16.0f)
                + 6.0f * std::cos(twoPi * static_cast<float>(t) / 32.0f + ctx.hatPhase)
                + std::normal_distribution<float>(0.0f, 3.0f)(rng);
        if (t == DnBGrid::kSnare2 || t == DnBGrid::kSnare4)
        {
            // Yield to the backbone: drop or duck under the snare.
            if (lane == TrackType::HiHat && ticks.size() > 4 && chance(rng, 0.25f))
                continue;
            v *= 0.78f;
        }
        else if (barHas(bar, TrackType::Kick, t))
        {
            v *= 0.90f;
        }
        bar.push_back(makeEvent(lane, t, juce::roundToInt(juce::jlimit(28.0f, 112.0f, v)), role));
    }

    // Ride-driven loops keep a quiet closed hat on the off-beats of beats 2 and 4.
    if (ctx.carrier == DnBCarrierMode::RideDriven)
        for (const int t : { 24, 56 })
            bar.push_back(makeEvent(TrackType::HiHat, t, randomInt(rng, 44, 58), DnBRole::HatCarrier));
}

// 6. Hat accents (Hat Accent lane): a few offbeat 16ths, never on top of kick / snare.
void addHatAccents(Bar& bar, const CandidateContext& ctx, std::mt19937& rng)
{
    const float rate = ctx.style.hatAccentRate * (0.6f + ctx.params.density * 0.8f);
    std::vector<int> free;
    for (const int t : { 12, 28, 44, 60, 4, 20, 36, 52 })
        if (!barHas(bar, TrackType::Kick, t) && !barHas(bar, TrackType::Snare, t))
            free.push_back(t);
    for (int n = 0; n < 2 && !free.empty(); ++n)
    {
        if (!chance(rng, rate * 0.6f))
            continue;
        const size_t index = static_cast<size_t>(randomInt(rng, 0, static_cast<int>(free.size()) - 1));
        bar.push_back(makeEvent(TrackType::HatFX, free[index], randomInt(rng, 88, 106), DnBRole::HatAccent));
        free.erase(free.begin() + static_cast<std::ptrdiff_t>(index));
    }
}

// 7. Open hat = air / lift / kick response, at most one per bar.
void addOpenHat(Bar& bar, const CandidateContext& ctx, std::mt19937& rng, bool phraseEnd)
{
    if (!chance(rng, ctx.style.openHatRate * (0.7f + 0.6f * ctx.params.density) * (phraseEnd ? 1.4f : 1.0f)))
        return;
    std::vector<std::pair<int, float>> candidates;
    for (const int t : { 8, 24, 40, 56 })
    {
        if (barHas(bar, TrackType::Snare, t) || barHas(bar, TrackType::Kick, t))
            continue;
        const bool afterKick = barHas(bar, TrackType::Kick, t - 8) || barHas(bar, TrackType::Kick, t - 4);
        candidates.emplace_back(t, (afterKick ? 2.0f : 0.6f) + (phraseEnd && t == 56 ? 2.0f : 0.0f));
    }
    if (candidates.empty())
        return;
    // The closed hat under it stays in the pattern (it is choked when the lanes are written), so
    // pruning an open hat that does not earn its place leaves the carrier intact.
    const int tick = pickWeighted<int>(rng, candidates);
    bar.push_back(makeEvent(TrackType::OpenHat, tick, randomInt(rng, 82, 100), DnBRole::OpenHatLift, false, 6));
}

Bar generateBar(const CandidateContext& ctx, std::mt19937& rng, bool phraseEnd)
{
    Bar bar;
    addSnareBackbone(bar, ctx, rng);
    addKicks(bar, ctx, rng);
    addGhostKicks(bar, ctx, rng);
    addGhostSnares(bar, ctx, rng);
    addCarrier(bar, ctx, rng);
    addHatAccents(bar, ctx, rng);
    addOpenHat(bar, ctx, rng, phraseEnd);
    return bar;
}

//------------------------------------------------------------------------------
// Variations keep identity: a response changes one or two things, a development re-voices
// the kick / ghost layer over the same backbone and carrier.
Bar deriveResponse(const Bar& source, const CandidateContext& ctx, std::mt19937& rng)
{
    Bar bar = source;
    const int operations = 1 + (chance(rng, 0.35f) ? 1 : 0);
    for (int op = 0; op < operations; ++op)
    {
        const int kind = pickWeighted<int>(rng, { { 0, 0.40f }, { 1, 0.25f }, { 2, 0.20f }, { 3, 0.15f } });
        if (kind == 0)
        {
            // Move one non-anchor kick by a 16th to a free spot.
            std::vector<size_t> movable;
            for (size_t i = 0; i < bar.size(); ++i)
                if (bar[i].lane == TrackType::Kick && bar[i].tick != 0 && bar[i].role != DnBRole::KickAnchor)
                    movable.push_back(i);
            if (movable.empty())
                continue;
            auto& kick = bar[movable[static_cast<size_t>(randomInt(rng, 0, static_cast<int>(movable.size()) - 1))]];
            const int target = kick.tick + (chance(rng, 0.5f) ? 4 : -4);
            if (target <= 0 || target >= 64 || target == DnBGrid::kSnare2 || target == DnBGrid::kSnare4
                || (isStumbleAfterSnare(target) && ctx.style.substyle != DnBSubstyle::Breakbeat)
                || barHas(bar, TrackType::Kick, target) || barHas(bar, TrackType::Snare, target))
                continue;
            kick.tick = target;
            kick.role = distanceToNextSnare(target) <= 10 ? DnBRole::KickPickup : DnBRole::KickResponse;
        }
        else if (kind == 1)
        {
            // Pickup kick into the next downbeat.
            const int tick = chance(rng, 0.6f) ? 60 : 56;
            if (!barHas(bar, TrackType::Kick, tick) && !barHas(bar, TrackType::Snare, tick))
                bar.push_back(makeEvent(TrackType::Kick, tick, randomInt(rng, 96, 108), DnBRole::KickPickup));
        }
        else if (kind == 2)
        {
            // One more ghost before the second snare.
            const bool drag = ctx.style.substyle == DnBSubstyle::Breakbeat && chance(rng, 0.5f);
            const int tick = DnBGrid::kSnare4 - (drag ? 2 : 4);
            if (!barHas(bar, TrackType::Snare, tick) && !barHas(bar, TrackType::Kick, tick))
            {
                const float ratio = ctx.style.ghostRatioMin + (ctx.style.ghostRatioMax - ctx.style.ghostRatioMin) * betaSample(rng, 2.0f, 4.0f);
                auto ghost = makeEvent(TrackType::Snare, tick, static_cast<int>(116 * ratio), DnBRole::GhostPreSnare, true);
                ghost.anchorBar = 0;
                ghost.anchorTick = DnBGrid::kSnare4;
                bar.push_back(ghost);
            }
        }
        else
        {
            // 32nd hat pickup into the next bar.
            for (const int tick : { 60, 62 })
                if (!barHas(bar, TrackType::HatFX, tick))
                    bar.push_back(makeEvent(TrackType::HatFX, tick, randomInt(rng, 70, 92), DnBRole::HatPickup));
        }
    }
    return bar;
}

Bar deriveDevelopment(const Bar& source, const CandidateContext& ctx, std::mt19937& rng, bool phraseEnd)
{
    const Bar fresh = generateBar(ctx, rng, phraseEnd);
    Bar bar;
    // Keep the backbone, the carrier and (often) the 2-step anchor; take the new kick / ghost layer.
    const bool keepAnchor = chance(rng, 0.5f);
    for (const auto& e : source)
    {
        const bool kickLayer = e.lane == TrackType::Kick || e.lane == TrackType::GhostKick || (e.lane == TrackType::Snare && e.ghost);
        if (!kickLayer || e.tick == 0 || (keepAnchor && e.role == DnBRole::KickAnchor))
            bar.push_back(e);
    }
    for (const auto& e : fresh)
    {
        const bool kickLayer = e.lane == TrackType::Kick || e.lane == TrackType::GhostKick || (e.lane == TrackType::Snare && e.ghost);
        if (kickLayer && e.tick != 0 && !(keepAnchor && e.role == DnBRole::KickAnchor) && !barHas(bar, e.lane, e.tick))
            bar.push_back(e);
    }
    return bar;
}

// Moves the second backbone snare (48) to `target` (40 or 56). The ghosts / ghost kicks that
// belonged to it go; the kicks clear the new snare (4 + 10: the 2-step kick moves to 6, sometimes a
// kick on 14 - Ghosthack Upfront 4 + 10 bars; 4 + 14: nothing after the 2-step kick).
void displaceSecondSnare(Bar& bar, int target, std::mt19937& rng)
{
    const bool early = target < DnBGrid::kSnare4;
    bar.erase(std::remove_if(bar.begin(), bar.end(), [&](const DnBEvent& e)
    {
        if (e.anchorTick == DnBGrid::kSnare4)
            return true;
        if (e.lane == TrackType::Kick && e.tick != 0)
            return early ? e.tick > 24 : e.tick > DnBGrid::kSnare4 - 8;
        return e.lane == TrackType::Snare && e.ghost && std::abs(e.tick - target) <= 2;
    }), bar.end());
    for (auto& e : bar)
        if (isBackbone(e) && e.tick == DnBGrid::kSnare4)
            e.tick = target;
    if (early)
    {
        if (!barHas(bar, TrackType::Kick, 24))
            bar.push_back(makeEvent(TrackType::Kick, 24, randomInt(rng, 108, 120), DnBRole::KickAnchor));
        if (chance(rng, 0.5f))
            bar.push_back(makeEvent(TrackType::Kick, 56, randomInt(rng, 96, 108), DnBRole::KickResponse));
    }
}

// A fill must lead somewhere: it rolls into the next downbeat.
void applyFill(Bar& bar, const CandidateContext& ctx, std::mt19937& rng)
{
    bar.erase(std::remove_if(bar.begin(), bar.end(), [](const DnBEvent& e)
    {
        const bool decoration = e.lane == TrackType::HiHat || e.lane == TrackType::HatFX || e.lane == TrackType::OpenHat
            || e.lane == TrackType::Ride || e.lane == TrackType::GhostKick || (e.lane == TrackType::Snare && e.ghost);
        return e.tick > DnBGrid::kSnare4 + 1 && (decoration || (e.lane == TrackType::Kick && e.tick != 0));
    }), bar.end());

    const bool busy = ctx.style.substyle == DnBSubstyle::Breakbeat || ctx.style.substyle == DnBSubstyle::Neurofunk;
    const std::vector<int> roll = pickWeighted<std::vector<int>>(rng, {
        { { 52, 56, 60 }, busy ? 0.15f : 0.40f },
        { { 52, 54, 56, 58, 60, 62 }, busy ? 0.35f : 0.15f },
        { { 50, 54, 56, 58, 60, 62 }, busy ? 0.30f : 0.15f },
        { { 56, 58, 60, 62 }, 0.20f },
        { { 54, 58, 60 }, 0.10f }
    });
    const int count = static_cast<int>(roll.size());
    for (int i = 0; i < count; ++i)
    {
        const float rise = count > 1 ? static_cast<float>(i) / static_cast<float>(count - 1) : 1.0f;
        bar.push_back(makeEvent(TrackType::Snare, roll[static_cast<size_t>(i)], juce::roundToInt(66.0f + 42.0f * rise) + randomInt(rng, -3, 3), DnBRole::Fill));
    }
    if (chance(rng, 0.5f))
        for (const int tick : { 56, 60 })
            if (!barHas(bar, TrackType::Snare, tick) && !barHas(bar, TrackType::Kick, tick))
            {
                bar.push_back(makeEvent(TrackType::Kick, tick, randomInt(rng, 100, 114), DnBRole::Turnaround));
                break;
            }
}

//------------------------------------------------------------------------------
// Performance: velocity humanization, per-lane swing and per-lane microtiming.
struct LaneFeel
{
    float swing = 0.0f;    // share of the UI swing this note takes
    float humanize = 0.0f; // share of the UI humanize this note takes
};

LaneFeel feelFor(const DnBEvent& e)
{
    switch (e.role)
    {
        case DnBRole::DownbeatAnchor:
        case DnBRole::KickAnchor:
        case DnBRole::KickPickup:
        case DnBRole::KickResponse:
        case DnBRole::KickSyncopation:
        case DnBRole::Turnaround: return { 0.08f, 0.15f };
        case DnBRole::SnareBackbeat: return { 0.03f, 0.12f };
        case DnBRole::GhostPreSnare:
        case DnBRole::GhostPostSnare:
        case DnBRole::BreakDetail: return { 0.80f, 0.80f };
        case DnBRole::GhostKick: return { 0.60f, 0.70f };
        case DnBRole::HatCarrier: return { 0.75f, 0.55f };
        case DnBRole::HatAccent:
        case DnBRole::HatPickup: return { 0.70f, 0.60f };
        case DnBRole::RideCarrier: return { 0.70f, 0.55f };
        case DnBRole::OpenHatLift: return { 0.50f, 0.50f };
        case DnBRole::Fill: return { 0.30f, 0.40f };
        case DnBRole::CrashMarker: return { 0.0f, 0.20f };
    }
    return { 0.5f, 0.5f };
}

void applyPerformance(DnBPattern& pattern, const DnBGenerationParams& params, const DnBStyleProfile& style, std::mt19937& rng)
{
    const float ratio = std::clamp(params.swingPercent / 100.0f, 0.50f, 0.75f);
    const float swingPpq = 240.0f * (2.0f * ratio - 1.0f);
    for (auto& e : pattern.events)
    {
        const auto feel = feelFor(e);
        float micro = 0.0f;
        if (e.tick % 8 == 4)
            micro += swingPpq * feel.swing;
        else if (e.tick % 4 == 2)
            micro += 0.5f * swingPpq * feel.swing;

        const float sigma = params.humanize * feel.humanize * 16.0f * (0.5f + style.microtiming);
        if (sigma > 0.01f)
            micro += std::normal_distribution<float>(0.0f, sigma)(rng);
        e.micro = juce::roundToInt(juce::jlimit(-36.0f, 36.0f, micro));
        // The backbone never rushes and stays nearly on the grid.
        if (e.role == DnBRole::SnareBackbeat || e.role == DnBRole::DownbeatAnchor)
            e.micro = juce::jlimit(-2, 8, e.micro);

        const float velocityNoise = std::normal_distribution<float>(0.0f, 2.0f + 3.0f * params.humanize)(rng);
        e.velocity = juce::jlimit(1, 127, e.velocity + juce::roundToInt(velocityNoise));
    }
}
} // namespace

//==============================================================================
void DnBGrammar::repair(DnBPattern& pattern)
{
    auto& events = pattern.events;

    // Kicks never sit on the backbone (16 / 48, or a displaced second snare).
    std::map<int, std::vector<int>> backbone; // bar -> backbone ticks
    auto backboneTicks = [&backbone](int bar) -> const std::vector<int>&
    {
        static const std::vector<int> regular { DnBGrid::kSnare2, DnBGrid::kSnare4 };
        const auto found = backbone.find(bar);
        return found != backbone.end() ? found->second : regular;
    };
    for (const auto& e : events)
        if (isBackbone(e) && e.tick != DnBGrid::kSnare2 && e.tick != DnBGrid::kSnare4)
        {
            auto& ticks = backbone.try_emplace(e.bar, std::vector<int> { DnBGrid::kSnare2, DnBGrid::kSnare4 }).first->second;
            ticks.push_back(e.tick);
        }
    events.erase(std::remove_if(events.begin(), events.end(), [&](const DnBEvent& e)
    {
        if (e.lane != TrackType::Kick)
            return false;
        const auto& ticks = backboneTicks(e.bar);
        return std::find(ticks.begin(), ticks.end(), e.tick) != ticks.end();
    }), events.end());

    // One event per lane / bar / tick: keep the strongest (non-ghost first).
    std::stable_sort(events.begin(), events.end(), [](const DnBEvent& a, const DnBEvent& b)
    {
        if (a.lane != b.lane) return a.lane < b.lane;
        if (a.bar != b.bar) return a.bar < b.bar;
        if (a.tick != b.tick) return a.tick < b.tick;
        if (a.ghost != b.ghost) return !a.ghost;
        return a.velocity > b.velocity;
    });
    events.erase(std::unique(events.begin(), events.end(), [](const DnBEvent& a, const DnBEvent& b)
    {
        return a.lane == b.lane && a.bar == b.bar && a.tick == b.tick;
    }), events.end());

    // No orphan ghosts: a ghost / break detail / ghost kick needs its strong event nearby.
    auto mainSnareVelocity = [&events](int bar, int tick) -> int
    {
        for (const auto& e : events)
            if (e.lane == TrackType::Snare && e.bar == bar && e.tick == tick && !e.ghost)
                return e.velocity;
        return -1;
    };
    events.erase(std::remove_if(events.begin(), events.end(), [&](const DnBEvent& e)
    {
        const bool needsAnchor = (e.lane == TrackType::Snare && e.ghost) || e.lane == TrackType::GhostKick;
        if (!needsAnchor)
            return false;
        if (e.anchorTick < 0 || mainSnareVelocity(e.bar, e.anchorTick) < 0)
            return true;
        const int reach = e.role == DnBRole::BreakDetail ? 12 : 8;
        return std::abs(e.tick - e.anchorTick) > reach;
    }), events.end());

    // Velocity hierarchy: ghost < anchor; hats duck under a backbone snare.
    for (auto& e : events)
    {
        if ((e.lane == TrackType::Snare && e.ghost) || e.lane == TrackType::GhostKick)
        {
            const int anchor = mainSnareVelocity(e.bar, e.anchorTick);
            if (anchor > 0)
                e.velocity = std::min(e.velocity, juce::roundToInt(anchor * 0.65f));
        }
        if (e.lane == TrackType::HiHat || e.lane == TrackType::Ride || e.lane == TrackType::HatFX)
        {
            for (const int s : backboneTicks(e.bar))
            {
                const int snare = mainSnareVelocity(e.bar, s);
                if (snare > 0 && std::abs(e.tick - s) <= 2)
                    e.velocity = std::min(e.velocity, snare - 12);
            }
        }
        e.velocity = juce::jlimit(1, 127, e.velocity);
    }

    // No machine-gun hats: break runs of 4+ consecutive 32nds.
    for (const auto lane : { TrackType::HiHat, TrackType::HatFX })
    {
        int run = 0;
        int previous = -100;
        for (auto it = events.begin(); it != events.end();)
        {
            if (it->lane != lane)
            {
                ++it;
                continue;
            }
            const int absolute = it->absoluteTick();
            run = absolute - previous == 2 ? run + 1 : 1;
            previous = absolute;
            if (run >= 4)
            {
                it = events.erase(it);
                run = 0;
                continue;
            }
            ++it;
        }
    }

    // The very first note of the loop cannot start before the loop.
    for (auto& e : events)
        if (e.bar == 0 && e.tick == 0)
            e.micro = std::max(0, e.micro);

    std::sort(events.begin(), events.end(), [](const DnBEvent& a, const DnBEvent& b)
    {
        return a.absoluteTick() != b.absoluteTick() ? a.absoluteTick() < b.absoluteTick() : a.lane < b.lane;
    });
}

DnBPattern DnBGrammar::generateCandidate(const DnBGenerationParams& params,
                                         const DnBStyleProfile& style,
                                         std::mt19937& rng)
{
    DnBPattern pattern;
    pattern.bars = std::clamp(params.bars, 1, 16);

    CandidateContext ctx { style, params };
    std::vector<std::pair<DnBCarrierMode, float>> carriers;
    for (int i = 0; i < static_cast<int>(DnBCarrierMode::Count); ++i)
        carriers.emplace_back(static_cast<DnBCarrierMode>(i), style.carrierWeights[static_cast<size_t>(i)]);
    ctx.carrier = pickWeighted(rng, carriers);
    pattern.carrier = ctx.carrier;
    ctx.hatPhase = uniform01(rng) * 6.2831853f;
    for (const int t : { 12, 36 })
        if (chance(rng, style.hatPickupRate))
            ctx.hatPickups.push_back(t);
    // Broken 16ths: offbeat eighths always, the rest with gaps (never three empty 16ths in a row).
    for (int i = 0; i < 16; ++i)
        ctx.brokenMask[static_cast<size_t>(i)] = (i % 4 == 2) || chance(rng, 0.55f);
    for (int i = 2; i < 16; ++i)
        if (!ctx.brokenMask[static_cast<size_t>(i)] && !ctx.brokenMask[static_cast<size_t>(i - 1)] && !ctx.brokenMask[static_cast<size_t>(i - 2)])
            ctx.brokenMask[static_cast<size_t>(i)] = true;

    // Phrase topology over 2-bar phrases (variation is an intention, not a law: AAAA is valid).
    static const std::array<const char*, 6> topologies { "AAAA", "AABA", "AABB", "ABAA", "ABAC", "AABC" };
    std::vector<std::pair<int, float>> topologyOptions;
    for (int i = 0; i < 6; ++i)
        topologyOptions.emplace_back(i, style.topologyWeights[static_cast<size_t>(i)]);
    const juce::String topology = topologies[static_cast<size_t>(pickWeighted(rng, topologyOptions))];
    const int phrases = (pattern.bars + 1) / 2;

    const float answerVariation = std::clamp(style.variation * (0.6f + 0.8f * params.variation), 0.0f, 0.95f);
    std::map<char, Bar> statements;
    std::map<char, Bar> responses;
    std::vector<Bar> bars(static_cast<size_t>(pattern.bars));
    pattern.barRoles.assign(static_cast<size_t>(pattern.bars), DnBBarRole::Statement);
    pattern.barLetters.assign(static_cast<size_t>(pattern.bars), 'A');
    juce::String usedTopology;

    for (int phrase = 0; phrase < phrases; ++phrase)
    {
        const char letter = static_cast<char>(topology[phrase % 4]);
        usedTopology << juce::String::charToString(letter);
        const int first = phrase * 2;
        const bool lastPhraseBar = first + 1 >= pattern.bars;

        if (statements.find(letter) == statements.end())
        {
            if (letter == 'B' && statements.count('A') > 0)
            {
                statements[letter] = deriveDevelopment(statements['A'], ctx, rng, lastPhraseBar);
                pattern.barRoles[static_cast<size_t>(first)] = DnBBarRole::Development;
            }
            else
            {
                statements[letter] = generateBar(ctx, rng, lastPhraseBar);
                pattern.barRoles[static_cast<size_t>(first)] = DnBBarRole::Statement;
            }
        }
        else
        {
            pattern.barRoles[static_cast<size_t>(first)] = DnBBarRole::Repeat;
        }
        bars[static_cast<size_t>(first)] = statements[letter];
        pattern.barLetters[static_cast<size_t>(first)] = letter;

        if (first + 1 < pattern.bars)
        {
            const int second = first + 1;
            pattern.barLetters[static_cast<size_t>(second)] = letter;
            if (chance(rng, answerVariation))
            {
                // A repeated letter usually brings back its own answer (A A' A A').
                if (responses.count(letter) == 0 || chance(rng, 0.3f))
                    responses[letter] = deriveResponse(statements[letter], ctx, rng);
                bars[static_cast<size_t>(second)] = responses[letter];
                pattern.barRoles[static_cast<size_t>(second)] = DnBBarRole::Response;
            }
            else
            {
                bars[static_cast<size_t>(second)] = statements[letter];
                pattern.barRoles[static_cast<size_t>(second)] = DnBBarRole::Repeat;
            }
        }
    }
    pattern.topology = usedTopology;

    std::map<char, bool> dropsDownbeat;
    for (int bar = 1; bar < pattern.bars; bar += 2)
    {
        const char letter = pattern.barLetters[static_cast<size_t>(bar)];
        if (dropsDownbeat.count(letter) == 0)
            dropsDownbeat[letter] = chance(rng, style.answerDropsDownbeat);
        auto& events = bars[static_cast<size_t>(bar)];
        const bool hasTwoStep = std::any_of(events.begin(), events.end(), [](const DnBEvent& e)
        {
            return e.lane == TrackType::Kick && e.tick >= 24 && e.tick <= 44;
        });
        if (dropsDownbeat[letter] && hasTwoStep)
            events.erase(std::remove_if(events.begin(), events.end(), [](const DnBEvent& e)
            {
                return (e.lane == TrackType::Kick || e.lane == TrackType::HiHat) && e.tick == 0 && e.role != DnBRole::HatCarrier;
            }), events.end());
    }

    // Fills: a probability at phrase edges (never a fixed rule), not twice in a row.
    bool previousFill = false;
    std::vector<int> fillBars;
    for (int bar = 1; bar < pattern.bars; bar += 2)
    {
        const bool sectionEdge = (bar + 1) % 4 == 0;
        const bool lastBar = bar == pattern.bars - 1;
        const float edge = lastBar ? 1.2f : sectionEdge ? 1.0f : 0.25f;
        const float probability = style.fillRate * edge * (previousFill ? 0.4f : 1.0f) * (0.7f + 0.6f * params.density);
        previousFill = chance(rng, probability);
        if (previousFill)
        {
            applyFill(bars[static_cast<size_t>(bar)], ctx, rng);
            pattern.barRoles[static_cast<size_t>(bar)] = DnBBarRole::Fill;
            fillBars.push_back(bar);
        }
    }

    // A displaced second snare in the phrase's answer bars: 4 + 10 (the 2-step kick moves to 6) or
    // 4 + 14 (kicks 0 / 10 stay). A fill bar keeps its own ending.
    if (params.displacedSnareTick > 0)
        for (int bar = 1; bar < pattern.bars; bar += 2)
            if (pattern.barRoles[static_cast<size_t>(bar)] != DnBBarRole::Fill)
                displaceSecondSnare(bars[static_cast<size_t>(bar)], params.displacedSnareTick, rng);

    for (int bar = 0; bar < pattern.bars; ++bar)
    {
        for (auto e : bars[static_cast<size_t>(bar)])
        {
            e.bar = bar;
            if (e.anchorTick >= 0)
                e.anchorBar = bar;
            pattern.events.push_back(e);
        }
    }

    // Crash marks a structural reset: after a fill, sometimes on a new phrase letter.
    for (const int fillBar : fillBars)
    {
        if (!chance(rng, 0.7f))
            continue;
        auto crash = makeEvent(TrackType::Cymbal, 0, randomInt(rng, 96, 114), DnBRole::CrashMarker, false, 32);
        crash.bar = (fillBar + 1) % pattern.bars;
        pattern.events.push_back(crash);
    }
    for (int bar = 2; bar < pattern.bars; bar += 2)
    {
        if (pattern.barLetters[static_cast<size_t>(bar)] != pattern.barLetters[static_cast<size_t>(bar - 2)] && chance(rng, 0.25f))
        {
            auto crash = makeEvent(TrackType::Cymbal, 0, randomInt(rng, 90, 108), DnBRole::CrashMarker, false, 32);
            crash.bar = bar;
            pattern.events.push_back(crash);
        }
    }

    applyPerformance(pattern, params, style, rng);
    repair(pattern);
    return pattern;
}
} // namespace bbg
