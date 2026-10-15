#include "AtmosphericBass.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <set>

namespace bbg
{
namespace
{
constexpr int kSteps = AtmosGrid::kStepsPerBar;

float uniform(std::mt19937& rng)
{
    return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng);
}

bool coin(std::mt19937& rng, float p)
{
    return uniform(rng) < p;
}

template <typename T>
int pickIndex(std::mt19937& rng, const T& weights)
{
    float total = 0.0f;
    for (const auto w : weights)
        total += std::max(0.0f, static_cast<float>(w));
    float roll = uniform(rng) * total;
    for (size_t i = 0; i < weights.size(); ++i)
    {
        roll -= std::max(0.0f, static_cast<float>(weights[i]));
        if (roll <= 0.0f)
            return static_cast<int>(i);
    }
    return static_cast<int>(weights.size()) - 1;
}

const std::array<int, 7>& scaleOf(int mode)
{
    static const std::array<int, 7> minor { 0, 2, 3, 5, 7, 8, 10 };
    static const std::array<int, 7> major { 0, 2, 4, 5, 7, 9, 11 };
    static const std::array<int, 7> harmonic { 0, 2, 3, 5, 7, 8, 11 };
    return mode == 1 ? major : mode == 2 ? harmonic : minor;
}

// Root plans as scale degrees (0 = i, 2 = III, 3 = iv, 4 = v, 5 = VI, 6 = VII), one root per bar
// of a 4-bar phrase: the minor progressions of melodic house / garage.
struct RootPlan
{
    std::array<int, 4> degrees;
    float weight;
};

const std::vector<RootPlan>& rootPlans(AtmosphericSubstyle substyle)
{
    static const std::vector<RootPlan> pulse { { { 0, 0, 0, 0 }, 0.20f }, { { 0, 0, 5, 6 }, 0.20f }, { { 0, 5, 2, 6 }, 0.20f },
                                               { { 0, 3, 5, 4 }, 0.15f }, { { 0, 6, 5, 6 }, 0.15f }, { { 0, 0, 3, 3 }, 0.10f } };
    static const std::vector<RootPlan> twoStep { { { 0, 0, 0, 0 }, 0.30f }, { { 0, 0, 5, 5 }, 0.25f }, { { 0, 0, 5, 6 }, 0.25f },
                                                 { { 0, 3, 0, 3 }, 0.20f } };
    static const std::vector<RootPlan> broken { { { 0, 5, 2, 6 }, 0.25f }, { { 0, 3, 5, 4 }, 0.25f }, { { 0, 6, 5, 3 }, 0.25f },
                                                { { 0, 5, 3, 4 }, 0.25f } };
    return substyle == AtmosphericSubstyle::TwoStep ? twoStep : substyle == AtmosphericSubstyle::Broken ? broken : pulse;
}

// 0 on, 1 after (<= 2 16ths), 2 before (<= 2 16ths), 3 away. `kicks` holds this bar's kicks and
// the next bar's kicks shifted by 16 (a note just before the next downbeat kick is "before").
int relationOf(int step, const std::vector<int>& kicks)
{
    bool after = false, before = false;
    for (const int k : kicks)
    {
        if (k == step)
            return 0;
        after = after || (step - k >= 1 && step - k <= 2);
        before = before || (k - step >= 1 && k - step <= 2);
    }
    return after ? 1 : before ? 2 : 3;
}

std::vector<int> barKicks(const std::vector<std::vector<int>>& kickSteps, int bar, int bars)
{
    std::vector<int> k;
    if (bar >= 0 && bar < static_cast<int>(kickSteps.size()))
        k = kickSteps[static_cast<size_t>(bar)];
    const int next = (bar + 1) % std::max(1, bars);
    if (next < static_cast<int>(kickSteps.size()))
        for (const int s : kickSteps[static_cast<size_t>(next)])
            k.push_back(s + kSteps);
    return k;
}

float amountScale(int amount)
{
    return amount <= 0 ? 0.65f : amount == 1 ? 1.0f : 1.45f;
}

// A bass motif: one bar of notes, each with a harmonic role against the bar's root. The motif is
// repeated in every bar (a groove, not a random walk); the roots follow the phrase plan.
enum class Role
{
    Root,
    Octave,
    Fifth,
    Third,
    Approach // a scale step leading into the next bar's root
};

struct MotifNote
{
    int step;
    int length;    // 16ths
    Role role;
    int minAmount; // lane buttons [1][2][3] = 0, 1, 2: the note plays from this amount up
};

struct Motif
{
    const char* name;
    std::vector<MotifNote> notes;
    float weight;
};

const std::vector<Motif>& motifs(AtmosphericSubstyle substyle)
{
    using R = Role;
    // Pulse: the bass lives between the four-on-the-floor kicks (off-beat 8ths), short notes the
    // kick does not have to fight (no sidechain yet: notes end before the next kick).
    static const std::vector<Motif> pulse {
        { "offbeat", { { 2, 2, R::Root, 0 }, { 6, 2, R::Root, 1 }, { 10, 2, R::Root, 0 }, { 14, 2, R::Root, 0 }, { 7, 1, R::Octave, 2 }, { 15, 1, R::Octave, 2 } }, 0.30f },
        { "offbeat octave", { { 2, 2, R::Root, 0 }, { 6, 2, R::Octave, 1 }, { 10, 2, R::Root, 0 }, { 14, 2, R::Octave, 0 }, { 11, 1, R::Root, 2 } }, 0.25f },
        { "house push", { { 2, 2, R::Root, 0 }, { 7, 1, R::Root, 1 }, { 10, 2, R::Fifth, 0 }, { 13, 2, R::Root, 0 }, { 15, 1, R::Approach, 2 } }, 0.25f },
        { "dotted", { { 3, 1, R::Root, 0 }, { 6, 2, R::Root, 0 }, { 10, 1, R::Root, 1 }, { 11, 1, R::Octave, 2 }, { 14, 2, R::Approach, 0 } }, 0.20f },
    };
    // Two-Step: a sub that holds the root with the kick and answers late in the bar.
    static const std::vector<Motif> twoStep {
        { "hold", { { 0, 9, R::Root, 0 }, { 10, 4, R::Root, 0 }, { 14, 2, R::Approach, 1 }, { 7, 2, R::Octave, 2 } }, 0.30f },
        { "long", { { 0, 10, R::Root, 0 }, { 10, 4, R::Fifth, 0 }, { 14, 2, R::Approach, 2 } }, 0.25f },
        { "answer", { { 0, 6, R::Root, 0 }, { 6, 3, R::Octave, 1 }, { 11, 5, R::Root, 0 } }, 0.25f },
        { "off", { { 2, 6, R::Root, 0 }, { 10, 4, R::Root, 0 }, { 14, 2, R::Approach, 2 }, { 7, 1, R::Octave, 1 } }, 0.20f },
    };
    // Broken: a melodic line, long notes moving in thirds and steps.
    static const std::vector<Motif> broken {
        { "rise", { { 0, 6, R::Root, 0 }, { 6, 4, R::Third, 0 }, { 12, 4, R::Fifth, 0 } }, 0.25f },
        { "hold third", { { 0, 8, R::Root, 0 }, { 10, 4, R::Third, 0 }, { 14, 2, R::Approach, 2 } }, 0.25f },
        { "late", { { 3, 5, R::Root, 0 }, { 10, 6, R::Third, 1 }, { 8, 2, R::Fifth, 2 } }, 0.25f },
        { "fall", { { 0, 4, R::Root, 0 }, { 5, 5, R::Fifth, 1 }, { 11, 5, R::Third, 0 } }, 0.25f },
    };
    return substyle == AtmosphericSubstyle::TwoStep ? twoStep : substyle == AtmosphericSubstyle::Broken ? broken : pulse;
}

int degreePitch(int keyRoot, int mode, int degree, int nearPitch)
{
    const auto& scale = scaleOf(mode);
    const int pc = (keyRoot + scale[static_cast<size_t>(((degree % 7) + 7) % 7)]) % 12;
    int p = nearPitch - 6;
    while (((p % 12) + 12) % 12 != pc)
        ++p;
    return p;
}

AtmosBassLine generateCandidate(const AtmosBassParams& params, const AtmosStyleProfile& style, const std::vector<std::vector<int>>& kickSteps,
                                std::mt19937& rng)
{
    AtmosBassLine line;
    const int bars = std::clamp(params.bars, 1, 16);
    const int centre = style.bassRegister;
    const int lo = centre - 9;
    const int hi = centre + 14;

    const auto& plans = rootPlans(style.substyle);
    std::vector<float> planWeights;
    for (const auto& p : plans)
        planWeights.push_back(p.weight);
    const auto& plan = plans[static_cast<size_t>(pickIndex(rng, planWeights))];

    const auto& library = motifs(style.substyle);
    std::vector<float> motifWeights;
    for (const auto& m : library)
        motifWeights.push_back(m.weight);
    const auto& motif = library[static_cast<size_t>(pickIndex(rng, motifWeights))];
    // Broken re-voices a bar now and then (third <-> fifth); the others keep the motif.
    const float revoice = style.substyle == AtmosphericSubstyle::Broken ? 0.3f : 0.0f;
    const bool approachAtPhraseEnd = coin(rng, 0.6f);

    auto rootOf = [&](int bar) { return degreePitch(params.keyRoot, params.scaleMode, plan.degrees[static_cast<size_t>(bar % 4)], centre); };

    for (int bar = 0; bar < bars; ++bar)
    {
        const int degree = plan.degrees[static_cast<size_t>(bar % 4)];
        const int root = rootOf(bar);
        const int nextRoot = rootOf((bar + 1) % std::max(1, bars));
        const bool swapVoices = coin(rng, revoice);
        const bool phraseEnd = (bar % 4) == 3 || bar == bars - 1;

        std::vector<MotifNote> notes;
        for (const auto& n : motif.notes)
            if (params.amount >= n.minAmount)
                notes.push_back(n);
        std::sort(notes.begin(), notes.end(), [](const MotifNote& a, const MotifNote& b) { return a.step < b.step; });
        // The phrase end leads into the next root.
        if (approachAtPhraseEnd && phraseEnd && nextRoot != root && !notes.empty() && notes.back().step >= 10)
            notes.back().role = Role::Approach;

        for (const auto& n : notes)
        {
            auto role = n.role;
            if (swapVoices && (role == Role::Third || role == Role::Fifth))
                role = role == Role::Third ? Role::Fifth : Role::Third;
            int pitch = root;
            switch (role)
            {
                case Role::Root: pitch = root; break;
                case Role::Octave: pitch = root + 12 <= hi ? root + 12 : root; break;
                case Role::Fifth:
                {
                    const int up = degreePitch(params.keyRoot, params.scaleMode, degree + 4, root + 7);
                    pitch = up <= hi - 2 ? up : up - 12;
                    break;
                }
                case Role::Third:
                {
                    pitch = degreePitch(params.keyRoot, params.scaleMode, degree + 2, root + 3);
                    break;
                }
                case Role::Approach:
                {
                    // The scale tone a step above or below the next root, on the side nearer this one.
                    const int above = degreePitch(params.keyRoot, params.scaleMode, plan.degrees[static_cast<size_t>((bar + 1) % 4)] + 1, nextRoot + 2);
                    const int below = degreePitch(params.keyRoot, params.scaleMode, plan.degrees[static_cast<size_t>((bar + 1) % 4)] - 1, nextRoot - 2);
                    pitch = nextRoot == root ? root + 12 <= hi ? root + 12 : root
                                             : (std::abs(above - root) < std::abs(below - root) ? above : below);
                    break;
                }
            }
            while (pitch < lo)
                pitch += 12;
            while (pitch > hi)
                pitch -= 12;

            AtmosBassNote note;
            note.step = bar * kSteps + n.step;
            note.lengthSteps = n.length;
            note.pitch = pitch;
            note.velocity = n.step == notes.front().step ? 108 : (n.step % 4 == 2 ? 102 : 94);
            line.notes.push_back(note);
        }
        line.barRoots.push_back(((root % 12) + 12) % 12);
    }

    std::sort(line.notes.begin(), line.notes.end(), [](const AtmosBassNote& a, const AtmosBassNote& b) { return a.step < b.step; });
    // No overlaps (a note ends where the next starts; the loop wraps to the first note). Pulse:
    // a note also ends at the next kick, so the sub never rings through the four-on-the-floor.
    for (size_t i = 0; i < line.notes.size(); ++i)
    {
        auto& n = line.notes[i];
        int end = i + 1 < line.notes.size() ? line.notes[i + 1].step : line.notes.front().step + bars * kSteps;
        if (style.substyle == AtmosphericSubstyle::Pulse)
        {
            const int bar = n.step / kSteps;
            for (const int k : barKicks(kickSteps, bar, bars))
            {
                const int abs = bar * kSteps + k;
                if (abs > n.step && abs < end)
                    end = abs;
            }
        }
        n.lengthSteps = std::clamp(n.lengthSteps, 1, std::max(1, end - n.step));
    }
    return line;
}

float fitAround(float value, float target, float tolerance)
{
    const float d = (value - target) / std::max(0.05f, tolerance);
    return std::exp(-0.5f * d * d);
}

template <size_t N>
float l1(const std::array<float, N>& a, const std::array<float, N>& b)
{
    float d = 0.0f;
    for (size_t i = 0; i < N; ++i)
        d += std::abs(a[i] - b[i]);
    return d;
}
} // namespace

