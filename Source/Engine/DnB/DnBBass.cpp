#include "DnBBass.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <random>
#include <set>

namespace bbg
{
const char* toString(DnBBassArchetype archetype)
{
    switch (archetype)
    {
        case DnBBassArchetype::SubReese: return "Sub / Reese";
        case DnBBassArchetype::Rolling: return "Rolling";
        case DnBBassArchetype::Stab: return "Stab";
        case DnBBassArchetype::Wobble: return "Wobble";
        case DnBBassArchetype::DubSub: return "Dub Sub";
        case DnBBassArchetype::MelodicSub: return "Melodic Sub";
        default: break;
    }
    return "Bass";
}

namespace
{
constexpr int kBar = DnBGrid::kTicksPerBar;
constexpr int kLowestPitch = 24;   // C1
constexpr int kHighestPitch = 52;  // E3

float uniform01(std::mt19937& rng) { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); }
bool chance(std::mt19937& rng, float p) { return uniform01(rng) < p; }
int randomInt(std::mt19937& rng, int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); }

float gaussianFit(float x, float mu, float sigma)
{
    const float z = (x - mu) / sigma;
    return std::exp(-0.5f * z * z);
}

template <typename T>
T pickWeighted(std::mt19937& rng, const std::vector<std::pair<T, float>>& options)
{
    float total = 0.0f;
    for (const auto& o : options)
        total += std::max(0.0f, o.second);
    if (total <= 0.0f)
        return options.front().first;
    float roll = uniform01(rng) * total;
    for (const auto& o : options)
    {
        roll -= std::max(0.0f, o.second);
        if (roll <= 0.0f)
            return o.first;
    }
    return options.back().first;
}

const std::array<int, 7>& scaleFor(int scaleMode)
{
    static const std::array<int, 7> minor { 0, 2, 3, 5, 7, 8, 10 };
    static const std::array<int, 7> major { 0, 2, 4, 5, 7, 9, 11 };
    static const std::array<int, 7> harmonicMinor { 0, 2, 3, 5, 7, 8, 11 };
    return scaleMode == 1 ? major : scaleMode == 2 ? harmonicMinor : minor;
}

bool inScale(int pitch, int keyRoot, int scaleMode)
{
    const auto& scale = scaleFor(scaleMode);
    const int degree = ((pitch - keyRoot) % 12 + 12) % 12;
    return std::find(scale.begin(), scale.end(), degree) != scale.end();
}

int snapToScale(int pitch, int keyRoot, int scaleMode)
{
    if (inScale(pitch, keyRoot, scaleMode)) return pitch;
    if (inScale(pitch - 1, keyRoot, scaleMode)) return pitch - 1;
    return pitch + 1;
}

// DnB sub register: the tonic sits between E1 (28) and D#2 (39); chord roots stay near it.
int tonicPitch(int keyRoot)
{
    return 28 + ((keyRoot - 4) % 12 + 12) % 12;
}

int rootPitch(int pitchClass, int tonic)
{
    int best = tonic;
    int bestDistance = 99;
    for (int octave = 1; octave <= 3; ++octave)
    {
        const int candidate = pitchClass + 12 * octave;
        if (candidate < 26 || candidate > 41)
            continue;
        if (std::abs(candidate - tonic) < bestDistance)
        {
            best = candidate;
            bestDistance = std::abs(candidate - tonic);
        }
    }
    return best;
}

// The sample's bass segment at a time (kept here so the bass needs no analyzer code to link).
const SampleBassSegment* segmentAt(const SampleHarmony& harmony, double seconds)
{
    for (const auto& segment : harmony.bass)
        if (seconds >= segment.startSeconds && seconds < segment.endSeconds)
            return &segment;
    return nullptr;
}

bool nearSnare(int tickInBar)
{
    return std::abs(tickInBar - DnBGrid::kSnare2) <= 1 || std::abs(tickInBar - DnBGrid::kSnare4) <= 1;
}

//------------------------------------------------------------------------------
// Harmony: chord roots per half bar - from the sample when it speaks clearly, otherwise a
// DnB progression in the key (half-time harmonic rhythm: usually one chord per bar).
std::vector<int> planRoots(const DnBBassParams& params, const DnBSampleLens& lens, std::mt19937& rng, bool& sampleLed)
{
    const int slots = params.bars * 2;
    std::vector<int> roots(static_cast<size_t>(slots), params.keyRoot);
    sampleLed = lens.valid && lens.rootsFromSample;
    if (sampleLed)
    {
        int previous = params.keyRoot;
        for (int i = 0; i < slots; ++i)
        {
            const int pc = i < static_cast<int>(lens.halfBarRoot.size()) ? lens.halfBarRoot[static_cast<size_t>(i)] : -1;
            previous = pc >= 0 ? pc : previous;
            roots[static_cast<size_t>(i)] = previous;
        }
        return roots;
    }

    const auto sub = static_cast<DnBSubstyle>(params.substyle);
    const bool minimal = sub == DnBSubstyle::Neurofunk || sub == DnBSubstyle::JumpUp || sub == DnBSubstyle::Roller;
    const bool lush = sub == DnBSubstyle::Liquid;
    std::vector<std::pair<std::array<int, 4>, float>> progressions;
    if (params.scaleMode == 1)
        progressions = { { { 0, 0, 3, 4 }, 1.0f }, { { 0, 5, 3, 4 }, lush ? 1.4f : 0.8f }, { { 0, 4, 5, 3 }, lush ? 1.2f : 0.6f },
                         { { 0, 0, 0, 0 }, minimal ? 1.6f : 0.4f } };
    else
        progressions = { { { 0, 0, 5, 6 }, 1.0f }, { { 0, 5, 2, 6 }, lush ? 1.6f : 0.7f }, { { 0, 3, 0, 4 }, 0.6f },
                         { { 0, 0, 0, 5 }, minimal ? 1.4f : 0.6f }, { { 0, 6, 5, 6 }, sub == DnBSubstyle::Breakbeat ? 1.2f : 0.7f },
                         { { 0, 2, 5, 6 }, lush ? 1.3f : 0.5f }, { { 0, 0, 0, 0 }, minimal ? 1.6f : 0.3f } };
    const auto degrees = pickWeighted(rng, progressions);
    const auto& scale = scaleFor(params.scaleMode);
    for (int bar = 0; bar < params.bars; ++bar)
    {
        const int pc = (params.keyRoot + scale[static_cast<size_t>(degrees[static_cast<size_t>(bar % 4)])]) % 12;
        roots[static_cast<size_t>(bar * 2)] = pc;
        roots[static_cast<size_t>(bar * 2 + 1)] = pc;
    }
    // Liquid anticipates the next chord on the last half bar of a phrase now and then.
    if (lush && params.bars >= 2 && chance(rng, 0.35f))
        for (int bar = 1; bar < params.bars; bar += 2)
            roots[static_cast<size_t>(bar * 2 + 1)] = roots[static_cast<size_t>(((bar + 1) % params.bars) * 2)];
    return roots;
}

