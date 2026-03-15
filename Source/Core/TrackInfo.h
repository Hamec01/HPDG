#pragma once

#include <juce_core/juce_core.h>

#include "TrackType.h"

namespace bbg
{
struct TrackInfo
{
    TrackType type {};
    juce::String displayName;
    int defaultMidiNote = 36;
    bool enabledByDefault = true;
    bool isGhostTrack = false;
    bool supportsDragExport = true;
    bool visibleInUI = true;
};
} // namespace bbg
