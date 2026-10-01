#include "BoomBapBassGenerator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

#include "BoomBapTiming.h"
#include "../../Core/ProjectLaneAccess.h"
#include "../../Core/TimingGrid.h"

namespace bbg
{
namespace
{
constexpr int kStepTicks = TimingGrid::Sixteenth;
constexpr int kStepsPerBar = 16;
constexpr int kLowestPitch = 24;   // C1
constexpr int kHighestPitch = 55;  // G3

const std::array<int, 7>& scaleFor(int scaleMode)
{
    static const std::array<int, 7> minor { 0, 2, 3, 5, 7, 8, 10 };
    static const std::array<int, 7> major { 0, 2, 4, 5, 7, 9, 11 };
    static const std::array<int, 7> harmonicMinor { 0, 2, 3, 5, 7, 8, 11 };
    return scaleMode == 1 ? major : scaleMode == 2 ? harmonicMinor : minor;
}

bool chance(std::mt19937& rng, float probability)
{
    return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) < probability;
}

int randomInt(std::mt19937& rng, int lo, int hi)
{
    return std::uniform_int_distribution<int>(lo, hi)(rng);
}

template <typename T>
const T& pickOne(std::mt19937& rng, const std::vector<T>& options)
{
    return options[static_cast<size_t>(randomInt(rng, 0, static_cast<int>(options.size()) - 1))];
}

int pickWeighted(std::mt19937& rng, const std::vector<std::pair<int, float>>& options)
{
    float total = 0.0f;
    for (const auto& option : options)
        total += option.second;
    float roll = std::uniform_real_distribution<float>(0.0f, total)(rng);
    for (const auto& option : options)
    {
        if (roll < option.second)
            return option.first;
        roll -= option.second;
    }
    return options.back().first;
}

int stepOf(const NoteEvent& note)
{
    return static_cast<int>(std::lround(note.gridTick / static_cast<double>(kStepTicks)));
}

//==============================================================================
// Classic style: the original generator, kept as-is (it sometimes gives exactly the right line).
namespace classic
{
constexpr int kLowestBassPitch = 31;   // G1
constexpr int kHighestBassPitch = 47;  // B2
constexpr int kBreathTicks = 60;       // 1/64 of air before the next note

int placePitch(int pitchClass, int previousPitch)
{
    int best = -1;
    for (int octave = 1; octave <= 4; ++octave)
    {
        const int candidate = pitchClass + 12 * octave;
        if (candidate < kLowestBassPitch || candidate > kHighestBassPitch)
            continue;
        if (best < 0 || std::abs(candidate - previousPitch) < std::abs(best - previousPitch))
            best = candidate;
    }
    return best >= 0 ? best : juce::jlimit(kLowestBassPitch, kHighestBassPitch, 36 + pitchClass);
}

struct BassEvent
{
    int tick = 0;
    int timingOffset = 0;
    int pitchClass = 0;
    int octaveUp = 0;
    int velocity = 100;
    const char* role = "bass_anchor";
};

std::vector<NoteEvent> generate(const PatternProject& project, const std::vector<int>& roots, std::mt19937& rng)
{
    const auto& params = project.params;
    const int bars = juce::jmax(1, params.bars);
    const int ticksPerBar = TimingGrid::TicksPerBar4_4;
    const int patternTicks = bars * ticksPerBar;
    const float density = juce::jlimit(0.0f, 1.0f, params.densityAmount);
    const auto& scale = scaleFor(params.scaleMode);

    const auto* kick = ProjectLaneAccess::findTrackState(project, TrackType::Kick);
    const auto* snare = ProjectLaneAccess::findTrackState(project, TrackType::Snare);
    auto snareAt = [&](int step)
    {
        return snare != nullptr && std::any_of(snare->notes.begin(), snare->notes.end(), [step](const NoteEvent& note)
        {
            return !note.isGhost && stepOf(note) == step;
        });
    };

    std::vector<BassEvent> events;
    for (int bar = 0; bar < bars; ++bar)
    {
        const int root = roots[static_cast<size_t>(bar)];
        const int barStart = bar * ticksPerBar;

        // 1. The root on beat 1.
        events.push_back({ barStart, 0, root, 0, 100 + randomInt(rng, 0, 10), "bass_anchor" });

        // 2. Lock with kicks in the second half of the bar (never on the snare backbeat).
        std::vector<const NoteEvent*> candidates;
        if (kick != nullptr)
        {
            for (const auto& note : kick->notes)
            {
                const int step = stepOf(note);
                const int stepInBar = step - bar * 16;
                if (stepInBar < 6 || stepInBar > 15 || stepInBar == 12 || snareAt(step) || note.isGhost)
                    continue;
                candidates.push_back(&note);
            }
        }
        std::sort(candidates.begin(), candidates.end(), [](const NoteEvent* a, const NoteEvent* b) { return a->velocity > b->velocity; });
        const int extraBudget = 1 + static_cast<int>(std::lround(density * 1.5f));
        int extras = 0;
        for (const auto* note : candidates)
        {
            if (extras >= extraBudget || !chance(rng, 0.78f))
                continue;
            BassEvent event;
            event.tick = stepOf(*note) * kStepTicks;
            event.timingOffset = note->timingOffsetTicks + (note->gridTick - event.tick); // sit with the kick's feel
            const float pick = std::uniform_real_distribution<float>(0.0f, 1.0f)(rng);
            event.pitchClass = pick < 0.22f ? (root + 7) % 12 : root;
            event.octaveUp = pick >= 0.22f && pick < 0.32f ? 1 : 0;
            event.velocity = 92 + randomInt(rng, 0, 12);
            event.role = "bass_kick";
            events.push_back(event);
            ++extras;
        }

        // 3. Approach the next bar's root from a scale step below (or above).
        const int nextRoot = roots[static_cast<size_t>((bar + 1) % bars)];
        const bool latePositionFree = std::none_of(events.begin(), events.end(), [&](const BassEvent& event)
        {
            return event.tick >= barStart + 13 * kStepTicks && event.tick < barStart + ticksPerBar;
        });
        if (bars > 1 && latePositionFree && !snareAt(bar * 16 + 14) && chance(rng, (nextRoot != root ? 0.35f : 0.15f) + 0.2f * density))
        {
            int degree = 0;
            for (int i = 0; i < 7; ++i)
                if ((juce::jlimit(0, 11, params.keyRoot) + scale[static_cast<size_t>(i)]) % 12 == nextRoot)
                    degree = i;
            const bool fromBelow = chance(rng, 0.7f);
            const int approachDegree = (degree + (fromBelow ? 6 : 1)) % 7;
            BassEvent event;
            event.tick = barStart + 14 * kStepTicks;
            event.pitchClass = (juce::jlimit(0, 11, params.keyRoot) + scale[static_cast<size_t>(approachDegree)]) % 12;
            event.velocity = 84 + randomInt(rng, 0, 10);
            event.role = "bass_approach";
            events.push_back(event);
        }
    }

    std::sort(events.begin(), events.end(), [](const BassEvent& a, const BassEvent& b) { return a.tick < b.tick; });
    events.erase(std::unique(events.begin(), events.end(), [](const BassEvent& a, const BassEvent& b) { return a.tick == b.tick; }), events.end());

    std::vector<NoteEvent> notes;
    int previousPitch = 38;
    for (size_t i = 0; i < events.size(); ++i)
    {
        const auto& event = events[i];
        const int nextTick = i + 1 < events.size() ? events[i + 1].tick : patternTicks;
        NoteEvent note;
        note.gridTick = event.tick;
        note.timingOffsetTicks = event.timingOffset;
        note.pitch = juce::jlimit(24, 60, placePitch(event.pitchClass, previousPitch) + 12 * event.octaveUp);
        note.lengthTicks = juce::jmax(TimingGrid::ThirtySecond, nextTick - event.tick - kBreathTicks);
        note.velocity = juce::jlimit(1, 127, event.velocity);
        note.semanticRole = event.role;
        notes.push_back(note);
        if (event.octaveUp == 0)
            previousPitch = note.pitch;
    }
    return notes;
}
} // namespace classic

//==============================================================================
// Phrase-based styles (modelled on played boom bap bass loops).

// One note of a bar figure, relative to the bar's chord root (or, for approach notes, to the
// next bar's root). lengthSteps 0 = legato: held until the next note.
struct FigureNote
{
    int step = 0;
    int interval = 0;
    int lengthSteps = 0;
    int velocity = 80;
    const char* role = "bass_line";
    bool towardNextRoot = false; // interval is relative to the next bar's root
    bool chromatic = false;      // deliberately outside the key (passing / leading tone)
    bool ghost = false;
    bool onKick = false;         // re-placed onto this bar's kicks when instantiated
};

using Figure = std::vector<FigureNote>;

enum class BarKind
{
    Statement,
    Answer,
    Variation,
    Ending
};

struct StyleFigures
{
    Figure statement;
    Figure answer;
    Figure variation;
    Figure ending;
    int registerShift = 0;  // +12 plays the whole line an octave up
    bool swung = false;     // odd 16ths take the full swing offset
};

FigureNote note(int step, int interval, int lengthSteps, int velocity, const char* role = "bass_line")
{
    FigureNote n;
    n.step = step;
    n.interval = interval;
    n.lengthSteps = lengthSteps;
    n.velocity = velocity;
    n.role = role;
    return n;
}

FigureNote anchor(int lengthSteps, int velocity)
{
    return note(0, 0, lengthSteps, velocity, "bass_anchor");
}

FigureNote approach(int step, int intervalToNextRoot, int velocity)
{
    auto n = note(step, intervalToNextRoot, 0, velocity, "bass_approach");
    n.towardNextRoot = true;
    n.chromatic = intervalToNextRoot == -1 || intervalToNextRoot == 1;
    if (n.chromatic)
        n.role = "bass_chromatic";
    return n;
}

FigureNote chromaticNote(int step, int interval, int velocity)
{
    auto n = note(step, interval, 0, velocity, "bass_chromatic");
    n.chromatic = true;
    return n;
}

void sortFigure(Figure& figure)
{
    std::sort(figure.begin(), figure.end(), [](const FigureNote& a, const FigureNote& b) { return a.step < b.step; });
    figure.erase(std::unique(figure.begin(), figure.end(), [](const FigureNote& a, const FigureNote& b) { return a.step == b.step; }), figure.end());
}

//------------------------------------------------------------------------------
// Sparse low: root, the fifth (usually below) held long, then a short late figure.
StyleFigures buildSparseLow(std::mt19937& rng)
{
    StyleFigures s;
    const int headLength = chance(rng, 0.6f) ? 2 : 4;
    const int heldInterval = pickWeighted(rng, { { -5, 0.55f }, { 0, 0.25f }, { -2, 0.20f } });
    const std::vector<std::pair<int, int>> lateSteps { { 10, 12 }, { 8, 12 }, { 10, 14 }, { 12, 14 }, { 9, 10 } };
    const std::vector<std::pair<int, int>> lateIntervals { { 3, 3 }, { 7, 7 }, { 3, 5 }, { -2, -2 }, { 7, 5 }, { 10, 7 }, { -4, -4 }, { 5, 3 } };

    auto head = [&](Figure& figure)
    {
        figure.push_back(anchor(headLength, 72 + randomInt(rng, 0, 12)));
        figure.push_back(note(headLength, heldInterval, 0, 82 + randomInt(rng, 0, 8)));
    };
    auto late = [&](Figure& figure, std::pair<int, int> steps, std::pair<int, int> intervals)
    {
        figure.push_back(note(steps.first, intervals.first, 2, 78 + randomInt(rng, 0, 8)));
        figure.push_back(note(steps.second, intervals.second, 0, 72 + randomInt(rng, 0, 8)));
    };

    const auto steps = pickOne(rng, lateSteps);
    const auto intervalsA = pickOne(rng, lateIntervals);
    auto intervalsB = pickOne(rng, lateIntervals);
    if (intervalsB == intervalsA)
        intervalsB = { -4, -4 };

    head(s.statement);
    late(s.statement, steps, intervalsA);
    head(s.answer);
    late(s.answer, chance(rng, 0.5f) ? steps : pickOne(rng, lateSteps), intervalsB);

    // Variation: the held note runs on and a single low note answers it.
    s.variation.push_back(anchor(headLength, 76));
    s.variation.push_back(note(headLength, heldInterval, 0, 84));
    s.variation.push_back(note(chance(rng, 0.5f) ? 10 : 12, chance(rng, 0.5f) ? -5 : 0, 0, 82));

    // Ending: root and held fifth, then let it ring.
    s.ending.push_back(anchor(2, 76));
    s.ending.push_back(note(2, heldInterval, 0, 84));
    if (chance(rng, 0.5f))
        s.ending.push_back(approach(14, -2, 70));
    return s;
}

//------------------------------------------------------------------------------
// Pump: steady eighths on root / fifth / octave / b7, the odd 16th split or ghost.
Figure pumpBar(std::mt19937& rng, bool longHead)
{
    Figure figure;
    figure.push_back(anchor(longHead ? 4 : 2, 100 + randomInt(rng, 0, 8)));
    const int firstSlot = longHead ? 4 : 2;
    for (int step = firstSlot; step < 16; step += 2)
    {
        int interval = pickWeighted(rng, { { 0, 0.34f }, { 7, 0.30f }, { 12, 0.12f }, { 10, 0.10f }, { 3, 0.07f }, { 5, 0.07f } });
        if (step == 12 && chance(rng, 0.55f))
            interval = 0;
        const int velocity = step == 12 ? 88 + randomInt(rng, 0, 16) : interval >= 12 ? 60 + randomInt(rng, 0, 8) : 74 + randomInt(rng, 0, 12);
        figure.push_back(note(step, interval, 0, velocity));
    }

    // One eighth split into two 16ths (octave then b7, like a little turn).
    if (chance(rng, 0.5f))
    {
        const int step = pickOne(rng, std::vector<int> { 6, 8, 10 });
        for (auto& n : figure)
            if (n.step == step)
                n.interval = 12, n.velocity = 62;
        figure.push_back(note(step + 1, chance(rng, 0.6f) ? 10 : 7, 0, 54));
    }
    // A ghost 16th pickup.
    if (chance(rng, 0.35f))
    {
        auto ghost = note(pickOne(rng, std::vector<int> { 7, 11, 15 }), chance(rng, 0.5f) ? 10 : 3, 0, 33, "bass_ghost");
        ghost.ghost = true;
        figure.push_back(ghost);
    }
    sortFigure(figure);
    return figure;
}

StyleFigures buildPump(std::mt19937& rng)
{
    StyleFigures s;
    const bool longHead = chance(rng, 0.65f);
    s.statement = pumpBar(rng, longHead);

    // Answer: same head, the second half re-rolled.
    s.answer = s.statement;
    const auto fresh = pumpBar(rng, longHead);
    s.answer.erase(std::remove_if(s.answer.begin(), s.answer.end(), [](const FigureNote& n) { return n.step >= 8; }), s.answer.end());
    for (const auto& n : fresh)
        if (n.step >= 8)
            s.answer.push_back(n);
    sortFigure(s.answer);

    // Variation: pump on one tone (fifth / root / octave alternation).
    const int mode = randomInt(rng, 0, 2);
    s.variation.push_back(anchor(2, 96));
    s.variation.push_back(note(2, 0, 0, 82));
    for (int step = 4; step < 16; step += 2)
    {
        const int interval = mode == 0 ? 7 : mode == 1 ? 0 : ((step / 2) % 2 == 0 ? 0 : 12);
        s.variation.push_back(note(step, interval, 0, 80 + randomInt(rng, 0, 12)));
    }

    // Ending: root eighths into a held root.
    s.ending.push_back(anchor(2, 84));
    s.ending.push_back(note(2, 0, 4, 92));
    s.ending.push_back(note(6, 0, 0, 92));
    if (chance(rng, 0.5f))
        s.ending.push_back(approach(14, -2, 76));
    return s;
}

//------------------------------------------------------------------------------
// Chromatic walk: root held, then eighths that walk (with passing tones) into the next root.
Figure walkBar(std::mt19937& rng)
{
    Figure figure;
    const bool longHead = chance(rng, 0.7f);
    figure.push_back(anchor(longHead ? 4 : 2, 82 + randomInt(rng, 0, 6)));
    if (!longHead)
        figure.push_back(note(2, 0, 0, 70));
    figure.push_back(note(4, 0, 0, 68 + randomInt(rng, 0, 6)));
    figure.push_back(note(6, chance(rng, 0.7f) ? 0 : 7, 0, 74 + randomInt(rng, 0, 6)));

    // Three walking notes on 8 / 10 / 12, each within a fourth of the previous one.
    static const std::vector<int> pool { 1, 3, 5, 6, 7, -2, -4, -5 };
    int previous = 0;
    for (int step = 8; step <= 12; step += 2)
    {
        int interval = 0;
        for (int attempt = 0; attempt < 12; ++attempt)
        {
            interval = pickOne(rng, pool);
            if (std::abs(interval - previous) <= 6 && interval != previous)
                break;
        }
        const bool isChromatic = interval == 1 || interval == 6 || interval == -4;
        figure.push_back(isChromatic ? chromaticNote(step, interval, 70 + randomInt(rng, 0, 6))
                                     : note(step, interval, 0, 70 + randomInt(rng, 0, 6)));
        previous = interval;
    }
    // Lead into the next root: leading tone below, b2 above, or a whole step below.
    figure.push_back(approach(14, pickWeighted(rng, { { -1, 0.5f }, { 1, 0.2f }, { -2, 0.3f } }), 66 + randomInt(rng, 0, 6)));
    return figure;
}

StyleFigures buildChromaticWalk(std::mt19937& rng)
{
    StyleFigures s;
    s.registerShift = chance(rng, 0.5f) ? 12 : 0;
    s.statement = walkBar(rng);
    s.answer = chance(rng, 0.45f) ? s.statement : walkBar(rng);

    s.variation.push_back(anchor(2, 76));
    for (int step = 2; step <= 6; step += 2)
        s.variation.push_back(note(step, 0, 0, 66 + randomInt(rng, 0, 6)));
    s.variation.push_back(note(8, -5, 0, 70));
    s.variation.push_back(chromaticNote(10, -4, 72));
    s.variation.push_back(note(12, -5, 4, 66));

    s.ending.push_back(anchor(2, 70));
    s.ending.push_back(note(2, 0, 0, 70));
    return s;
}

//------------------------------------------------------------------------------
// Kick riff: a low root held, a high riff exactly on the kicks, then a falling pair.
StyleFigures buildKickRiff(std::mt19937& rng)
{
    StyleFigures s;
    const int riffInterval = pickWeighted(rng, { { 19, 0.35f }, { 12, 0.30f }, { 15, 0.15f }, { 17, 0.10f }, { 14, 0.10f } });
    const std::vector<std::pair<int, int>> fallingPairs { { 14, 12 }, { 15, 12 }, { 10, 7 }, { 14, 7 }, { 12, 10 } };
    const auto pairA = pickOne(rng, fallingPairs);
    auto pairB = pickOne(rng, fallingPairs);
    if (pairB == pairA)
        pairB = { 14, 7 };

    auto riff = [&](Figure& figure, int interval)
    {
        for (const int step : { 7, 9, 10 })
        {
            auto n = note(step, interval, step == 9 ? 1 : 2, step == 7 ? 82 : 69, "bass_kick");
            n.onKick = true;
            figure.push_back(n);
        }
    };

    s.statement.push_back(anchor(0, 60 + randomInt(rng, 0, 8)));
    riff(s.statement, riffInterval);
    s.statement.push_back(note(12, pairA.first, 2, 69));
    s.statement.push_back(note(14, pairA.second, 0, 66));

    // Answer: the low root rings on, one more root on the kick, then the other falling pair.
    s.answer.push_back(anchor(0, 47 + randomInt(rng, 0, 8)));
    s.answer.push_back(note(10, 0, 2, 44));
    s.answer.push_back(note(12, pairB.first, 2, 64));
    s.answer.push_back(note(14, pairB.second, 0, 59));

    s.variation.push_back(anchor(0, 60));
    riff(s.variation, riffInterval - 12 >= 7 ? riffInterval - 12 : 7);
    s.variation.push_back(note(12, pairB.first, 2, 66));
    s.variation.push_back(note(14, pairB.second, 0, 62));

    s.ending.push_back(anchor(0, 56));
    return s;
}

//------------------------------------------------------------------------------
// Swing melodic: low root, rest under the backbeat, then a swung 16th melody up high.
StyleFigures buildSwingMelodic(std::mt19937& rng, int scaleMode)
{
    StyleFigures s;
    s.swung = true;
    const std::vector<int> pool = scaleMode == 1 ? std::vector<int> { 7, 9, 12, 14, 16 }
                                                 : std::vector<int> { 7, 10, 12, 14, 15 };
    const std::vector<std::vector<int>> rhythms {
        { 5, 6, 7, 8, 10, 14 },
        { 6, 7, 8, 10, 13, 14 },
        { 5, 6, 8, 10, 12, 14 },
        { 6, 8, 9, 10, 14 },
        { 5, 7, 8, 10, 11, 14 }
    };

    auto melody = [&](const std::vector<int>& rhythm, int startIndex)
    {
        Figure figure;
        figure.push_back(anchor(4, 96 + randomInt(rng, 0, 10)));
        int index = startIndex;
        for (const int step : rhythm)
        {
            // Mostly stepwise through the pool; a repeated tone only now and then.
            const int maxIndex = static_cast<int>(pool.size()) - 1;
            int move = chance(rng, 0.2f) ? 0 : (chance(rng, 0.5f) ? 1 : -1) * randomInt(rng, 1, 2);
            if (index + move < 0 || index + move > maxIndex)
                move = -move;
            index = juce::jlimit(0, maxIndex, index + move);
            const bool odd = step % 2 == 1;
            figure.push_back(note(step, pool[static_cast<size_t>(index)], odd ? 1 : 0, odd ? 70 + randomInt(rng, 0, 10) : 84 + randomInt(rng, 0, 20)));
        }
        // Whole step below the next root (an octave up), on the last swung 16th.
        figure.push_back(approach(15, 10, 57 + randomInt(rng, 0, 8)));
        sortFigure(figure);
        return figure;
    };

    const auto rhythm = pickOne(rng, rhythms);
    const int startIndex = randomInt(rng, 1, static_cast<int>(pool.size()) - 2);
    s.statement = melody(rhythm, startIndex);
    // Answer: same rhythm and opening, new second half.
    s.answer = s.statement;
    const auto fresh = melody(rhythm, startIndex);
    for (auto& n : s.answer)
        if (n.step >= 8 && n.step < 15)
            for (const auto& f : fresh)
                if (f.step == n.step)
                    n.interval = f.interval;

    s.variation = s.statement;
    s.variation.erase(std::remove_if(s.variation.begin(), s.variation.end(), [](const FigureNote& n) { return n.step > 10; }), s.variation.end());
    for (auto& n : s.variation)
        if (n.step == 10)
            n.lengthSteps = 4;
    s.variation.push_back(approach(14, -2, 70));

    s.ending.push_back(anchor(8, 90));
    s.ending.push_back(note(14, pool[1], 1, 70));
    s.ending.push_back(approach(15, 10, 60));
    return s;
}

//------------------------------------------------------------------------------
BarKind barKindFor(int bar, int bars, bool answerIsStatement)
{
    if (bars >= 4 && bar == bars - 1)
        return BarKind::Ending;
    if (bars >= 4 && bar % 4 == 3)
        return BarKind::Variation;
    if (bar % 2 == 1)
        return answerIsStatement ? BarKind::Statement : BarKind::Answer;
    return BarKind::Statement;
}

// Tonic in the low bass register (D1..C#2).
int tonicPitchFor(int pitchClass)
{
    int pitch = 24 + pitchClass;
    if (pitch < 26)
        pitch += 12;
    return pitch;
}

// A bar's chord root in the octave nearest the tonic (so the line does not jump octaves between
// chords), optionally the whole line an octave up.
int rootPitchFor(int pitchClass, int tonicPitch, int registerShift)
{
    const int low = 24 + pitchClass;
    const int pitch = std::abs(low - tonicPitch) <= std::abs(low + 12 - tonicPitch) ? low : low + 12;
    return pitch + registerShift;
}

int foldIntoRange(int pitch)
{
    while (pitch < kLowestPitch)
        pitch += 12;
    while (pitch > kHighestPitch)
        pitch -= 12;
    return pitch;
}

bool inScale(int pitch, int keyRoot, const std::array<int, 7>& scale)
{
    const int degree = ((pitch - keyRoot) % 12 + 12) % 12;
    return std::find(scale.begin(), scale.end(), degree) != scale.end();
}

// Moves an out-of-key note to the nearest scale tone (down first).
int snapToScale(int pitch, int keyRoot, const std::array<int, 7>& scale)
{
    if (inScale(pitch, keyRoot, scale))
        return pitch;
    if (inScale(pitch - 1, keyRoot, scale))
        return pitch - 1;
    return pitch + 1;
}

std::vector<NoteEvent> renderFigures(const PatternProject& project, const std::vector<int>& roots,
                                     const StyleFigures& figures, std::mt19937& rng)
{
    const auto& params = project.params;
    const int bars = juce::jmax(1, params.bars);
    const int patternTicks = bars * TimingGrid::TicksPerBar4_4;
    const int keyRoot = juce::jlimit(0, 11, params.keyRoot);
    const auto& scale = scaleFor(params.scaleMode);
    const int swingOffset = BoomBapTiming::getSixteenthSwingOffsetPPQ(BoomBapTiming::parameterPercentToSwingRatio(params.swingPercent));
    const int fullSwing = juce::jmax(swingOffset, kStepTicks / 3); // swung-16th feel: at least triplet swing

    // Kick steps (with their timing) so kick-locked notes sit exactly with the kick.
    std::map<int, int> kickOffsetByStep;
    if (const auto* kick = ProjectLaneAccess::findTrackState(project, TrackType::Kick); kick != nullptr)
        for (const auto& k : kick->notes)
            if (!k.isGhost)
                kickOffsetByStep.emplace(stepOf(k), k.timingOffsetTicks + (k.gridTick - stepOf(k) * kStepTicks));

    const bool answerIsStatement = chance(rng, 0.25f);
    const int tonicPitch = tonicPitchFor(keyRoot);
    const float calm = BoomBapBassGenerator::effectiveCalm(project.sampleContext.mood, BoomBapBassGenerator::bassAmountOf(project));

    struct Placed
    {
        int tick = 0;
        int offset = 0;
        int pitch = 36;
        int lengthSteps = 0;
        int velocity = 80;
        const char* role = "bass_line";
        bool ghost = false;
    };
    std::vector<Placed> placed;

    for (int bar = 0; bar < bars; ++bar)
    {
        const auto kind = barKindFor(bar, bars, answerIsStatement);
        const auto& figure = kind == BarKind::Answer ? figures.answer
                           : kind == BarKind::Variation ? figures.variation
                           : kind == BarKind::Ending ? figures.ending
                                                     : figures.statement;
        const int rootPitch = rootPitchFor(roots[static_cast<size_t>(bar)], tonicPitch, figures.registerShift);
        int nextRootPitch = rootPitchFor(roots[static_cast<size_t>((bar + 1) % bars)], tonicPitch, figures.registerShift);
        if (nextRootPitch - rootPitch > 6)
            nextRootPitch -= 12;
        else if (rootPitch - nextRootPitch > 6)
            nextRootPitch += 12;

        // Kick-locked riff notes move onto this bar's actual kicks (6..11) when the kick has at
        // least two there; with fewer the riff keeps its own steps (still taking a kick's feel).
        std::vector<int> barKickSteps;
        for (const auto& [step, offset] : kickOffsetByStep)
            if (step >= bar * kStepsPerBar + 6 && step <= bar * kStepsPerBar + 11)
                barKickSteps.push_back(step - bar * kStepsPerBar);
        if (barKickSteps.size() < 2)
            barKickSteps.clear();
        size_t kickCursor = 0;

        for (const auto& n : figure)
        {
            int step = n.step;
            if (n.onKick && !barKickSteps.empty())
            {
                if (kickCursor >= barKickSteps.size())
                    continue;
                step = barKickSteps[kickCursor++];
            }

            int pitch = (n.towardNextRoot ? nextRootPitch : rootPitch) + n.interval;
            if (!n.chromatic)
                pitch = snapToScale(pitch, keyRoot, scale);
            pitch = foldIntoRange(pitch);

            const int absoluteStep = bar * kStepsPerBar + step;
            int offset = 0;
            if (const auto it = kickOffsetByStep.find(absoluteStep); it != kickOffsetByStep.end() && (n.onKick || step % 2 == 1))
                offset = it->second;
            else if (step % 2 == 1)
                offset = figures.swung ? fullSwing : swingOffset;

            // A calm sample leaves room: no ghost notes, and most off-beat 16ths drop out (the
            // previous note rings on instead).
            const bool offbeat16 = step % 2 == 1 && !n.onKick && !n.towardNextRoot;
            if (step != 0 && ((n.ghost && calm > 0.4f) || (offbeat16 && chance(rng, 0.7f * calm))))
                continue;

            const int velocity = juce::jlimit(1, 127, n.velocity + randomInt(rng, -4, 4));
            placed.push_back({ absoluteStep * kStepTicks, offset, pitch, n.lengthSteps, velocity, n.role, n.ghost });
        }
    }

    std::sort(placed.begin(), placed.end(), [](const Placed& a, const Placed& b) { return a.tick < b.tick; });
    placed.erase(std::unique(placed.begin(), placed.end(), [](const Placed& a, const Placed& b) { return a.tick == b.tick; }), placed.end());

    constexpr int kBreathTicks = 30;
    std::vector<NoteEvent> notes;
    for (size_t i = 0; i < placed.size(); ++i)
    {
        const auto& p = placed[i];
        const int nextTick = i + 1 < placed.size() ? placed[i + 1].tick : patternTicks;
        const int room = nextTick - p.tick - kBreathTicks;
        NoteEvent n;
        n.gridTick = p.tick;
        n.timingOffsetTicks = p.offset;
        n.pitch = p.pitch;
        n.lengthTicks = juce::jmax(TimingGrid::ThirtySecond, p.lengthSteps > 0 ? juce::jmin(p.lengthSteps * kStepTicks - kBreathTicks, room) : room);
        n.velocity = p.velocity;
        n.isGhost = p.ghost;
        n.semanticRole = p.role;
        notes.push_back(n);
    }
    return notes;
}
} // namespace