//------------------------------------------------------------------------------
// One onset of the bass grammar, before pitches are resolved.
struct Onset
{
    int tick = 0;            // in bar
    int interval = 0;        // semitones over the chord root sounding at that tick
    int length = 0;          // 0 = legato to the next onset
    int velocity = 100;
    bool glide = false;
    bool tension = false;
    bool anchor = false;     // structural: never thinned by the sample lens
    const char* role = "dnb_bass";
};

struct BarContext
{
    int bar = 0;
    bool answerBar = false;  // second bar of a 2-bar phrase
    bool phraseEnd = false;
    bool rootChangesMidBar = false;
    int rootInterval = 0;    // next half-bar root relative to this one (for approaches)
    std::vector<int> kicks;
    float busy = 1.0f;       // amount x density
    bool holdFromPrevious = false; // sustained archetypes: the previous note rings on through this downbeat
    int wobbleVariant = 0;   // 0 octave wub, 1 quarter pulse, 2 running eighths
    bool neuro = false;      // b2 tension stabs only in neurofunk
};

int twoStepKick(const BarContext& bar)
{
    for (const int k : bar.kicks)
        if (k >= 32 && k <= 46)
            return k;
    return -1;
}

std::vector<Onset> subReese(const BarContext& bar, std::mt19937& rng)
{
    // Played reese / sub parts hold one note for one to four bars; the movement is in the sound
    // and in the occasional bend (F -> A -> F as overlapping, gliding notes).
    std::vector<Onset> out;
    if (!bar.holdFromPrevious)
        out.push_back({ 0, 0, 0, randomInt(rng, 110, 120), false, false, true, "dnb_bass_anchor" });
    if (bar.rootChangesMidBar)
        out.push_back({ 32, 0, 0, randomInt(rng, 104, 114), chance(rng, 0.5f), false, true, "dnb_bass_change" });
    else if (bar.answerBar && chance(rng, 0.35f))
    {
        const int at = bar.holdFromPrevious ? 0 : 8;
        const int bend = pickWeighted<int>(rng, { { 4, 0.35f }, { 3, 0.25f }, { 5, 0.20f }, { 7, 0.20f } });
        out.push_back({ at, bend, 0, randomInt(rng, 100, 110), true, false, false, "dnb_bass_bend" });
        out.push_back({ at + 24, 0, 0, randomInt(rng, 100, 110), true, false, false, "dnb_bass_bend_back" });
    }
    const int anchor = twoStepKick(bar);
    if (anchor > 0 && anchor != 32 && bar.busy > 0.9f && chance(rng, 0.35f * bar.busy))
        out.push_back({ anchor, chance(rng, 0.25f) ? 12 : 0, 0, randomInt(rng, 104, 116), false, false, false, "dnb_bass_kick_lock" });
    if (bar.busy > 0.8f && chance(rng, 0.3f * bar.busy))
        out.push_back({ chance(rng, 0.5f) ? 24 : 56, chance(rng, 0.6f) ? 12 : 7, 4, randomInt(rng, 92, 104), false, false, false, "dnb_bass_offbeat" });
    if (bar.busy > 1.2f)
        for (const int k : bar.kicks)
            if (k != 0 && k != anchor && chance(rng, 0.5f))
                out.push_back({ k, 0, 4, randomInt(rng, 98, 110), false, false, false, "dnb_bass_kick_lock" });
    return out;
}

struct RollingMotif
{
    std::vector<int> ticks;
    std::vector<int> intervals;
    int length = 5; // 0 = legato
};

std::vector<Onset> rolling(const BarContext& bar, const RollingMotif& motif, std::mt19937& rng)
{
    std::vector<Onset> out;
    for (size_t i = 0; i < motif.ticks.size(); ++i)
    {
        const int tick = motif.ticks[i];
        if (tick != 0 && !chance(rng, std::clamp(0.35f + 0.55f * bar.busy, 0.0f, 1.0f)))
            continue;
        Onset o { tick, motif.intervals[i % motif.intervals.size()], motif.length, tick == 0 ? 116 : (tick % 8 == 0 ? 104 : 96), false, false, tick == 0,
                  tick == 0 ? "dnb_bass_anchor" : "dnb_bass_roll" };
        out.push_back(o);
    }
    // The answer bar leads into the next root.
    if (bar.answerBar && out.size() >= 3)
    {
        auto& last = out.back();
        last.interval = bar.rootInterval == 0 ? 12 : bar.rootInterval - (bar.rootInterval > 0 ? 2 : -2);
        last.role = "dnb_bass_approach";
    }
    return out;
}

std::vector<Onset> stab(const BarContext& bar, std::mt19937& rng, int seedVariant)
{
    std::vector<Onset> out;
    for (const int k : bar.kicks)
        if (k == 0 || chance(rng, 0.9f))
            out.push_back({ k, chance(rng, 0.8f) ? 0 : 12, 3, k == 0 ? 118 : randomInt(rng, 106, 116), false, false, k == 0,
                            k == 0 ? "dnb_bass_anchor" : "dnb_bass_kick_lock" });
    // Off-beat answers, different in the answer bar (call / response).
    std::mt19937 local(static_cast<std::mt19937::result_type>(seedVariant * 7919 + (bar.answerBar ? 17 : 3)));
    std::vector<std::pair<int, float>> offbeats { { 8, 1.0f }, { 10, 0.6f }, { 24, 1.0f }, { 26, 0.7f }, { 28, 0.8f },
                                                  { 36, 0.6f }, { 56, 1.0f }, { 58, 0.7f }, { 60, 0.8f } };
    const int count = std::clamp(static_cast<int>(std::lround(2.0f * bar.busy)), 1, 4);
    for (int n = 0; n < count && !offbeats.empty(); ++n)
    {
        const int tick = pickWeighted(local, offbeats);
        offbeats.erase(std::remove_if(offbeats.begin(), offbeats.end(), [tick](const auto& o) { return std::abs(o.first - tick) < 4; }), offbeats.end());
        if (std::any_of(out.begin(), out.end(), [tick](const Onset& o) { return std::abs(o.tick - tick) < 2; }))
            continue;
        const int interval = pickWeighted<int>(rng, { { 12, 0.35f }, { 7, 0.12f }, { -2, 0.15f }, { 2, 0.10f }, { 10, 0.08f },
                                                      { 0, 0.20f }, { 1, bar.neuro ? 0.12f : 0.0f } });
        out.push_back({ tick, interval, 3, randomInt(rng, 96, 110), false, interval == 1, false, interval == 1 ? "dnb_bass_tension" : "dnb_bass_answer" });
    }
    // A dive into the next phrase now and then.
    if (bar.phraseEnd && chance(rng, 0.35f))
        out.push_back({ 60, 12, 0, 104, false, false, false, "dnb_bass_dive" });
    return out;
}

