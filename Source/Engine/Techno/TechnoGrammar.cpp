#include "TechnoGrammar.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace bbg
{
namespace
{
using namespace TechnoGrid;

bool chance(std::mt19937& rng, float p)
{
    return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) < p;
}

int randomInt(std::mt19937& rng, int lo, int hi)
{
    return std::uniform_int_distribution<int>(lo, hi)(rng);
}

float uniform(std::mt19937& rng, float lo, float hi)
{
    return std::uniform_real_distribution<float>(lo, hi)(rng);
}

template <size_t N>
int pickWeighted(std::mt19937& rng, const std::array<float, N>& weights)
{
    float total = 0.0f;
    for (const float w : weights)
        total += std::max(0.0f, w);
    if (total <= 0.0f)
        return 0;
    float roll = uniform(rng, 0.0f, total);
    for (size_t i = 0; i < N; ++i)
    {
        roll -= std::max(0.0f, weights[i]);
        if (roll <= 0.0f)
            return static_cast<int>(i);
    }
    return static_cast<int>(N) - 1;
}

constexpr int kRollSubTick = 120; // the second 32nd of a 16th

// Accent profile inside a beat: A = [beat, e, and, a] (section 3.4).
constexpr std::array<float, 4> kHatAccent { 0.55f, 0.35f, 1.00f, 0.42f };

std::vector<int> hatSteps(TechnoHatMode mode, bool openOnOffbeats)
{
    std::vector<int> steps;
    for (int s = 0; s < kStepsPerBar; ++s)
    {
        switch (mode)
        {
            case TechnoHatMode::Eighths: if (s % 2 == 0) steps.push_back(s); break;
            case TechnoHatMode::Sixteenths: steps.push_back(s); break;
            case TechnoHatMode::SixteenthsNoBeat: if (s % 4 != 0) steps.push_back(s); break;
            case TechnoHatMode::Offbeat:
                // With the open hat owning the "and"s the closed hat takes the "a"s.
                if (openOnOffbeats ? s % 4 == 3 : s % 4 == 2) steps.push_back(s);
                break;
            default: break;
        }
    }
    return steps;
}

TechnoPercCycle makePercCycle(const TechnoStyleProfile& style, std::mt19937& rng)
{
    TechnoPercCycle cycle;
    if (chance(rng, style.percPolymeter) || style.euclids.empty())
    {
        // A 3 / 5 / 6 / 7-step cell against the bar (grouping dissonance), restarted each bar.
        static const std::array<int, 4> lengths { 3, 5, 6, 7 };
        cycle.polymeter = true;
        cycle.length = lengths[static_cast<size_t>(randomInt(rng, 0, 3))];
        cycle.pulses = { randomInt(rng, 1, cycle.length - 1) }; // off the cycle start (= off beat 1)
        if (cycle.length >= 5 && chance(rng, 0.45f))
        {
            const int second = (cycle.pulses[0] + randomInt(rng, 2, cycle.length - 2)) % cycle.length;
            if (second != cycle.pulses[0])
                cycle.pulses.push_back(second);
        }
        return cycle;
    }
    const auto& kn = style.euclids[static_cast<size_t>(randomInt(rng, 0, static_cast<int>(style.euclids.size()) - 1))];
    cycle.k = kn[0];
    cycle.n = kn[1];
    cycle.rotation = randomInt(rng, 0, cycle.n - 1);
    return cycle;
}

// Hits of the perc cycle kept off the kick axis: a hit on a beat moves one 16th later (if free),
// else it is dropped (masking, section 3.5).
std::vector<int> percStepsOffAxis(const TechnoPercCycle& cycle)
{
    std::set<int> steps;
    for (const int s : cycle.hitsInBar())
    {
        if (!isBeat(s))
            steps.insert(s);
        else if (s + 1 < kStepsPerBar)
            steps.insert(s + 1);
    }
    return { steps.begin(), steps.end() };
}

void add(TechnoPattern& p, TrackType lane, int bar, int step, int velocity, TechnoRole role, int length = 1, int subTick = 0, bool ghost = false)
{
    TechnoEvent e;
    e.lane = lane;
    e.bar = bar;
    e.step = step;
    e.subTick = subTick;
    e.velocity = juce::jlimit(1, 127, velocity);
    e.role = role;
    e.length = length;
    e.ghost = ghost;
    p.events.push_back(e);
}

// Swing on the off 16ths for the carriers, humanize; the kick axis and the clap stay on the grid.
void applyPerformance(TechnoPattern& p, const TechnoGenerationParams& params, std::mt19937& rng)
{
    const float ratio = std::clamp(params.swingPercent / 100.0f, 0.50f, 0.75f);
    const float swingPpq = 240.0f * (2.0f * ratio - 1.0f);
    for (auto& e : p.events)
    {
        float swingShare = 0.0f;
        float humanShare = 0.0f;
        switch (e.role)
        {
            case TechnoRole::KickAxis:
            case TechnoRole::KickPickup: humanShare = 0.05f; break;
            case TechnoRole::Clap:
            case TechnoRole::ClapDisplaced: humanShare = 0.15f; break;
            case TechnoRole::HatCarrier:
            case TechnoRole::RideCarrier: swingShare = 1.0f; humanShare = 0.6f; break;
            case TechnoRole::Perc: swingShare = 0.8f; humanShare = 0.7f; break;
            case TechnoRole::Rumble: swingShare = 0.5f; humanShare = 0.3f; break;
            case TechnoRole::OffbeatOpen: humanShare = 0.3f; break;
            case TechnoRole::HatRoll:
            case TechnoRole::Crash: break;
        }
        float micro = e.step % 2 == 1 && e.subTick == 0 ? swingPpq * swingShare : 0.0f;
        const float sigma = params.humanize * humanShare * 10.0f;
        if (sigma > 0.01f)
            micro += std::normal_distribution<float>(0.0f, sigma)(rng);
        e.micro = juce::roundToInt(juce::jlimit(-24.0f, 60.0f, micro));
        if (e.role == TechnoRole::KickAxis)
            e.micro = juce::jlimit(-2, 2, e.micro);
        const float noise = std::normal_distribution<float>(0.0f, 1.5f + 4.0f * params.humanize * humanShare)(rng);
        e.velocity = juce::jlimit(1, 127, e.velocity + juce::roundToInt(noise));
    }
}
} // namespace

