#include "StyleLabReferenceService.h"

#include <algorithm>

#include "TemporaryMidiExportService.h"
#include "../Core/LaneDefaults.h"
#include "../Core/PatternProjectSerialization.h"
#include "../Core/Sub808TrackAccess.h"
#include "../Core/TrackRegistry.h"
#include "../Utils/TimingHelpers.h"

namespace bbg
{
namespace
{
juce::String makeId()
{
    return juce::Uuid().toString();
}

juce::String sanitizePathSegment(const juce::String& input)
{
    auto text = input.trim();
    if (text.isEmpty())
        return "Unsorted";

    juce::String cleaned;
    cleaned.preallocateBytes(text.getNumBytesAsUTF8());

    for (auto character : text)
    {
        if (juce::CharacterFunctions::isLetterOrDigit(character))
            cleaned << juce::String::charToString(character);
        else if (character == ' ' || character == '-' || character == '_')
            cleaned << "_";
    }

    cleaned = cleaned.trimCharactersAtStart("_").trimCharactersAtEnd("_");
    return cleaned.isEmpty() ? "Unsorted" : cleaned;
}

juce::var valueTreeToVar(const juce::ValueTree& node)
{
    auto* object = new juce::DynamicObject();
    object->setProperty("type", node.getType().toString());

    auto* properties = new juce::DynamicObject();
    for (int propertyIndex = 0; propertyIndex < node.getNumProperties(); ++propertyIndex)
    {
        const auto propertyName = node.getPropertyName(propertyIndex);
        properties->setProperty(propertyName.toString(), node.getProperty(propertyName));
    }
    object->setProperty("properties", juce::var(properties));

    juce::Array<juce::var> children;
    for (int childIndex = 0; childIndex < node.getNumChildren(); ++childIndex)
        children.add(valueTreeToVar(node.getChild(childIndex)));

    object->setProperty("children", juce::var(children));
    return juce::var(object);
}

juce::var laneDefinitionToVar(const StyleLabLaneDefinition& lane)
{
    auto* object = new juce::DynamicObject();
    object->setProperty("id", lane.id);
    object->setProperty("laneName", lane.laneName);
    object->setProperty("groupName", lane.groupName);
    object->setProperty("dependencyName", lane.dependencyName);
    object->setProperty("generationPriority", lane.generationPriority);
    object->setProperty("isCore", lane.isCore);
    object->setProperty("enabled", lane.enabled);
    object->setProperty("isRuntimeRegistryLane", lane.isRuntimeRegistryLane);
    object->setProperty("runtimeTrackType", lane.runtimeTrackType.has_value() ? juce::String(toString(*lane.runtimeTrackType)) : juce::String());
    return juce::var(object);
}

juce::var soundLayerToVar(const SoundLayerState& sound)
{
    auto* object = new juce::DynamicObject();
    object->setProperty("pan", sound.pan);
    object->setProperty("width", sound.width);
    object->setProperty("eqTone", sound.eqTone);
    object->setProperty("compression", sound.compression);
    object->setProperty("reverb", sound.reverb);
    object->setProperty("gate", sound.gate);
    object->setProperty("transient", sound.transient);
    object->setProperty("drive", sound.drive);
    return juce::var(object);
}

std::vector<const RuntimeLaneDefinition*> orderedRuntimeLanes(const PatternProject& project)
{
    std::vector<const RuntimeLaneDefinition*> ordered;
    ordered.reserve(project.runtimeLaneProfile.lanes.size());

    const auto appendLane = [&ordered](const RuntimeLaneDefinition& lane)
    {
        const bool alreadyAdded = std::any_of(ordered.begin(), ordered.end(), [&lane](const RuntimeLaneDefinition* existing)
        {
            return existing != nullptr && existing->laneId == lane.laneId;
        });

        if (!alreadyAdded)
            ordered.push_back(&lane);
    };

    for (const auto& laneId : project.runtimeLaneOrder)
    {
        if (const auto* lane = findRuntimeLaneById(project.runtimeLaneProfile, laneId))
            appendLane(*lane);
    }

    for (const auto& lane : project.runtimeLaneProfile.lanes)
        appendLane(lane);

    return ordered;
}

const TrackState* findTrackForLaneId(const PatternProject& project, const RuntimeLaneId& laneId)
{
    for (const auto& track : project.tracks)
    {
        if (track.laneId == laneId)
            return &track;
    }

    return nullptr;
}

juce::var noteToMetadataVar(const NoteEvent& note)
{
    auto* object = new juce::DynamicObject();
    const int startTick = note.step * ticksPerStep() + note.microOffset;
    object->setProperty("pitch", note.pitch);
    object->setProperty("step", note.step);
    object->setProperty("lengthSteps", note.length);
    object->setProperty("velocity", note.velocity);
    object->setProperty("microOffsetTicks", note.microOffset);
    object->setProperty("startTick", startTick);
    object->setProperty("lengthTicks", note.length * ticksPerStep());
    object->setProperty("barIndex", note.step / 16);
    object->setProperty("stepInBar", note.step % 16);
    object->setProperty("isGhost", note.isGhost);
    object->setProperty("semanticRole", note.semanticRole);
    object->setProperty("isSlide", note.isSlide);
    object->setProperty("isLegato", note.isLegato);
    object->setProperty("glideToNext", note.glideToNext);
    return juce::var(object);
}

juce::var trackStateToVar(const TrackState& track)
{
    auto* object = new juce::DynamicObject();
    object->setProperty("enabled", track.enabled);
    object->setProperty("muted", track.muted);
    object->setProperty("solo", track.solo);
    object->setProperty("locked", track.locked);
    object->setProperty("laneRole", track.laneRole);
    object->setProperty("laneVolume", track.laneVolume);
    object->setProperty("selectedSampleIndex", track.selectedSampleIndex);
    object->setProperty("selectedSampleName", track.selectedSampleName);
    object->setProperty("sound", soundLayerToVar(track.sound));
    object->setProperty("sub808Mono", track.sub808Settings.mono);
    object->setProperty("sub808CutItself", track.sub808Settings.cutItself);
    object->setProperty("sub808GlideTimeMs", track.sub808Settings.glideTimeMs);
    object->setProperty("sub808OverlapMode", static_cast<int>(track.sub808Settings.overlapMode));
    object->setProperty("sub808ScaleSnapPolicy", static_cast<int>(track.sub808Settings.scaleSnapPolicy));

    const auto effectiveSub808Notes = track.type == TrackType::Sub808 ? sub808NotesForRead(track) : std::vector<Sub808NoteEvent> {};
    juce::Array<juce::var> notes;
    int taggedNoteCount = 0;
    if (track.type == TrackType::Sub808)
    {
        for (const auto& note : effectiveSub808Notes)
        {
            const auto legacy = toLegacyNoteEvent(note);
            if (legacy.semanticRole.isNotEmpty())
                ++taggedNoteCount;
            notes.add(noteToMetadataVar(legacy));
        }
    }
    else
    {
        for (const auto& note : track.notes)
        {
            if (note.semanticRole.isNotEmpty())
                ++taggedNoteCount;
            notes.add(noteToMetadataVar(note));
        }
    }

    object->setProperty("noteCount", track.type == TrackType::Sub808 ? static_cast<int>(effectiveSub808Notes.size()) : static_cast<int>(track.notes.size()));
    object->setProperty("taggedNoteCount", taggedNoteCount);
    object->setProperty("notes", juce::var(notes));
    return juce::var(object);
}

juce::var trackToMetadataVar(const TrackState& track, const RuntimeLaneDefinition* lane)
{
    auto* object = new juce::DynamicObject();
    const auto* info = TrackRegistry::find(track.type);
    object->setProperty("trackType", info != nullptr ? juce::String(toString(track.type)) : juce::String());
    object->setProperty("displayName", info != nullptr ? info->displayName : juce::String());
    object->setProperty("laneId", track.laneId);
    object->setProperty("runtimeTrackType", track.runtimeTrackType.has_value() ? juce::String(toString(*track.runtimeTrackType)) : juce::String());
    object->setProperty("bindingKind", lane != nullptr && lane->runtimeTrackType.has_value() ? "backed" : "unbacked");
    object->setProperty("laneName", lane != nullptr ? lane->laneName : juce::String());
    object->setProperty("laneGroup", lane != nullptr ? lane->groupName : juce::String());
    object->setProperty("laneDependency", lane != nullptr ? lane->dependencyName : juce::String());
    object->setProperty("generationPriority", lane != nullptr ? lane->generationPriority : 50);
    object->setProperty("isCoreLane", lane != nullptr ? lane->isCore : true);
    object->setProperty("isRuntimeRegistryLane", lane != nullptr ? lane->isRuntimeRegistryLane : false);
    object->setProperty("laneParams", trackStateToVar(track));
    return juce::var(object);
}

juce::var runtimeLaneToMetadataVar(const PatternProject& project, const RuntimeLaneDefinition& lane, int orderIndex)
{
    auto* object = new juce::DynamicObject();
    const auto* track = findTrackForLaneId(project, lane.laneId);

    object->setProperty("orderIndex", orderIndex);
    object->setProperty("laneId", lane.laneId);
    object->setProperty("laneName", lane.laneName);
    object->setProperty("groupName", lane.groupName);
    object->setProperty("dependencyName", lane.dependencyName);
    object->setProperty("generationPriority", lane.generationPriority);
    object->setProperty("isCore", lane.isCore);
    object->setProperty("isVisibleInEditor", lane.isVisibleInEditor);
    object->setProperty("enabledByDefault", lane.enabledByDefault);
    object->setProperty("supportsDragExport", lane.supportsDragExport);
    object->setProperty("isGhostTrack", lane.isGhostTrack);
    object->setProperty("defaultMidiNote", lane.defaultMidiNote);
    object->setProperty("isRuntimeRegistryLane", lane.isRuntimeRegistryLane);
    object->setProperty("runtimeTrackType", lane.runtimeTrackType.has_value() ? juce::String(toString(*lane.runtimeTrackType)) : juce::String());
    object->setProperty("bindingKind", track != nullptr ? "backed" : "unbacked");
    object->setProperty("hasBackingTrack", track != nullptr);
    object->setProperty("laneParamsAvailable", track != nullptr);

    if (track != nullptr)
    {
        object->setProperty("trackType", juce::String(toString(track->type)));
        object->setProperty("laneParams", trackStateToVar(*track));
    }
    else
    {
        object->setProperty("trackType", lane.runtimeTrackType.has_value() ? juce::String(toString(*lane.runtimeTrackType)) : juce::String());
    }

    return juce::var(object);
}

juce::var runtimeLaneLayoutToVar(const PatternProject& project)
{
    juce::Array<juce::var> lanes;
    const auto orderedLanes = orderedRuntimeLanes(project);
    for (int index = 0; index < static_cast<int>(orderedLanes.size()); ++index)
    {
        if (orderedLanes[static_cast<size_t>(index)] != nullptr)
            lanes.add(runtimeLaneToMetadataVar(project, *orderedLanes[static_cast<size_t>(index)], index));
    }

    return juce::var(lanes);
}

juce::var runtimeLaneOrderToVar(const PatternProject& project)
{
    juce::Array<juce::var> order;
    for (const auto& laneId : project.runtimeLaneOrder)
        order.add(laneId);

    return juce::var(order);
}

StyleLabLaneDefinition designerLaneFromRuntimeLane(const PatternProject& project, const RuntimeLaneDefinition& lane)
{
    StyleLabLaneDefinition designerLane;
    designerLane.id = lane.laneId.isNotEmpty() ? lane.laneId : makeId();
    designerLane.laneName = lane.laneName;
    designerLane.groupName = lane.groupName;
    designerLane.dependencyName = lane.dependencyName;
    designerLane.generationPriority = lane.generationPriority;
    designerLane.isCore = lane.isCore;
    if (const auto* track = findTrackForLaneId(project, lane.laneId))
        designerLane.enabled = track->enabled;
    else
        designerLane.enabled = lane.enabledByDefault;
    designerLane.isRuntimeRegistryLane = lane.isRuntimeRegistryLane;
    designerLane.runtimeTrackType = lane.runtimeTrackType;
    return designerLane;
}

bool designerLayoutMatchesRuntimeProject(const StyleLabState& state, const PatternProject& project)
{
    const auto orderedLanes = orderedRuntimeLanes(project);
    if (state.laneDefinitions.size() != orderedLanes.size())
        return false;

    for (size_t index = 0; index < orderedLanes.size(); ++index)
    {
        const auto* runtimeLane = orderedLanes[index];
        const auto& designerLane = state.laneDefinitions[index];
        if (runtimeLane == nullptr)
            return false;

        if (designerLane.laneName != runtimeLane->laneName
            || designerLane.groupName != runtimeLane->groupName
            || designerLane.dependencyName != runtimeLane->dependencyName
            || designerLane.generationPriority != runtimeLane->generationPriority
            || designerLane.isCore != runtimeLane->isCore
            || designerLane.enabled != (findTrackForLaneId(project, runtimeLane->laneId) != nullptr
                ? findTrackForLaneId(project, runtimeLane->laneId)->enabled
                : runtimeLane->enabledByDefault)
            || designerLane.isRuntimeRegistryLane != runtimeLane->isRuntimeRegistryLane
            || designerLane.runtimeTrackType != runtimeLane->runtimeTrackType)
        {
            return false;
        }
    }

    return true;
}

juce::String buildExportConflictDescription(const PatternProject& project, const StyleLabState& state)
{
    juce::StringArray messages;

    if (!state.laneDefinitions.empty() && !designerLayoutMatchesRuntimeProject(state, project))
    {
        messages.add("[EDITOR NOTE] Style Lab designer layout differs from the current runtime lane model. Reference export stores runtime lane state from PatternProject; designer layout is kept only as secondary metadata.");
    }

    const auto orderedLanes = orderedRuntimeLanes(project);
    const bool hasUnbackedRuntimeLane = std::any_of(orderedLanes.begin(), orderedLanes.end(), [](const RuntimeLaneDefinition* lane)
    {
        return lane != nullptr && !lane->runtimeTrackType.has_value();
    });

    if (hasUnbackedRuntimeLane)
    {
        messages.add("[RUNTIME NOTE] Custom runtime lanes are exported as unbacked layout entries. No generator-backed lane params are synthesized for them.");
    }

    return messages.joinIntoString(" ");
}

juce::var projectToMetadataVar(const PatternProject& project)
{
    auto* object = new juce::DynamicObject();
    object->setProperty("bpm", project.params.bpm);
    object->setProperty("swingPercent", project.params.swingPercent);
    object->setProperty("bars", project.params.bars);
    object->setProperty("phraseLengthBars", project.phraseLengthBars);
    object->setProperty("phraseRoleSummary", project.phraseRoleSummary);
    object->setProperty("previewStartStep", project.previewStartStep);
    object->setProperty("selectedTrackIndex", project.selectedTrackIndex);
    object->setProperty("soundModuleTrackIndex", project.soundModuleTrackIndex);
    object->setProperty("globalSound", soundLayerToVar(project.globalSound));
    object->setProperty("runtimeLaneOrder", runtimeLaneOrderToVar(project));
    object->setProperty("runtimeLaneLayout", runtimeLaneLayoutToVar(project));

    juce::Array<juce::var> tracks;
    int totalNotes = 0;
    int taggedNotes = 0;
    int backedLaneCount = 0;
    for (const auto& track : project.tracks)
    {
        const auto* lane = findRuntimeLaneById(project.runtimeLaneProfile, track.laneId);
        tracks.add(trackToMetadataVar(track, lane));
        ++backedLaneCount;
        if (track.type == TrackType::Sub808)
        {
            const auto effectiveSub808Notes = sub808NotesForRead(track);
            totalNotes += static_cast<int>(effectiveSub808Notes.size());
            taggedNotes += static_cast<int>(std::count_if(effectiveSub808Notes.begin(), effectiveSub808Notes.end(), [](const Sub808NoteEvent& note)
            {
                return note.semanticRole.isNotEmpty();
            }));
        }
        else
        {
            totalNotes += static_cast<int>(track.notes.size());
            taggedNotes += static_cast<int>(std::count_if(track.notes.begin(), track.notes.end(), [](const NoteEvent& note)
            {
                return note.semanticRole.isNotEmpty();
            }));
        }
    }

    const auto selectedTrackLaneId = (project.selectedTrackIndex >= 0 && project.selectedTrackIndex < static_cast<int>(project.tracks.size()))
        ? project.tracks[static_cast<size_t>(project.selectedTrackIndex)].laneId
        : juce::String();
    const auto soundModuleTrackLaneId = (project.soundModuleTrackIndex >= 0 && project.soundModuleTrackIndex < static_cast<int>(project.tracks.size()))
        ? project.tracks[static_cast<size_t>(project.soundModuleTrackIndex)].laneId
        : juce::String();

    object->setProperty("selectedTrackLaneId", selectedTrackLaneId);
    object->setProperty("soundModuleTrackLaneId", soundModuleTrackLaneId);
    object->setProperty("tracks", juce::var(tracks));
    object->setProperty("totalRuntimeLaneCount", static_cast<int>(project.runtimeLaneProfile.lanes.size()));
    object->setProperty("backedLaneCount", backedLaneCount);
    object->setProperty("unbackedLaneCount", static_cast<int>(project.runtimeLaneProfile.lanes.size()) - backedLaneCount);
    object->setProperty("totalNotes", totalNotes);
    object->setProperty("taggedNotes", taggedNotes);
    return juce::var(object);
}

juce::var stateToMetadataVar(const StyleLabState& state, const PatternProject& project, const juce::String& conflictMessage)
{
    auto* object = new juce::DynamicObject();
    object->setProperty("genre", state.genre);
    object->setProperty("substyle", state.substyle);
    object->setProperty("bars", state.bars);
    object->setProperty("tempoMin", state.tempoRange.getStart());
    object->setProperty("tempoMax", state.tempoRange.getEnd());
    object->setProperty("exportedAt", juce::Time::getCurrentTime().toISO8601(true));
    object->setProperty("conflictMessage", conflictMessage);
    object->setProperty("laneLayoutSource", "PatternProject.runtime");

    juce::Array<juce::var> designerLanes;
    for (const auto& lane : state.laneDefinitions)
        designerLanes.add(laneDefinitionToVar(lane));

    object->setProperty("runtimeLaneOrder", runtimeLaneOrderToVar(project));
    object->setProperty("laneLayout", runtimeLaneLayoutToVar(project));
    object->setProperty("designerLaneLayout", juce::var(designerLanes));
    object->setProperty("referenceProject", projectToMetadataVar(project));
    return juce::var(object);
}

}

StyleLabState StyleLabReferenceService::createDefaultState(const PatternProject& project,
                                                           const juce::String& genre,
                                                           const juce::String& substyle,
                                                           int bars,
                                                           int bpm)
{
    StyleLabState state;
    state.genre = genre.isNotEmpty() ? genre : "Boom Bap";
    state.substyle = substyle.isNotEmpty() ? substyle : "Classic";
    state.bars = juce::jlimit(1, 16, bars > 0 ? bars : juce::jmax(1, project.phraseLengthBars));

    const int boundedBpm = juce::jlimit(60, 180, bpm);
    state.tempoRange = { juce::jmax(60, boundedBpm - 6), juce::jmin(180, boundedBpm + 6) };

    const auto orderedLanes = orderedRuntimeLanes(project);
    if (!orderedLanes.empty())
    {
        state.laneDefinitions.reserve(orderedLanes.size());
        for (const auto* lane : orderedLanes)
        {
            if (lane != nullptr)
                state.laneDefinitions.push_back(designerLaneFromRuntimeLane(project, *lane));
        }
    }
    else
    {
        state.laneDefinitions.reserve(TrackRegistry::all().size());
        for (const auto& info : TrackRegistry::all())
        {
            StyleLabLaneDefinition lane;
            lane.id = makeId();
            lane.laneName = info.displayName;
            lane.groupName = LaneDefaults::defaultGroupForTrack(info.type);
            lane.dependencyName = LaneDefaults::defaultDependencyForTrack(info.type);
            lane.generationPriority = LaneDefaults::defaultPriorityForTrack(info.type);
            lane.isCore = LaneDefaults::defaultCoreForTrack(info.type);
            lane.enabled = true;
            lane.isRuntimeRegistryLane = true;
            lane.runtimeTrackType = info.type;
            state.laneDefinitions.push_back(std::move(lane));
        }
    }

    return state;
}

juce::String StyleLabReferenceService::buildReferenceMetadataJson(const PatternProject& project,
                                                                  const StyleLabState& state,
                                                                  juce::String* conflictMessage)
{
    const auto resolvedConflictMessage = buildExportConflictDescription(project, state);
    if (conflictMessage != nullptr)
        *conflictMessage = resolvedConflictMessage;

    return juce::JSON::toString(stateToMetadataVar(state, project, resolvedConflictMessage), true);
}

StyleLabReferenceExportResult StyleLabReferenceService::saveReferencePattern(const PatternProject& project,
                                                                             const StyleLabState& state)
{
    StyleLabReferenceExportResult result;
    result.conflictMessage = buildExportConflictDescription(project, state);

    auto root = getReferenceRootDirectory()
        .getChildFile(sanitizePathSegment(state.genre))
        .getChildFile(sanitizePathSegment(state.substyle));

    const auto stamp = juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S");
    const auto folderName = stamp + "_bars" + juce::String(state.bars)
        + "_bpm" + juce::String(state.tempoRange.getStart()) + "-" + juce::String(state.tempoRange.getEnd());
    result.directory = root.getChildFile(folderName);

    if (!result.directory.createDirectory())
    {
        result.errorMessage = "Failed to create Style Lab reference directory.";
        return result;
    }

    result.jsonFile = result.directory.getChildFile("pattern.json");
    result.metadataFile = result.directory.getChildFile("metadata.json");
    result.midiFile = result.directory.getChildFile("pattern.mid");

    const auto patternTree = PatternProjectSerialization::serialize(project);
    const auto patternJson = juce::JSON::toString(valueTreeToVar(patternTree), true);
    if (!result.jsonFile.replaceWithText(patternJson))
    {
        result.errorMessage = "Failed to write pattern JSON.";
        return result;
    }

    const auto metadataJson = buildReferenceMetadataJson(project, state, &result.conflictMessage);
    if (!result.metadataFile.replaceWithText(metadataJson))
    {
        result.errorMessage = "Failed to write metadata JSON.";
        return result;
    }

    const auto tempMidi = TemporaryMidiExportService::createTempFileForPattern(project);
    if (!tempMidi.existsAsFile())
    {
        result.errorMessage = "Failed to export MIDI reference file.";
        return result;
    }

    if (!tempMidi.copyFileTo(result.midiFile))
    {
        result.errorMessage = "Failed to copy MIDI reference file.";
        return result;
    }

    result.success = true;
    return result;
}

juce::String StyleLabReferenceService::buildConflictDescription(const StyleLabState& state)
{
    for (const auto& lane : state.laneDefinitions)
    {
        if (!lane.runtimeTrackType.has_value() || !lane.isRuntimeRegistryLane)
            return "[REFERENCE NOTE] This designer layout includes reference-only lanes or unbound edits. They do not change the current runtime lane model until explicitly applied.";

        const auto* info = TrackRegistry::find(*lane.runtimeTrackType);
        if (info != nullptr && lane.laneName != info->displayName)
            return "[REFERENCE NOTE] Renaming a runtime-linked lane here changes designer metadata only until the runtime project itself is updated.";
    }

    return "";
}

juce::File StyleLabReferenceService::getReferenceRootDirectory()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("DRUMENGINE")
    .getChildFile("StyleLabReferences");
}
} // namespace bbg