std::vector<Onset> wobble(const BarContext& bar, std::mt19937& rng)
{
    std::vector<Onset> out;
    if (bar.wobbleVariant == 1)
    {
        // Straight quarter pulse, snares included (the sidechained "drop" bass).
        for (const int tick : { 0, 16, 32, 48 })
            out.push_back({ tick, bar.phraseEnd && tick == 48 ? 12 : 0, 13, tick == 0 ? 116 : 108, false, false, tick == 0,
                            tick == 0 ? "dnb_bass_anchor" : "dnb_bass_pulse" });
        return out;
    }
    if (bar.wobbleVariant == 2)
    {
        static const std::array<int, 8> shape { 0, -2, 0, 12, -2, 0, 2, 0 };
        for (int i = 0; i < 8; ++i)
            if (i == 0 || chance(rng, std::clamp(0.5f + 0.4f * bar.busy, 0.0f, 1.0f)))
                out.push_back({ i * 8, shape[static_cast<size_t>(i)], 7, i == 0 ? 114 : 100, false, false, i == 0,
                                i == 0 ? "dnb_bass_anchor" : "dnb_bass_run" });
        return out;
    }
    std::vector<int> ticks { 0, 8, 24, 32, 40, 56 };
    if (bar.busy > 1.2f)
    {
        ticks.push_back(12);
        ticks.push_back(44);
    }
    std::sort(ticks.begin(), ticks.end());
    static const std::array<std::array<int, 4>, 3> shapes {{ { 0, 12, 0, 12 }, { 0, 0, 12, 7 }, { 0, 12, 7, 12 } }};
    const auto& shape = shapes[static_cast<size_t>(randomInt(rng, 0, 2))];
    int index = 0;
    for (const int tick : ticks)
    {
        if (tick != 0 && tick != 32 && !chance(rng, std::clamp(0.4f + 0.5f * bar.busy, 0.0f, 1.0f)))
            continue;
        out.push_back({ tick, shape[static_cast<size_t>(index++ % 4)], 5, tick % 16 == 0 ? 114 : 102, false, false, tick == 0,
                        tick == 0 ? "dnb_bass_anchor" : "dnb_bass_wub" });
    }
    if (bar.phraseEnd)
        out.push_back({ 60, 12, 0, 106, false, false, false, "dnb_bass_dive" }); // dives into the downbeat
    return out;
}

std::vector<Onset> dubSub(const BarContext& bar, std::mt19937& rng)
{
    std::vector<Onset> out;
    if (!bar.holdFromPrevious)
        out.push_back({ 0, 0, 0, randomInt(rng, 112, 122), false, false, true, "dnb_bass_anchor" });
    if (bar.rootChangesMidBar)
        out.push_back({ 32, 0, 0, 108, chance(rng, 0.5f), false, true, "dnb_bass_change" });
    else if (chance(rng, 0.55f * std::min(1.0f, bar.busy)))
    {
        const int tick = pickWeighted<int>(rng, { { 24, 0.4f }, { 28, 0.2f }, { twoStepKick(bar) > 0 ? twoStepKick(bar) : 40, 0.4f } });
        const int interval = pickWeighted<int>(rng, { { 0, 0.5f }, { -5, 0.25f }, { 7, 0.15f }, { 12, 0.10f } });
        out.push_back({ tick, interval, 0, randomInt(rng, 100, 112), chance(rng, 0.45f), false, false, "dnb_bass_boom" });
    }
    return out;
}

std::vector<Onset> melodicSub(const BarContext& bar, int scaleMode, std::mt19937& rng)
{
    std::vector<Onset> out;
    const int third = scaleMode == 1 ? 4 : 3;
    out.push_back({ 0, 0, 0, randomInt(rng, 108, 118), false, false, true, "dnb_bass_anchor" });
    if (bar.rootChangesMidBar)
        out.push_back({ 32, 0, 0, 104, chance(rng, 0.3f), false, true, "dnb_bass_change" });
    else if (chance(rng, 0.7f))
        out.push_back({ chance(rng, 0.5f) ? 24 : 28, chance(rng, 0.5f) ? third : 7, 0, randomInt(rng, 96, 106), false, false, false, "dnb_bass_chord_tone" });
    if (!bar.rootChangesMidBar && chance(rng, 0.6f * bar.busy))
        out.push_back({ 40, chance(rng, 0.6f) ? 7 : 12, 0, randomInt(rng, 94, 104), false, false, false, "dnb_bass_chord_tone" });
    if (chance(rng, 0.7f))
    {
        // Step towards the next root from a scale step below / above.
        const int target = bar.rootInterval;
        const int approach = target + (chance(rng, 0.65f) ? -2 : 2);
        out.push_back({ chance(rng, 0.5f) ? 56 : 60, approach, 0, randomInt(rng, 88, 98), false, false, false, "dnb_bass_approach" });
    }
    return out;
}