TechnoPattern TechnoGrammar::generateCandidate(const TechnoGenerationParams& params, const TechnoStyleProfile& style, std::mt19937& rng)
{
    TechnoPattern p;
    p.bars = std::clamp(params.bars, 1, 16);
    const float density = std::clamp(params.density, 0.0f, 1.0f);

    // Phrase roles (section 3.7).
    for (int bar = 0; bar < p.bars; ++bar)
    {
        TechnoBarRole role = TechnoBarRole::Main;
        if (p.bars >= 4 && bar == p.bars - 1 && chance(rng, style.fill))
            role = TechnoBarRole::Fill;
        else if (bar % 4 == 3 || (bar % 2 == 1 && chance(rng, 0.35f)))
            role = TechnoBarRole::Variation;
        p.barRoles.push_back(role);
    }

    // Pattern-level choices: the layers that make the loop.
    auto hatWeights = style.hatWeights;
    hatWeights[static_cast<size_t>(TechnoHatMode::Sixteenths)] *= 0.6f + 0.8f * density;
    hatWeights[static_cast<size_t>(TechnoHatMode::SixteenthsNoBeat)] *= 0.7f + 0.6f * density;
    hatWeights[static_cast<size_t>(TechnoHatMode::Eighths)] *= 1.4f - 0.8f * density;
    hatWeights[static_cast<size_t>(TechnoHatMode::Offbeat)] *= 1.4f - 0.8f * density;
    p.hatMode = static_cast<TechnoHatMode>(pickWeighted(rng, hatWeights));
    const bool openOn = chance(rng, style.offbeatOpen);
    const bool clapOn = chance(rng, style.clap);
    p.rideOn = chance(rng, style.ride);
    p.percOn = chance(rng, std::min(1.0f, style.perc * (0.6f + 0.6f * density)));
    p.perc = makePercCycle(style, rng);
    const bool rumbleOn = chance(rng, style.rumble);

    // Rumble template: which of d = 1, 2, 3 after each beat sound (fixed for the loop).
    std::array<bool, 4> rumbleAt { false, true, true, true };
    for (int d = 1; d <= 3; ++d)
        rumbleAt[static_cast<size_t>(d)] = chance(rng, 0.8f);
    // Sparse 16ths at low density: some "e"s drop out of a 16th carrier (same in every bar).
    std::set<int> droppedHatSteps;
    if (p.hatMode == TechnoHatMode::Sixteenths || p.hatMode == TechnoHatMode::SixteenthsNoBeat)
        for (int s = 1; s < kStepsPerBar; s += 4)
            if (chance(rng, (1.0f - density) * 0.5f))
                droppedHatSteps.insert(s);

    const auto closedSteps = hatSteps(p.hatMode, openOn);
    const auto percSteps = percStepsOffAxis(p.perc);
    const int pickupStep = chance(rng, 0.6f) ? 14 : 15;
    const int displacedClapStep = chance(rng, 0.6f) ? 15 : 13;

    for (int bar = 0; bar < p.bars; ++bar)
    {
        const auto role = p.barRoles[static_cast<size_t>(bar)];
        const bool variation = role == TechnoBarRole::Variation;
        const bool fill = role == TechnoBarRole::Fill;

        // 3.1 Kick axis + pickup / drop.
        const bool dropLast = fill && chance(rng, style.dropKick);
        for (int beat = 0; beat < 4; ++beat)
        {
            if (dropLast && beat == 3)
                continue;
            add(p, TrackType::Kick, bar, beat * 4, beat == 0 ? 124 : 118, TechnoRole::KickAxis);
        }
        if ((variation || (fill && !dropLast)) && chance(rng, style.pickupKick * (fill ? 1.6f : 1.0f)))
            add(p, TrackType::Kick, bar, pickupStep, 96, TechnoRole::KickPickup);

        // 3.2 Rumble: v(d) = v0 * 0.72^(d-1).
        if (rumbleOn)
            for (int beat = 0; beat < 4; ++beat)
            {
                if (dropLast && beat == 3)
                    continue;
                for (int d = 1; d <= 3; ++d)
                    if (rumbleAt[static_cast<size_t>(d)])
                        add(p, TrackType::GhostKick, bar, beat * 4 + d, juce::roundToInt(78.0f * std::pow(0.72f, static_cast<float>(d - 1))),
                            TechnoRole::Rumble, 1, 0, true);
            }

        // 3.3 Clap on 2 / 4, displaced in answers, a flam into the next phrase.
        if (clapOn)
        {
            add(p, TrackType::Snare, bar, 4, 108, TechnoRole::Clap);
            add(p, TrackType::Snare, bar, 12, 112, TechnoRole::Clap);
            if (variation && chance(rng, style.clapDisplaced))
                add(p, TrackType::Snare, bar, displacedClapStep, 78, TechnoRole::ClapDisplaced);
            if (fill && chance(rng, 0.45f))
                add(p, TrackType::Snare, bar, 15, 84, TechnoRole::ClapDisplaced);
        }

        // 3.6 (decided first: the roll replaces the last two 16ths of the closed hat).
        const bool roll = fill ? chance(rng, std::max(style.hatRoll, 0.3f))
                               : (bar % 4 == 3 && bar != p.bars - 1 && chance(rng, style.hatRoll * 0.6f));

        // 3.4 Offbeat open hat ("tchak"), closed hats with the accent profile (choked under it).
        if (openOn)
            for (const int s : { 2, 6, 10, 14 })
                add(p, TrackType::OpenHat, bar, s, s == 14 ? 92 : 98, TechnoRole::OffbeatOpen, 2);
        for (const int s : closedSteps)
        {
            if (droppedHatSteps.count(s) > 0 || (openOn && s % 4 == 2))
                continue;
            if (roll && s >= 14)
                continue; // the roll takes over
            const float accent = kHatAccent[static_cast<size_t>(s % 4)];
            add(p, TrackType::HiHat, bar, s, juce::roundToInt(44.0f + 56.0f * accent), TechnoRole::HatCarrier);
        }
        // A variation bar adds one extra 16th to an 8th carrier.
        if (variation && p.hatMode == TechnoHatMode::Eighths && chance(rng, 0.5f))
            add(p, TrackType::HiHat, bar, 15, 58, TechnoRole::HatCarrier);

        // Ride: eighths, offbeats accented.
        if (p.rideOn)
            for (int s = 0; s < kStepsPerBar; s += 2)
                add(p, TrackType::Ride, bar, s, s % 4 == 2 ? 96 : 62, TechnoRole::RideCarrier, 2);

        // 3.5 Percussion; a variation bar rotates the cycle one step (small mutation).
        if (p.percOn)
        {
            const int shift = variation && chance(rng, 0.5f) ? 1 : 0;
            bool firstHit = true;
            for (const int base : percSteps)
            {
                int s = base + shift;
                if (s >= kStepsPerBar || isBeat(s) || p.has(TrackType::Kick, bar, s))
                    continue; // never on the axis or a pickup kick (masking)
                add(p, TrackType::Perc, bar, s, firstHit ? 100 : 80, TechnoRole::Perc);
                firstHit = false;
            }
        }

        // 3.6 Roll at the end of bar 4 / 8 / the fill; crash at the top of the phrase.
        if (roll)
        {
            int velocity = 58;
            for (const int s : { 14, 15 })
                for (const int sub : { 0, kRollSubTick })
                {
                    add(p, TrackType::HatFX, bar, s, velocity, TechnoRole::HatRoll, 1, sub);
                    velocity += 12;
                }
        }
        if ((bar == 0 || (bar == 8 && p.bars >= 16)) && chance(rng, style.crash))
            add(p, TrackType::Cymbal, bar, 0, 100, TechnoRole::Crash, 8);
    }

    applyPerformance(p, params, rng);
    return p;
}