std::vector<int> BoomBapBassGenerator::progressionRoots(const GeneratorParams& params, std::mt19937& rng)
{
    // Scale-degree indices per bar (4-bar cycle). One-chord vamps and i-bVI swings are as common
    // in boom bap as moving progressions.
    static const std::vector<std::array<int, 4>> minorProgressions {
        { 0, 0, 5, 4 }, { 0, 3, 0, 4 }, { 0, 6, 5, 6 }, { 0, 0, 3, 3 }, { 0, 5, 3, 4 },
        { 0, 0, 0, 0 }, { 0, 5, 0, 5 }, { 0, 0, 5, 5 }, { 0, 6, 0, 6 }
    };
    static const std::vector<std::array<int, 4>> majorProgressions {
        { 0, 0, 3, 4 }, { 0, 5, 3, 4 }, { 0, 3, 0, 4 }, { 0, 4, 5, 3 }, { 0, 0, 0, 0 }, { 0, 3, 0, 3 }
    };
    const auto& progressions = params.scaleMode == 1 ? majorProgressions : minorProgressions;
    const auto& degrees = progressions[std::uniform_int_distribution<size_t>(0, progressions.size() - 1)(rng)];
    const auto& scale = scaleFor(params.scaleMode);
    const int keyRoot = juce::jlimit(0, 11, params.keyRoot);

    std::vector<int> roots;
    const int bars = juce::jmax(1, params.bars);
    for (int bar = 0; bar < bars; ++bar)
        roots.push_back((keyRoot + scale[static_cast<size_t>(degrees[static_cast<size_t>(bar % 4)])]) % 12);
    return roots;
}

