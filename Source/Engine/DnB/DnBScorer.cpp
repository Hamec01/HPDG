#include "DnBScorer.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace bbg
{
namespace
{
float gaussianFit(float x, float mu, float sigma)
{
    const float z = (x - mu) / sigma;
    return std::exp(-0.5f * z * z);
}

bool isMainKick(const DnBEvent& e)
{
    return e.lane == TrackType::Kick;
}

bool isBackbone(const DnBEvent& e)
{
    return e.lane == TrackType::Snare && e.role == DnBRole::SnareBackbeat;
}

bool isCarrier(const DnBEvent& e)
{
    return e.role == DnBRole::HatCarrier || e.role == DnBRole::RideCarrier;
}

bool isGhostLike(const DnBEvent& e)
{
    return (e.lane == TrackType::Snare && e.ghost) || e.lane == TrackType::GhostKick;
}

const DnBEvent* findAt(const DnBPattern& p, TrackType lane, int bar, int tick, bool mainOnly)
{
    for (const auto& e : p.events)
        if (e.lane == lane && e.bar == bar && e.tick == tick && (!mainOnly || !e.ghost))
            return &e;
    return nullptr;
}

// Onset set of the rhythmic skeleton of one bar (carrier hats excluded) for bar similarity.
std::set<int> skeleton(const DnBPattern& p, int bar)
{
    std::set<int> out;
    for (const auto& e : p.events)
        if (e.bar == bar && !isCarrier(e) && e.lane != TrackType::Cymbal)
            out.insert(static_cast<int>(e.lane) * 64 + e.tick);
    return out;
}

float jaccard(const std::set<int>& a, const std::set<int>& b)
{
    if (a.empty() && b.empty())
        return 1.0f;
    int both = 0;
    for (const int x : a)
        both += b.count(x) > 0 ? 1 : 0;
    const int either = static_cast<int>(a.size() + b.size()) - both;
    return either > 0 ? static_cast<float>(both) / static_cast<float>(either) : 1.0f;
}

float syncopationWeight(const DnBEvent& e)
{
    if (isMainKick(e)) return 1.0f;
    if (isBackbone(e)) return 0.5f;
    if (e.role == DnBRole::BreakDetail) return 0.5f;
    if (e.lane == TrackType::GhostKick) return 0.4f;
    if (e.lane == TrackType::Snare && e.ghost) return 0.35f;
    return 0.0f; // hats / air / fills are not judged as syncopation here
}
} // namespace

DnBScore DnBScorer::score(const DnBPattern& p, const DnBStyleProfile& style)
{
    DnBScore s;
    const int bars = std::max(1, p.bars);

    // --- Anchor clarity + meter stability (macro) ------------------------------------------
    float anchorSum = 0.0f;
    float macroSum = 0.0f;
    float fastSum = 0.0f;
    int barsWithoutBackbone = 0;
    for (int bar = 0; bar < bars; ++bar)
    {
        const auto* s2 = findAt(p, TrackType::Snare, bar, DnBGrid::kSnare2, true);
        const auto* s4 = findAt(p, TrackType::Snare, bar, DnBGrid::kSnare4, true);
        // A phrase's answer bar may carry the pulse with the 2-step kick alone (K0 K10 | K10).
        const bool twoStepBar = bar % 2 == 1 && std::any_of(p.events.begin(), p.events.end(), [bar](const DnBEvent& e)
        {
            return e.bar == bar && e.lane == TrackType::Kick && e.tick >= 24 && e.tick <= 44;
        });
        const bool k0 = findAt(p, TrackType::Kick, bar, 0, true) != nullptr || twoStepBar;
        int dominant = 0;
        int checked = 0;
        for (const auto* snare : { s2, s4 })
        {
            if (snare == nullptr)
                continue;
            ++checked;
            bool loudest = true;
            for (const auto& e : p.events)
                if (&e != snare && e.bar == bar && std::abs(e.tick - snare->tick) <= 2 && e.lane != TrackType::Kick && e.velocity >= snare->velocity)
                    loudest = false;
            dominant += loudest ? 1 : 0;
        }
        const float dominance = checked > 0 ? static_cast<float>(dominant) / static_cast<float>(checked) : 0.0f;
        anchorSum += (0.4f * (s2 != nullptr) + 0.4f * (s4 != nullptr) + 0.2f * k0) * (0.7f + 0.3f * dominance);
        macroSum += (static_cast<float>(k0) + (s2 != nullptr) + (s4 != nullptr)) / 3.0f;
        barsWithoutBackbone += (s2 == nullptr || s4 == nullptr || !k0) ? 1 : 0;

        int coveredEighths = 0;
        for (int slot = 0; slot < 8; ++slot)
        {
            const int tick = slot * 8;
            const bool covered = std::any_of(p.events.begin(), p.events.end(), [&](const DnBEvent& e)
            {
                return e.bar == bar && e.tick == tick && (isCarrier(e) || isMainKick(e) || isBackbone(e) || e.lane == TrackType::OpenHat);
            });
            coveredEighths += covered ? 1 : 0;
        }
        fastSum += static_cast<float>(coveredEighths) / 8.0f;
    }
    s.anchorClarity = anchorSum / static_cast<float>(bars);
    s.meterStability = 0.55f * macroSum / static_cast<float>(bars) + 0.45f * fastSum / static_cast<float>(bars);

    // --- Interlock: every main kick relates to the snare / meter --------------------------
    int kicks = 0;
    int related = 0;
    for (const auto& e : p.events)
    {
        if (!isMainKick(e))
            continue;
        ++kicks;
        int dPre = 64;
        int dPost = 64;
        for (const int snare : { -16, 16, 48, 80 })
        {
            if (snare > e.tick) dPre = std::min(dPre, snare - e.tick);
            if (snare < e.tick) dPost = std::min(dPost, e.tick - snare);
        }
        const bool relation = e.tick == 0 || e.role == DnBRole::KickAnchor || e.role == DnBRole::Turnaround
            || dPre <= 12 || dPost <= 12 || (64 - e.tick) <= 8;
        related += relation ? 1 : 0;
    }
    s.interlock = kicks > 0 ? static_cast<float>(related) / static_cast<float>(kicks) : 0.0f;

    // --- Syncopation (onset on a weak position not followed by an onset on the stronger one) --
    float syncSum = 0.0f;
    for (const auto& e : p.events)
    {
        const float weight = syncopationWeight(e);
        if (weight <= 0.0f)
            continue;
        const float m = DnBGrid::metricStrength(e.tick);
        if (m >= 0.9f)
            continue;
        int u = e.tick + 1;
        while (DnBGrid::metricStrength(u) <= m)
            ++u;
        const int uBar = u >= 64 ? (e.bar + 1) % bars : e.bar;
        const int uTick = u % 64;
        const bool resolved = findAt(p, TrackType::Kick, uBar, uTick, true) != nullptr
            || findAt(p, TrackType::Snare, uBar, uTick, true) != nullptr;
        if (!resolved)
            syncSum += (DnBGrid::metricStrength(u) - m) * (static_cast<float>(e.velocity) / 127.0f) * weight;
    }
    s.syncopation = syncSum / static_cast<float>(bars * 4);
    s.syncopationFit = gaussianFit(s.syncopation, style.syncTarget, 0.04f);

    // --- Forward motion: energy leading into / answering each backbone snare ----------------
    float forwardSum = 0.0f;
    int snares = 0;
    for (const auto& snare : p.events)
    {
        if (!isBackbone(snare))
            continue;
        ++snares;
        const int at = snare.absoluteTick();
        float pre = 0.0f;
        float post = 0.0f;
        for (const auto& e : p.events)
        {
            if (&e == &snare || isCarrier(e))
                continue;
            const int d = e.absoluteTick() - at;
            const float v = static_cast<float>(e.velocity) / 127.0f;
            if (d < 0 && d > -16)
                pre += v * std::exp(static_cast<float>(d) / 6.0f);
            else if (d > 0 && d < 16)
                post += v * std::exp(-static_cast<float>(d) / 6.0f);
        }
        forwardSum += pre + 0.5f * post;
    }
    s.forwardMotion = snares > 0 ? forwardSum / static_cast<float>(snares) : 0.0f;
    s.forwardFit = gaussianFit(s.forwardMotion, style.forwardTarget, 0.20f);

    // --- Ghost context + velocity hierarchy -----------------------------------------------
    int ghosts = 0;
    int contextual = 0;
    int checks = 0;
    int passed = 0;
    for (const auto& e : p.events)
    {
        if (isGhostLike(e))
        {
            ++ghosts;
            const auto* anchor = e.anchorTick >= 0 ? findAt(p, TrackType::Snare, e.bar, e.anchorTick, true) : nullptr;
            if (anchor != nullptr && std::abs(e.tick - e.anchorTick) <= 12)
            {
                ++contextual;
                ++checks;
                passed += e.velocity < anchor->velocity ? 1 : 0;
            }
        }
        if (e.lane == TrackType::HiHat || e.lane == TrackType::HatFX || e.lane == TrackType::Ride)
        {
            for (const int tick : { DnBGrid::kSnare2, DnBGrid::kSnare4 })
            {
                const auto* snare = findAt(p, TrackType::Snare, e.bar, tick, true);
                if (snare != nullptr && std::abs(e.tick - tick) <= 2)
                {
                    ++checks;
                    passed += e.velocity < snare->velocity ? 1 : 0;
                }
            }
        }
    }
    s.ghostContext = ghosts > 0 ? static_cast<float>(contextual) / static_cast<float>(ghosts) : 1.0f;
    s.velocityHierarchy = checks > 0 ? static_cast<float>(passed) / static_cast<float>(checks) : 1.0f;

    // --- Negative space: 1/8 windows without skeleton events -------------------------------
    int emptyWindows = 0;
    for (int bar = 0; bar < bars; ++bar)
        for (int window = 0; window < 8; ++window)
        {
            const bool busy = std::any_of(p.events.begin(), p.events.end(), [&](const DnBEvent& e)
            {
                return e.bar == bar && e.tick / 8 == window && !isCarrier(e) && e.lane != TrackType::Cymbal;
            });
            emptyWindows += busy ? 0 : 1;
        }
    s.negativeSpace = static_cast<float>(emptyWindows) / static_cast<float>(bars * 8);
    s.negativeSpaceFit = gaussianFit(s.negativeSpace, style.negativeSpaceTarget, 0.15f);

    // --- Repetition (bar-to-bar similarity vs the style's target) --------------------------
    if (bars > 1)
    {
        float similarity = 0.0f;
        for (int bar = 1; bar < bars; ++bar)
            similarity += jaccard(skeleton(p, bar - 1), skeleton(p, bar));
        s.repetition = similarity / static_cast<float>(bars - 1);
        s.repetitionFit = gaussianFit(s.repetition, style.repetitionTarget, 0.20f);
    }
    else
    {
        s.repetition = style.repetitionTarget;
        s.repetitionFit = 1.0f;
    }

    // --- Variation keeps identity: retention^a * difference^b -----------------------------
    float variationSum = 0.0f;
    int varied = 0;
    for (int bar = 0; bar < bars && bar < static_cast<int>(p.barRoles.size()); ++bar)
    {
        const auto role = p.barRoles[static_cast<size_t>(bar)];
        if (role != DnBBarRole::Response && role != DnBBarRole::Development)
            continue;
        int source = role == DnBBarRole::Response ? bar - 1 : 0;
        if (source < 0 || source == bar)
            continue;
        const auto a = skeleton(p, source);
        const auto b = skeleton(p, bar);
        int important = 0;
        int kept = 0;
        for (const auto& e : p.events)
        {
            if (e.bar != source || !(isMainKick(e) || isBackbone(e)))
                continue;
            ++important;
            kept += findAt(p, e.lane, bar, e.tick, true) != nullptr ? 1 : 0;
        }
        const float retention = important > 0 ? static_cast<float>(kept) / static_cast<float>(important) : 1.0f;
        const float difference = 1.0f - jaccard(a, b);
        variationSum += std::pow(retention, 0.6f) * std::pow(std::min(1.0f, difference / 0.2f), 0.4f);
        ++varied;
    }
    s.variationFit = varied > 0 ? variationSum / static_cast<float>(varied) : 1.0f;

    // --- Timing plausibility (backbone near the grid) ---------------------------------------
    float microSum = 0.0f;
    int anchors = 0;
    for (const auto& e : p.events)
        if (isMainKick(e) || isBackbone(e))
        {
            microSum += static_cast<float>(std::abs(e.micro));
            ++anchors;
        }
    const float meanMicro = anchors > 0 ? microSum / static_cast<float>(anchors) : 0.0f;
    s.timingPlausibility = std::clamp(1.0f - (meanMicro - 10.0f) / 30.0f, 0.0f, 1.0f);

    // --- Phrase resolution: a fill lands on the next downbeat -------------------------------
    int fills = 0;
    int resolvedFills = 0;
    for (int bar = 0; bar < bars && bar < static_cast<int>(p.barRoles.size()); ++bar)
    {
        if (p.barRoles[static_cast<size_t>(bar)] != DnBBarRole::Fill)
            continue;
        ++fills;
        resolvedFills += findAt(p, TrackType::Kick, (bar + 1) % bars, 0, true) != nullptr ? 1 : 0;
    }
    s.phraseResolution = fills > 0 ? static_cast<float>(resolvedFills) / static_cast<float>(fills) : 1.0f;

    // --- Dynamic contrast ------------------------------------------------------------------
    float mean = 0.0f;
    for (const auto& e : p.events)
        mean += static_cast<float>(e.velocity);
    mean /= static_cast<float>(std::max<size_t>(1, p.events.size()));
    float variance = 0.0f;
    for (const auto& e : p.events)
        variance += (static_cast<float>(e.velocity) - mean) * (static_cast<float>(e.velocity) - mean);
    const float deviation = std::sqrt(variance / static_cast<float>(std::max<size_t>(1, p.events.size())));
    s.contrast = std::clamp(deviation / 28.0f, 0.0f, 1.0f);

    // --- Penalties --------------------------------------------------------------------------
    float penalties = 0.0f;
    std::vector<float> hatVelocities;
    for (int bar = 0; bar < bars; ++bar)
    {
        int barKicks = 0;
        int barEvents = 0;
        for (const auto& e : p.events)
        {
            if (e.bar != bar)
                continue;
            ++barEvents;
            barKicks += isMainKick(e) ? 1 : 0;
            if (isMainKick(e) && (e.tick == DnBGrid::kSnare2 || e.tick == DnBGrid::kSnare4))
                penalties += 0.30f; // collision
            if (e.lane == TrackType::HiHat)
                hatVelocities.push_back(static_cast<float>(e.velocity));
        }
        if (barKicks > style.maxKicks + 1)
            penalties += 0.15f * static_cast<float>(barKicks - style.maxKicks - 1);
        if (barEvents > 34)
            penalties += 0.02f * static_cast<float>(barEvents - 34);
    }
    penalties += 0.30f * static_cast<float>(barsWithoutBackbone); // meter loss
    penalties += 0.20f * static_cast<float>(ghosts - contextual);  // orphans
    if (hatVelocities.size() > 4)
    {
        float hatMean = 0.0f;
        for (const float v : hatVelocities) hatMean += v;
        hatMean /= static_cast<float>(hatVelocities.size());
        float hatVariance = 0.0f;
        for (const float v : hatVelocities) hatVariance += (v - hatMean) * (v - hatMean);
        if (std::sqrt(hatVariance / static_cast<float>(hatVelocities.size())) < 4.0f)
            penalties += 0.10f; // flat velocity
    }
    s.penalties = penalties;

    // --- Gates, core, secondary ---------------------------------------------------------------
    s.passedGates = true;
    auto gate = [&s](float value, float threshold, const char* name)
    {
        if (s.passedGates && value < threshold)
        {
            s.passedGates = false;
            s.failedGate = name;
        }
    };
    gate(s.anchorClarity, 0.75f, "anchor clarity");
    gate(s.meterStability, 0.55f, "meter stability");
    gate(s.interlock, 0.70f, "interlock");
    gate(s.velocityHierarchy, 0.85f, "velocity hierarchy");
    gate(s.ghostContext, 0.95f, "ghost context");

    const std::array<std::pair<float, float>, 5> core {
        std::pair<float, float> { s.anchorClarity, 2.0f },
        { s.meterStability, 1.5f },
        { s.interlock, 1.5f },
        { std::max(0.05f, s.syncopationFit), 1.5f },
        { s.velocityHierarchy, 1.0f }
    };
    float logSum = 0.0f;
    float weightSum = 0.0f;
    for (const auto& [value, weight] : core)
    {
        logSum += weight * std::log(std::max(0.01f, value));
        weightSum += weight;
    }
    s.core = std::exp(logSum / weightSum);
    s.secondary = 0.18f * s.forwardFit + 0.12f * s.ghostContext + 0.18f * s.negativeSpaceFit + 0.20f * s.repetitionFit
                + 0.12f * s.variationFit + 0.05f * s.timingPlausibility + 0.10f * s.phraseResolution + 0.05f * s.contrast;
    s.quality = 0.7f * s.core + 0.3f * s.secondary - s.penalties;
    return s;
}

int DnBScorer::pruneSecondary(DnBPattern& pattern, const DnBStyleProfile& style)
{
    // Quietest secondary notes are questioned first.
    std::vector<size_t> order;
    for (size_t i = 0; i < pattern.events.size(); ++i)
        if (isSecondaryRole(pattern.events[i].role))
            order.push_back(i);
    std::sort(order.begin(), order.end(), [&pattern](size_t a, size_t b) { return pattern.events[a].velocity < pattern.events[b].velocity; });
    if (order.size() > 24)
        order.resize(24);

    std::vector<bool> remove(pattern.events.size(), false);
    float full = score(pattern, style).quality;
    int removed = 0;
    for (const size_t index : order)
    {
        DnBPattern without = pattern;
        without.events.clear();
        for (size_t i = 0; i < pattern.events.size(); ++i)
            if (!remove[i] && i != index)
                without.events.push_back(pattern.events[i]);
        const float quality = score(without, style).quality;
        // Contribution(e) = Q_full - Q_without: a note that adds nothing (or hurts) goes.
        if (full - quality < 0.0005f)
        {
            remove[index] = true;
            full = quality;
            ++removed;
        }
    }

    if (removed > 0)
    {
        std::vector<DnBEvent> kept;
        for (size_t i = 0; i < pattern.events.size(); ++i)
            if (!remove[i])
                kept.push_back(pattern.events[i]);
        pattern.events = std::move(kept);
    }
    return removed;
}
} // namespace bbg
