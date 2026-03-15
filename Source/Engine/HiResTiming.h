#pragma once

#include <algorithm>
#include <cmath>

#include "../Core/NoteEvent.h"
#include "../Core/TrackState.h"
#include "../Utils/TimingHelpers.h"

namespace bbg
{
namespace HiResTiming
{
constexpr int kTicksPerQuarter = kInternalPpq;
constexpr int kTicksPerBar4_4 = kInternalPpq * 4;
constexpr int kTicks1_8 = kTicksPerBar4_4 / 8;    // 480
constexpr int kTicks1_16 = kTicksPerBar4_4 / 16;  // 240
constexpr int kTicks1_32 = kTicksPerBar4_4 / 32;  // 120
constexpr int kTicks1_64 = kTicksPerBar4_4 / 64;  // 60
constexpr int kTicks1_12 = kTicksPerBar4_4 / 12;  // 320
constexpr int kTicks1_24 = kTicksPerBar4_4 / 24;  // 160

inline int quantizeTicks(int tick, int divisionTicks)
{
    const int safeDiv = std::max(1, divisionTicks);
    return static_cast<int>(std::round(static_cast<double>(tick) / static_cast<double>(safeDiv))) * safeDiv;
}

inline void addNoteAtTick(TrackState& track,
                          int pitch,
                          int tick,
                          int velocity,
                          bool isGhost,
                          int totalBars,
                          int lengthSteps = 1)
{
    const int bars = std::max(1, totalBars);
    const int totalTicks = bars * 16 * ticksPerStep();
    const int clampedTick = std::clamp(tick, 0, std::max(0, totalTicks - 1));

    int step = clampedTick / ticksPerStep();
    int micro = clampedTick - step * ticksPerStep();
    if (micro > ticksPerStep() / 2)
    {
        micro -= ticksPerStep();
        ++step;
    }

    step = std::clamp(step, 0, std::max(0, bars * 16 - 1));
    micro = std::clamp(micro, -ticksPerStep() / 2, ticksPerStep() / 2);

    track.notes.push_back({ pitch,
                            step,
                            std::max(1, lengthSteps),
                            std::clamp(velocity, 1, 127),
                            micro,
                            isGhost });
}

inline int noteTick(const NoteEvent& note)
{
    return note.step * ticksPerStep() + note.microOffset;
}
}
} // namespace bbg
