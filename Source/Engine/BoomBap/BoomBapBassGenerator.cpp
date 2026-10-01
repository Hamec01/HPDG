#include "BoomBapBassGenerator.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "../../Core/ProjectLaneAccess.h"
#include "../../Core/TimingGrid.h"

namespace bbg
{
namespace
{
constexpr int kLowestBassPitch = 31;   // G1
constexpr int kHighestBassPitch = 47;  // B2
constexpr int kBreathTicks = 60;       // 1/64 of air before the next note

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

// Places a pitch class in the bass register, as close as possible to the previous note.
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
}

std::vector<int> BoomBapBassGenerator::progressionRoots(const GeneratorParams& params, std::mt19937& rng)
{
    static const std::vector<std::array<int, 4>> minorProgressions { { 0, 0, 5, 4 }, { 0, 3, 0, 4 }, { 0, 6, 5, 6 }, { 0, 0, 3, 3 }, { 0, 5, 3, 4 } };
    static const std::vector<std::array<int, 4>> majorProgressions { { 0, 0, 3, 4 }, { 0, 5, 3, 4 }, { 0, 3, 0, 4 }, { 0, 4, 5, 3 } };
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

std::vector<NoteEvent> BoomBapBassGenerator::generate(const PatternProject& project, std::mt19937& rng)
{
    const auto& params = project.params;
    const int bars = juce::jmax(1, params.bars);
    const int ticksPerBar = TimingGrid::TicksPerBar4_4;
    const int patternTicks = bars * ticksPerBar;
    const float density = juce::jlimit(0.0f, 1.0f, params.densityAmount);
    const auto roots = progressionRoots(params, rng);
    const auto& scale = scaleFor(params.scaleMode);

    const auto* kick = ProjectLaneAccess::findTrackState(project, TrackType::Kick);
    const auto* snare = ProjectLaneAccess::findTrackState(project, TrackType::Snare);
    auto snareAt = [&](int step)
    {
        return snare != nullptr && std::any_of(snare->notes.begin(), snare->notes.end(), [step](const NoteEvent& note)
        {
            return !note.isGhost && static_cast<int>(std::lround(note.gridTick / static_cast<double>(TimingGrid::Sixteenth))) == step;
        });
    };

    std::vector<BassEvent> events;
    for (int bar = 0; bar < bars; ++bar)
    {
        const int root = roots[static_cast<size_t>(bar)];
        const int barStart = bar * ticksPerBar;

        // 1. The root on beat 1.
        events.push_back({ barStart, 0, root, 0, 100 + std::uniform_int_distribution<int>(0, 10)(rng), "bass_anchor" });

        // 2. Lock with kicks in the second half of the bar (never on the snare backbeat).
        std::vector<const NoteEvent*> candidates;
        if (kick != nullptr)
        {
            for (const auto& note : kick->notes)
            {
                const int step = static_cast<int>(std::lround(note.gridTick / static_cast<double>(TimingGrid::Sixteenth)));
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
            event.tick = static_cast<int>(std::lround(note->gridTick / static_cast<double>(TimingGrid::Sixteenth))) * TimingGrid::Sixteenth;
            event.timingOffset = note->timingOffsetTicks + (note->gridTick - event.tick); // sit with the kick's feel
            const float pick = std::uniform_real_distribution<float>(0.0f, 1.0f)(rng);
            event.pitchClass = pick < 0.22f ? (root + 7) % 12 : root;
            event.octaveUp = pick >= 0.22f && pick < 0.32f ? 1 : 0;
            event.velocity = 92 + std::uniform_int_distribution<int>(0, 12)(rng);
            event.role = "bass_kick";
            events.push_back(event);
            ++extras;
        }

        // 3. Approach the next bar's root from a scale step below (or above).
        const int nextRoot = roots[static_cast<size_t>((bar + 1) % bars)];
        const bool latePositionFree = std::none_of(events.begin(), events.end(), [&](const BassEvent& event)
        {
            return event.tick >= barStart + 13 * TimingGrid::Sixteenth && event.tick < barStart + ticksPerBar;
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
            event.tick = barStart + 14 * TimingGrid::Sixteenth;
            event.pitchClass = (juce::jlimit(0, 11, params.keyRoot) + scale[static_cast<size_t>(approachDegree)]) % 12;
            event.velocity = 84 + std::uniform_int_distribution<int>(0, 10)(rng);
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
} // namespace bbg
