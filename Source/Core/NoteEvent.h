#pragma once

#include <juce_core/juce_core.h>

#include "TimingGrid.h"

namespace bbg
{
// Tick-native drum/melodic note event (PPQ 960 — see TimingGrid.h).
//
// gridTick and timingOffsetTicks are NEVER combined into one field. They answer two
// different questions:
//   - gridTick:            "where does this note musically/structurally belong" (a real
//                           1/16, 1/32, 1/64 or triplet position — not a disguised 1/16).
//   - timingOffsetTicks:   "how far did swing/humanize/groove nudge it from that position".
// Actual playback/render position is always gridTick + timingOffsetTicks (see startTick()).
// Generators write gridTick directly at whatever resolution they choose; GrooveEngine and
// HumanizeEngine only ever touch timingOffsetTicks.
struct NoteEvent
{
    int pitch = 36;

    int gridTick = 0;
    int timingOffsetTicks = 0;
    int lengthTicks = TimingGrid::Sixteenth;

    int velocity = 100;    // 1-127
    bool isGhost = false;
    juce::String semanticRole;
    bool isSlide = false;
    bool isLegato = false;
    bool glideToNext = false;

    int startTick() const noexcept { return gridTick + timingOffsetTicks; }
    int endTick() const noexcept { return startTick() + juce::jmax(1, lengthTicks); }
};
} // namespace bbg
