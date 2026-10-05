#include "TechnoBass.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace bbg
{
namespace
{
using namespace TechnoGrid;

constexpr float kSidechainTau = 1.1f; // steps
constexpr int kBreath = 20;           // PPQ of air before the next attack

bool chance(std::mt19937& rng, float p)
{
    return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) < p;
}

template <size_t N>
int pickWeighted(std::mt19937& rng, const std::array<float, N>& weights)
{
    float total = 0.0f;
    for (const float w : weights)
        total += std::max(0.0f, w);
    if (total <= 0.0f)
        return 0;
    float roll = std::uniform_real_distribution<float>(0.0f, total)(rng);
    for (size_t i = 0; i < N; ++i)
    {
        roll -= std::max(0.0f, weights[i]);
        if (roll <= 0.0f)
            return static_cast<int>(i);
    }
    return static_cast<int>(N) - 1;
}

// g(d) = 1 - exp(-d / tau): the sidechain envelope after a kick.
float sidechain(int d)
{
    return 1.0f - std::exp(-static_cast<float>(d) / kSidechainTau);
}

// Degrees I, VIII, V, b7, 3, IV, b2 in semitones over the bar root.
int degreeSemitones(int degree, int scaleMode)
{
    static constexpr std::array<int, 7> minor { 0, 12, 7, 10, 3, 5, 1 };
    const int value = minor[static_cast<size_t>(std::clamp(degree, 0, 6))];
    return degree == 4 && scaleMode == 1 ? 4 : value;
}

// P(j | i) = rho * delta(j, i) + (1 - rho) * pi_j.
int nextDegree(std::mt19937& rng, const TechnoStyleProfile& style, int previous, float rho)
{
    if (previous >= 0 && chance(rng, rho))
        return previous;
    return pickWeighted(rng, style.degreeDistribution);
}

struct MotifNote
{
    int step = 0;          // inside the motif (0..15, Acid 0..31)
    int degree = 0;
    bool octave = false;
    int lengthTicks = 0;   // 0 = legato until the next note
    float velocity = 100.0f;
    bool accent = false;
    bool glide = false;
};

struct Motif
{
    int steps = 16;
    std::vector<MotifNote> notes;
};

// Root progressions (section 5.3): one root per 4 bars.
std::vector<int> progressionOffsets(std::mt19937& rng, int scaleMode)
{
    const int six = scaleMode == 1 ? 9 : 8;
    const std::array<float, 5> weights { 0.60f, 0.12f, 0.12f, 0.10f, 0.06f };
    switch (pickWeighted(rng, weights))
    {
        case 1: return { 0, six };
        case 2: return { 0, 10 };
        case 3: return { 0, 5 };
        case 4: return { 0, 7 };
        default: return { 0 };
    }
}

Motif buildMotif(TechnoBassArchetype archetype, const TechnoStyleProfile& style, int amount, float density, std::mt19937& rng)
{
    Motif m;
    const float rho = style.degreeInertia;
    const float rootFirst = style.degreeDistribution[0] + 0.15f;
    int previous = 0;
    auto degreeAt = [&](bool groupStart)
    {
        previous = groupStart ? (chance(rng, rootFirst) ? 0 : nextDegree(rng, style, previous, rho)) : nextDegree(rng, style, previous, rho);
        return previous;
    };

    switch (archetype)
    {
        case TechnoBassArchetype::Offbeat:
        {
            const int length = chance(rng, 0.5f) ? 300 : 360; // 0.6 / 0.75 of an eighth
            for (int beat = 0; beat < 4; ++beat)
            {
                if (amount == 0 && beat % 2 == 1)
                    continue;
                m.notes.push_back({ beat * 4 + 2, degreeAt(beat == 0), false, length, 104.0f });
                if (amount == 2 && chance(rng, 0.5f))
                    m.notes.push_back({ beat * 4 + 3, degreeAt(false), false, 180, 84.0f });
            }
            break;
        }
        case TechnoBassArchetype::Rolling:
        {
            // Variants of d in {1, 2, 3}: 123, 23, 13, 12; v = v0 * 0.85^(d - 1).
            const std::array<std::vector<int>, 4> variants { { { 1, 2, 3 }, { 2, 3 }, { 1, 3 }, { 1, 2 } } };
            int variant = pickWeighted(rng, std::array<float, 4> { 0.45f, 0.25f, 0.15f, 0.15f });
            if (amount == 0)
                variant = 1;
            else if (amount == 2)
                variant = 0;
            for (int beat = 0; beat < 4; ++beat)
                for (const int d : variants[static_cast<size_t>(variant)])
                    m.notes.push_back({ beat * 4 + d, degreeAt(d == variants[static_cast<size_t>(variant)].front()), false, 200,
                                        104.0f * std::pow(0.85f, static_cast<float>(d - 1)) });
            // Rolling basses repeat their note: re-draw with higher inertia.
            for (size_t i = 1; i < m.notes.size(); ++i)
                if (chance(rng, 0.5f))
                    m.notes[i].degree = m.notes[i - 1].degree;
            break;
        }
        case TechnoBassArchetype::Rumble:
        {
            const std::vector<int> ds = amount == 0 ? std::vector<int> { 1, 2 } : std::vector<int> { 1, 2, 3 };
            for (int beat = 0; beat < 4; ++beat)
                for (const int d : ds)
                    m.notes.push_back({ beat * 4 + d, 0, false, 0, 70.0f * std::pow(0.8f, static_cast<float>(d - 1)) });
            break;
        }
        case TechnoBassArchetype::Acid:
        {
            m.steps = 32;
            const float pOn = std::clamp(0.5f + 0.15f * static_cast<float>(amount - 1) + 0.2f * (density - 0.5f), 0.3f, 0.85f);
            for (int s = 0; s < 32; ++s)
            {
                if (s % 4 == 0 || !chance(rng, pOn))
                    continue;
                MotifNote n { s, degreeAt(s % 16 == 1 || s % 16 == 2), chance(rng, 0.22f), 200, 0.0f };
                n.accent = chance(rng, 0.28f);
                n.velocity = n.accent ? 118.0f : 84.0f;
                m.notes.push_back(n);
            }
            // At least a few notes per bar, then slides into a directly following note.
            for (int bar = 0; bar < 2; ++bar)
            {
                int count = static_cast<int>(std::count_if(m.notes.begin(), m.notes.end(), [bar](const MotifNote& n) { return n.step / 16 == bar; }));
                for (const int s : { 2, 6, 10, 14, 3, 11 })
                {
                    if (count >= 5)
                        break;
                    const int step = bar * 16 + s;
                    if (std::none_of(m.notes.begin(), m.notes.end(), [step](const MotifNote& n) { return n.step == step; }))
                    {
                        m.notes.push_back({ step, degreeAt(false), false, 200, 84.0f });
                        ++count;
                    }
                }
            }
            std::sort(m.notes.begin(), m.notes.end(), [](const MotifNote& a, const MotifNote& b) { return a.step < b.step; });
            for (size_t i = 0; i + 1 < m.notes.size(); ++i)
                if (m.notes[i + 1].step == m.notes[i].step + 1 && chance(rng, 0.18f / 0.5f))
                {
                    m.notes[i].glide = true;
                    m.notes[i].lengthTicks = 0;
                }
            break;
        }
        case TechnoBassArchetype::Pulse:
        {
            std::vector<int> steps = amount == 0 ? std::vector<int> { 2 } : amount == 2 ? std::vector<int> { 2, 6, 10, 14 } : std::vector<int> { 2, 10 };
            const int length = amount == 2 ? 420 : (chance(rng, 0.5f) ? 960 : 1200);
            for (size_t i = 0; i < steps.size(); ++i)
                m.notes.push_back({ steps[i], i == 0 ? 0 : degreeAt(false), false, length, 100.0f });
            break;
        }
        case TechnoBassArchetype::Dub:
        {
            std::vector<int> steps = amount == 0 ? std::vector<int> { 2, 10 } : amount == 2 ? std::vector<int> { 2, 7, 10, 14 } : std::vector<int> { 2, 7, 10 };
            for (size_t i = 0; i < steps.size(); ++i)
                m.notes.push_back({ steps[i], i == 0 ? 0 : degreeAt(false), false, 0, i == 0 ? 104.0f : 92.0f });
            break;
        }
        default:
            break;
    }
    std::sort(m.notes.begin(), m.notes.end(), [](const MotifNote& a, const MotifNote& b) { return a.step < b.step; });
    return m;
}

TechnoBassLine render(TechnoBassArchetype archetype, const Motif& motif, const TechnoBassParams& params, const TechnoStyleProfile& style,
                      const std::vector<std::vector<int>>& kickSteps, const std::vector<int>& barRoots, std::mt19937& rng)
{
    TechnoBassLine line;
    line.archetype = archetype;
    line.barRoots = barRoots;
    const int bars = params.bars;
    const char* role = archetype == TechnoBassArchetype::Acid ? "techno_bass_acid"
                     : archetype == TechnoBassArchetype::Rumble ? "techno_bass_rumble"
                     : archetype == TechnoBassArchetype::Rolling ? "techno_bass_rolling"
                     : "techno_bass";

    auto isKick = [&](int bar, int step)
    {
        const auto& kicks = kickSteps[static_cast<size_t>(bar)];
        return std::find(kicks.begin(), kicks.end(), step) != kicks.end();
    };
    // d(s): 16ths since the last kick (back into the previous bar).
    auto stepsSinceKick = [&](int bar, int step)
    {
        for (int back = 0; back < 32; ++back)
        {
            int b = bar;
            int s = step - back;
            while (s < 0)
            {
                s += kStepsPerBar;
                b = (b - 1 + bars) % bars;
            }
            if (isKick(b, s))
                return back;
        }
        return step % 4;
    };

    struct Placed
    {
        int step;
        int degree;
        bool octave;
        int lengthTicks;
        float velocity;
        bool accent;
        bool glide;
        int bar;
    };
    std::vector<Placed> placed;
    for (int bar = 0; bar < bars; ++bar)
    {
        const bool fill = bars >= 4 && bar == bars - 1;
        const bool variation = !fill && bar % 4 == 3;
        const int motifBar = motif.steps > 16 ? bar % 2 : 0;
        std::vector<MotifNote> notes;
        for (const auto& n : motif.notes)
            if (n.step / 16 == motifBar)
            {
                auto copy = n;
                copy.step %= 16;
                notes.push_back(copy);
            }

        // A': one or two small mutations (drop / octave flip / one 16th later).
        if (variation && !notes.empty())
        {
            const int ops = chance(rng, 0.5f) ? 2 : 1;
            for (int op = 0; op < ops; ++op)
            {
                const auto index = static_cast<size_t>(std::uniform_int_distribution<int>(0, static_cast<int>(notes.size()) - 1)(rng));
                const int kind = std::uniform_int_distribution<int>(0, 2)(rng);
                if (kind == 0 && notes.size() > 2)
                    notes.erase(notes.begin() + static_cast<std::ptrdiff_t>(index));
                else if (kind == 1)
                    notes[index].octave = !notes[index].octave;
                else if (notes[index].step + 1 < kStepsPerBar && (notes[index].step + 1) % 4 != 0)
                    notes[index].step += 1;
            }
        }
        // F: the last beat becomes an approach (V or b7) into the next phrase's root.
        if (fill)
        {
            notes.erase(std::remove_if(notes.begin(), notes.end(), [](const MotifNote& n) { return n.step >= 12; }), notes.end());
            notes.push_back({ 14, chance(rng, 0.5f) ? 2 : 3, false, 360, 106.0f, true, false });
        }

        for (const auto& n : notes)
        {
            if (isKick(bar, n.step))
                continue; // sidechain gate: never with the kick
            placed.push_back({ bar * kStepsPerBar + n.step, n.degree, n.octave, n.lengthTicks, n.velocity, n.accent, n.glide, bar });
        }
    }
    std::sort(placed.begin(), placed.end(), [](const Placed& a, const Placed& b) { return a.step < b.step; });
    placed.erase(std::unique(placed.begin(), placed.end(), [](const Placed& a, const Placed& b) { return a.step == b.step; }), placed.end());

    const int totalTicks = bars * kStepsPerBar * kPpqPerStep;
    for (size_t i = 0; i < placed.size(); ++i)
    {
        const auto& p = placed[i];
        const int bar = p.bar;
        const int rootPc = ((barRoots[static_cast<size_t>(bar)] % 12) + 12) % 12;
        const int rootPitch = style.bassLow + ((rootPc - style.bassLow % 12) + 12) % 12;
        int pitch = rootPitch + degreeSemitones(p.degree, params.scaleMode) + (p.octave ? 12 : 0);
        while (pitch > style.bassHigh)
            pitch -= 12;

        const int start = p.step * kPpqPerStep;
        const int next = i + 1 < placed.size() ? placed[i + 1].step * kPpqPerStep : totalTicks;
        TechnoBassNote note;
        note.step = p.step;
        note.pitch = pitch;
        note.glide = p.glide && i + 1 < placed.size() && placed[i + 1].step == p.step + 1;
        note.lengthTicks = note.glide ? next - start + 30 : (p.lengthTicks == 0 ? next - start - kBreath : std::min(p.lengthTicks, next - start - kBreath));
        note.lengthTicks = std::max(60, note.lengthTicks);
        const int d = stepsSinceKick(bar, p.step % kStepsPerBar);
        const float duck = archetype == TechnoBassArchetype::Acid ? 0.8f + 0.2f * sidechain(d) : 0.55f + 0.45f * sidechain(d);
        note.velocity = juce::jlimit(1, 127, juce::roundToInt(p.velocity * duck));
        note.accent = p.accent;
        note.role = role;
        line.notes.push_back(note);
    }
    return line;
}

void scoreLine(TechnoBassLine& line, const TechnoBassParams& params, const TechnoStyleProfile& style,
               const std::vector<std::vector<int>>& kickSteps, const TechnoSampleLens& lens)
{
    auto fit = [](float x, float target, float sigma) { return std::exp(-(x - target) * (x - target) / (2.0f * sigma * sigma)); };
    const int bars = params.bars;
    const auto count = static_cast<float>(line.notes.size());

    int clashes = 0;
    int onRoot = 0;
    for (const auto& n : line.notes)
    {
        const int bar = n.step / kStepsPerBar;
        const auto& kicks = kickSteps[static_cast<size_t>(bar)];
        clashes += std::find(kicks.begin(), kicks.end(), n.step % kStepsPerBar) != kicks.end() ? 1 : 0;
        onRoot += (n.pitch % 12) == ((line.barRoots[static_cast<size_t>(bar)] % 12 + 12) % 12) ? 1 : 0;
    }
    line.kickClash = count > 0.0f ? static_cast<float>(clashes) / count : 0.0f;
    line.rootShare = count > 0.0f ? static_cast<float>(onRoot) / count : 0.0f;
    const float rootTarget = std::min(0.95f, style.degreeDistribution[0] + style.degreeDistribution[1]
                                                 + (line.archetype == TechnoBassArchetype::Rumble ? 0.25f : 0.0f));
    line.rootFit = fit(line.rootShare, rootTarget, 0.18f);

    // Notes per bar against the archetype's norm, scaled by the [1][2][3] amount.
    static constexpr std::array<float, 6> perBar { 4.0f, 10.0f, 10.0f, 9.0f, 2.0f, 3.0f };
    static constexpr std::array<float, 3> amountFactor { 0.55f, 1.0f, 1.35f };
    const float target = perBar[static_cast<size_t>(line.archetype)] * amountFactor[static_cast<size_t>(std::clamp(params.amount, 0, 2))];
    line.densityFit = fit(count / static_cast<float>(bars), target, 0.35f * target + 0.5f);

    // Repetition: Jaccard of onset positions of neighbouring bars.
    auto onsets = [&](int bar)
    {
        std::set<int> set;
        for (const auto& n : line.notes)
            if (n.step / kStepsPerBar == bar)
                set.insert(n.step % kStepsPerBar);
        return set;
    };
    float sum = 0.0f;
    for (int bar = 1; bar < bars; ++bar)
    {
        const auto a = onsets(bar - 1);
        const auto b = onsets(bar);
        int inter = 0;
        for (const int x : a)
            inter += b.count(x) > 0 ? 1 : 0;
        const int uni = static_cast<int>(a.size() + b.size()) - inter;
        sum += uni > 0 ? static_cast<float>(inter) / static_cast<float>(uni) : 1.0f;
    }
    line.repetition = bars > 1 ? sum / static_cast<float>(bars - 1) : 1.0f;
    line.repetitionFit = fit(line.repetition, line.archetype == TechnoBassArchetype::Acid ? 0.6f : 0.82f, 0.18f);

    // A sample with its own strong bass wants a bass that leaves it room.
    const bool sparse = line.archetype == TechnoBassArchetype::Offbeat || line.archetype == TechnoBassArchetype::Pulse
        || line.archetype == TechnoBassArchetype::Rumble || line.archetype == TechnoBassArchetype::Dub;
    line.sampleFit = lens.valid && lens.bassStrength > 0.5f && !sparse ? 0.6f : 1.0f;

    line.passedGates = line.kickClash == 0.0f && !line.notes.empty();
    const float product = std::max(0.01f, line.rootFit) * std::max(0.01f, line.densityFit) * std::max(0.01f, line.repetitionFit)
        * line.sampleFit;
    // The style's archetype prior: a well-fitting line of a rare archetype must not beat the
    // style's own bass (Q = fit^(1/4) * (0.6 + 0.4 w_a / w_max)).
    const auto& weights = style.bassArchetypeWeights;
    const float maxWeight = std::max(0.001f, *std::max_element(weights.begin(), weights.end()));
    const float prior = 0.6f + 0.4f * weights[static_cast<size_t>(line.archetype)] / maxWeight;
    line.quality = std::pow(product, 1.0f / 4.0f) * prior;
}
} // namespace

