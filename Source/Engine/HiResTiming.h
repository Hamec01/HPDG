#pragma once

#include <algorithm>
#include <cmath>

#include "../Core/NoteEvent.h"
#include "../Core/TimingGrid.h"
#include "../Core/TrackState.h"
#include "../Utils/TimingHelpers.h"

namespace bbg
{
namespace HiResTiming
{
// These alias TimingGrid (Source/Core/TimingGrid.h), the single canonical source for PPQ-960
// tick math — kept here under their historical names so existing call sites (Trap/Drill/Rap/
// BoomBap hat generators) don't need to change in this stage.
constexpr int kTicksPerQuarter = TimingGrid::Quarter;
constexpr int kTicksPerBar4_4 = TimingGrid::TicksPerBar4_4;
constexpr int kTicks1_8 = TimingGrid::Eighth;             // 480
constexpr int kTicks1_16 = TimingGrid::Sixteenth;         // 240
constexpr int kTicks1_32 = TimingGrid::ThirtySecond;      // 120
constexpr int kTicks1_64 = TimingGrid::SixtyFourth;       // 60
constexpr int kTicks1_12 = TimingGrid::EighthTriplet;     // 320 (1/8 triplet)
constexpr int kTicks1_24 = TimingGrid::SixteenthTriplet;  // 160 (1/16 triplet)

inline int quantizeTicks(int tick, int divisionTicks)
{
    const int safeDiv = std::max(1, divisionTicks);
    return static_cast<int>(std::round(static_cast<double>(tick) / static_cast<double>(safeDiv))) * safeDiv;
}

// `tick` is the true structural (grid) position — it is stored as-is in gridTick, not
// decomposed into a 1/16 step + remainder. This is what makes a 1/32/1/64/triplet position
// a real structural position rather than a disguised 1/16 + microOffset.
// `lengthSteps` keeps its historical unit (count of 1/16s) since every existing caller already
// thinks in those terms; it is converted to lengthTicks internally.
inline void addNoteAtTick(TrackState& track,
                          int pitch,
                          int tick,
                          int velocity,
                          bool isGhost,
                          int totalBars,
                          int lengthSteps = 1)
{
    const int bars = std::max(1, totalBars);
    const int totalTicks = bars * TimingGrid::TicksPerBar4_4;
    const int clampedTick = std::clamp(tick, 0, std::max(0, totalTicks - 1));

    NoteEvent note;
    note.pitch = pitch;
    note.gridTick = clampedTick;
    note.timingOffsetTicks = 0;
    note.lengthTicks = std::max(1, lengthSteps) * TimingGrid::Sixteenth;
    note.velocity = std::clamp(velocity, 1, 127);
    note.isGhost = isGhost;
    track.notes.push_back(note);
}

inline int noteTick(const NoteEvent& note)
{
    return note.startTick();
}
}
} // namespace bbg