//------------------------------------------------------------------------------
DnBBassArchetype pickArchetype(const DnBBassParams& params, std::mt19937& rng)
{
    using A = DnBBassArchetype;
    std::array<float, static_cast<size_t>(A::Count)> w {};
    switch (static_cast<DnBSubstyle>(params.substyle))
    {
        case DnBSubstyle::Roller:    w = { 0.20f, 0.55f, 0.15f, 0.00f, 0.00f, 0.10f }; break;
        case DnBSubstyle::Liquid:    w = { 0.35f, 0.10f, 0.00f, 0.00f, 0.10f, 0.45f }; break;
        case DnBSubstyle::Neurofunk: w = { 0.25f, 0.15f, 0.50f, 0.10f, 0.00f, 0.00f }; break;
        case DnBSubstyle::JumpUp:    w = { 0.10f, 0.10f, 0.25f, 0.55f, 0.00f, 0.00f }; break;
        case DnBSubstyle::Breakbeat: w = { 0.20f, 0.15f, 0.15f, 0.00f, 0.50f, 0.00f }; break;
        case DnBSubstyle::Modern:
        default:                     w = { 0.35f, 0.15f, 0.25f, 0.10f, 0.05f, 0.10f }; break;
    }
    // [1] Low favours the sustained archetypes, [3] Full the busy ones.
    const float sustained = params.amount == 0 ? 1.6f : params.amount == 2 ? 0.6f : 1.0f;
    const float busy = params.amount == 0 ? 0.5f : params.amount == 2 ? 1.6f : 1.0f;
    w[static_cast<size_t>(A::SubReese)] *= sustained;
    w[static_cast<size_t>(A::DubSub)] *= sustained;
    w[static_cast<size_t>(A::MelodicSub)] *= sustained;
    w[static_cast<size_t>(A::Rolling)] *= busy;
    w[static_cast<size_t>(A::Stab)] *= busy;
    w[static_cast<size_t>(A::Wobble)] *= busy;
    std::vector<std::pair<A, float>> options;
    for (int i = 0; i < static_cast<int>(A::Count); ++i)
        options.emplace_back(static_cast<A>(i), w[static_cast<size_t>(i)]);
    return pickWeighted(rng, options);
}

struct ArchetypeTargets
{
    float kickLock;
    float onsetsPerBar;
    float sustain;
    float repetition;
    bool shortNotes;
};

ArchetypeTargets targetsFor(DnBBassArchetype archetype)
{
    switch (archetype)
    {
        case DnBBassArchetype::SubReese: return { 0.30f, 1.0f, 0.92f, 0.70f, false };
        case DnBBassArchetype::Rolling: return { 0.45f, 6.0f, 0.55f, 0.85f, true };
        case DnBBassArchetype::Stab: return { 0.80f, 4.5f, 0.30f, 0.65f, true };
        case DnBBassArchetype::Wobble: return { 0.60f, 5.5f, 0.50f, 0.75f, true };
        case DnBBassArchetype::DubSub: return { 0.30f, 0.9f, 0.90f, 0.70f, false };
        case DnBBassArchetype::MelodicSub: return { 0.40f, 3.2f, 0.85f, 0.55f, false };
        default: break;
    }
    return { 0.5f, 3.0f, 0.6f, 0.7f, false };
}

float amountScale(int amount)
{
    return amount == 0 ? 0.65f : amount == 2 ? 1.45f : 1.0f;
}