AtmosBassTraits AtmosphericBassGenerator::measure(const std::vector<AtmosBassNote>& notes, int bars, const std::vector<std::vector<int>>& kickSteps)
{
    AtmosBassTraits t;
    bars = std::max(1, bars);
    if (notes.empty())
        return t;
    t.notesPerBar = static_cast<float>(notes.size()) / static_cast<float>(bars);
    std::vector<float> lengths;
    int changes = 0, longNotes = 0, counted = 0;
    for (size_t i = 0; i < notes.size(); ++i)
    {
        lengths.push_back(static_cast<float>(notes[i].lengthSteps) / 4.0f);
        longNotes += notes[i].lengthSteps >= 8 ? 1 : 0;
        if (i > 0)
        {
            const int d = std::abs(notes[i].pitch - notes[i - 1].pitch);
            changes += d != 0 ? 1 : 0;
            const int c = d == 0 ? 0 : d <= 2 ? 1 : d <= 4 ? 2 : d <= 7 ? 3 : d == 12 ? 4 : -1;
            if (c >= 0)
            {
                t.intervals[static_cast<size_t>(c)] += 1.0f;
                ++counted;
            }
        }
        const int bar = notes[i].step / kSteps;
        ++t.kickRelation[static_cast<size_t>(relationOf(notes[i].step % kSteps, barKicks(kickSteps, bar, bars)))];
    }
    for (auto& v : t.intervals)
        v /= static_cast<float>(std::max(1, counted));
    for (auto& v : t.kickRelation)
        v /= static_cast<float>(notes.size());
    t.pitchChangesPerBar = static_cast<float>(changes) / static_cast<float>(bars);
    std::nth_element(lengths.begin(), lengths.begin() + static_cast<long>(lengths.size() / 2), lengths.end());
    t.medianBeats = lengths[lengths.size() / 2];
    t.longShare = static_cast<float>(longNotes) / static_cast<float>(notes.size());
    return t;
}

