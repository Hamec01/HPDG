#include "AtmosphericGrammar.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace bbg
{
namespace
{
using Steps = std::set<int>;

float uniform(std::mt19937& rng)
{
    return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng);
}

bool coin(std::mt19937& rng, float p)
{
    return uniform(rng) < p;
}

int pick(std::mt19937& rng, const std::vector<std::pair<int, float>>& weighted)
{
    float total = 0.0f;
    for (const auto& w : weighted)
        total += std::max(0.0f, w.second);
    if (total <= 0.0f)
        return weighted.empty() ? -1 : weighted.front().first;
    float roll = uniform(rng) * total;
    for (const auto& w : weighted)
    {
        roll -= std::max(0.0f, w.second);
        if (roll <= 0.0f)
            return w.first;
    }
    return weighted.back().first;
}

// Weighted pick of one step from a position profile, skipping `exclude`.
int pickStep(std::mt19937& rng, const std::array<float, 16>& profile, const Steps& exclude)
{
    std::vector<std::pair<int, float>> w;
    for (int s = 0; s < AtmosGrid::kStepsPerBar; ++s)
        if (exclude.count(s) == 0)
            w.push_back({ s, profile[static_cast<size_t>(s)] + 0.01f });
    return w.empty() ? -1 : pick(rng, w);
}

// Sections: which bars of the loop carry a layer (the passport's "whole sections without the clap").
enum class Section
{
    All,
    SecondHalf, // the layer enters halfway through every 4-bar block
    None,
    AltBlocks   // every other 4-bar block (8+ bars); SecondHalf in shorter loops
};

const char* toString(Section s)
{
    switch (s)
    {
        case Section::All: return "all bars";
        case Section::SecondHalf: return "second half";
        case Section::None: return "none";
        case Section::AltBlocks: return "alternate blocks";
    }
    return "?";
}

bool sectionActive(Section s, int bar, int bars)
{
    const int block = std::min(4, bars);
    switch (s)
    {
        case Section::All: return true;
        case Section::None: return false;
        case Section::AltBlocks:
            if (bars >= 8)
                return (bar / 4) % 2 == 1;
            [[fallthrough]];
        case Section::SecondHalf: return bars == 1 || (bar % block) >= block / 2;
    }
    return true;
}

int humanizeTicks(std::mt19937& rng, float humanize, float scale)
{
    const int range = static_cast<int>(std::lround(humanize * scale));
    return range > 0 ? std::uniform_int_distribution<int>(-range, range)(rng) : 0;
}

struct Writer
{
    AtmosPattern& pattern;
    std::mt19937& rng;
    float humanize;

    void add(TrackType lane, int bar, int step, int velocity, AtmosRole role, bool ghost = false, float microScale = 0.0f)
    {
        AtmosEvent e;
        e.lane = lane;
        e.bar = bar;
        e.step = step;
        e.velocity = std::clamp(velocity + std::uniform_int_distribution<int>(-4, 4)(rng), 1, 127);
        e.role = role;
        e.ghost = ghost;
        e.micro = microScale > 0.0f ? humanizeTicks(rng, humanize, microScale) : 0;
        pattern.events.push_back(e);
    }
};

// ---- Pulse (family A) ------------------------------------------------------------------------
void generatePulse(const AtmosGenerationParams& params, const AtmosStyleProfile&, std::mt19937& rng, AtmosPattern& out)
{
    Writer w { out, rng, params.humanize };
    const int bars = out.bars;

    // The loop is the track's main section (maintainer, 2026-10-14: sections made it sound like an
    // intro): the full groove from bar 1, variation only at the phrase end. Kick "four" (1-5-9-13)
    // or, rarely, "half" (1-9) for the whole loop; never off the beat.
    enum class KickMode { Four, Half, Mixed };
    const auto kickMode = static_cast<KickMode>(pick(rng, { { 0, 0.85f }, { 1, 0.15f } }));
    const bool dropLastBeat = bars >= 2 && coin(rng, 0.35f);

    const auto clap = Section::All;
    const auto hatSection = Section::All;
    enum class HatMode { Offbeat, OffbeatTails, Eighths };
    const auto hatMode = static_cast<HatMode>(pick(rng, { { 0, 0.50f }, { 1, 0.30f }, { 2, 0.20f } }));
    // Open hats: none, on every off-beat 8th, or only the last one of the bar (the lift into 1).
    enum class OpenMode { None, All, LastOffbeat };
    const auto openMode = static_cast<OpenMode>(pick(rng, { { 0, 0.50f }, { 1, 0.25f }, { 2, 0.25f } }));
    const float tailP = 0.35f * (0.5f + params.density);

    for (int bar = 0; bar < bars; ++bar)
    {
        const int block = std::min(4, bars);
        const bool half = kickMode == KickMode::Half || (kickMode == KickMode::Mixed && bars > 1 && (bar % block) < block / 2);
        for (int beat = 0; beat < 4; ++beat)
        {
            if (half && (beat == 1 || beat == 3))
                continue;
            if (dropLastBeat && bar == bars - 1 && beat == 3)
                continue;
            w.add(TrackType::Kick, bar, beat * 4, beat == 0 ? 116 : 108, AtmosRole::Kick);
        }

        // A loop with no clap at all is not a beat: "none" keeps the clap for the last bar.
        const bool clapOn = clap == Section::None ? bar == bars - 1 : sectionActive(clap, bar, bars);
        if (clapOn)
        {
            w.add(TrackType::Snare, bar, 4, 108, AtmosRole::Clap);
            w.add(TrackType::Snare, bar, 12, 110, AtmosRole::Clap);
        }
        // Flams and pickups (passport: 16ths 4, 7, 12, 15 at 0.07-0.09).
        const bool blockEnd = (bar % std::min(4, bars)) == std::min(4, bars) - 1;
        if (coin(rng, blockEnd ? 0.40f : 0.22f))
            w.add(TrackType::Snare, bar, 14, 72, AtmosRole::SnarePickup, true, 6.0f);
        if (clapOn && coin(rng, 0.08f))
            w.add(TrackType::Snare, bar, 3, 62, AtmosRole::SnarePickup, true, 6.0f);
        if (clapOn && coin(rng, 0.08f))
            w.add(TrackType::Snare, bar, 11, 62, AtmosRole::SnarePickup, true, 6.0f);
        if (coin(rng, 0.06f))
            w.add(TrackType::Snare, bar, 6, 66, AtmosRole::SnarePickup, true, 6.0f);

        if (!sectionActive(hatSection, bar, bars))
            continue;
        for (int s = 2; s < 16; s += 4) // the off-beat 8ths carry the groove
        {
            const bool open = openMode == OpenMode::All || (openMode == OpenMode::LastOffbeat && s == 14);
            w.add(open ? TrackType::OpenHat : TrackType::HiHat, bar, s, 92, open ? AtmosRole::OpenHat : AtmosRole::Hat, false, 8.0f);
        }
        if (hatMode == HatMode::Eighths)
            for (int s = 0; s < 16; s += 4)
                w.add(TrackType::HiHat, bar, s, 66, AtmosRole::Hat, true, 8.0f);
        if (hatMode == HatMode::OffbeatTails)
            for (const int s : { 3, 11, 15 })
                if (coin(rng, tailP))
                    w.add(TrackType::HiHat, bar, s, 58, AtmosRole::Hat, true, 8.0f);
    }

    out.plan = juce::String("kick ") + (kickMode == KickMode::Four ? "four" : kickMode == KickMode::Half ? "half" : "half -> four")
               + (dropLastBeat ? " (drop)" : "") + " | clap " + toString(clap) + " | hats "
               + (hatMode == HatMode::Offbeat ? "off-beat" : hatMode == HatMode::OffbeatTails ? "off-beat + tails" : "eighths")
               + (openMode == OpenMode::All ? " (open)" : openMode == OpenMode::LastOffbeat ? " (open on 15)" : "") + ", "
               + toString(hatSection);
}

// ---- Two-Step (family B) ---------------------------------------------------------------------
void generateTwoStep(const AtmosGenerationParams& params, const AtmosStyleProfile& style, std::mt19937& rng, AtmosPattern& out)
{
    Writer w { out, rng, params.humanize };
    const int bars = out.bars;
    const std::vector<std::pair<int, float>> kickExtras { { 7, 0.06f }, { 8, 0.10f }, { 12, 0.08f }, { 13, 0.05f }, { 15, 0.03f }, { 3, 0.02f } };
    const std::vector<std::pair<int, float>> pickups { { 2, 0.12f }, { 7, 0.12f }, { 9, 0.15f }, { 14, 0.14f }, { 15, 0.09f }, { 11, 0.06f } };

    Steps motifKicks { 0, 10 };
    if (coin(rng, 0.40f))
        motifKicks.insert(pick(rng, kickExtras));
    Steps motifPickups;
    const int pickupCount = pick(rng, { { 0, 0.40f }, { 1, 0.45f }, { 2, 0.15f } });
    for (int i = 0; i < pickupCount; ++i)
    {
        std::vector<std::pair<int, float>> free;
        for (const auto& p : pickups)
            if (motifPickups.count(p.first) == 0 && motifKicks.count(p.first) == 0)
                free.push_back(p);
        if (!free.empty())
            motifPickups.insert(pick(rng, free));
    }

    // Hat motif on the 8ths (strongest on 1 and 9), a few 16ths.
    Steps motifHats;
    for (int s = 0; s < 16; ++s)
    {
        const float p = AtmosGrid::isEighth(s) ? std::min(0.9f, style.hatProfile[static_cast<size_t>(s)] * 1.1f)
                                               : style.hatProfile[static_cast<size_t>(s)] * 0.8f * (0.5f + params.density);
        if (coin(rng, p))
            motifHats.insert(s);
    }
    if (motifHats.size() < 2)
        motifHats.insert({ 0, 8 });
    const bool openHat = coin(rng, 0.30f);
    const int openStep = coin(rng, 0.5f) ? 14 : 6;

    Steps previousKicks;
    for (int bar = 0; bar < bars; ++bar)
    {
        const bool blockEnd = bars > 1 && (bar % std::min(4, bars)) == std::min(4, bars) - 1;
        Steps kicks = motifKicks;
        if (bar > 0 && coin(rng, 0.5f))
        {
            const int variant = pick(rng, { { 0, 0.45f }, { 1, 0.30f }, { 2, 0.25f } });
            if (variant == 0)
                kicks.insert(pick(rng, kickExtras));
            else if (variant == 1 && kicks.size() > 2)
                kicks.erase(std::prev(kicks.end()));
            else
            {
                kicks.erase(10);
                kicks.insert(coin(rng, 0.6f) ? 8 : 11);
            }
        }
        if (blockEnd && coin(rng, 0.5f))
            kicks.insert(coin(rng, 0.5f) ? 13 : 15);
        for (const int s : kicks)
            w.add(TrackType::Kick, bar, s, s == 0 || s == 10 ? 114 : 100, s == 0 || s == 10 ? AtmosRole::Kick : AtmosRole::KickExtra);
        previousKicks = kicks;

        w.add(TrackType::Snare, bar, 4, 110, AtmosRole::Clap);
        w.add(TrackType::Snare, bar, 12, 112, AtmosRole::Clap);
        Steps barPickups = motifPickups;
        if (coin(rng, 0.25f) && !barPickups.empty())
            barPickups.erase(barPickups.begin());
        if (blockEnd && coin(rng, 0.5f))
            barPickups.insert(coin(rng, 0.5f) ? 14 : 15);
        for (const int s : barPickups)
            if (kicks.count(s) == 0)
                w.add(TrackType::Snare, bar, s, 64, AtmosRole::SnarePickup, true, 5.0f);

        Steps hats = motifHats;
        if (coin(rng, 0.30f))
        {
            const int s = pickStep(rng, style.hatProfile, {});
            if (hats.count(s) > 0 && hats.size() > 2)
                hats.erase(s);
            else
                hats.insert(s);
        }
        for (const int s : hats)
        {
            const bool open = openHat && s == openStep;
            w.add(open ? TrackType::OpenHat : TrackType::HiHat, bar, s, AtmosGrid::isEighth(s) ? (s % 8 == 0 ? 90 : 80) : 60,
                  open ? AtmosRole::OpenHat : AtmosRole::Hat, !AtmosGrid::isEighth(s), 7.0f);
        }
    }

    juce::String k;
    for (const int s : motifKicks)
        k << (k.isEmpty() ? "" : "-") << (s + 1);
    out.plan = "kick motif " + k + " | pickups " + juce::String(static_cast<int>(motifPickups.size())) + " | hats "
               + juce::String(static_cast<int>(motifHats.size())) + " / bar" + (openHat ? " + open" : "");
}

// ---- Broken (family D1) ----------------------------------------------------------------------
// A broken-beat groove: a kick / snare motif from the passport's strongest positions (kick 1, 6, 14,
// 16; snare 5 + 13 with ghosts on 8, 16, 3, 7), kept as a loop the ear can follow; the answer bars
// change one detail (a kick moved, a ghost added) instead of re-composing the bar.
struct BrokenMotif
{
    const char* name;
    std::vector<int> kicks;
    std::vector<int> ghosts;
    std::vector<int> answerKicks; // the kick bar of the answer (bars 2 / 4 of a phrase)
    float weight;
};

const std::vector<BrokenMotif>& brokenMotifs()
{
    static const std::vector<BrokenMotif> motifs {
        { "1-6-14", { 0, 5, 13 }, { 7, 15 }, { 0, 5, 10, 13 }, 0.22f },
        { "1-6-11-14", { 0, 5, 10, 13 }, { 7 }, { 0, 5, 13, 15 }, 0.18f },
        { "1-7-14-16", { 0, 6, 13, 15 }, { 2, 9 }, { 0, 6, 10 }, 0.16f },
        { "1-4-11", { 0, 3, 10 }, { 7, 14 }, { 0, 3, 10, 13 }, 0.16f },
        { "1-9-11-14", { 0, 8, 10, 13 }, { 7, 15 }, { 0, 5, 10 }, 0.14f },
        { "1-6-9-16", { 0, 5, 8, 15 }, { 2, 11 }, { 0, 5, 10, 13 }, 0.14f },
    };
    return motifs;
}

void generateBroken(const AtmosGenerationParams& params, const AtmosStyleProfile& style, std::mt19937& rng, AtmosPattern& out)
{
    Writer w { out, rng, params.humanize };
    const int bars = out.bars;
    const auto& library = brokenMotifs();
    std::vector<std::pair<int, float>> weighted;
    for (size_t i = 0; i < library.size(); ++i)
        weighted.push_back({ static_cast<int>(i), library[i].weight });
    const auto& motif = library[static_cast<size_t>(pick(rng, weighted))];

    // The answer: every second bar (A B A B') or only the phrase end (A A A B).
    const bool answerEveryOther = coin(rng, 0.5f);
    // Ghost count follows density; one extra ghost in the last bar of a phrase.
    const bool extraGhostAtEnd = coin(rng, 0.6f);

    // Hat motif: the passport profile, kept for the whole loop (one hat may move per bar).
    Steps hats;
    const int hatTarget = std::max(3, static_cast<int>(std::lround(style.hatsPerBar * (0.7f + 0.6f * params.density))));
    while (static_cast<int>(hats.size()) < hatTarget)
        hats.insert(pickStep(rng, style.hatProfile, hats));
    const bool openOn15 = coin(rng, 0.35f);

    for (int bar = 0; bar < bars; ++bar)
    {
        const int inPhrase = bar % std::min(4, bars);
        const bool phraseEnd = bars > 1 && inPhrase == std::min(4, bars) - 1;
        const bool answer = bars > 1 && (answerEveryOther ? inPhrase % 2 == 1 : phraseEnd);

        Steps kicks(answer ? motif.answerKicks.begin() : motif.kicks.begin(), answer ? motif.answerKicks.end() : motif.kicks.end());
        Steps snares { 4, 12 };
        Steps ghosts(motif.ghosts.begin(), motif.ghosts.end());
        if (params.density < 0.35f && ghosts.size() > 1)
            ghosts.erase(std::prev(ghosts.end()));
        if (phraseEnd && extraGhostAtEnd)
        {
            const int s = pickStep(rng, style.snareProfile, [&] { Steps ex { 4, 12 }; ex.insert(kicks.begin(), kicks.end()); ex.insert(ghosts.begin(), ghosts.end()); return ex; }());
            if (s >= 0)
                ghosts.insert(s);
        }
        for (auto it = ghosts.begin(); it != ghosts.end();)
            it = kicks.count(*it) > 0 ? ghosts.erase(it) : std::next(it);

        Steps barHats = hats;
        if (bar > 0 && coin(rng, 0.25f))
        {
            const int s = pickStep(rng, style.hatProfile, barHats);
            if (s >= 0)
            {
                barHats.erase(std::next(barHats.begin(), std::uniform_int_distribution<int>(0, static_cast<int>(barHats.size()) - 1)(rng)));
                barHats.insert(s);
            }
        }

        for (const int s : kicks)
            w.add(TrackType::Kick, bar, s, s == 0 ? 116 : 104, s == 0 ? AtmosRole::Kick : AtmosRole::KickExtra, false, 3.0f);
        for (const int s : snares)
            w.add(TrackType::Snare, bar, s, 110, AtmosRole::Clap);
        for (const int s : ghosts)
            w.add(TrackType::Snare, bar, s, 64, AtmosRole::SnarePickup, true, 5.0f);
        for (const int s : barHats)
        {
            const bool open = openOn15 && s == 14;
            w.add(open ? TrackType::OpenHat : TrackType::HiHat, bar, s, AtmosGrid::isEighth(s) ? 86 : 62, open ? AtmosRole::OpenHat : AtmosRole::Hat,
                  !AtmosGrid::isEighth(s), 7.0f);
        }
    }

    out.plan = juce::String("motif ") + motif.name + " | answer " + (answerEveryOther ? "every other bar" : "phrase end") + " | hats "
               + juce::String(hatTarget) + " / bar" + (openOn15 ? " + open on 15" : "");
}

Steps stepsOf(const AtmosPattern& p, TrackType lane, int bar)
{
    Steps s;
    for (const auto& e : p.events)
        if (e.lane == lane && e.bar == bar)
            s.insert(e.step);
    return s;
}

float fitAround(float value, float target, float tolerance)
{
    const float d = (value - target) / std::max(0.05f, tolerance);
    return std::exp(-0.5f * d * d);
}
} // namespace