//==============================================================================
float TechnoScorer::syncopation(const TechnoPattern& pattern)
{
    // Polyphonic LHL syncopation (Witek 2014): a layer's onset at n followed by its own silence
    // at the next stronger position r scores w(r) - w(n), weighted per layer.
    const std::map<TrackType, float> layerWeight {
        { TrackType::Kick, 2.0f }, { TrackType::Snare, 1.5f }, { TrackType::Perc, 1.2f },
        { TrackType::HiHat, 0.6f }, { TrackType::OpenHat, 0.6f }, { TrackType::Ride, 0.5f }
    };
    float total = 0.0f;
    for (const auto& [lane, weight] : layerWeight)
        for (int bar = 0; bar < pattern.bars; ++bar)
        {
            std::array<bool, kStepsPerBar + 1> on {};
            for (const auto& e : pattern.events)
                if (e.lane == lane && e.bar == bar && e.subTick == 0)
                    on[static_cast<size_t>(e.step)] = true;
            // Beat 1 of the next bar (the kick is always there).
            on[kStepsPerBar] = lane == TrackType::Kick;
            for (int n = 0; n < kStepsPerBar; ++n)
            {
                if (!on[static_cast<size_t>(n)])
                    continue;
                const float wn = metricWeight(n);
                int r = n + 1;
                while (r < kStepsPerBar && metricWeight(r) <= wn)
                    ++r;
                const float wr = r >= kStepsPerBar ? 1.0f : metricWeight(r);
                if (!on[static_cast<size_t>(r)])
                    total += weight * std::max(0.0f, wr - wn);
            }
        }
    return total / static_cast<float>(std::max(1, pattern.bars));
}

