#pragma once

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <vector>

#include "../Core/NoteEvent.h"
#include "../Core/TimingGrid.h"
#include "../Core/GeneratorParams.h"
#include "../Core/TrackType.h"

namespace bbg::LaneStrength
{
// Lane strength (maintainer, 2026-10-15): one knob per lane in place of pan. 0 = the lane as
// generated; left removes hits, weakest first (ghosts, support, off-grid detail), keeping the
// lane's backbone (the first kick of a bar, the backbeat snare, the first 808 of a bar); right adds hits from
// the reserve: positions the genre's own engine plays on this lane in alternative generations,
// most frequent first, never a 16th next to a kick / snare / 808 already there.
// The knob is monotonic: turning further only removes (or adds) more of the same ordered list.

namespace detail
{
inline int stepOf(const NoteEvent& n)
{
    return static_cast<int>(std::lround(static_cast<double>(n.gridTick) / TimingGrid::Sixteenth));
}

inline float metricWeight(int step)
{
    step = ((step % 16) + 16) % 16;
    if (step == 0) return 1.00f;
    if (step == 8) return 0.75f;
    if (step % 4 == 0) return 0.60f;
    if (step % 2 == 0) return 0.35f;
    return 0.15f;
}

inline float roleWeight(const juce::String& semanticRole)
{
    const auto r = semanticRole.toLowerCase();
    if (r.contains("backbone") || r.contains("anchor") || r.contains("axis") || r.contains("main") || r.contains("clap"))
        return 1.0f;
    if (r.contains("accent") || r.contains("downbeat"))
        return 0.6f;
    if (r.contains("ghost") || r.contains("fill") || r.contains("support") || r.contains("pickup") || r.contains("extra")
        || r.contains("sample_accent") || r.contains("roll"))
        return 0.1f;
    return 0.4f;
}

inline float importance(const NoteEvent& n)
{
    return roleWeight(n.semanticRole) + 0.6f * metricWeight(stepOf(n)) + 0.5f * static_cast<float>(n.velocity) / 127.0f
           - (n.isGhost ? 0.6f : 0.0f);
}

inline bool tightLane(TrackType lane)
{
    return lane == TrackType::Kick || lane == TrackType::GhostKick || lane == TrackType::Snare || lane == TrackType::ClapGhostSnare
           || lane == TrackType::Sub808;
}

inline int loopDistance(int a, int b, int loop)
{
    const int d = std::abs(a - b) % std::max(1, loop);
    return std::min(d, loop - d);
}

// The notes a lane keeps at full left.
inline std::vector<bool> protectedNotes(TrackType lane, const std::vector<NoteEvent>& notes, int bars)
{
    std::vector<bool> keep(notes.size(), false);
    if (lane == TrackType::Kick)
    {
        // The first kick of every bar (the one on 1 when there is one).
        std::set<int> seenBars;
        for (size_t i = 0; i < notes.size(); ++i) // notes are sorted by time
            if (!notes[i].isGhost && seenBars.insert(stepOf(notes[i]) / 16).second)
                keep[i] = true;
    }
    else if (lane == TrackType::Snare || lane == TrackType::ClapGhostSnare)
    {
        // Backbeat = the steps a non-ghost snare plays in at least half of the bars.
        std::vector<std::set<int>> perStep(16);
        for (const auto& n : notes)
            if (!n.isGhost)
                perStep[static_cast<size_t>(((stepOf(n) % 16) + 16) % 16)].insert(stepOf(n) / 16);
        for (size_t i = 0; i < notes.size(); ++i)
        {
            const int s = ((stepOf(notes[i]) % 16) + 16) % 16;
            keep[i] = !notes[i].isGhost && static_cast<int>(perStep[static_cast<size_t>(s)].size()) * 2 >= std::max(1, bars);
        }
    }
    else if (lane == TrackType::Sub808)
    {
        std::set<int> seenBars;
        for (size_t i = 0; i < notes.size(); ++i) // notes are sorted by time
            if (seenBars.insert(stepOf(notes[i]) / 16).second)
                keep[i] = true;
    }
    return keep;
}
// Trap hats (maintainer, 2026-10-17): turning a hat lane down first removes whole rolls (never a
// fragment of one), then the 16ths between the 8ths, then the 8ths between the quarters; each grid
// position goes in every bar at once (the rarest position first), so what is left stays a straight,
// repeating pulse. Hats on quarter notes are kept. Returns the removal units in order; notes in no
// unit are protected.
inline bool trapHatLane(TrackType lane, GenreType genre)
{
    return genre == GenreType::Trap && (lane == TrackType::HiHat || lane == TrackType::HatFX);
}

inline std::vector<std::vector<size_t>> trapHatRemovalUnits(const std::vector<NoteEvent>& notes) // sorted by time
{
    const auto isRoll = [](const NoteEvent& n) { return n.semanticRole.containsIgnoreCase("roll"); };
    std::vector<std::vector<size_t>> rolls;
    int lastRollTick = 0;
    for (size_t i = 0; i < notes.size(); ++i)
    {
        if (!isRoll(notes[i]))
            continue;
        if (rolls.empty() || notes[i].gridTick - lastRollTick >= TimingGrid::Sixteenth)
            rolls.emplace_back();
        rolls.back().push_back(i);
        lastRollTick = notes[i].gridTick;
    }
    // The weakest roll first (mean importance), the later one on a tie.
    const auto meanImportance = [&](const std::vector<size_t>& unit)
    {
        float sum = 0.0f;
        for (const auto i : unit)
            sum += importance(notes[i]);
        return sum / static_cast<float>(unit.size());
    };
    std::stable_sort(rolls.begin(), rolls.end(), [&](const auto& a, const auto& b)
    {
        const float ia = meanImportance(a);
        const float ib = meanImportance(b);
        return ia != ib ? ia < ib : notes[a.front()].gridTick > notes[b.front()].gridTick;
    });

    // Straight hats grouped by their position in the bar (a 16th, or an off-grid tick).
    std::map<int, std::vector<size_t>> sixteenths, eighths;
    for (size_t i = 0; i < notes.size(); ++i)
    {
        if (isRoll(notes[i]))
            continue;
        const int inBar = ((notes[i].gridTick % TimingGrid::TicksPerBar4_4) + TimingGrid::TicksPerBar4_4) % TimingGrid::TicksPerBar4_4;
        if (inBar % (4 * TimingGrid::Sixteenth) == 0)
            continue; // quarter notes stay
        (inBar % (2 * TimingGrid::Sixteenth) == 0 ? eighths : sixteenths)[inBar].push_back(i);
    }
    const auto byRarity = [](std::map<int, std::vector<size_t>>& columns)
    {
        std::vector<std::pair<int, std::vector<size_t>>> ordered(columns.begin(), columns.end());
        std::stable_sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b)
        {
            if (a.second.size() != b.second.size())
                return a.second.size() < b.second.size();
            return metricWeight(a.first / TimingGrid::Sixteenth) < metricWeight(b.first / TimingGrid::Sixteenth);
        });
        std::vector<std::vector<size_t>> units;
        for (auto& column : ordered)
            units.push_back(std::move(column.second));
        return units;
    };

