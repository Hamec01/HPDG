#pragma once

#include <optional>
#include <vector>

#include <juce_core/juce_core.h>

#include "../Core/GeneratorParams.h"
#include "../Core/PatternProject.h"
#include "../Services/StyleLabReferenceBrowserService.h"

namespace bbg
{
struct ResolvedStyleDefinitionLane
{
    RuntimeLaneId laneId;
    juce::String laneName;
    juce::String groupName;
    juce::String dependencyName;
    int generationPriority = 50;
    bool isCore = true;
    bool isVisibleInEditor = true;
    bool enabledByDefault = true;
    bool supportsDragExport = true;
    bool isGhostTrack = false;
    int defaultMidiNote = 36;
    bool isRuntimeRegistryLane = false;
    std::optional<TrackType> runtimeTrackType;
    StyleLabReferenceLaneParams laneParams;
    juce::NamedValueSet skeletonHints;
    juce::NamedValueSet notePriorityHints;
};

struct ResolvedStyleDefinition
{
    GenreType genre = GenreType::BoomBap;
    juce::String genreName;
    juce::String substyleName;
    juce::String source;
    bool loadedFromReference = false;
    std::vector<ResolvedStyleDefinitionLane> lanes;
    juce::NamedValueSet styleHints;
    std::optional<ReferenceHatSkeleton> referenceHatSkeleton;
    std::optional<ReferenceHatCorpus> referenceHatCorpus;
    std::optional<ReferenceKickCorpus> referenceKickCorpus;
};

using StyleDefinitionLane = ResolvedStyleDefinitionLane;
using StyleDefinition = ResolvedStyleDefinition;

class StyleDefinitionLoader
{
public:
    static std::optional<ResolvedStyleDefinition> loadLatestForStyle(const juce::String& genreName,
                                                                     const juce::String& substyleName,
                                                                     const juce::File& rootDirectory,
                                                                     juce::String* errorMessage = nullptr);

    static ResolvedStyleDefinition buildFallback(GenreType genre, int substyleIndex);
    static ResolvedStyleDefinition fromReferenceRecord(GenreType genre, const StyleLabReferenceRecord& record);
    static juce::String genreDisplayName(GenreType genre);
    static juce::String substyleNameFor(GenreType genre, int substyleIndex);
};
} // namespace bbg