AtmosPattern AtmosphericGrammar::generateCandidate(const AtmosGenerationParams& params, const AtmosStyleProfile& style, std::mt19937& rng)
{
    AtmosPattern pattern;
    pattern.bars = std::clamp(params.bars, 1, 16);
    switch (style.substyle)
    {
        case AtmosphericSubstyle::TwoStep: generateTwoStep(params, style, rng, pattern); break;
        case AtmosphericSubstyle::Broken: generateBroken(params, style, rng, pattern); break;
        case AtmosphericSubstyle::Pulse:
        default: generatePulse(params, style, rng, pattern); break;
    }
    std::sort(pattern.events.begin(), pattern.events.end(),
              [](const AtmosEvent& a, const AtmosEvent& b) { return a.bar != b.bar ? a.bar < b.bar : a.step < b.step; });
    return pattern;
}

AtmosTraits AtmosphericScorer::measure(const AtmosPattern& pattern)
{
    AtmosTraits t;
    const int bars = std::max(1, pattern.bars);
    int kicks = 0, snares = 0, hats = 0, kickOff = 0, snareOff = 0;
    for (const auto& e : pattern.events)
    {
        if (e.lane == TrackType::Kick)
        {
            ++kicks;
            kickOff += AtmosGrid::isBeat(e.step) ? 0 : 1;
        }
        else if (e.lane == TrackType::Snare)
        {
            ++snares;
            snareOff += e.step == 4 || e.step == 12 ? 0 : 1;
        }
        else if (e.lane == TrackType::HiHat || e.lane == TrackType::OpenHat)
            ++hats;
    }
    t.kicksPerBar = static_cast<float>(kicks) / static_cast<float>(bars);
    t.snaresPerBar = static_cast<float>(snares) / static_cast<float>(bars);
    t.hatsPerBar = static_cast<float>(hats) / static_cast<float>(bars);
    t.kickOffBeat = kicks > 0 ? static_cast<float>(kickOff) / static_cast<float>(kicks) : 0.0f;
    t.snareOff24 = snares > 0 ? static_cast<float>(snareOff) / static_cast<float>(snares) : 0.0f;

    if (bars > 1)
    {
        int repeats = 0;
        float share = 0.0f;
        auto motif = stepsOf(pattern, TrackType::Kick, 0);
        for (const int s : stepsOf(pattern, TrackType::Snare, 0))
            motif.insert(s + 16);
        for (int bar = 1; bar < bars; ++bar)
        {
            repeats += stepsOf(pattern, TrackType::Kick, bar) == stepsOf(pattern, TrackType::Kick, bar - 1) ? 1 : 0;
            auto cur = stepsOf(pattern, TrackType::Kick, bar);
            for (const int s : stepsOf(pattern, TrackType::Snare, bar))
                cur.insert(s + 16);
            int inter = 0;
            for (const int s : cur)
                inter += motif.count(s) > 0 ? 1 : 0;
            const auto uni = cur.size() + motif.size() - static_cast<size_t>(inter);
            share += uni > 0 ? static_cast<float>(inter) / static_cast<float>(uni) : 1.0f;
        }
        t.kickRepeat = static_cast<float>(repeats) / static_cast<float>(bars - 1);
        t.motifShare = share / static_cast<float>(bars - 1);
    }
    else
    {
        t.kickRepeat = -1.0f; // undefined for one bar
        t.motifShare = 1.0f;
    }
    return t;
}