    auto units = std::move(rolls);
    for (auto& unit : byRarity(sixteenths))
        units.push_back(std::move(unit));
    for (auto& unit : byRarity(eighths))
        units.push_back(std::move(unit));
    return units;
}
} // namespace detail

// Hats thinned on the grid (Trap knob, every genre at Intensity 2): whole units in
// detail::trapHatRemovalUnits order until the share of removable hats `strength` asks for is gone.
inline std::vector<NoteEvent> removeHatsByGrid(std::vector<NoteEvent> notes, float strength)
{
    std::sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b) { return a.gridTick < b.gridTick; });
    const auto units = detail::trapHatRemovalUnits(notes);
    size_t removable = 0;
    for (const auto& unit : units)
        removable += unit.size();
    const auto count = static_cast<size_t>(std::lround(std::clamp(-strength, 0.0f, 1.0f) * static_cast<float>(removable)));
    std::vector<bool> drop(notes.size(), false);
    size_t removed = 0;
    for (const auto& unit : units)
    {
        if (removed >= count)
            break;
        for (const auto i : unit)
            drop[i] = true;
        removed += unit.size();
    }
    std::vector<NoteEvent> out;
    for (size_t i = 0; i < notes.size(); ++i)
        if (!drop[i])
            out.push_back(notes[i]);
    return out;
}

