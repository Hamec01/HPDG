#include "SampleBassLineComposer.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>

#include "../Core/ProjectLaneAccess.h"
#include "../Core/ProjectStateController.h"
#include "../Core/TimingGrid.h"

namespace bbg
{
namespace
{
constexpr int kStep = TimingGrid::Sixteenth;
constexpr int kStepsPerBar = 16;
constexpr int kBarTicks = kStep * kStepsPerBar;

enum class Role
{
    FollowBass,
    Kicks,
    Melody
};

char roleLetter(Role role)
{
    switch (role)
    {
        case Role::FollowBass: return 'B';
        case Role::Kicks: return 'K';
        case Role::Melody: return 'M';
    }
    return '?';
}

// Register and habits per genre.
struct Shape
{
    bool trap = false;
    int low = 28;          // lowest pitch of the line
    int high = 50;         // highest
    int melodyCentre = 40; // where a melody's median lands when brought down
    int maxMelodyNotesPerBar = 6;
    float snareChance = 0.3f;
    float glideChance = 0.0f;
    float weightBass = 0.38f;
    float weightKicks = 0.32f;
    float weightMelody = 0.30f;
};

Shape shapeFor(GenreType genre, int keyRoot)
{
    Shape s;
    if (genre == GenreType::Trap)
    {
        // 808: around the key root in octave 1-2, long notes, glides between melody notes.
        s.trap = true;
        s.low = 24;
        s.high = 45;
        s.melodyCentre = 30 + ((keyRoot + 6) % 12);
        s.maxMelodyNotesPerBar = 5;
        s.snareChance = 0.12f;
        s.glideChance = 0.35f;
        s.weightBass = 0.33f;
        s.weightKicks = 0.42f;
        s.weightMelody = 0.25f;
    }
    else
    {
        s.low = 28;   // E1
        s.high = 52;  // E3
        s.melodyCentre = 40;
    }
    return s;
}

struct MappedNote
{
    int tick = 0;        // exact position in pattern ticks
    int endTick = 0;
    int pitch = 0;
    float strength = 0.0f;

    int step() const { return static_cast<int>(std::lround(tick / static_cast<double>(kStep))); }
    int offset() const { return juce::jlimit(-40, 40, tick - step() * kStep); }
};

// The sample's notes on the pattern grid. The sample loops (loopTicks) under the pattern.
std::vector<MappedNote> mapLine(const std::vector<SampleLineNote>& line, double secondsPerTick, double originSeconds,
                                int loopTicks, int patternTicks)
{
    std::vector<MappedNote> mapped;
    for (const auto& note : line)
    {
        double start = (note.startSeconds - originSeconds) / secondsPerTick;
        double end = (note.endSeconds - originSeconds) / secondsPerTick;
        // A pickup a little before beat 1 still belongs to beat 1; earlier notes are the end
        // of the previous pass of the loop.
        if (start < -kStep / 2.0)
        {
            start += loopTicks;
            end += loopTicks;
        }
        if (start >= loopTicks)
            continue;
        for (int pass = 0; pass * loopTicks < patternTicks; ++pass)
        {
            MappedNote m;
            m.tick = juce::jmax(0, static_cast<int>(std::lround(start)) + pass * loopTicks);
            if (m.tick >= patternTicks || m.step() * kStep >= patternTicks)
                break;
            m.endTick = juce::jmin(patternTicks, static_cast<int>(std::lround(end)) + pass * loopTicks);
            m.pitch = note.midiNote;
            m.strength = note.strength;
            mapped.push_back(m);
        }
    }
    std::sort(mapped.begin(), mapped.end(), [](const MappedNote& a, const MappedNote& b) { return a.tick < b.tick; });
    return mapped;
}

int placeNear(int pitch, int previous, int low, int high)
{
    const int pc = ((pitch % 12) + 12) % 12;
    int best = -1;
    for (int candidate = pc; candidate <= 127; candidate += 12)
    {
        if (candidate < low || candidate > high)
            continue;
        if (best < 0 || std::abs(candidate - previous) < std::abs(best - previous))
            best = candidate;
    }
    return best >= 0 ? best : juce::jlimit(low, high, pitch);
}

int foldInto(int pitch, int low, int high)
{
    while (pitch < low)
        pitch += 12;
    while (pitch > high)
        pitch -= 12;
    return pitch;
}

bool chance(std::mt19937& rng, float probability)
{
    return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) < probability;
}

struct Placed
{
    int step = 0;
    int offset = 0;
    int pitch = 36;
    int lengthSteps = 0;   // 0 = until the next note
    int velocity = 100;
    const char* role = "sample_line|bass";
    bool glideToNext = false;
    int priority = 0;      // which note wins when two land on one step
};
} // namespace