AtmosScore AtmosphericScorer::score(const AtmosPattern& pattern, const AtmosStyleProfile& style, float userDensity)
{
    AtmosScore s;
    s.traits = measure(pattern);
    const auto& t = s.traits;
    const int bars = std::max(1, pattern.bars);

    const float hatTarget = style.hatsPerBar * (userDensity >= 0.0f ? 0.75f + 0.5f * userDensity : 1.0f);
    const float repeatFit = t.kickRepeat < 0.0f ? 1.0f : fitAround(t.kickRepeat, style.kickRepeat, 0.25f);
    s.fit = 0.20f * fitAround(t.kicksPerBar, style.kicksPerBar, 0.30f * style.kicksPerBar)
            + 0.20f * fitAround(t.snaresPerBar, style.snaresPerBar, 0.30f * style.snaresPerBar)
            + 0.15f * fitAround(t.hatsPerBar, hatTarget, 0.35f * hatTarget)
            + 0.15f * fitAround(t.kickOffBeat, style.kickOffBeat, 0.15f)
            + 0.10f * fitAround(t.snareOff24, style.snareOff24, 0.20f)
            + 0.20f * repeatFit;
    s.quality = s.fit;

    // Hard gates.
    auto fail = [&s](const char* why)
    {
        if (s.failedGate.isEmpty())
            s.failedGate = why;
    };
    if (t.kicksPerBar <= 0.0f) fail("no kick");
    if (t.hatsPerBar <= 0.0f) fail("no hats");
    bool anyBackbeat = false;
    for (int bar = 0; bar < bars; ++bar)
    {
        const auto kicks = stepsOf(pattern, TrackType::Kick, bar);
        const auto snares = stepsOf(pattern, TrackType::Snare, bar);
        const bool backbeat = snares.count(4) > 0 || snares.count(12) > 0;
        anyBackbeat = anyBackbeat || backbeat;
        switch (style.substyle)
        {
            case AtmosphericSubstyle::Pulse:
                if (kicks.empty() || kicks.count(0) == 0) fail("bar without the downbeat kick");
                for (const int k : kicks)
                    if (!AtmosGrid::isBeat(k)) fail("kick off the beat");
                break;
            case AtmosphericSubstyle::TwoStep:
                if (snares.count(4) == 0 || snares.count(12) == 0) fail("two-step snare missing");
                if (bar == 0 && kicks.count(0) == 0) fail("no kick on 1");
                break;
            case AtmosphericSubstyle::Broken:
            {
                if (!backbeat) fail("bar without the 2 / 4 anchor");
                int stacked = 0;
                for (const int k : kicks)
                    stacked += snares.count(k) > 0 ? 1 : 0;
                if (stacked > 1) fail("kick stacked on the clap twice");
                if (bar == 0 && kicks.count(0) == 0 && kicks.count(1) == 0 && kicks.count(2) == 0) fail("no kick at the top of the loop");
                break;
            }
            default: break;
        }
    }
    if (!anyBackbeat) fail("no clap on 2 / 4");
    if (style.substyle == AtmosphericSubstyle::Broken && t.motifShare < 0.20f) fail("no recognisable motif");
    s.passedGates = s.failedGate.isEmpty();
    return s;
}
} // namespace bbg