// strength in -1..1. `reserve` is ordered (first = most idiomatic). loopTicks = bars * 3840.
inline std::vector<NoteEvent> apply(TrackType lane, std::vector<NoteEvent> notes, float strength, const std::vector<NoteEvent>& reserve,
                                    int loopTicks, GenreType genre = GenreType::BoomBap)
{
    strength = std::clamp(strength, -1.0f, 1.0f);
    if (std::abs(strength) < 0.005f || loopTicks <= 0)
        return notes;
    std::sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b) { return a.gridTick < b.gridTick; });
    const int bars = std::max(1, loopTicks / TimingGrid::TicksPerBar4_4);

    if (strength < 0.0f && detail::trapHatLane(lane, genre))
        return removeHatsByGrid(std::move(notes), strength);

    if (strength < 0.0f)
    {
        const auto keep = detail::protectedNotes(lane, notes, bars);
        std::vector<size_t> removable;
        for (size_t i = 0; i < notes.size(); ++i)
            if (!keep[i])
                removable.push_back(i);
        std::stable_sort(removable.begin(), removable.end(), [&](size_t a, size_t b)
        {
            const float ia = detail::importance(notes[a]);
            const float ib = detail::importance(notes[b]);
            return ia != ib ? ia < ib : notes[a].gridTick > notes[b].gridTick;
        });
        const auto count = static_cast<size_t>(std::lround(-strength * static_cast<float>(removable.size())));
        std::vector<bool> drop(notes.size(), false);
        for (size_t k = 0; k < count && k < removable.size(); ++k)
            drop[removable[k]] = true;
        std::vector<NoteEvent> out;
        for (size_t i = 0; i < notes.size(); ++i)
        {
            if (!drop[i])
            {
                out.push_back(notes[i]);
                continue;
            }
            // A removed 808 retrigger: the note before it rings on instead.
            if (lane == TrackType::Sub808 && !out.empty())
            {
                out.back().lengthTicks = std::max(out.back().lengthTicks, notes[i].gridTick + notes[i].lengthTicks - out.back().gridTick);
                out.back().glideToNext = notes[i].glideToNext;
            }
        }
        return out;
    }

    const auto count = static_cast<size_t>(std::lround(strength * static_cast<float>(reserve.size())));
    const int minGap = detail::tightLane(lane) ? TimingGrid::Sixteenth : TimingGrid::Sixteenth / 2;
    for (size_t k = 0; k < count && k < reserve.size(); ++k)
    {
        auto add = reserve[k];
        if (add.gridTick < 0 || add.gridTick >= loopTicks)
            continue;
        const bool crowded = std::any_of(notes.begin(), notes.end(), [&](const NoteEvent& n)
        {
            return detail::loopDistance(n.gridTick, add.gridTick, loopTicks) < minGap + (detail::tightLane(lane) ? 1 : 0);
        });
        if (crowded)
            continue;
        add.semanticRole = "strength_add";
        if (lane == TrackType::Sub808 && !notes.empty())
        {
            // A retrigger of the 808 sounding there: its pitch, the previous note ends at the new one.
            auto previous = std::max_element(notes.begin(), notes.end(), [&](const NoteEvent& a, const NoteEvent& b)
            {
                const int da = (add.gridTick - a.gridTick + loopTicks) % loopTicks;
                const int db = (add.gridTick - b.gridTick + loopTicks) % loopTicks;
                return da > db; // max_element of "smaller distance back"
            });
            add.pitch = previous->pitch;
            add.glideToNext = false;
            add.isSlide = false;
            if (previous->gridTick < add.gridTick && previous->gridTick + previous->lengthTicks > add.gridTick)
            {
                previous->lengthTicks = add.gridTick - previous->gridTick;
                previous->glideToNext = false;
            }
            int next = loopTicks + std::min_element(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b) { return a.gridTick < b.gridTick; })->gridTick;
            for (const auto& n : notes)
                if (n.gridTick > add.gridTick)
                    next = std::min(next, n.gridTick);
            add.lengthTicks = std::max(TimingGrid::Sixteenth, next - add.gridTick);
        }
        notes.push_back(add);
        std::sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b) { return a.gridTick < b.gridTick; });
    }
    return notes;
}

