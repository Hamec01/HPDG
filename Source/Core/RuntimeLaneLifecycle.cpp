#include "RuntimeLaneLifecycle.h"

#include <algorithm>

namespace bbg
{
namespace RuntimeLaneLifecycle
{
namespace
{
bool containsLaneId(const PatternProject& project, const RuntimeLaneId& laneId)
{
    return std::any_of(project.runtimeLaneProfile.lanes.begin(), project.runtimeLaneProfile.lanes.end(), [&laneId](const RuntimeLaneDefinition& lane)
    {
        return lane.laneId == laneId;
    });
}

bool containsRuntimeTrackType(const PatternProject& project, TrackType type)
{
    return std::any_of(project.runtimeLaneProfile.lanes.begin(), project.runtimeLaneProfile.lanes.end(), [type](const RuntimeLaneDefinition& lane)
    {
        return lane.runtimeTrackType.has_value() && *lane.runtimeTrackType == type;
    });
}

bool containsLaneName(const PatternProject& project, const juce::String& laneName)
{
    return std::any_of(project.runtimeLaneProfile.lanes.begin(), project.runtimeLaneProfile.lanes.end(), [&laneName](const RuntimeLaneDefinition& lane)
    {
        return lane.laneName.equalsIgnoreCase(laneName);
    });
}

juce::String sanitizedLaneName(const juce::String& preferredName, const juce::String& fallback)
{
    const auto trimmed = preferredName.trim();
    return trimmed.isNotEmpty() ? trimmed : fallback;
}

RuntimeLaneId baseCustomLaneIdForName(const juce::String& laneName)
{
    juce::String normalized = laneName.toLowerCase();
    juce::String result;
    bool lastWasSeparator = false;

    for (const auto character : normalized)
    {
        if (juce::CharacterFunctions::isLetterOrDigit(character))
        {
            result << character;
            lastWasSeparator = false;
            continue;
        }

        if (!lastWasSeparator)
        {
            result << '-';
            lastWasSeparator = true;
        }
    }

    result = result.trimCharactersAtStart("-").trimCharactersAtEnd("-");
    if (result.isEmpty())
        result = "lane";

    return "custom:" + result;
}

RuntimeLaneId makeUniqueLaneId(const PatternProject& project, const RuntimeLaneId& baseLaneId)
{
    RuntimeLaneId candidate = baseLaneId.isNotEmpty() ? baseLaneId : "custom:lane";
    const RuntimeLaneId seed = candidate;
    int suffix = 2;

    while (containsLaneId(project, candidate))
    {
        candidate = seed + "-" + juce::String(suffix);
        ++suffix;
    }

    return candidate;
}

juce::String makeUniqueLaneName(const PatternProject& project, const juce::String& baseLaneName)
{
    const auto seed = sanitizedLaneName(baseLaneName, "Custom Lane");
    if (!containsLaneName(project, seed))
        return seed;

    int suffix = 2;
    juce::String candidate;
    do
    {
        candidate = seed + " " + juce::String(suffix);
        ++suffix;
    }
    while (containsLaneName(project, candidate));

    return candidate;
}

int clampedInsertIndex(const PatternProject& project, int insertIndex)
{
    if (insertIndex < 0)
        return static_cast<int>(project.runtimeLaneProfile.lanes.size());

    return juce::jlimit(0, static_cast<int>(project.runtimeLaneProfile.lanes.size()), insertIndex);
}

void insertLaneIdAt(std::vector<RuntimeLaneId>& order, const RuntimeLaneId& laneId, int index)
{
    order.erase(std::remove(order.begin(), order.end(), laneId), order.end());
    order.insert(order.begin() + index, laneId);
}

std::optional<RuntimeLaneDefinition> defaultRegistryLaneFor(TrackType type)
{
    const auto profile = TrackRegistry::createDefaultRuntimeLaneProfile();
    if (const auto* lane = findRuntimeLaneForTrack(profile, type))
        return *lane;

    return std::nullopt;
}
}

RuntimeLaneDefinition createCustomLaneDefinition(const PatternProject& project, const juce::String& preferredName)
{
    RuntimeLaneDefinition lane;
    lane.laneName = sanitizedLaneName(preferredName, "Custom Lane");
    lane.laneId = makeUniqueLaneId(project, baseCustomLaneIdForName(lane.laneName));
    lane.groupName = "Custom";
    lane.generationPriority = 50;
    lane.isCore = false;
    lane.isVisibleInEditor = true;
    lane.enabledByDefault = true;
    lane.supportsDragExport = false;
    lane.isGhostTrack = false;
    lane.defaultMidiNote = 36;
    lane.isRuntimeRegistryLane = false;
    lane.runtimeTrackType = std::nullopt;
    lane.editorCapabilities = makeLaneEditorCapabilities(lane.runtimeTrackType);
    return lane;
}

std::vector<TrackType> listAvailableRegistryLaneTypes(const PatternProject& project)
{
    std::vector<TrackType> available;
    for (const auto& info : TrackRegistry::all())
    {
        if (!containsRuntimeTrackType(project, info.type))
            available.push_back(info.type);
    }

    return available;
}

bool addLane(PatternProject& project, RuntimeLaneDefinition lane, int insertIndex)
{
    if (lane.runtimeTrackType.has_value())
    {
        if (const auto registryLane = defaultRegistryLaneFor(*lane.runtimeTrackType))
        {
            if (lane.laneId.isEmpty())
                lane.laneId = registryLane->laneId;
            if (lane.laneName.isEmpty())
                lane.laneName = registryLane->laneName;
            if (lane.groupName.isEmpty())
                lane.groupName = registryLane->groupName;
            if (lane.dependencyName.isEmpty())
                lane.dependencyName = registryLane->dependencyName;
            lane.generationPriority = registryLane->generationPriority;
            lane.isCore = registryLane->isCore;
            lane.isVisibleInEditor = registryLane->isVisibleInEditor;
            lane.enabledByDefault = registryLane->enabledByDefault;
            lane.supportsDragExport = registryLane->supportsDragExport;
            lane.isGhostTrack = registryLane->isGhostTrack;
            lane.defaultMidiNote = registryLane->defaultMidiNote;
            lane.isRuntimeRegistryLane = true;
            lane.editorCapabilities = registryLane->editorCapabilities;
        }
    }

    lane.editorCapabilities = makeLaneEditorCapabilities(lane.runtimeTrackType);

    lane.laneName = makeUniqueLaneName(project,
                                       sanitizedLaneName(lane.laneName,
                                                         lane.runtimeTrackType.has_value() ? juce::String(toString(*lane.runtimeTrackType)) : "Custom Lane"));
    lane.laneId = makeUniqueLaneId(project, lane.laneId.isNotEmpty() ? lane.laneId : baseCustomLaneIdForName(lane.laneName));

    const int index = clampedInsertIndex(project, insertIndex);
    project.runtimeLaneProfile.lanes.insert(project.runtimeLaneProfile.lanes.begin() + index, lane);
    insertLaneIdAt(project.runtimeLaneOrder, lane.laneId, juce::jlimit(0, static_cast<int>(project.runtimeLaneOrder.size()), index));

    if (lane.runtimeTrackType.has_value())
    {
        TrackState state;
        state.type = *lane.runtimeTrackType;
        state.laneId = lane.laneId;
        state.runtimeTrackType = lane.runtimeTrackType;
        state.enabled = lane.enabledByDefault;
        project.tracks.push_back(std::move(state));
    }

    return true;
}

bool addRegistryLane(PatternProject& project, TrackType type, int insertIndex)
{
    if (const auto lane = defaultRegistryLaneFor(type))
        return addLane(project, *lane, insertIndex);

    return false;
}

bool renameLane(PatternProject& project, const RuntimeLaneId& laneId, const juce::String& newLaneName)
{
    if (laneId.isEmpty())
        return false;

    const auto trimmed = newLaneName.trim();
    if (trimmed.isEmpty())
        return false;

    for (auto& lane : project.runtimeLaneProfile.lanes)
    {
        if (lane.laneId != laneId)
            continue;

        if (lane.laneName == trimmed)
            return false;

        lane.laneName = trimmed;
        return true;
    }

    return false;
}

bool deleteLane(PatternProject& project, const RuntimeLaneId& laneId)
{
    if (laneId.isEmpty())
        return false;

    const auto laneIt = std::find_if(project.runtimeLaneProfile.lanes.begin(), project.runtimeLaneProfile.lanes.end(), [&laneId](const RuntimeLaneDefinition& lane)
    {
        return lane.laneId == laneId;
    });

    if (laneIt == project.runtimeLaneProfile.lanes.end())
        return false;

    project.runtimeLaneProfile.lanes.erase(laneIt);
    project.runtimeLaneOrder.erase(std::remove(project.runtimeLaneOrder.begin(), project.runtimeLaneOrder.end(), laneId), project.runtimeLaneOrder.end());
    project.tracks.erase(std::remove_if(project.tracks.begin(), project.tracks.end(), [&laneId](const TrackState& track)
    {
        return track.laneId == laneId;
    }), project.tracks.end());
    return true;
}

bool deleteAllLanes(PatternProject& project)
{
    if (project.runtimeLaneProfile.lanes.empty() && project.runtimeLaneOrder.empty() && project.tracks.empty())
        return false;

    project.runtimeLaneProfile.lanes.clear();
    project.runtimeLaneOrder.clear();
    project.tracks.clear();
    project.selectedTrackIndex = -1;
    project.soundModuleTrackIndex = -1;
    return true;
}
} // namespace RuntimeLaneLifecycle
} // namespace bbg