#pragma once

#include <optional>
#include <vector>

#include <juce_core/juce_core.h>

#include "GeneratorParams.h"
#include "NoteEvent.h"
#include "SoundLayerState.h"
#include "Sub808Types.h"
#include "TrackType.h"

namespace bbg
{
struct TrackState
{
    TrackType type = TrackType::Kick;
    juce::String laneId;
    std::optional<TrackType> runtimeTrackType;
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
    SoundLayerState sound;

    std::vector<NoteEvent> notes;
    std::vector<NoteEvent> baseNotes;
    std::vector<Sub808NoteEvent> sub808Notes;
    std::vector<Sub808NoteEvent> baseSub808Notes;
    GeneratorParams performanceBaseParams;
    bool hasPerformanceBaseParams = false;
    Sub808LaneSettings sub808Settings;
};
} // namespace bbg