float BoomBapBassGenerator::effectiveCalm(const SampleMood& mood, int amount)
{
    switch (juce::jlimit(0, 2, amount))
    {
        case 0:  return std::max(mood.calmness(), 0.55f); // Low: a few long notes
        case 1:  return mood.calmness() * 0.6f;           // More: a calm sample still calms it
        default: return 0.0f;                             // Full
    }
}

int BoomBapBassGenerator::bassAmountOf(const PatternProject& project)
{
    const auto* bass = ProjectLaneAccess::findTrackState(project, TrackType::Sub808);
    return bass != nullptr ? juce::jlimit(0, 2, bass->sub808Settings.bassAmount) : 0;
}

BoomBapBassGenerator::Style BoomBapBassGenerator::pickStyle(std::mt19937& rng, const SampleMood& mood, int amount)
{
    // Full: the busy, all-the-way styles (the line plays through the whole bar).
    if (amount >= 2)
        return static_cast<Style>(pickWeighted(rng, {
            { static_cast<int>(Style::Classic), 0.06f },
            { static_cast<int>(Style::Pump), 0.33f },
            { static_cast<int>(Style::ChromaticWalk), 0.25f },
            { static_cast<int>(Style::KickRiff), 0.14f },
            { static_cast<int>(Style::SwingMelodic), 0.22f }
        }));

    // Low: a few long notes - the spacious styles, never the busy swung one.
    if (amount <= 0)
        return static_cast<Style>(pickWeighted(rng, {
            { static_cast<int>(Style::Classic), 0.32f },
            { static_cast<int>(Style::SparseLow), 0.46f },
            { static_cast<int>(Style::KickRiff), 0.14f },
            { static_cast<int>(Style::ChromaticWalk), 0.04f },
            { static_cast<int>(Style::Pump), 0.04f }
        }));

    // Swing Melodic is the loudest gesture: rare without a sample, rarer on a calm or straight one.
    const float calm = effectiveCalm(mood, amount);
    const float swingFeel = mood.valid ? 0.5f + mood.swing : 1.0f;
    return static_cast<Style>(pickWeighted(rng, {
        { static_cast<int>(Style::Classic), 0.18f },
        { static_cast<int>(Style::SparseLow), 0.16f + 0.40f * calm },
        { static_cast<int>(Style::Pump), 0.22f - 0.14f * calm },
        { static_cast<int>(Style::ChromaticWalk), 0.15f - 0.09f * calm },
        { static_cast<int>(Style::KickRiff), 0.19f - 0.06f * calm },
        { static_cast<int>(Style::SwingMelodic), std::max(0.02f, (0.10f - 0.08f * calm) * swingFeel) }
    }));
}

