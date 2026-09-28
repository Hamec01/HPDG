#pragma once

// Canonical PPQ-960 tick math. This is the single source of truth for "how many ticks is
// 1/16", "where does bar N start", "quantize this tick to this division" etc.
//
// Nothing outside this file should redefine these constants or reimplement this arithmetic.
// HiResTiming.h's kTicks1_* constants and GridEditorComponent's per-resolution tick math both
// alias/delegate to this file (see HiResTiming.h and GridEditorComponent.cpp) so there is
// exactly one place that can get the numbers wrong.
//
// Stage 1 of the TIMING GRID V2 migration: this header only adds shared math. It does not
// change NoteEvent, generators, or the editor's coordinate system yet.

#include <algorithm>
#include <cmath>

#include "GridDivision.h"
#include "../Utils/TimingHelpers.h"

namespace bbg::TimingGrid
{
constexpr int PPQ = kInternalPpq; // 960 — kept as the one canonical definition in TimingHelpers.h.
static_assert(PPQ == 960, "TimingGrid assumes PPQ=960; update the constants below if this ever changes.");

constexpr int TicksPerBeat = PPQ;
constexpr int TicksPerBar4_4 = PPQ * 4; // 3840

constexpr int Quarter = PPQ;                    // 960
constexpr int QuarterTriplet = PPQ * 2 / 3;     // 640

constexpr int Eighth = PPQ / 2;                 // 480
constexpr int EighthTriplet = PPQ / 3;          // 320

constexpr int Sixteenth = PPQ / 4;              // 240
constexpr int SixteenthTriplet = PPQ / 6;       // 160

constexpr int ThirtySecond = PPQ / 8;           // 120
constexpr int ThirtySecondTriplet = PPQ / 12;   // 80

constexpr int SixtyFourth = PPQ / 16;           // 60
constexpr int SixtyFourthTriplet = PPQ / 24;    // 40

// Division -> ticks. Auto falls back to Sixteenth (a neutral default; genre-specific "Auto"
// generation behaviour lives in the generators, not here). Free returns 1 (no quantization).
constexpr int divisionTicks(GridDivision division) noexcept
{
    switch (division)
    {
        case GridDivision::Auto:                return Sixteenth;
        case GridDivision::Free:                return 1;
        case GridDivision::Quarter:             return Quarter;
        case GridDivision::QuarterTriplet:      return QuarterTriplet;
        case GridDivision::Eighth:               return Eighth;
        case GridDivision::EighthTriplet:        return EighthTriplet;
        case GridDivision::Sixteenth:            return Sixteenth;
        case GridDivision::SixteenthTriplet:     return SixteenthTriplet;
        case GridDivision::ThirtySecond:         return ThirtySecond;
        case GridDivision::ThirtySecondTriplet:  return ThirtySecondTriplet;
        case GridDivision::SixtyFourth:          return SixtyFourth;
        case GridDivision::SixtyFourthTriplet:   return SixtyFourthTriplet;
    }
    return Sixteenth;
}

constexpr int barStartTick(int bar) noexcept
{
    return bar * TicksPerBar4_4;
}

constexpr int beatStartTick(int bar, int beat) noexcept
{
    return barStartTick(bar) + beat * TicksPerBeat;
}

// Position within the bar, always in [0, TicksPerBar4_4), even for negative input ticks.
inline int tickInBar(int absoluteTick) noexcept
{
    const int wrapped = absoluteTick % TicksPerBar4_4;
    return wrapped < 0 ? wrapped + TicksPerBar4_4 : wrapped;
}

inline int barIndexFromTick(int absoluteTick) noexcept
{
    if (absoluteTick >= 0)
        return absoluteTick / TicksPerBar4_4;

    return -(((-absoluteTick) + TicksPerBar4_4 - 1) / TicksPerBar4_4);
}

inline int quantizeTick(int tick, int divisionTicksValue) noexcept
{
    const int safeDiv = std::max(1, divisionTicksValue);
    return static_cast<int>(std::lround(static_cast<double>(tick) / static_cast<double>(safeDiv))) * safeDiv;
}

inline int quantizeTick(int tick, GridDivision division) noexcept
{
    return quantizeTick(tick, divisionTicks(division));
}

inline bool isOnGrid(int tick, GridDivision division) noexcept
{
    const int div = divisionTicks(division);
    if (div <= 1)
        return true; // Free (or degenerate): every tick counts as "on grid".

    return (tick % div) == 0;
}
} // namespace bbg::TimingGrid