bool SampleBassLineComposer::wants(const PatternProject& project, const SampleHarmony& harmony)
{
    if (project.params.genre != GenreType::BoomBap && project.params.genre != GenreType::Trap)
        return false;
    const auto* bass = ProjectLaneAccess::findTrackState(project, TrackType::Sub808);
    if (bass == nullptr || bass->locked || !bass->enabled || bass->sub808Settings.bassAmount != 1)
        return false;
    return harmony.valid
        && (harmony.lines.bass.size() >= 2 || harmony.lines.melody.size() >= 3 || harmony.confidentBassNotes() >= 2);
}

SampleBassLineReport SampleBassLineComposer::compose(PatternProject& project,
                                                     const SampleHarmony& harmony,
                                                     double bpm,
                                                     double originSeconds,
                                                     std::mt19937& rng)
{
    SampleBassLineReport report;
    if (!wants(project, harmony) || bpm <= 20.0)
        return report;

    const int bars = juce::jmax(1, project.params.bars);
    const int patternTicks = bars * kBarTicks;
    const int patternSteps = bars * kStepsPerBar;
    const double secondsPerTick = 60.0 / (bpm * TimingGrid::PPQ);
    const double barSeconds = kBarTicks * secondsPerTick;
    const int keyRoot = juce::jlimit(0, 11, project.params.keyRoot);
    const auto shape = shapeFor(project.params.genre, keyRoot);

    // How long the sample loop is, in whole bars of the pattern's grid.
    double lastSecond = originSeconds;
    for (const auto& n : harmony.lines.bass)
        lastSecond = std::max(lastSecond, n.endSeconds);
    for (const auto& n : harmony.lines.melody)
        lastSecond = std::max(lastSecond, n.endSeconds);
    if (!harmony.bass.empty())
        lastSecond = std::max(lastSecond, harmony.bass.back().endSeconds);
    const int loopBars = juce::jlimit(1, 16, static_cast<int>(std::lround((lastSecond - originSeconds) / barSeconds)));
    const int loopTicks = loopBars * kBarTicks;
    const double loopSeconds = loopTicks * secondsPerTick;

    const auto bassLine = mapLine(harmony.lines.bass, secondsPerTick, originSeconds, loopTicks, patternTicks);
    const auto melodyLine = mapLine(harmony.lines.melody, secondsPerTick, originSeconds, loopTicks, patternTicks);

    // What the sample's bass plays at a step: the line where it sounds, else the beat estimate.
    auto samplePitchAt = [&](int step) -> int
    {
        const int tick = step * kStep + kStep / 2;
        for (const auto& n : bassLine)
            if (tick >= n.tick && tick < n.endTick)
                return n.pitch;
        const double seconds = originSeconds + std::fmod(step * kStep * secondsPerTick, loopSeconds);
        if (const auto* segment = harmony.segmentAt(seconds); segment != nullptr && segment->midiNote >= 0 && segment->confidence >= 0.35f)
            return segment->midiNote;
        return -1;
    };

    // Drums of the pattern (kick feel is copied onto notes that lock to it).
    std::map<int, int> kickOffsetByStep;
    std::vector<int> snareSteps;
    if (const auto* kick = ProjectLaneAccess::findTrackState(project, TrackType::Kick); kick != nullptr)
        for (const auto& k : kick->notes)
            if (!k.isGhost)
            {
                const int step = static_cast<int>(std::lround(k.gridTick / static_cast<double>(kStep)));
                kickOffsetByStep.emplace(step, k.timingOffsetTicks + (k.gridTick - step * kStep));
            }
    if (const auto* snare = ProjectLaneAccess::findTrackState(project, TrackType::Snare); snare != nullptr)
        for (const auto& s : snare->notes)
            if (!s.isGhost)
                snareSteps.push_back(static_cast<int>(std::lround(s.gridTick / static_cast<double>(kStep))));
    auto kickNear = [&](int step) -> int
    {
        for (const int candidate : { step, step - 1, step + 1 })
            if (kickOffsetByStep.count(candidate) > 0)
                return candidate;
        return -1;
    };

    // Per bar: what the sample offers.
    auto inBar = [](const std::vector<MappedNote>& line, int bar)
    {
        std::vector<MappedNote> result;
        for (const auto& n : line)
            if (n.step() >= bar * kStepsPerBar && n.step() < (bar + 1) * kStepsPerBar)
                result.push_back(n);
        return result;
    };
    auto bassCoverage = [&](int bar)
    {
        int covered = 0;
        for (int step = bar * kStepsPerBar; step < (bar + 1) * kStepsPerBar; ++step)
            covered += samplePitchAt(step) >= 0 ? 1 : 0;
        return covered / static_cast<float>(kStepsPerBar);
    };
    std::vector<bool> bassOk(static_cast<size_t>(bars));
    std::vector<bool> melodyOk(static_cast<size_t>(bars));
    std::vector<bool> kicksOk(static_cast<size_t>(bars));
    int bassBars = 0;
    int melodyBars = 0;
    for (int bar = 0; bar < bars; ++bar)
    {
        bassOk[static_cast<size_t>(bar)] = !inBar(bassLine, bar).empty() && bassCoverage(bar) >= 0.25f;
        melodyOk[static_cast<size_t>(bar)] = inBar(melodyLine, bar).size() >= 2;
        kicksOk[static_cast<size_t>(bar)] = std::any_of(kickOffsetByStep.begin(), kickOffsetByStep.end(), [bar](const auto& entry)
        {
            return entry.first >= bar * kStepsPerBar && entry.first < (bar + 1) * kStepsPerBar;
        });
        bassBars += bassOk[static_cast<size_t>(bar)] ? 1 : 0;
        melodyBars += melodyOk[static_cast<size_t>(bar)] ? 1 : 0;
    }

    // Phrase: a 2-bar cell of roles, repeated; every 4th bar may vary.
    const float bassShare = bassBars / static_cast<float>(bars);
    const float melodyShare = melodyBars / static_cast<float>(bars);
    auto pickRole = [&](std::optional<Role> avoid)
    {
        std::vector<std::pair<Role, float>> options {
            { Role::FollowBass, shape.weightBass * bassShare },
            { Role::Kicks, shape.weightKicks },
            { Role::Melody, shape.weightMelody * melodyShare }
        };
        float total = 0.0f;
        for (auto& option : options)
        {
            if (avoid.has_value() && option.first == *avoid)
                option.second *= 0.15f;
            total += option.second;
        }
        float roll = std::uniform_real_distribution<float>(0.0f, total)(rng);
        for (const auto& option : options)
        {
            if (roll < option.second)
                return option.first;
            roll -= option.second;
        }
        return Role::Kicks;
    };
    const Role first = pickRole(std::nullopt);
    const Role second = chance(rng, 0.55f) ? first : pickRole(first);
    const Role variation = pickRole(second);

    std::vector<Role> roles(static_cast<size_t>(bars));
    for (int bar = 0; bar < bars; ++bar)
    {
        Role role = bar % 2 == 0 ? first : second;
        if (bars >= 4 && bar % 4 == 3 && chance(rng, 0.6f))
            role = variation;
        // A role the sample cannot give in this bar falls back to the kicks (or its bass).
        if (role == Role::FollowBass && !bassOk[static_cast<size_t>(bar)])
            role = Role::Kicks;
        if (role == Role::Melody && !melodyOk[static_cast<size_t>(bar)])
            role = bassOk[static_cast<size_t>(bar)] ? Role::FollowBass : Role::Kicks;
        if (role == Role::Kicks && !kicksOk[static_cast<size_t>(bar)] && bassOk[static_cast<size_t>(bar)])
            role = Role::FollowBass;
        roles[static_cast<size_t>(bar)] = role;
        report.plan << (bar > 0 ? " " : "") << roleLetter(role);
    }

    // Melody brought down: one octave shift for the whole line keeps its contour.
    int melodyShift = 0;
    if (!melodyLine.empty())
    {
        std::vector<int> pitches;
        for (const auto& n : melodyLine)
            pitches.push_back(n.pitch);
        std::nth_element(pitches.begin(), pitches.begin() + static_cast<std::ptrdiff_t>(pitches.size() / 2), pitches.end());
        const int median = pitches[pitches.size() / 2];
        melodyShift = 12 * static_cast<int>(std::lround((shape.melodyCentre - median) / 12.0));
    }

    const int anchor = shape.trap ? foldInto(24 + keyRoot, shape.low, shape.low + 11) : foldInto(36 + keyRoot, 31, 42);
    std::vector<Placed> placed;
    int previousPitch = anchor;
    auto velocityFor = [&](float strength, int base) { return juce::jlimit(1, 127, base + static_cast<int>(std::lround(18.0f * strength))); };

    for (int bar = 0; bar < bars; ++bar)
    {
        const int barStart = bar * kStepsPerBar;
        const auto role = roles[static_cast<size_t>(bar)];
        size_t barFirst = placed.size();

        if (role == Role::FollowBass)
        {
            for (const auto& n : inBar(bassLine, bar))
            {
                Placed p;
                p.step = n.step();
                p.offset = n.offset();
                if (const int kick = kickNear(p.step); kick >= 0 && kick >= barStart && kick < barStart + kStepsPerBar)
                {
                    p.step = kick;
                    p.offset = kickOffsetByStep[kick];
                    ++report.onKick;
                }
                p.pitch = placeNear(n.pitch, previousPitch, shape.low, shape.high);
                p.lengthSteps = juce::jmax(1, static_cast<int>(std::lround((n.endTick - n.tick) / static_cast<double>(kStep))));
                p.velocity = velocityFor(n.strength, shape.trap ? 98 : 86);
                p.role = "sample_line|bass";
                p.priority = 2;
                previousPitch = p.pitch;
                placed.push_back(p);
                ++report.fromSampleBass;
            }
        }
        else if (role == Role::Kicks)
        {
            std::vector<int> kicks;
            for (const auto& [step, offset] : kickOffsetByStep)
                if (step >= barStart && step < barStart + kStepsPerBar)
                    kicks.push_back(step);
            for (size_t i = 0; i < kicks.size(); ++i)
            {
                // Boom bap: a kick right after the previous one lets the note ring instead.
                if (!shape.trap && i > 0 && kicks[i] - kicks[i - 1] <= 1 && chance(rng, 0.6f))
                    continue;
                int pitch = samplePitchAt(kicks[i]);
                if (pitch < 0)
                    pitch = previousPitch;
                Placed p;
                p.step = kicks[i];
                p.offset = kickOffsetByStep[kicks[i]];
                p.pitch = placeNear(pitch, previousPitch, shape.low, shape.high);
                p.velocity = shape.trap ? 104 + static_cast<int>(rng() % 12) : 94 + static_cast<int>(rng() % 12);
                p.role = "sample_line|kick";
                p.priority = 3;
                previousPitch = p.pitch;
                placed.push_back(p);
                ++report.onKick;
            }
        }
        else // Melody
        {
            auto notes = inBar(melodyLine, bar);
            // Thin out to the strongest / longest notes (the first one always stays).
            if (static_cast<int>(notes.size()) > shape.maxMelodyNotesPerBar)
            {
                std::vector<size_t> order(notes.size());
                for (size_t i = 0; i < order.size(); ++i)
                    order[i] = i;
                std::sort(order.begin() + 1, order.end(), [&](size_t a, size_t b)
                {
                    return (notes[a].endTick - notes[a].tick) * (0.5f + notes[a].strength)
                         > (notes[b].endTick - notes[b].tick) * (0.5f + notes[b].strength);
                });
                order.resize(static_cast<size_t>(shape.maxMelodyNotesPerBar));
                std::sort(order.begin(), order.end());
                std::vector<MappedNote> kept;
                for (const auto index : order)
                    kept.push_back(notes[index]);
                notes = kept;
            }
            for (size_t i = 0; i < notes.size(); ++i)
            {
                const auto& n = notes[i];
                Placed p;
                p.step = n.step();
                p.offset = n.offset();
                if (i == 0)
                    if (const int kick = kickNear(p.step); kick >= barStart && kick < barStart + kStepsPerBar)
                    {
                        p.step = kick;
                        p.offset = kickOffsetByStep[kick];
                        ++report.onKick;
                    }
                p.pitch = foldInto(n.pitch + melodyShift, shape.low, shape.high);
                p.lengthSteps = juce::jmax(1, static_cast<int>(std::lround((n.endTick - n.tick) / static_cast<double>(kStep))));
                p.velocity = velocityFor(n.strength, shape.trap ? 96 : 80);
                p.role = "sample_line|melody";
                p.priority = 1;
                // Trap: the 808 slides into the next melody note now and then.
                if (shape.trap && i + 1 < notes.size())
                {
                    const int interval = std::abs(foldInto(notes[i + 1].pitch + melodyShift, shape.low, shape.high) - p.pitch);
                    const bool close = notes[i + 1].tick - n.endTick <= kStep;
                    if (close && interval >= 2 && interval <= 12 && chance(rng, shape.glideChance))
                    {
                        p.glideToNext = true;
                        p.lengthSteps = 0; // legato into the next note
                    }
                }
                previousPitch = p.pitch;
                placed.push_back(p);
                ++report.fromMelody;
            }
        }

        // "Start with the kick": a line that comes in late in the bar gets a note on the bar's
        // first kick, holding the sample's bass there (or the line's first pitch).
        if (role != Role::Kicks && placed.size() > barFirst && placed[barFirst].step > barStart + 2)
        {
            const auto firstKick = std::find_if(kickOffsetByStep.begin(), kickOffsetByStep.end(), [&](const auto& entry)
            {
                return entry.first >= barStart && entry.first < placed[barFirst].step - 1;
            });
            if (firstKick != kickOffsetByStep.end() && chance(rng, 0.55f))
            {
                const int pitch = samplePitchAt(firstKick->first);
                Placed p;
                p.step = firstKick->first;
                p.offset = firstKick->second;
                p.pitch = placeNear(pitch >= 0 ? pitch : placed[barFirst].pitch, placed[barFirst].pitch, shape.low, shape.high);
                p.velocity = shape.trap ? 104 : 92;
                p.role = "sample_line|kick";
                p.priority = 3;
                placed.push_back(p);
                ++report.onKick;
            }
        }

        // Now and then a short answer with the snare (octave or fifth over what sounds).
        if (chance(rng, shape.snareChance))
        {
            std::vector<int> barSnares;
            for (const int step : snareSteps)
                if (step >= barStart && step < barStart + kStepsPerBar)
                    barSnares.push_back(step);
            if (!barSnares.empty())
            {
                const int step = barSnares.back();
                const bool taken = std::any_of(placed.begin() + static_cast<std::ptrdiff_t>(barFirst), placed.end(), [step](const Placed& p) { return p.step == step; });
                if (!taken)
                {
                    int sounding = samplePitchAt(step);
                    if (sounding < 0)
                        sounding = previousPitch;
                    const int base = placeNear(sounding, previousPitch, shape.low, shape.high);
                    Placed p;
                    p.step = step;
                    p.offset = 0;
                    p.pitch = foldInto(base + (chance(rng, 0.65f) ? 12 : 7), shape.low, shape.high + 7);
                    p.lengthSteps = 2;
                    p.velocity = shape.trap ? 92 : 78;
                    p.role = "sample_line|snare";
                    p.priority = 0;
                    placed.push_back(p);
                    ++report.withSnare;
                }
            }
        }
    }

    // One note per step (the higher-priority role wins), monophonic lengths.
    std::sort(placed.begin(), placed.end(), [](const Placed& a, const Placed& b)
    {
        return a.step != b.step ? a.step < b.step : a.priority > b.priority;
    });
    placed.erase(std::unique(placed.begin(), placed.end(), [](const Placed& a, const Placed& b) { return a.step == b.step; }), placed.end());
    placed.erase(std::remove_if(placed.begin(), placed.end(), [patternSteps](const Placed& p) { return p.step < 0 || p.step >= patternSteps; }), placed.end());
    if (placed.empty())
        return report;

    constexpr int kBreath = 30;
    std::vector<NoteEvent> notes;
    for (size_t i = 0; i < placed.size(); ++i)
    {
        const auto& p = placed[i];
        const int tick = p.step * kStep;
        const int nextTick = i + 1 < placed.size() ? placed[i + 1].step * kStep : patternTicks;
        const int room = nextTick - tick - (p.glideToNext ? 0 : kBreath);
        NoteEvent n;
        n.gridTick = tick;
        n.timingOffsetTicks = p.offset;
        n.pitch = juce::jlimit(12, 72, p.pitch);
        // Trap 808s and kick notes ring until the next note; the sample's own notes keep their length.
        const bool ring = p.lengthSteps == 0 || (shape.trap && p.priority >= 2);
        n.lengthTicks = juce::jmax(TimingGrid::ThirtySecond, ring ? room : juce::jmin(p.lengthSteps * kStep - kBreath, room));
        n.velocity = p.velocity;
        n.semanticRole = p.role;
        n.glideToNext = p.glideToNext;
        if (i > 0 && placed[i - 1].glideToNext)
            n.isSlide = true;
        notes.push_back(n);
    }

    ProjectStateController::setTrackNotes(project, TrackType::Sub808, notes);
    report.applied = true;
    report.notes = static_cast<int>(notes.size());
    return report;
}

juce::String describeSampleBassLineReport(const SampleBassLineReport& report)
{
    if (!report.applied)
        return "Sample bass line [2]: not applied";
    return "Sample bass line [2]: bars " + report.plan
        + " | notes " + juce::String(report.notes)
        + " | from sample bass " + juce::String(report.fromSampleBass)
        + " | from melody " + juce::String(report.fromMelody)
        + " | on kick " + juce::String(report.onKick)
        + " | with snare " + juce::String(report.withSnare);
}
} // namespace bbg