DnBBassLine buildCandidate(const DnBBassParams& params, const DnBDrumFrame& drums, const DnBSampleLens& lens, std::mt19937& rng)
{
    DnBBassLine line;
    line.archetype = pickArchetype(params, rng);
    line.halfBarRoots = planRoots(params, lens, rng, line.sampleLed);
    const int tonic = tonicPitch(params.keyRoot);
    const float busy = amountScale(params.amount) * (0.7f + 0.6f * params.density);

    RollingMotif motif;
    {
        static const std::array<std::vector<int>, 4> motifs {
            std::vector<int> { 0, 12, 20, 28, 36, 44, 52, 60 },
            std::vector<int> { 0, 8, 20, 24, 32, 40, 52, 56 },
            std::vector<int> { 0, 4, 20, 28, 32, 36, 52, 60 },
            std::vector<int> { 0, 12, 24, 28, 40, 44, 56 }
        };
        static const std::array<std::vector<int>, 4> shapes {
            std::vector<int> { 0, 0, 12, 0, 7, 0, 12, 10 },
            std::vector<int> { 0, 12, 0, 12, 0, 7, 0, 12 },
            std::vector<int> { 0, 0, 0, 12, 0, 0, 7, 0 },
            std::vector<int> { 0, 7, 12, 7, 0, 10, 12, 7 }
        };
        if (chance(rng, 0.35f))
        {
            // Played lines: root / octave on 1, the dotted 8th, 3-and, 4 (snare included) - legato.
            const bool eighths = chance(rng, 0.4f);
            motif.ticks = eighths ? std::vector<int> { 0, 8, 16, 24, 32, 40, 48, 56 } : std::vector<int> { 0, 24, 40, 48 };
            motif.intervals = eighths ? std::vector<int> { 0, -2, 0, 12, -2, 0, 2, 0 } : std::vector<int> { 0, 12, 0, 12 };
            motif.length = 0;
        }
        else
        {
            motif.ticks = motifs[static_cast<size_t>(randomInt(rng, 0, 3))];
            motif.intervals = shapes[static_cast<size_t>(randomInt(rng, 0, 3))];
        }
    }
    const int stabVariant = randomInt(rng, 0, 9999);
    const int wobbleVariant = pickWeighted<int>(rng, { { 0, 0.4f }, { 1, 0.35f }, { 2, 0.25f } });
    const float holdChance = params.amount == 0 ? 0.70f : params.amount == 2 ? 0.20f : 0.45f;
    const bool sustainedArchetype = line.archetype == DnBBassArchetype::SubReese || line.archetype == DnBBassArchetype::DubSub;
    const bool rhythmicArchetype = line.archetype == DnBBassArchetype::Rolling || line.archetype == DnBBassArchetype::Wobble
        || line.archetype == DnBBassArchetype::Stab;
    int previousRoot = -1;

    for (int bar = 0; bar < params.bars; ++bar)
    {
        BarContext ctx;
        ctx.bar = bar;
        ctx.answerBar = bar % 2 == 1;
        ctx.phraseEnd = bar % 2 == 1 || bar == params.bars - 1;
        const int rootA = line.halfBarRoots[static_cast<size_t>(bar * 2)];
        const int rootB = line.halfBarRoots[static_cast<size_t>(bar * 2 + 1)];
        const int nextRoot = line.halfBarRoots[static_cast<size_t>(((bar + 1) % params.bars) * 2)];
        ctx.rootChangesMidBar = rootA != rootB;
        ctx.rootInterval = rootPitch(nextRoot, tonic) - rootPitch(rootB, tonic);
        ctx.kicks = bar < static_cast<int>(drums.kicks.size()) ? drums.kicks[static_cast<size_t>(bar)] : std::vector<int> { 0 };
        if (std::find(ctx.kicks.begin(), ctx.kicks.end(), 0) == ctx.kicks.end())
            ctx.kicks.insert(ctx.kicks.begin(), 0);
        ctx.busy = busy;
        ctx.wobbleVariant = wobbleVariant;
        ctx.neuro = params.substyle == static_cast<int>(DnBSubstyle::Neurofunk);
        ctx.holdFromPrevious = sustainedArchetype && bar > 0 && rootA == previousRoot && chance(rng, holdChance);
        previousRoot = rootB;

        std::vector<Onset> onsets;
        switch (line.archetype)
        {
            case DnBBassArchetype::SubReese: onsets = subReese(ctx, rng); break;
            case DnBBassArchetype::Rolling: onsets = rolling(ctx, motif, rng); break;
            case DnBBassArchetype::Stab: onsets = stab(ctx, rng, stabVariant); break;
            case DnBBassArchetype::Wobble: onsets = wobble(ctx, rng); break;
            case DnBBassArchetype::DubSub: onsets = dubSub(ctx, rng); break;
            case DnBBassArchetype::MelodicSub: onsets = melodicSub(ctx, params.scaleMode, rng); break;
            default: break;
        }

        // Phrase shapes from played parts: a run down to close the loop, a flick up at the end of
        // the 4th bar of an 8-bar loop.
        const bool lastBar = params.bars >= 4 && bar == params.bars - 1;
        if (lastBar && rhythmicArchetype && chance(rng, 0.6f))
        {
            onsets.erase(std::remove_if(onsets.begin(), onsets.end(), [](const Onset& o) { return o.tick >= 32; }), onsets.end());
            const bool down = chance(rng, 0.7f);
            const std::array<int, 4> run = down ? std::array<int, 4> { 12, 10, 7, 5 } : std::array<int, 4> { 0, 3, 5, 7 };
            for (int i = 0; i < 4; ++i)
                onsets.push_back({ 32 + i * 8, run[static_cast<size_t>(i)], 0, 100 + i * 2, false, false, false, "dnb_bass_fill" });
        }
        else if ((lastBar || (params.bars >= 8 && bar == 3)) && chance(rng, 0.45f)
                 && std::none_of(onsets.begin(), onsets.end(), [](const Onset& o) { return o.tick >= 56; }))
        {
            onsets.push_back({ 56, 12, 4, 104, false, false, false, "dnb_bass_flick" });
        }

        for (const auto& o : onsets)
        {
            // Sustained bass never starts on the backbone snare; rhythmic lines may (played DnB basses
            // pulse through the snare - the sidechain makes the room).
            if (o.tick != 0 && nearSnare(o.tick) && !rhythmicArchetype)
                continue;

            // The sample lens: where the sample's own low end is busy, the bass holds back;
            // where it breathes, the bass speaks. With a bass-heavy sample, follow its attacks.
            const int step = (bar * kBar + o.tick) / 4;
            if (lens.valid && !o.anchor && step < static_cast<int>(lens.lowEnergy.size()))
            {
                float keep = 1.0f;
                if (lens.mode == DnBSampleLens::Mode::Counter)
                    keep = std::clamp(0.35f + 0.8f * (1.0f - lens.lowEnergy[static_cast<size_t>(step)]), 0.2f, 1.0f);
                else if (lens.mode == DnBSampleLens::Mode::Support)
                {
                    const bool kickHere = std::find(ctx.kicks.begin(), ctx.kicks.end(), o.tick) != ctx.kicks.end();
                    keep = (lens.sampleBassOnset[static_cast<size_t>(step)] || kickHere) ? 1.0f : 0.45f;
                }
                if (!chance(rng, keep))
                    continue;
            }

            const int root = rootPitch(o.tick < 32 ? rootA : rootB, tonic);
            int pitch = root + o.interval;
            if (!o.tension)
                pitch = snapToScale(pitch, params.keyRoot, params.scaleMode);
            while (pitch < kLowestPitch) pitch += 12;
            while (pitch > kHighestPitch) pitch -= 12;

            DnBBassNote note;
            note.start = bar * kBar + o.tick;
            note.length = o.length;
            note.pitch = pitch;
            note.velocity = juce::jlimit(1, 127, o.velocity + randomInt(rng, -3, 3));
            note.glide = o.glide;
            note.tension = o.tension;
            note.role = o.role;
            line.notes.push_back(note);
        }
    }

    // Order, de-duplicate, then resolve lengths: legato to the next note, short notes keep their
    // length, a glide target makes the previous note ring into it.
    std::sort(line.notes.begin(), line.notes.end(), [](const DnBBassNote& a, const DnBBassNote& b) { return a.start < b.start; });
    line.notes.erase(std::unique(line.notes.begin(), line.notes.end(), [](const DnBBassNote& a, const DnBBassNote& b) { return a.start == b.start; }),
                     line.notes.end());
    const int loopEnd = params.bars * kBar;
    for (size_t i = 0; i < line.notes.size(); ++i)
    {
        auto& note = line.notes[i];
        const int next = i + 1 < line.notes.size() ? line.notes[i + 1].start : loopEnd;
        const int room = next - note.start;
        // A dive glides from the octave into the next downbeat: the next note slides.
        if (std::string_view(note.role) == "dnb_bass_dive" && i + 1 < line.notes.size())
            line.notes[i + 1].glide = true;
        const bool nextGlides = i + 1 < line.notes.size() && line.notes[i + 1].glide;
        if (nextGlides)
            note.length = room + 1; // still sounding when the next note arrives
        else if (note.length <= 0)
            note.length = std::max(2, room - 1);
        else
            note.length = std::max(2, std::min(note.length, room - 1));
        // Short-note archetypes duck under the backbone snare.
        if (line.archetype == DnBBassArchetype::Stab && !nextGlides)
        {
            const int tickInBar = note.start % kBar;
            for (const int snare : { DnBGrid::kSnare2, DnBGrid::kSnare4 })
                if (tickInBar < snare && tickInBar + note.length > snare - 1)
                    note.length = std::max(2, snare - 1 - tickInBar);
        }
    }
    return line;
}
} // namespace