const char* BoomBapBassGenerator::styleName(Style style)
{
    switch (style)
    {
        case Style::Classic: return "Classic";
        case Style::SparseLow: return "Sparse Low";
        case Style::Pump: return "Pump";
        case Style::ChromaticWalk: return "Chromatic Walk";
        case Style::KickRiff: return "Kick Riff";
        case Style::SwingMelodic: return "Swing Melodic";
        default: break;
    }
    return "Classic";
}

std::vector<NoteEvent> BoomBapBassGenerator::generate(const PatternProject& project, std::mt19937& rng)
{
    const auto style = pickStyle(rng, project.sampleContext.mood, bassAmountOf(project));
    return generate(project, rng, style);
}

std::vector<NoteEvent> BoomBapBassGenerator::generate(const PatternProject& project, std::mt19937& rng, Style style)
{
    const auto roots = progressionRoots(project.params, rng);
    switch (style)
    {
        case Style::SparseLow:     return renderFigures(project, roots, buildSparseLow(rng), rng);
        case Style::Pump:          return renderFigures(project, roots, buildPump(rng), rng);
        case Style::ChromaticWalk: return renderFigures(project, roots, buildChromaticWalk(rng), rng);
        case Style::KickRiff:      return renderFigures(project, roots, buildKickRiff(rng), rng);
        case Style::SwingMelodic:  return renderFigures(project, roots, buildSwingMelodic(rng, project.params.scaleMode), rng);
        case Style::Classic:
        default:                   return classic::generate(project, roots, rng);
    }
}
} // namespace bbg
