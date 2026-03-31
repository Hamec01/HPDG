#pragma once

#include <optional>
#include <vector>

#include <juce_core/juce_core.h>

#include "../Core/PatternProject.h"
#include "../Core/SoundLayerState.h"

namespace bbg
{
struct StyleLabReferenceLaneParams
{
    bool available = false;
    bool enabled = true;
    bool muted = false;
    bool solo = false;
    bool locked = false;
    juce::String laneRole;
    float laneVolume = 0.85f;
    int selectedSampleIndex = 0;
    juce::String selectedSampleName;
    SoundLayerState sound;
    int noteCount = 0;
    int taggedNoteCount = 0;
};

struct StyleLabReferenceLaneRecord
{
    int orderIndex = -1;
    int generationPriority = 50;
    juce::String laneId;
    juce::String laneName;
    juce::String groupName;
    juce::String dependencyName;
    juce::String bindingKind;
    juce::String runtimeTrackType;
    juce::String trackType;
    bool hasBackingTrack = false;
    bool laneParamsAvailable = false;
    bool isRuntimeRegistryLane = false;
    bool isCore = true;
    bool isVisibleInEditor = true;
    bool enabledByDefault = true;
    bool supportsDragExport = true;
    bool isGhostTrack = false;
    int defaultMidiNote = 36;
    StyleLabReferenceLaneParams laneParams;
};

struct StyleLabReferenceDrillHatMotionSummary
{
    bool available = false;
    float averageRollLength = 0.0f;
    float densityVariation = 0.0f;
    float accentAlternation = 0.0f;
    float silenceGapIntent = 0.0f;
    float burstClusterRate = 0.0f;
    float tripletRate = 0.0f;
};

struct StyleLabReferenceRecord
{
    juce::File directory;
    juce::File metadataFile;
    juce::File jsonFile;
    juce::File midiFile;
    juce::String genre;
    juce::String substyle;
    int bars = 0;
    int tempoMin = 0;
    int tempoMax = 0;
    juce::StringArray tags;
    juce::String mood;
    juce::String densityProfile;
    int referencePriority = 50;
    juce::String notesSummary;
    juce::String authoringNotes;
    juce::String exportedAt;
    juce::String conflictMessage;
    juce::String selectedTrackLaneId;
    juce::String soundModuleTrackLaneId;
    int totalRuntimeLaneCount = 0;
    int backedLaneCount = 0;
    int unbackedLaneCount = 0;
    int totalNotes = 0;
    int taggedNotes = 0;
    bool hasDesignerMetadata = false;
    int designerLaneCount = 0;
    StyleLabReferenceDrillHatMotionSummary drillHatMotionSummary;
    std::vector<StyleLabReferenceLaneRecord> runtimeLanes;
};

struct StyleLabReferenceCatalog
{
    std::vector<StyleLabReferenceRecord> references;
    juce::StringArray warnings;
};

class StyleLabReferenceBrowserService
{
public:
    static StyleLabReferenceCatalog loadReferenceCatalog(const juce::File& rootDirectory);
    static std::optional<StyleLabReferenceRecord> parseMetadataJson(const juce::String& metadataJson,
                                                                    const juce::File& sourceDirectory,
                                                                    juce::String* errorMessage = nullptr);
    static bool applyReferenceToProject(const StyleLabReferenceRecord& record,
                                        PatternProject& project,
                                        juce::String* errorMessage = nullptr);
};
} // namespace bbg