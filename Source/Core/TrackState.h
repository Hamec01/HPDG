#pragma once

#include <vector>

#include <juce_core/juce_core.h>

#include "NoteEvent.h"
#include "TrackType.h"

namespace bbg
{
struct TrackState
{
    TrackType type = TrackType::Kick;
    bool enabled = true;
    bool muted = false;
    bool solo = false;
    bool locked = false;

    int templateId = 0;
    int variationId = 0;
    float mutationDepth = 0.0f;
    juce::String subProfile;
    juce::String laneRole;
    float laneVolume = 0.85f;
    int selectedSampleIndex = 0;
    juce::String selectedSampleName;

    std::vector<NoteEvent> notes;
};
} // namespace bbg