//==============================================================================
DnBDrumFrame DnBDrumFrame::fromPattern(const DnBPattern& pattern)
{
    DnBDrumFrame frame;
    frame.bars = pattern.bars;
    frame.kicks.assign(static_cast<size_t>(pattern.bars), {});
    frame.snares.assign(static_cast<size_t>(pattern.bars), {});
    for (const auto& e : pattern.events)
    {
        if (e.bar < 0 || e.bar >= pattern.bars)
            continue;
        if (e.lane == TrackType::Kick && !e.ghost)
            frame.kicks[static_cast<size_t>(e.bar)].push_back(e.tick);
        else if (e.lane == TrackType::Snare && !e.ghost)
            frame.snares[static_cast<size_t>(e.bar)].push_back(e.tick);
    }
    for (auto& k : frame.kicks)
        std::sort(k.begin(), k.end());
    return frame;
}

DnBDrumFrame DnBDrumFrame::fromNotes(const std::vector<NoteEvent>& kickNotes, const std::vector<NoteEvent>& snareNotes, int bars)
{
    DnBDrumFrame frame;
    frame.bars = std::max(1, bars);
    frame.kicks.assign(static_cast<size_t>(frame.bars), {});
    frame.snares.assign(static_cast<size_t>(frame.bars), {});
    auto place = [&frame](const NoteEvent& note, std::vector<std::vector<int>>& target)
    {
        const int lattice = static_cast<int>(std::lround(note.gridTick / static_cast<double>(DnBGrid::kPpqPerTick)));
        const int bar = lattice / kBar;
        if (bar >= 0 && bar < frame.bars)
            target[static_cast<size_t>(bar)].push_back(lattice % kBar);
    };
    for (const auto& note : kickNotes)
        if (!note.isGhost)
            place(note, frame.kicks);
    for (const auto& note : snareNotes)
        if (!note.isGhost)
            place(note, frame.snares);
    for (auto& k : frame.kicks)
        std::sort(k.begin(), k.end());
    return frame;
}

DnBSampleLens DnBSampleLens::build(const SampleAwareGenerationContext& context, int bars, double patternBpm)
{
    DnBSampleLens lens;
    const int steps = std::max(1, bars) * 16;
    lens.halfBarRoot.assign(static_cast<size_t>(bars * 2), -1);
    lens.halfBarConfidence.assign(static_cast<size_t>(bars * 2), 0.0f);
    lens.lowEnergy.assign(static_cast<size_t>(steps), 0.0f);
    lens.sampleBassOnset.assign(static_cast<size_t>(steps), false);
    if (!context.enabled)
        return lens;

    // Low-end occupancy per step: the analyzer's feature map (already on the sample's grid).
    const auto& featureSteps = context.featureMap.steps;
    if (!featureSteps.empty())
    {
        float peak = 0.0f;
        for (const auto& s : featureSteps)
            peak = std::max(peak, s.low);
        // Feature steps are 16ths of the sample's own tempo; at double time two pattern steps share one.
        const double ratio = patternBpm > 20.0 && context.harmonyBpm > 20.0 ? context.harmonyBpm / patternBpm : 1.0;
        for (int i = 0; i < steps; ++i)
        {
            const auto featureIndex = static_cast<size_t>(std::floor(i * ratio)) % featureSteps.size();
            lens.lowEnergy[static_cast<size_t>(i)] = peak > 0.0f ? featureSteps[featureIndex].low / peak : 0.0f;
        }
        lens.valid = true;
    }

    // Harmony: the sample's bass notes, voted per half bar on the pattern grid.
    const auto& harmony = context.harmony;
    if (harmony.valid && !harmony.bass.empty() && context.harmonyBpm > 20.0)
    {
        lens.valid = true;
        const double stepSeconds = 15.0 / (patternBpm > 20.0 ? patternBpm : context.harmonyBpm); // pattern 16ths in real time
        const double origin = context.harmonyOriginSeconds;
        const double sampleEnd = harmony.bass.back().endSeconds;
        const double loop = sampleEnd - origin;
        auto wrap = [&](double seconds)
        {
            return loop > 0.1 && seconds >= sampleEnd ? origin + std::fmod(seconds - origin, loop) : seconds;
        };

        int confidentSegments = 0;
        float lowSum = 0.0f;
        for (const auto& segment : harmony.bass)
            if (segment.midiNote >= 0 && segment.confidence >= 0.35f)
            {
                ++confidentSegments;
                lowSum += segment.lowEnergy;
            }
        const float confidentShare = static_cast<float>(confidentSegments) / static_cast<float>(harmony.bass.size());
        lens.bassStrength = std::clamp(confidentShare * (confidentSegments > 0 ? lowSum / static_cast<float>(confidentSegments) : 0.0f) * 1.4f, 0.0f, 1.0f);

        int known = 0;
        for (int half = 0; half < bars * 2; ++half)
        {
            std::array<float, 12> votes {};
            for (int s = 0; s < 8; ++s)
            {
                const auto* segment = segmentAt(harmony, wrap(origin + (half * 8 + s + 0.5) * stepSeconds));
                if (segment != nullptr && segment->midiNote >= 0 && segment->confidence >= 0.25f)
                    votes[static_cast<size_t>(segment->midiNote % 12)] += segment->confidence;
            }
            const auto best = std::max_element(votes.begin(), votes.end());
            if (*best >= 1.5f) // at least ~a beat of confident bass
            {
                lens.halfBarRoot[static_cast<size_t>(half)] = static_cast<int>(best - votes.begin());
                lens.halfBarConfidence[static_cast<size_t>(half)] = std::min(1.0f, *best / 8.0f);
                ++known;
            }
        }
        lens.rootsFromSample = known >= bars; // at least half of the half bars

        int previousNote = -1;
        for (int i = 0; i < steps; ++i)
        {
            const auto* segment = segmentAt(harmony, wrap(origin + (i + 0.25) * stepSeconds));
            const int note = segment != nullptr && segment->confidence >= 0.35f ? segment->midiNote : -1;
            lens.sampleBassOnset[static_cast<size_t>(i)] = note >= 0 && note != previousNote;
            previousNote = note;
        }
    }

    if (lens.valid)
        lens.mode = lens.bassStrength >= 0.35f ? Mode::Support : Mode::Counter;
    return lens;
}