// Builds the ordered reserve from alternative generations of the lane: positions (16ths) absent
// from `current`, most frequent across the variants first, then the stronger metric position.
inline std::vector<NoteEvent> buildReserve(const std::vector<NoteEvent>& current, const std::vector<std::vector<NoteEvent>>& variants, int loopTicks)
{
    std::set<int> taken;
    for (const auto& n : current)
        taken.insert(detail::stepOf(n));
    struct Slot
    {
        int count = 0;
        NoteEvent note;
    };
    std::vector<Slot> slots(static_cast<size_t>(std::max(1, loopTicks / TimingGrid::Sixteenth)));
    for (const auto& v : variants)
    {
        std::set<int> seen;
        for (const auto& n : v)
        {
            const int s = detail::stepOf(n);
            if (s < 0 || s >= static_cast<int>(slots.size()) || taken.count(s) > 0 || !seen.insert(s).second)
                continue;
            auto& slot = slots[static_cast<size_t>(s)];
            if (slot.count == 0 || n.velocity > slot.note.velocity)
                slot.note = n;
            ++slot.count;
        }
    }
    std::vector<std::pair<int, size_t>> order;
    for (size_t s = 0; s < slots.size(); ++s)
        if (slots[s].count > 0)
            order.push_back({ slots[s].count, s });
    std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b)
    {
        if (a.first != b.first)
            return a.first > b.first;
        return detail::metricWeight(static_cast<int>(a.second)) > detail::metricWeight(static_cast<int>(b.second));
    });
    std::vector<NoteEvent> reserve;
    for (const auto& [count, s] : order)
    {
        auto n = slots[s].note;
        n.gridTick = static_cast<int>(s) * TimingGrid::Sixteenth;
        reserve.push_back(n);
    }
    return reserve;
}
// Rack Intensity (maintainer, 2026-10-17): a 1-5 generation mode. The switch only selects it; the
// next Generate / Mutate / lane RG writes its lanes in that mode (the result is the lane's base,
// the lane Strength knobs work on it as before). 3 = the engines as they are.
//   1  the primitive beat (`primitive`): straight, on the grid, no ghosts / rolls / accents.
//   2  clearly fewer: no ghost notes and no rolls, then every lane thinned by -0.6 x its weight
//      (hats on the grid: 16ths, then 8ths between quarters, whole positions at once).
//   4, 5  more: hits added from the lane's reserve, +0.35 / +0.70 x its weight.
// Lane weight: detail lanes 1.0, kick / 808 0.6, snare 0.5 (their backbone is kept anyway).
inline float intensityLaneWeight(TrackType lane)
{
    switch (lane)
    {
        case TrackType::Kick:
        case TrackType::Sub808: return 0.6f;
        case TrackType::Snare: return 0.5f;
        default: return 1.0f;
    }
}

inline bool isRollNote(const NoteEvent& n) { return n.semanticRole.containsIgnoreCase("roll"); }