AtmosBassLine AtmosphericBassGenerator::search(const AtmosBassParams& params, const std::vector<std::vector<int>>& kickSteps, juce::String* report)
{
    const auto& style = getAtmosStyleProfile(params.substyle);
    const int bars = std::clamp(params.bars, 1, 16);
    const int count = std::clamp(params.candidateCount, 8, 128);
    const float scale = amountScale(params.amount) * (0.85f + 0.3f * params.density);

    std::vector<AtmosBassLine> candidates;
    candidates.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        std::mt19937 rng(static_cast<std::mt19937::result_type>(static_cast<uint32_t>(params.seed) * 2246822519u
                                                                + static_cast<uint32_t>(i) * 3266489917u + 0xa7b05u));
        auto line = generateCandidate(params, style, kickSteps, rng);
        line.traits = measure(line.notes, bars, kickSteps);
        const auto& t = line.traits;
        line.quality = 0.20f * fitAround(t.notesPerBar, style.bassNotesPerBar * scale, 0.30f * style.bassNotesPerBar * scale)
                       + 0.15f * fitAround(t.pitchChangesPerBar, style.bassPitchChangesPerBar * scale, 0.35f * style.bassPitchChangesPerBar * scale)
                       + 0.15f * fitAround(t.medianBeats, style.bassMedianBeats, 0.35f * style.bassMedianBeats)
                       + 0.10f * fitAround(t.longShare, style.bassLongShare, 0.15f)
                       + 0.20f * std::exp(-2.0f * l1(t.intervals, style.intervalMix))
                       + 0.20f * std::exp(-2.0f * l1(t.kickRelation, style.kickRelation));
        if (line.notes.empty())
            line.failedGate = "no bass";
        else if (t.kickRelation[0] > 0.5f)
            line.failedGate = "bass doubles the kick";
        for (const auto& n : line.notes)
            if (n.pitch < style.bassRegister - 9 || n.pitch > style.bassRegister + 14)
                line.failedGate = "out of register";
        line.passedGates = line.failedGate.isEmpty();
        candidates.push_back(std::move(line));
    }

    std::vector<size_t> eligible;
    for (size_t i = 0; i < candidates.size(); ++i)
        if (candidates[i].passedGates)
            eligible.push_back(i);
    const int passed = static_cast<int>(eligible.size());
    if (eligible.empty())
        for (size_t i = 0; i < candidates.size(); ++i)
            eligible.push_back(i);
    float best = -1.0e9f;
    for (const auto i : eligible)
        best = std::max(best, candidates[i].quality);
    std::vector<size_t> pool;
    std::vector<double> weights;
    double total = 0.0;
    for (const auto i : eligible)
        if (candidates[i].quality >= best - params.nearBestTolerance)
        {
            pool.push_back(i);
            weights.push_back(std::exp((candidates[i].quality - best) / std::max(0.001f, params.temperature)));
            total += weights.back();
        }
    std::mt19937 selection(static_cast<std::mt19937::result_type>(static_cast<uint32_t>(params.seed) * 747796405u + 0xa7b5u));
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
        const auto& c = candidates[chosen];
        const auto& t = c.traits;
        *report = "ATMOS BASS | " + juce::String(style.name) + " | candidates " + juce::String(count) + " | passed " + juce::String(passed)
                  + " | pool " + juce::String(static_cast<int>(pool.size())) + " | Q " + juce::String(c.quality, 3) + "\nnotes / bar "
                  + juce::String(t.notesPerBar, 2) + " | changes / bar " + juce::String(t.pitchChangesPerBar, 2) + " | median "
                  + juce::String(t.medianBeats, 2) + " beats | on kick " + juce::String(t.kickRelation[0], 2) + " | away "
                  + juce::String(t.kickRelation[3], 2);
    }
    return std::move(candidates[chosen]);
}

std::vector<NoteEvent> AtmosphericBassGenerator::toNotes(const AtmosBassLine& line)
{
    std::vector<NoteEvent> notes;
    for (const auto& n : line.notes)
    {
        NoteEvent e;
        e.gridTick = n.step * AtmosGrid::kPpqPerStep;
        e.lengthTicks = std::max(AtmosGrid::kPpqPerStep / 2, n.lengthSteps * AtmosGrid::kPpqPerStep - 20);
        e.pitch = n.pitch;
        e.velocity = n.velocity;
        e.semanticRole = "atmos_bass";
        notes.push_back(e);
    }
    return notes;
}
} // namespace bbg
