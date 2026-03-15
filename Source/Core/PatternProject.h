#pragma once

#include <vector>

#include <juce_core/juce_core.h>

#include "GeneratorParams.h"
#include "TrackRegistry.h"
#include "TrackState.h"

namespace bbg
{
struct PatternProject
{
    // PatternProject owns generated musical content and per-track musical state.
    // APVTS owns plugin parameter state (tempo sync, knobs, genre choices, seed).
    GeneratorParams params;
    std::vector<TrackState> tracks;
    int selectedTrackIndex = 0;
    int generationCounter = 0;
    int mutationCounter = 0;
    int phraseLengthBars = 1;
    juce::String phraseRoleSummary;
    int previewStartStep = 0;
};

inline PatternProject createDefaultProject()
{
    PatternProject project;
    project.tracks = TrackRegistry::createDefaultTrackStates();
    project.phraseLengthBars = project.params.bars;
    return project;
}
} // namespace bbg
