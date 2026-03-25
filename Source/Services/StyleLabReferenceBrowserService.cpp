#include "StyleLabReferenceBrowserService.h"

#include <algorithm>
#include <set>
#include <map>

#include "../Core/PatternProjectSerialization.h"
#include "../Core/TrackRegistry.h"
#include "StyleLabReferenceService.h"

namespace bbg
{
namespace
{
juce::String requireStringProperty(const juce::DynamicObject& object,
                                  const juce::Identifier& name,
                                  const juce::String& owner,
                                  juce::String& errorMessage)
{
    const auto value = object.getProperty(name);
    if (value.isVoid())
    {
        errorMessage = owner + "." + name.toString() + " is missing.";
        return {};
    }

    return value.toString();
}

const juce::DynamicObject* requireObject(const juce::var& value,
                                         const juce::String& label,
                                         juce::String& errorMessage)
{
    auto* object = value.getDynamicObject();
    if (object == nullptr)
        errorMessage = label + " must be an object.";
    return object;
}

const juce::Array<juce::var>* requireArray(const juce::var& value,
                                           const juce::String& label,
                                           juce::String& errorMessage)
{
    auto* array = value.getArray();
    if (array == nullptr)
        errorMessage = label + " must be an array.";
    return array;
}

float floatProperty(const juce::DynamicObject& object, const juce::Identifier& name, float fallback)
{
    const auto value = object.getProperty(name);
    return value.isVoid() ? fallback : static_cast<float>(value);
}

int intProperty(const juce::DynamicObject& object, const juce::Identifier& name, int fallback)
{
    const auto value = object.getProperty(name);
    return value.isVoid() ? fallback : static_cast<int>(value);
}

bool boolProperty(const juce::DynamicObject& object, const juce::Identifier& name, bool fallback)
{
    const auto value = object.getProperty(name);
    return value.isVoid() ? fallback : static_cast<bool>(value);
}

juce::String stringProperty(const juce::DynamicObject& object, const juce::Identifier& name, const juce::String& fallback = {})
{
    const auto value = object.getProperty(name);
    return value.isVoid() ? fallback : value.toString();
}

std::optional<TrackType> parseTrackTypeString(const juce::String& text)
{
    const auto normalized = text.trim();
    if (normalized.isEmpty())
        return std::nullopt;

    for (const auto& info : TrackRegistry::all())
    {
        if (normalized == juce::String(toString(info.type)))
        {
            return info.type;
        }
    }

    return std::nullopt;
}

enum class ReferenceBindingKind
{
    Backed,
    Unbacked,
    Unknown
};

ReferenceBindingKind parseBindingKind(const juce::String& text)
{
    if (text == "backed")
        return ReferenceBindingKind::Backed;
    if (text == "unbacked")
        return ReferenceBindingKind::Unbacked;
    return ReferenceBindingKind::Unknown;
}

TrackState* findTrackByLaneId(PatternProject& project, const RuntimeLaneId& laneId)
{
    for (auto& track : project.tracks)
    {
        if (track.laneId == laneId)
            return &track;
    }

    return nullptr;
}

TrackState* findTrackByRuntimeType(PatternProject& project, TrackType type)
{
    for (auto& track : project.tracks)
    {
        if (track.runtimeTrackType.has_value() && *track.runtimeTrackType == type)
            return &track;
        if (track.type == type)
            return &track;
    }

    return nullptr;
}

SoundLayerState parseSoundLayer(const juce::var& soundVar)
{
    SoundLayerState sound;
    if (auto* soundObject = soundVar.getDynamicObject())
    {
        sound.pan = floatProperty(*soundObject, "pan", sound.pan);
        sound.width = floatProperty(*soundObject, "width", sound.width);
        sound.eqTone = floatProperty(*soundObject, "eqTone", sound.eqTone);
        sound.compression = floatProperty(*soundObject, "compression", sound.compression);
        sound.reverb = floatProperty(*soundObject, "reverb", sound.reverb);
        sound.gate = floatProperty(*soundObject, "gate", sound.gate);
        sound.transient = floatProperty(*soundObject, "transient", sound.transient);
        sound.drive = floatProperty(*soundObject, "drive", sound.drive);
    }

    return sound;
}

StyleLabReferenceLaneParams parseLaneParams(const juce::var& laneParamsVar)
{
    StyleLabReferenceLaneParams params;
    auto* object = laneParamsVar.getDynamicObject();
    if (object == nullptr)
        return params;

    params.available = true;
    params.enabled = boolProperty(*object, "enabled", params.enabled);
    params.muted = boolProperty(*object, "muted", params.muted);
    params.solo = boolProperty(*object, "solo", params.solo);
    params.locked = boolProperty(*object, "locked", params.locked);
    params.laneRole = stringProperty(*object, "laneRole");
    params.laneVolume = floatProperty(*object, "laneVolume", params.laneVolume);
    params.selectedSampleIndex = intProperty(*object, "selectedSampleIndex", params.selectedSampleIndex);
    params.selectedSampleName = stringProperty(*object, "selectedSampleName");
    params.noteCount = intProperty(*object, "noteCount", params.noteCount);
    params.taggedNoteCount = intProperty(*object, "taggedNoteCount", params.taggedNoteCount);
    params.sound = parseSoundLayer(object->getProperty("sound"));
    return params;
}

StyleLabReferenceLaneRecord parseLaneRecord(const juce::var& laneVar)
{
    StyleLabReferenceLaneRecord lane;
    if (auto* object = laneVar.getDynamicObject())
    {
        lane.orderIndex = intProperty(*object, "orderIndex", lane.orderIndex);
        lane.generationPriority = intProperty(*object, "generationPriority", lane.generationPriority);
        lane.laneId = stringProperty(*object, "laneId");
        lane.laneName = stringProperty(*object, "laneName");
        lane.groupName = stringProperty(*object, "groupName");
        lane.dependencyName = stringProperty(*object, "dependencyName");
        lane.bindingKind = stringProperty(*object, "bindingKind");
        lane.runtimeTrackType = stringProperty(*object, "runtimeTrackType");
        lane.trackType = stringProperty(*object, "trackType");
        lane.hasBackingTrack = boolProperty(*object, "hasBackingTrack", lane.hasBackingTrack);
        lane.laneParamsAvailable = boolProperty(*object, "laneParamsAvailable", lane.laneParamsAvailable);
        lane.isRuntimeRegistryLane = boolProperty(*object, "isRuntimeRegistryLane", lane.isRuntimeRegistryLane);
        lane.isCore = boolProperty(*object, "isCore", lane.isCore);
        lane.isVisibleInEditor = boolProperty(*object, "isVisibleInEditor", lane.isVisibleInEditor);
        lane.enabledByDefault = boolProperty(*object, "enabledByDefault", lane.enabledByDefault);
        lane.supportsDragExport = boolProperty(*object, "supportsDragExport", lane.supportsDragExport);
        lane.isGhostTrack = boolProperty(*object, "isGhostTrack", lane.isGhostTrack);
        lane.defaultMidiNote = intProperty(*object, "defaultMidiNote", lane.defaultMidiNote);
        lane.laneParams = parseLaneParams(object->getProperty("laneParams"));
        if (lane.laneParams.available)
            lane.laneParamsAvailable = true;
    }

    return lane;
}

std::optional<StyleLabReferenceRecord> parseMetadataVar(const juce::var& rootVar,
                                                        const juce::File& sourceDirectory,
                                                        juce::String& errorMessage)
{
    const auto* rootObject = requireObject(rootVar, "metadata", errorMessage);
    if (rootObject == nullptr)
        return std::nullopt;

    StyleLabReferenceRecord record;
    record.directory = sourceDirectory;
    record.metadataFile = sourceDirectory.getChildFile("metadata.json");
    record.jsonFile = sourceDirectory.getChildFile("pattern.json");
    record.midiFile = sourceDirectory.getChildFile("pattern.mid");
    record.genre = stringProperty(*rootObject, "genre");
    record.substyle = stringProperty(*rootObject, "substyle");
    record.bars = intProperty(*rootObject, "bars", record.bars);
    record.tempoMin = intProperty(*rootObject, "tempoMin", record.tempoMin);
    record.tempoMax = intProperty(*rootObject, "tempoMax", record.tempoMax);
    record.exportedAt = stringProperty(*rootObject, "exportedAt");
    record.conflictMessage = stringProperty(*rootObject, "conflictMessage");

    const auto* orderArray = requireArray(rootObject->getProperty("runtimeLaneOrder"), "metadata.runtimeLaneOrder", errorMessage);
    if (orderArray == nullptr)
        return std::nullopt;

    const juce::var layoutVar = !rootObject->getProperty("runtimeLaneLayout").isVoid()
        ? rootObject->getProperty("runtimeLaneLayout")
        : rootObject->getProperty("laneLayout");
    const auto* layoutArray = requireArray(layoutVar, "metadata.laneLayout", errorMessage);
    if (layoutArray == nullptr)
        return std::nullopt;

    const auto designerLayoutVar = rootObject->getProperty("designerLaneLayout");
    if (const auto* designerArray = designerLayoutVar.getArray())
    {
        record.hasDesignerMetadata = true;
        record.designerLaneCount = designerArray->size();
    }

    const auto referenceProjectVar = rootObject->getProperty("referenceProject");
    if (const auto* referenceProjectObject = referenceProjectVar.getDynamicObject())
    {
        record.selectedTrackLaneId = stringProperty(*referenceProjectObject, "selectedTrackLaneId");
        record.soundModuleTrackLaneId = stringProperty(*referenceProjectObject, "soundModuleTrackLaneId");
        record.totalRuntimeLaneCount = intProperty(*referenceProjectObject, "totalRuntimeLaneCount", record.totalRuntimeLaneCount);
        record.backedLaneCount = intProperty(*referenceProjectObject, "backedLaneCount", record.backedLaneCount);
        record.unbackedLaneCount = intProperty(*referenceProjectObject, "unbackedLaneCount", record.unbackedLaneCount);
        record.totalNotes = intProperty(*referenceProjectObject, "totalNotes", record.totalNotes);
        record.taggedNotes = intProperty(*referenceProjectObject, "taggedNotes", record.taggedNotes);
    }

    std::map<juce::String, juce::var> layoutByLaneId;
    for (const auto& laneVar : *layoutArray)
    {
        if (auto* laneObject = laneVar.getDynamicObject())
        {
            const auto laneId = stringProperty(*laneObject, "laneId");
            if (laneId.isNotEmpty())
                layoutByLaneId[laneId] = laneVar;
        }
    }

    for (const auto& laneIdVar : *orderArray)
    {
        const auto laneId = laneIdVar.toString();
        if (laneId.isEmpty())
            continue;

        const auto it = layoutByLaneId.find(laneId);
        if (it == layoutByLaneId.end())
        {
            errorMessage = "metadata.laneLayout is missing laneId from runtimeLaneOrder: " + laneId;
            return std::nullopt;
        }

        record.runtimeLanes.push_back(parseLaneRecord(it->second));
        layoutByLaneId.erase(it);
    }

    for (const auto& stray : layoutByLaneId)
        record.runtimeLanes.push_back(parseLaneRecord(stray.second));

    std::sort(record.runtimeLanes.begin(), record.runtimeLanes.end(), [](const StyleLabReferenceLaneRecord& lhs, const StyleLabReferenceLaneRecord& rhs)
    {
        if (lhs.orderIndex != rhs.orderIndex)
            return lhs.orderIndex < rhs.orderIndex;
        return lhs.laneId < rhs.laneId;
    });

    return record;
}
} // namespace

StyleLabReferenceCatalog StyleLabReferenceBrowserService::loadReferenceCatalog(const juce::File& rootDirectory)
{
    StyleLabReferenceCatalog catalog;
    if (!rootDirectory.exists() || !rootDirectory.isDirectory())
        return catalog;

    std::vector<juce::File> candidateDirectories;
    for (const auto& genreEntry : juce::RangedDirectoryIterator(rootDirectory, false, "*", juce::File::findDirectories))
    {
        const auto genreDir = genreEntry.getFile();
        for (const auto& substyleEntry : juce::RangedDirectoryIterator(genreDir, false, "*", juce::File::findDirectories))
        {
            const auto substyleDir = substyleEntry.getFile();
            for (const auto& referenceEntry : juce::RangedDirectoryIterator(substyleDir, false, "*", juce::File::findDirectories))
            {
                const auto referenceDir = referenceEntry.getFile();
                if (referenceDir.getChildFile("metadata.json").existsAsFile())
                    candidateDirectories.push_back(referenceDir);
            }
        }
    }

    std::sort(candidateDirectories.begin(), candidateDirectories.end(), [](const juce::File& lhs, const juce::File& rhs)
    {
        return lhs.getFileName() > rhs.getFileName();
    });

    for (const auto& referenceDir : candidateDirectories)
    {
        const auto metadataFile = referenceDir.getChildFile("metadata.json");
        const auto metadataJson = metadataFile.loadFileAsString();
        juce::String errorMessage;
        if (const auto record = parseMetadataJson(metadataJson, referenceDir, &errorMessage))
        {
            catalog.references.push_back(*record);
        }
        else
        {
            catalog.warnings.add(referenceDir.getFileName() + ": " + errorMessage);
        }
    }

    return catalog;
}

std::optional<StyleLabReferenceRecord> StyleLabReferenceBrowserService::parseMetadataJson(const juce::String& metadataJson,
                                                                                           const juce::File& sourceDirectory,
                                                                                           juce::String* errorMessage)
{
    juce::String parseError;
    const auto parsed = juce::JSON::parse(metadataJson);
    if (parsed.isVoid())
    {
        parseError = "metadata.json parse failed.";
        if (errorMessage != nullptr)
            *errorMessage = parseError;
        return std::nullopt;
    }

    const auto record = parseMetadataVar(parsed, sourceDirectory, parseError);
    if (errorMessage != nullptr)
        *errorMessage = parseError;
    return record;
}

bool StyleLabReferenceBrowserService::applyReferenceToProject(const StyleLabReferenceRecord& record,
                                                              PatternProject& project,
                                                              juce::String* errorMessage)
{
    if (record.runtimeLanes.empty())
    {
        if (errorMessage != nullptr)
            *errorMessage = "Reference has no runtime lane metadata to apply.";
        return false;
    }

    juce::StringArray seenLaneIds;
    juce::StringArray warnings;
    std::set<TrackType> seenBackedTypes;

    for (const auto& laneRecord : record.runtimeLanes)
    {
        const auto laneId = laneRecord.laneId.trim();
        if (laneId.isEmpty())
        {
            if (errorMessage != nullptr)
                *errorMessage = "Reference runtime lane is missing laneId.";
            return false;
        }

        if (seenLaneIds.contains(laneId))
        {
            if (errorMessage != nullptr)
                *errorMessage = "Reference contains duplicate runtime laneId: " + laneId;
            return false;
        }

        const auto bindingKind = parseBindingKind(laneRecord.bindingKind);
        if (bindingKind == ReferenceBindingKind::Unknown)
        {
            if (errorMessage != nullptr)
                *errorMessage = "Reference lane '" + laneId + "' has invalid bindingKind '" + laneRecord.bindingKind + "'.";
            return false;
        }

        const auto parsedRuntimeType = parseTrackTypeString(laneRecord.runtimeTrackType);
        const auto parsedTrackType = parseTrackTypeString(laneRecord.trackType);
        const auto parsedType = parsedRuntimeType.has_value() ? parsedRuntimeType : parsedTrackType;

        if (bindingKind == ReferenceBindingKind::Backed)
        {
            if (!parsedType.has_value())
            {
                if (errorMessage != nullptr)
                    *errorMessage = "Backed runtime lane '" + laneId + "' is missing a valid runtimeTrackType/trackType token.";
                return false;
            }

            if (!laneRecord.hasBackingTrack)
                warnings.add("Lane '" + laneId + "' applied as backed from bindingKind, but hasBackingTrack=false in metadata.");

            if (!seenBackedTypes.insert(*parsedType).second)
            {
                warnings.add("Reference duplicates backed runtime track type '" + juce::String(toString(*parsedType)) + "'; applying first matching lane only.");
            }
        }
        else
        {
            if (laneRecord.hasBackingTrack || parsedType.has_value())
            {
                if (errorMessage != nullptr)
                    *errorMessage = "Unbacked runtime lane '" + laneId + "' contains contradictory backing metadata.";
                return false;
            }
        }

        seenLaneIds.add(laneId);
    }

    for (const auto& laneRecord : record.runtimeLanes)
    {
        const auto bindingKind = parseBindingKind(laneRecord.bindingKind);
        if (bindingKind == ReferenceBindingKind::Unbacked)
        {
            if (laneRecord.laneParamsAvailable || laneRecord.laneParams.available)
                warnings.add("Skipped lane params for unbacked lane '" + laneRecord.laneId + "'.");
            continue;
        }

        if (!laneRecord.laneParamsAvailable || !laneRecord.laneParams.available)
        {
            warnings.add("No lane params found for backed lane '" + laneRecord.laneId + "'; existing params were preserved.");
            continue;
        }

        TrackState* track = findTrackByLaneId(project, laneRecord.laneId);
        if (track == nullptr)
        {
            const auto parsedRuntimeType = parseTrackTypeString(laneRecord.runtimeTrackType);
            const auto parsedTrackType = parseTrackTypeString(laneRecord.trackType);
            const auto parsedType = parsedRuntimeType.has_value() ? parsedRuntimeType : parsedTrackType;
            if (parsedType.has_value())
                track = findTrackByRuntimeType(project, *parsedType);
        }

        if (track == nullptr)
        {
            warnings.add("Skipped reference lane '" + laneRecord.laneId + "' because no matching project lane exists.");
            continue;
        }

        track->enabled = laneRecord.laneParams.enabled;
        track->muted = laneRecord.laneParams.muted;
        track->solo = laneRecord.laneParams.solo;
        track->locked = laneRecord.laneParams.locked;
        track->laneRole = laneRecord.laneParams.laneRole;
        track->laneVolume = laneRecord.laneParams.laneVolume;
        track->selectedSampleIndex = laneRecord.laneParams.selectedSampleIndex;
        track->selectedSampleName = laneRecord.laneParams.selectedSampleName;
        track->sound = laneRecord.laneParams.sound;
    }

    PatternProjectSerialization::validate(project);
    if (errorMessage != nullptr)
        *errorMessage = warnings.joinIntoString(" ");
    return true;
}
} // namespace bbg