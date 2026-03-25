#pragma once

#include <optional>
#include <vector>

#include <juce_core/juce_core.h>

#include "../Core/PatternProject.h"

namespace bbg
{
struct StyleLabLaneDefinition
{
    juce::String id;
    juce::String laneName;
    juce::String groupName;
    juce::String dependencyName;
    int generationPriority = 50;
    bool isCore = true;
    bool enabled = true;
    bool isRuntimeRegistryLane = false;
    std::optional<TrackType> runtimeTrackType;
};

struct StyleLabState
{
    juce::String genre;
    juce::String substyle;
    int bars = 4;
    juce::Range<int> tempoRange { 88, 100 };
    std::vector<StyleLabLaneDefinition> laneDefinitions;
};

struct StyleLabReferenceExportResult
{
    bool success = false;
    juce::String errorMessage;
    juce::String conflictMessage;
    juce::File directory;
    juce::File jsonFile;
    juce::File midiFile;
    juce::File metadataFile;
};

class StyleLabReferenceService
{
public:
    static StyleLabState createDefaultState(const PatternProject& project,
                                            const juce::String& genre,
                                            const juce::String& substyle,
                                            int bars,
                                            int bpm);

    static juce::String buildReferenceMetadataJson(const PatternProject& project,
                                                   const StyleLabState& state,
                                                   juce::String* conflictMessage = nullptr);

    static StyleLabReferenceExportResult saveReferencePattern(const PatternProject& project,
                                                              const StyleLabState& state);

    static juce::String buildConflictDescription(const StyleLabState& state);
    static juce::File getReferenceRootDirectory();
};
} // namespace bbg