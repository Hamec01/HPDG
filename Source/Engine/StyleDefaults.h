#pragma once

#include <array>

#include <juce_core/juce_core.h>

#include "../Core/GeneratorParams.h"
#include "../Core/TrackType.h"

namespace bbg
{
struct LaneStyleDefaults
{
    bool enabledByDefault = true;
    float volumeDefault = 0.85f;

    float densityBias = 1.0f;
    float timingBias = 1.0f;
    float humanizeBias = 1.0f;

    std::array<juce::String, 3> preferredPatternFamilies {};

    float noteProbability = 1.0f;
    float phraseEndingProbability = 0.35f;
    float mutationIntensity = 1.0f;
    float rgVariationIntensity = 1.0f;
    float hatFxIntensity = 0.0f;
    float sub808Activity = 0.0f;
};

struct GenreStyleDefaults
{
    GenreType genre = GenreType::BoomBap;
    juce::String substyleName;
    float bpmDefault = 90.0f;

    float swingDefault = 56.0f;
    float velocityDefault = 0.50f;
    float timingDefault = 0.40f;
    float humanizeDefault = 0.30f;
    float densityDefault = 0.50f;

    std::array<LaneStyleDefaults, 11> laneDefaults {};
};

int trackTypeToLaneIndex(TrackType trackType);
const GenreStyleDefaults& getGenreStyleDefaults(GenreType genre, int substyleIndex);
const LaneStyleDefaults& getLaneStyleDefaults(const GenreStyleDefaults& style, TrackType trackType);

juce::StringArray getBoomBapSubstyleNames();
juce::StringArray getRapSubstyleNames();
juce::StringArray getTrapSubstyleNames();
juce::StringArray getDrillSubstyleNames();
} // namespace bbg