TechnoScore TechnoScorer::score(const TechnoPattern& pattern, const TechnoStyleProfile& style, float userDensity)
{
    TechnoScore s;
    auto fit = [](float x, float target, float sigma) { return std::exp(-(x - target) * (x - target) / (2.0f * sigma * sigma)); };

    // f1: the axis.
    int beats = 0;
    int beatsWithKick = 0;
    for (int bar = 0; bar < pattern.bars; ++bar)
    {
        if (pattern.barRoles[static_cast<size_t>(bar)] == TechnoBarRole::Fill)
            continue;
        for (int beat = 0; beat < 4; ++beat)
        {
            ++beats;
            beatsWithKick += pattern.has(TrackType::Kick, bar, beat * 4) ? 1 : 0;
        }
    }
    s.anchor = beats > 0 ? static_cast<float>(beatsWithKick) / static_cast<float>(beats) : 1.0f;

    // f2: percussion masking the kick.
    int percHits = 0;
    int masked = 0;
    for (const auto& e : pattern.events)
        if (e.lane == TrackType::Perc)
        {
            ++percHits;
            masked += pattern.has(TrackType::Kick, e.bar, e.step) ? 1 : 0;
        }
    s.interlock = percHits > 0 ? 1.0f - static_cast<float>(masked) / static_cast<float>(percHits) : 1.0f;

    // f3: syncopation at the style's centre.
    s.syncopation = syncopation(pattern);
    s.syncopationFit = fit(s.syncopation, style.syncTarget, 0.35f * style.syncTarget);

    // f4: density (weighted onsets per step, kick axis excluded).
    float weighted = 0.0f;
    for (const auto& e : pattern.events)
    {
        switch (e.lane)
        {
            case TrackType::HiHat:
            case TrackType::OpenHat:
            case TrackType::Ride:
            case TrackType::GhostKick: weighted += 0.5f; break;
            case TrackType::Perc:
            case TrackType::Snare: weighted += 1.0f; break;
            case TrackType::HatFX: weighted += 0.25f; break;
            default: break;
        }
    }
    s.density = weighted / static_cast<float>(pattern.bars * TechnoGrid::kStepsPerBar);
    // The user's density moves the target around the style's default (docs/audit/TECHNO_STAGE.md step 1):
    // a fixed target made the sharp selection undo the density slider (inverted in Minimal / Dub).
    const float densityTarget = userDensity < 0.0f
        ? style.densityTarget
        : style.densityTarget * std::clamp(1.0f + 1.2f * (userDensity - style.densityDefault), 0.4f, 1.6f);
    s.densityFit = fit(s.density, densityTarget, 0.25f * densityTarget);

    // f5: repetition = mean Jaccard of neighbouring bars.
    auto barSet = [&](int bar)
    {
        std::set<std::pair<int, int>> set;
        for (const auto& e : pattern.events)
            if (e.bar == bar && e.lane != TrackType::Cymbal)
                set.insert({ static_cast<int>(e.lane), e.step * 2 + (e.subTick > 0 ? 1 : 0) });
        return set;
    };
    if (pattern.bars > 1)
    {
        float sum = 0.0f;
        for (int bar = 1; bar < pattern.bars; ++bar)
        {
            const auto a = barSet(bar - 1);
            const auto b = barSet(bar);
            int inter = 0;
            for (const auto& x : a)
                inter += b.count(x) > 0 ? 1 : 0;
            const int uni = static_cast<int>(a.size() + b.size()) - inter;
            sum += uni > 0 ? static_cast<float>(inter) / static_cast<float>(uni) : 1.0f;
        }
        s.repetition = sum / static_cast<float>(pattern.bars - 1);
    }
    else
    {
        s.repetition = 1.0f;
    }
    s.repetitionFit = fit(s.repetition, style.repetitionTarget, 0.05f);

    // f6: the fill (and the roll it carries) belongs at the phrase end only.
    s.resolution = 1.0f;
    for (int bar = 0; bar + 1 < pattern.bars; ++bar)
        if (pattern.barRoles[static_cast<size_t>(bar)] == TechnoBarRole::Fill)
            s.resolution *= 0.5f;

    // Gates.
    s.passedGates = true;
    if (s.anchor < 0.95f)
    {
        s.passedGates = false;
        s.failedGate = "kick axis broken";
    }
    for (const auto& e : pattern.events)
        if (e.lane == TrackType::Snare && (e.step == 0 || e.step == 8))
        {
            s.passedGates = false;
            s.failedGate = "clap on beat 1 / 3";
        }

    const float product = s.anchor * s.anchor * std::max(0.01f, s.interlock) * std::max(0.01f, s.syncopationFit)
        * std::max(0.01f, s.densityFit) * std::max(0.01f, s.repetitionFit) * std::max(0.01f, s.resolution);
    s.quality = std::pow(product, 1.0f / 7.0f) - s.penalties;
    return s;
}
} // namespace bbg