juce::String DnBSampleLens::describe() const
{
    if (!valid)
        return "no sample (key controls + style progression)";
    juce::String text = mode == Mode::Support ? "support (the sample has its own bass)" : "counter (fills the sample's low-end gaps)";
    text << " | bass strength " << juce::String(bassStrength, 2) << " | roots " << (rootsFromSample ? "from the sample" : "from the key");
    return text;
}

DnBBassScore DnBBassGenerator::score(const DnBBassLine& line, const DnBBassParams& params, const DnBDrumFrame& drums,
                                     const DnBSampleLens& lens)
{
    DnBBassScore s;
    const auto targets = targetsFor(line.archetype);
    const int bars = std::max(1, params.bars);
    const int loopEnd = bars * kBar;
    const int tonic = tonicPitch(params.keyRoot);

    std::set<int> onsets;
    for (const auto& n : line.notes)
        onsets.insert(n.start);

    // Kick lock: how many main kicks the bass meets (a bass hit within a 32nd).
    int kicks = 0;
    int locked = 0;
    for (int bar = 0; bar < bars && bar < static_cast<int>(drums.kicks.size()); ++bar)
        for (const int k : drums.kicks[static_cast<size_t>(bar)])
        {
            ++kicks;
            const int at = bar * kBar + k;
            locked += (onsets.count(at) || onsets.count(at - 1) || onsets.count(at + 1)) ? 1 : 0;
        }
    s.kickLock = kicks > 0 ? static_cast<float>(locked) / static_cast<float>(kicks) : 0.0f;
    s.kickLockFit = gaussianFit(s.kickLock, targets.kickLock, 0.25f);

    // Snare clash: bass attacks on the backbone.
    int clashes = 0;
    for (const auto& n : line.notes)
        if (n.start % kBar != 0 && nearSnare(n.start % kBar))
            ++clashes;
    s.snareClash = line.notes.empty() ? 1.0f : static_cast<float>(clashes) / static_cast<float>(line.notes.size());

    // Harmony: note-time on chord tones of the root sounding at that moment.
    float chordTime = 0.0f;
    float totalTime = 0.0f;
    int outOfKey = 0;
    for (const auto& n : line.notes)
    {
        const int half = std::clamp(n.start / 32, 0, bars * 2 - 1);
        const int root = rootPitch(line.halfBarRoots[static_cast<size_t>(half)], tonic);
        const int interval = ((n.pitch - root) % 12 + 12) % 12;
        const bool chordTone = interval == 0 || interval == 7 || interval == 3 || interval == 4 || interval == 10;
        const float duration = static_cast<float>(std::max(1, n.length));
        totalTime += duration;
        if (n.tension || std::string_view(n.role) == "dnb_bass_approach")
            chordTime += duration * 0.5f; // a passing tone is fine, but not the backbone of the line
        else if (chordTone)
            chordTime += duration;
        if (!n.tension && !inScale(n.pitch, params.keyRoot, params.scaleMode))
            ++outOfKey;
    }
    s.harmonic = totalTime > 0.0f ? chordTime / totalTime : 0.0f;

    // Density / sustain / repetition against the archetype (and the [1][2][3] amount).
    s.density = static_cast<float>(line.notes.size()) / static_cast<float>(bars);
    const float densityTarget = targets.onsetsPerBar * amountScale(params.amount);
    s.densityFit = gaussianFit(s.density, densityTarget, std::max(0.8f, 0.35f * densityTarget));
    float sounding = 0.0f;
    for (const auto& n : line.notes)
        sounding += static_cast<float>(std::min(n.length, loopEnd - n.start));
    s.sustain = std::clamp(sounding / static_cast<float>(loopEnd), 0.0f, 1.0f);
    s.sustainFit = gaussianFit(s.sustain, targets.sustain, 0.2f);

    if (bars > 1)
    {
        float similarity = 0.0f;
        for (int bar = 1; bar < bars; ++bar)
        {
            std::set<int> a;
            std::set<int> b;
            for (const auto& n : line.notes)
            {
                if (n.start / kBar == bar - 1) a.insert(n.start % kBar);
                if (n.start / kBar == bar) b.insert(n.start % kBar);
            }
            int both = 0;
            for (const int x : a) both += b.count(x) ? 1 : 0;
            const int either = static_cast<int>(a.size() + b.size()) - both;
            similarity += either > 0 ? static_cast<float>(both) / static_cast<float>(either) : 1.0f;
        }
        s.repetition = similarity / static_cast<float>(bars - 1);
    }
    else
    {
        s.repetition = targets.repetition;
    }
    s.repetitionFit = gaussianFit(s.repetition, targets.repetition, 0.2f);

    std::set<int> pitches;
    for (const auto& n : line.notes)
        pitches.insert(n.pitch);
    const bool sustained = line.archetype == DnBBassArchetype::SubReese || line.archetype == DnBBassArchetype::DubSub;
    s.variety = sustained ? 1.0f : std::min(1.0f, static_cast<float>(pitches.size()) / 2.0f);

    // Sample fit: the roots the sample plays, and its low-end gaps.
    if (lens.valid)
    {
        float agree = 0.0f;
        float weight = 0.0f;
        for (int half = 0; half < bars * 2 && half < static_cast<int>(lens.halfBarRoot.size()); ++half)
        {
            const int sampleRoot = lens.halfBarRoot[static_cast<size_t>(half)];
            if (sampleRoot < 0)
                continue;
            for (const auto& n : line.notes)
            {
                if (n.start / 32 != half)
                    continue;
                const int interval = ((n.pitch % 12) - sampleRoot + 12) % 12;
                const float w = static_cast<float>(n.length) * lens.halfBarConfidence[static_cast<size_t>(half)];
                agree += (interval == 0 || interval == 7 ? 1.0f : interval == 3 || interval == 4 || interval == 10 ? 0.6f : 0.0f) * w;
                weight += w;
            }
        }
        s.sampleRoots = weight > 0.0f ? agree / weight : 1.0f;

        float space = 0.0f;
        int counted = 0;
        for (const auto& n : line.notes)
        {
            const int step = n.start / 4;
            if (step >= static_cast<int>(lens.lowEnergy.size()) || n.start % kBar == 0)
                continue;
            ++counted;
            if (lens.mode == DnBSampleLens::Mode::Support)
                space += lens.sampleBassOnset[static_cast<size_t>(step)] ? 1.0f : 0.4f;
            else
                space += 1.0f - lens.lowEnergy[static_cast<size_t>(step)];
        }
        s.sampleSpace = counted > 0 ? space / static_cast<float>(counted) : 1.0f;
    }

    // Penalties: register, wild leaps, notes out of key without a reason.
    float penalties = 0.1f * static_cast<float>(outOfKey);
    for (size_t i = 0; i < line.notes.size(); ++i)
    {
        const auto& n = line.notes[i];
        if (n.pitch < kLowestPitch || n.pitch > kHighestPitch)
            penalties += 0.2f;
        if (i > 0 && std::abs(n.pitch - line.notes[i - 1].pitch) > 12)
            penalties += 0.1f;
    }
    if (line.notes.empty())
        penalties += 1.0f;
    s.penalties = penalties;

    s.passedGates = true;
    auto gate = [&s](bool ok, const char* name)
    {
        if (s.passedGates && !ok)
        {
            s.passedGates = false;
            s.failedGate = name;
        }
    };
    gate(!line.notes.empty() && line.notes.front().start == 0, "no root on the first downbeat");
    const bool rhythmic = line.archetype == DnBBassArchetype::Rolling || line.archetype == DnBBassArchetype::Wobble
        || line.archetype == DnBBassArchetype::Stab;
    gate(rhythmic || s.snareClash <= 0.12f, "bass attacks on the snare backbone");
    if (rhythmic && s.snareClash > 0.35f)
        s.penalties += s.snareClash - 0.35f;
    gate(s.harmonic >= 0.70f, "harmony");

    const std::array<std::pair<float, float>, 5> core {
        std::pair<float, float> { std::max(0.05f, s.kickLockFit), 1.5f },
        { std::max(0.05f, s.harmonic), 1.5f },
        { std::max(0.05f, s.densityFit), 1.2f },
        { std::max(0.05f, 1.0f - s.snareClash), 1.0f },
        { std::max(0.05f, s.sustainFit), 1.0f }
    };
    float logSum = 0.0f;
    float weightSum = 0.0f;
    for (const auto& [value, w] : core)
    {
        logSum += w * std::log(value);
        weightSum += w;
    }
    s.core = std::exp(logSum / weightSum);
    s.secondary = 0.30f * s.repetitionFit + 0.20f * s.variety + 0.30f * s.sampleRoots + 0.20f * s.sampleSpace;
    s.quality = 0.7f * s.core + 0.3f * s.secondary - s.penalties;
    return s;
}

