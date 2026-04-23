#pragma once

#include <algorithm>

namespace bbg
{
constexpr int kInternalPpq = 960;
constexpr int kStepsPerQuarter = 4;

inline int ticksPerStep(int ppq = kInternalPpq)
{
    return ppq / kStepsPerQuarter;
}

inline int stepToTicks(int step, int ppq = kInternalPpq)
{
    return step * ticksPerStep(ppq);
}

inline int patternLengthTicks(int bars, int ppq = kInternalPpq)
{
    return std::max(1, bars) * ticksPerStep(ppq) * 16;
}

inline int rawNoteStartTicks(int step, int microOffset, int ppq = kInternalPpq)
{
    return stepToTicks(step, ppq) + microOffset;
}

inline int clampedNoteStartTicks(int step, int microOffset, int bars, int ppq = kInternalPpq)
{
    const int maxTick = std::max(0, patternLengthTicks(bars, ppq) - 1);
    return std::clamp(rawNoteStartTicks(step, microOffset, ppq), 0, maxTick);
}

inline bool stepWithinPatternBars(int step, int bars)
{
    return step >= 0 && step < std::max(1, bars) * 16;
}

inline double ticksToMs(int ticks, double bpm, int ppq = kInternalPpq)
{
    const auto safeBpm = std::max(1.0, bpm);
    const auto msPerQuarter = 60000.0 / safeBpm;
    return static_cast<double>(ticks) * (msPerQuarter / static_cast<double>(ppq));
}

inline int msToTicks(double ms, double bpm, int ppq = kInternalPpq)
{
    const auto safeBpm = std::max(1.0, bpm);
    const auto quarters = ms / (60000.0 / safeBpm);
    return static_cast<int>(quarters * static_cast<double>(ppq));
}

inline int ticksToSamples(int ticks, double sampleRate, double bpm, int ppq = kInternalPpq)
{
    const auto ms = ticksToMs(ticks, bpm, ppq);
    return static_cast<int>((ms / 1000.0) * sampleRate);
}

inline int stepToSamples(int step, double sampleRate, double bpm, int ppq = kInternalPpq)
{
    return ticksToSamples(stepToTicks(step, ppq), sampleRate, bpm, ppq);
}
} // namespace bbg