// Level 1. Every genre: the backbeat snare, the first 808 of a bar ringing on, every note on its
// grid position (no swing / humanize offsets); ghost kick, ghost snare, hat accents, open hats,
// ride, cymbal and percussion silent. Hats and kicks per genre:
//   Boom Bap     hats on quarter notes ("x---x---x---x---"), kick on 1 and 3;
//   Atmospheric  hats on quarter notes, kicks on quarter notes plus the first kick of a bar;
//   others       hats on every straight 8th, kicks on quarter notes plus the first kick of a bar.
inline std::vector<NoteEvent> primitive(TrackType lane, std::vector<NoteEvent> notes, int loopTicks, GenreType genre)
{
    std::sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b) { return a.gridTick < b.gridTick; });
    const int bars = std::max(1, loopTicks / TimingGrid::TicksPerBar4_4);
    const bool boomBap = genre == GenreType::BoomBap;
    const bool quarterHats = boomBap || genre == GenreType::Atmospheric; // sparse genres: 8ths would add hats
    const auto inBar = [](int tick) { return ((tick % TimingGrid::TicksPerBar4_4) + TimingGrid::TicksPerBar4_4) % TimingGrid::TicksPerBar4_4; };
    const auto straight = [](NoteEvent n)
    {
        n.timingOffsetTicks = 0;
        n.isGhost = false;
        return n;
    };
    // One hit on each of `positions` (ticks in the bar) in every bar: the lane's own note where it
    // plays one, otherwise a copy of its typical hit (first note, mean level).
    const auto onGrid = [&](const std::vector<int>& positions)
    {
        std::vector<NoteEvent> out;
        std::vector<NoteEvent> usable;
        for (const auto& n : notes)
            if (!n.isGhost && !isRollNote(n))
                usable.push_back(n);
        if (usable.empty())
            return out;
        int velocitySum = 0;
        for (const auto& n : usable)
            velocitySum += n.velocity;
        auto filler = straight(usable.front());
        filler.velocity = velocitySum / static_cast<int>(usable.size());
        filler.semanticRole = "intensity_grid";
        for (int bar = 0; bar < bars; ++bar)
            for (const int position : positions)
            {
                const int tick = bar * TimingGrid::TicksPerBar4_4 + position;
                const auto it = std::find_if(usable.begin(), usable.end(), [&](const NoteEvent& n) { return n.gridTick == tick; });
                auto n = it != usable.end() ? straight(*it) : filler;
                n.gridTick = tick;
                out.push_back(n);
            }
        return out;
    };

    std::vector<NoteEvent> out;
    switch (lane)
    {
        case TrackType::Kick:
        {
            if (boomBap)
                return onGrid({ 0, 8 * TimingGrid::Sixteenth });
            const auto first = detail::protectedNotes(lane, notes, bars);
            for (size_t i = 0; i < notes.size(); ++i)
                if (!notes[i].isGhost && (first[i] || inBar(notes[i].gridTick) % (4 * TimingGrid::Sixteenth) == 0))
                    out.push_back(straight(notes[i]));
            return out;
        }
        case TrackType::Snare:
        {
            const auto backbeat = detail::protectedNotes(lane, notes, bars);
            for (size_t i = 0; i < notes.size(); ++i)
                if (backbeat[i])
                    out.push_back(straight(notes[i]));
            return out;
        }
        case TrackType::HiHat:
        {
            std::vector<int> positions;
            const int pulse = (quarterHats ? 4 : 2) * TimingGrid::Sixteenth;
            for (int tick = 0; tick < TimingGrid::TicksPerBar4_4; tick += pulse)
                positions.push_back(tick);
            return onGrid(positions);
        }
        case TrackType::Sub808:
        {
            out = apply(lane, std::move(notes), -1.0f, {}, loopTicks); // first of every bar, ringing on
            for (auto& n : out)
                n.timingOffsetTicks = 0;
            return out;
        }
        default:
            return out;
    }
}

// The lane as the rack Intensity writes it at generation. `reserve`: hits for levels 4 / 5.
inline std::vector<NoteEvent> shapeForIntensity(TrackType lane, std::vector<NoteEvent> notes, int level,
                                                const std::vector<NoteEvent>& reserve, int loopTicks, GenreType genre)
{
    level = std::clamp(level, 1, 5);
    if (level == 3)
        return notes;
    if (level == 1)
        return primitive(lane, std::move(notes), loopTicks, genre);
    if (level == 2)
    {
        if (lane == TrackType::GhostKick || lane == TrackType::ClapGhostSnare)
            return {};
        if (lane != TrackType::Sub808) // a bass line's soft notes are part of the line, not ghosts to drop
            notes.erase(std::remove_if(notes.begin(), notes.end(), [](const NoteEvent& n) { return n.isGhost || isRollNote(n); }), notes.end());
        if (lane == TrackType::HiHat || lane == TrackType::HatFX)
            return removeHatsByGrid(std::move(notes), -0.6f); // a straight pulse is left, in every genre
        return apply(lane, std::move(notes), -0.6f * intensityLaneWeight(lane), {}, loopTicks, genre);
    }
    return apply(lane, std::move(notes), (level == 4 ? 0.35f : 0.70f) * intensityLaneWeight(lane), reserve, loopTicks, genre);
}

} // namespace bbg::LaneStrength
