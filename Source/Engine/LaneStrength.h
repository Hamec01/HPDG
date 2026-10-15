#pragma once

#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

#include "../Core/NoteEvent.h"
#include "../Core/TimingGrid.h"
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
} // namespace detail

// strength in -1..1. `reserve` is ordered (first = most idiomatic). loopTicks = bars * 3840.
inline std::vector<NoteEvent> apply(TrackType lane, std::vector<NoteEvent> notes, float strength, const std::vector<NoteEvent>& reserve,
                                    int loopTicks)
{
    strength = std::clamp(strength, -1.0f, 1.0f);
    if (std::abs(strength) < 0.005f || loopTicks <= 0)
        return notes;
    std::sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b) { return a.gridTick < b.gridTick; });
    const int bars = std::max(1, loopTicks / TimingGrid::TicksPerBar4_4);

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
} // namespace bbg::LaneStrength
