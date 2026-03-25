#pragma once

#include <juce_core/juce_core.h>

namespace bbg
{
struct NoteEvent
{
    int pitch = 36;
    int step = 0;          // 1/16 grid step index
    int length = 1;        // length in 1/16 steps
    int velocity = 100;    // 1-127
    int microOffset = 0;   // Signed tick offset at PPQ=960 from step-quantized position.
    bool isGhost = false;
    juce::String semanticRole;
};
} // namespace bbg