const char* toString(TechnoBassArchetype archetype)
{
    switch (archetype)
    {
        case TechnoBassArchetype::Offbeat: return "Offbeat";
        case TechnoBassArchetype::Rolling: return "Rolling";
        case TechnoBassArchetype::Rumble: return "Rumble";
        case TechnoBassArchetype::Acid: return "Acid";
        case TechnoBassArchetype::Pulse: return "Pulse";
        case TechnoBassArchetype::Dub: return "Dub";
        default: break;
    }
    return "Bass";
}

TechnoSampleLens TechnoSampleLens::build(const SampleAwareGenerationContext& context, int bars, double patternBpm)
{
    TechnoSampleLens lens;
    const auto& harmony = context.harmony;
    if (!context.enabled || !harmony.valid || context.harmonyBpm <= 20.0 || bars <= 0)
        return lens;

    // The pattern may run at an octave of the sample's tempo: map with that grid.
    double gridBpm = context.harmonyBpm;
    if (patternBpm > 20.0)
        gridBpm *= std::pow(2.0, std::round(std::log2(patternBpm / gridBpm)));
    const double barSeconds = 240.0 / gridBpm;
    const double origin = context.harmonyOriginSeconds;
    double end = origin;
    for (const auto& n : harmony.lines.bass)
        end = std::max(end, n.endSeconds);
    if (!harmony.bass.empty())
        end = std::max(end, harmony.bass.back().endSeconds);
    const int loopBars = std::max(1, static_cast<int>(std::lround((end - origin) / barSeconds)));

    float covered = 0.0f;
    for (int bar = 0; bar < bars; ++bar)
    {
        const double from = origin + (bar % loopBars) * barSeconds;
        const double to = from + barSeconds;
        std::array<double, 12> heard {};
        for (const auto& n : harmony.lines.bass)
            heard[static_cast<size_t>(n.midiNote % 12)] += std::max(0.0, std::min(to, n.endSeconds) - std::max(from, n.startSeconds));
        const auto best = std::max_element(heard.begin(), heard.end());
        int root = -1;
        if (*best >= 0.25 * barSeconds)
            root = static_cast<int>(std::distance(heard.begin(), best));
        else
            for (const auto& segment : harmony.bass) // the per-beat estimate at the bar start
                if (from + 0.05 >= segment.startSeconds && from + 0.05 < segment.endSeconds)
                {
                    if (segment.midiNote >= 0 && segment.confidence >= 0.35f)
                        root = segment.midiNote % 12;
                    break;
                }
        lens.barRoot.push_back(root);
        double sounding = 0.0;
        for (const double h : heard)
            sounding += h;
        covered += static_cast<float>(std::min(1.0, sounding / barSeconds));
    }
    lens.bassStrength = covered / static_cast<float>(bars);
    lens.valid = std::any_of(lens.barRoot.begin(), lens.barRoot.end(), [](int r) { return r >= 0; });
    return lens;
}