DnBBassLine DnBBassGenerator::search(const DnBBassParams& params, const DnBDrumFrame& drums, const DnBSampleLens& lens,
                                     juce::String* report)
{
    const int count = std::clamp(params.candidateCount, 8, 256);
    std::vector<DnBBassLine> candidates;
    candidates.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        std::mt19937 rng(static_cast<std::mt19937::result_type>(static_cast<uint32_t>(params.seed) * 2246822519u
                                                                 + static_cast<uint32_t>(i) * 3266489917u + 0x42415353u));
        auto line = buildCandidate(params, drums, lens, rng);
        line.score = score(line, params, drums, lens);
        candidates.push_back(std::move(line));
    }

    std::vector<size_t> eligible;
    for (size_t i = 0; i < candidates.size(); ++i)
        if (candidates[i].score.passedGates)
            eligible.push_back(i);
    const int passed = static_cast<int>(eligible.size());
    if (eligible.empty())
        for (size_t i = 0; i < candidates.size(); ++i)
            eligible.push_back(i);

    float best = -1.0e9f;
    for (const auto i : eligible)
        best = std::max(best, candidates[i].score.quality);
    std::vector<size_t> pool;
    std::vector<double> weights;
    double total = 0.0;
    for (const auto i : eligible)
        if (candidates[i].score.quality >= best - params.nearBestTolerance)
        {
            pool.push_back(i);
            weights.push_back(std::exp((candidates[i].score.quality - best) / std::max(0.001f, params.temperature)));
            total += weights.back();
        }
    std::mt19937 selection(static_cast<std::mt19937::result_type>(static_cast<uint32_t>(params.seed) * 1103515245u + 0x53454c42u));
    double roll = std::uniform_real_distribution<double>(0.0, total)(selection);
    size_t chosen = pool.front();
    for (size_t k = 0; k < pool.size(); ++k)
    {
        roll -= weights[k];
        if (roll <= 0.0)
        {
            chosen = pool[k];
            break;
        }
    }

    if (report != nullptr)
    {
        const auto& line = candidates[chosen];
        const auto& s = line.score;
        static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        juce::String roots;
        for (size_t i = 0; i < line.halfBarRoots.size(); i += 2)
            roots << (i > 0 ? " " : "") << names[line.halfBarRoots[i] % 12];
        *report = "DNB BASS\narchetype: " + juce::String(toString(line.archetype)) + " | amount " + juce::String(params.amount + 1)
                + " | roots per bar: " + roots + (line.sampleLed ? " (from the sample)" : " (key progression)")
                + "\nsample lens: " + lens.describe()
                + "\ncandidates " + juce::String(count) + " | passed gates " + juce::String(passed) + " | pool " + juce::String(static_cast<int>(pool.size()))
                + " | Q " + juce::String(s.quality, 3) + " (core " + juce::String(s.core, 2) + ": kick lock " + juce::String(s.kickLock, 2)
                + ", harmony " + juce::String(s.harmonic, 2) + ", density " + juce::String(s.density, 1) + "/bar, sustain " + juce::String(s.sustain, 2)
                + "; sample roots " + juce::String(s.sampleRoots, 2) + ", sample space " + juce::String(s.sampleSpace, 2) + ")";
    }
    return std::move(candidates[chosen]);
}

std::vector<NoteEvent> DnBBassGenerator::toNotes(const DnBBassLine& line)
{
    std::vector<NoteEvent> notes;
    for (const auto& n : line.notes)
    {
        NoteEvent note;
        note.pitch = n.pitch;
        note.gridTick = n.start * DnBGrid::kPpqPerTick;
        note.timingOffsetTicks = 0;
        note.lengthTicks = std::max(TimingGrid::ThirtySecond / 2, n.length * DnBGrid::kPpqPerTick - 12);
        note.velocity = n.velocity;
        note.isSlide = n.glide;
        note.semanticRole = n.role;
        notes.push_back(note);
    }
    return notes;
}
} // namespace bbg