juce::String TechnoSampleLens::describe() const
{
    if (!valid)
        return "sample lens: none";
    static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    juce::String roots;
    for (const int r : barRoot)
        roots << (roots.isEmpty() ? "" : " ") << (r >= 0 ? names[r] : "-");
    return "sample lens: roots " + roots + " | sample bass " + juce::String(bassStrength, 2);
}

TechnoBassLine TechnoBassGenerator::search(const TechnoBassParams& params, const std::vector<std::vector<int>>& kickSteps,
                                           const TechnoSampleLens& lens, juce::String* report)
{
    const auto& style = getTechnoStyleProfile(params.substyle);
    const int bars = std::clamp(params.bars, 1, 16);
    auto p = params;
    p.bars = bars;
    std::vector<std::vector<int>> kicks = kickSteps;
    kicks.resize(static_cast<size_t>(bars));

    auto weights = style.bassArchetypeWeights;
    if (lens.valid && lens.bassStrength > 0.5f)
    {
        // The sample already has a bass: favour the sparse archetypes (section 5.5).
        weights[static_cast<size_t>(TechnoBassArchetype::Offbeat)] *= 1.6f;
        weights[static_cast<size_t>(TechnoBassArchetype::Pulse)] *= 1.6f;
        weights[static_cast<size_t>(TechnoBassArchetype::Rumble)] *= 1.4f;
        weights[static_cast<size_t>(TechnoBassArchetype::Rolling)] *= 0.5f;
        weights[static_cast<size_t>(TechnoBassArchetype::Acid)] *= 0.6f;
    }

    std::vector<TechnoBassLine> candidates;
    const int count = std::clamp(params.candidateCount, 4, 128);
    for (int i = 0; i < count; ++i)
    {
        std::mt19937 rng(static_cast<std::mt19937::result_type>(static_cast<uint32_t>(params.seed) * 2246822519u
                                                                 + static_cast<uint32_t>(i) * 3266489917u + 0x7ec4u));
        const auto archetype = static_cast<TechnoBassArchetype>(pickWeighted(rng, weights));
        // Roots per bar: the sample's where known, else the key with a progression.
        const auto offsets = progressionOffsets(rng, params.scaleMode);
        std::vector<int> roots;
        bool fromSample = false;
        for (int bar = 0; bar < bars; ++bar)
        {
            int root = (params.keyRoot + offsets[static_cast<size_t>((bar / 4) % static_cast<int>(offsets.size()))]) % 12;
            if (lens.valid && bar < static_cast<int>(lens.barRoot.size()) && lens.barRoot[static_cast<size_t>(bar)] >= 0)
            {
                root = lens.barRoot[static_cast<size_t>(bar)];
                fromSample = true;
            }
            roots.push_back(root);
        }
        const auto motif = buildMotif(archetype, style, std::clamp(params.amount, 0, 2), params.density, rng);
        auto line = render(archetype, motif, p, style, kicks, roots, rng);
        line.sampleRoots = fromSample;
        scoreLine(line, p, style, kicks, lens);
        candidates.push_back(std::move(line));
    }

    std::vector<size_t> eligible;
    for (size_t i = 0; i < candidates.size(); ++i)
        if (candidates[i].passedGates)
            eligible.push_back(i);
    if (eligible.empty())
        for (size_t i = 0; i < candidates.size(); ++i)
            eligible.push_back(i);
    float best = -1.0f;
    for (const auto i : eligible)
        best = std::max(best, candidates[i].quality);
    std::vector<size_t> pool;
    std::vector<double> w;
    double total = 0.0;
    for (const auto i : eligible)
        if (candidates[i].quality >= best - params.nearBestTolerance)
        {
            pool.push_back(i);
            w.push_back(std::exp((candidates[i].quality - best) / std::max(0.001f, params.temperature)));
            total += w.back();
        }
    std::mt19937 selection(static_cast<std::mt19937::result_type>(static_cast<uint32_t>(params.seed) * 747796405u + 0x7ba5u));
    double roll = std::uniform_real_distribution<double>(0.0, total)(selection);
    size_t chosen = pool.front();
    for (size_t k = 0; k < pool.size(); ++k)
    {
        roll -= w[k];
        if (roll <= 0.0)
        {
            chosen = pool[k];
            break;
        }
    }

    auto& line = candidates[chosen];
    if (report != nullptr)
    {
        static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        juce::String roots;
        for (const int r : line.barRoots)
            roots << (roots.isEmpty() ? "" : " ") << names[(r % 12 + 12) % 12];
        *report = "TECHNO BASS\narchetype " + juce::String(toString(line.archetype)) + " | amount " + juce::String(params.amount + 1)
            + " | notes " + juce::String(static_cast<int>(line.notes.size())) + " | roots " + roots + (line.sampleRoots ? " (from sample)" : "")
            + "\nQ " + juce::String(line.quality, 3) + " = root " + juce::String(line.rootShare, 2) + " (fit " + juce::String(line.rootFit, 2)
            + ") density fit " + juce::String(line.densityFit, 2) + " repetition " + juce::String(line.repetition, 2)
            + " (fit " + juce::String(line.repetitionFit, 2) + ") sample " + juce::String(line.sampleFit, 2)
            + " | kick clashes " + juce::String(line.kickClash, 2) + " | pool " + juce::String(static_cast<int>(pool.size())) + "/" + juce::String(count)
            + "\n" + lens.describe();
    }
    return line;
}

std::vector<NoteEvent> TechnoBassGenerator::toNotes(const TechnoBassLine& line)
{
    std::vector<NoteEvent> notes;
    for (size_t i = 0; i < line.notes.size(); ++i)
    {
        const auto& n = line.notes[i];
        NoteEvent e;
        e.gridTick = n.step * kPpqPerStep;
        e.lengthTicks = n.lengthTicks;
        e.pitch = n.pitch;
        e.velocity = n.velocity;
        e.glideToNext = n.glide;
        e.isSlide = i > 0 && line.notes[i - 1].glide;
        e.semanticRole = juce::String(n.role) + (n.accent ? "|accent" : "");
        notes.push_back(e);
    }
    return notes;
}
} // namespace bbg
