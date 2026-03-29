#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <juce_core/juce_core.h>

#include "../Source/Core/PatternProject.h"
#include "../Source/Core/LaneDefaults.h"
#include "../Source/Core/PatternProjectSerialization.h"
#include "../Source/Core/ProjectLaneAccess.h"
#include "../Source/Core/ProjectStateController.h"
#include "../Source/Core/RuntimeLaneLifecycle.h"
#include "../Source/Core/TrackRegistry.h"
#include "../Source/Engine/Drill/DrillHatGenerator.h"
#include "../Source/Engine/Drill/DrillStyleProfile.h"
#include "../Source/Engine/StyleDefinitionLoader.h"
#include "../Source/Engine/StyleInfluence.h"
#include "../Source/Services/StyleLabReferenceService.h"
#include "../Source/Utils/TimingHelpers.h"

namespace bbg
{
namespace
{
[[noreturn]] void fail(const juce::String& message)
{
    throw std::runtime_error(message.toStdString());
}

void expect(bool condition, const juce::String& message)
{
    if (!condition)
        fail(message);
}

bool nearlyEqual(float left, float right, float epsilon = 0.0001f)
{
    return std::abs(left - right) <= epsilon;
}

bool noteEventEquals(const NoteEvent& left, const NoteEvent& right)
{
    return left.pitch == right.pitch
        && left.step == right.step
        && left.length == right.length
        && left.velocity == right.velocity
        && left.microOffset == right.microOffset
    && left.isGhost == right.isGhost
        && left.semanticRole == right.semanticRole
        && left.isSlide == right.isSlide
        && left.isLegato == right.isLegato
        && left.glideToNext == right.glideToNext;
}

bool sub808NoteEventEquals(const Sub808NoteEvent& left, const Sub808NoteEvent& right)
{
    return left.pitch == right.pitch
        && left.step == right.step
        && left.length == right.length
        && left.velocity == right.velocity
        && left.microOffset == right.microOffset
        && left.semanticRole == right.semanticRole
        && left.isSlide == right.isSlide
        && left.isLegato == right.isLegato
        && left.glideToNext == right.glideToNext;
}

template <typename EventType, typename Compare>
bool vectorEquals(const std::vector<EventType>& left, const std::vector<EventType>& right, Compare compare)
{
    if (left.size() != right.size())
        return false;

    for (size_t index = 0; index < left.size(); ++index)
    {
        if (!compare(left[index], right[index]))
            return false;
    }

    return true;
}

juce::ValueTree wrapSerializedProject(const PatternProject& project)
{
    juce::ValueTree root("ROOT");
    root.addChild(PatternProjectSerialization::serialize(project), -1, nullptr);
    return root;
}

PatternProject roundTrip(const PatternProject& project)
{
    PatternProject restored;
    expect(PatternProjectSerialization::deserialize(wrapSerializedProject(project), restored),
           "PatternProject deserialize failed during roundtrip.");
    return restored;
}

TrackState* findTrackByType(PatternProject& project, TrackType type)
{
    for (auto& track : project.tracks)
    {
        if (track.type == type)
            return &track;
    }

    return nullptr;
}

TrackState makeTrack(TrackType type)
{
    TrackState track;
    track.type = type;
    track.runtimeTrackType = type;
    track.enabled = true;
    return track;
}

const TrackState* findTrackByType(const PatternProject& project, TrackType type)
{
    for (const auto& track : project.tracks)
    {
        if (track.type == type)
            return &track;
    }

    return nullptr;
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

const TrackState* findTrackByLaneId(const PatternProject& project, const RuntimeLaneId& laneId)
{
    for (const auto& track : project.tracks)
    {
        if (track.laneId == laneId)
            return &track;
    }

    return nullptr;
}

int countAbsoluteMicroOffset(const TrackState& track)
{
    int total = 0;
    for (const auto& note : track.notes)
        total += std::abs(note.microOffset);
    return total;
}

int findTrackIndexByLaneId(const PatternProject& project, const RuntimeLaneId& laneId)
{
    for (int index = 0; index < static_cast<int>(project.tracks.size()); ++index)
    {
        if (project.tracks[static_cast<size_t>(index)].laneId == laneId)
            return index;
    }

    return -1;
}

juce::var parseJson(const juce::String& jsonText)
{
    const auto parsed = juce::JSON::parse(jsonText);
    expect(!parsed.isVoid(), "JSON parse failed.");
    return parsed;
}

juce::DynamicObject* requireObject(const juce::var& value, const juce::String& label)
{
    auto* object = value.getDynamicObject();
    expect(object != nullptr, label + " must be an object.");
    return object;
}

juce::var propertyOf(const juce::var& value, const juce::String& propertyName, const juce::String& ownerLabel)
{
    auto* object = requireObject(value, ownerLabel);
    const auto property = object->getProperty(propertyName);
    expect(!property.isVoid(), ownerLabel + "." + propertyName + " is missing.");
    return property;
}

const juce::Array<juce::var>& requireArray(const juce::var& value, const juce::String& label)
{
    auto* array = value.getArray();
    expect(array != nullptr, label + " must be an array.");
    return *array;
}

const juce::var* findObjectByStringProperty(const juce::Array<juce::var>& array,
                                            const juce::String& propertyName,
                                            const juce::String& expectedValue)
{
    for (const auto& item : array)
    {
        auto* object = item.getDynamicObject();
        if (object == nullptr)
            continue;

        if (object->getProperty(propertyName).toString() == expectedValue)
            return &item;
    }

    return nullptr;
}

void expectRuntimeLaneIntegrity(const PatternProject& project, const juce::String& label)
{
    std::set<juce::String> laneIds;
    std::set<juce::String> laneNames;

    for (const auto& lane : project.runtimeLaneProfile.lanes)
    {
        expect(lane.laneId.isNotEmpty(), label + ": laneId must not be empty.");
        expect(laneIds.insert(lane.laneId).second, label + ": duplicate laneId detected: " + lane.laneId);
        expect(laneNames.insert(lane.laneName.toLowerCase()).second,
               label + ": duplicate laneName detected: " + lane.laneName);
    }

    expect(project.runtimeLaneOrder.size() == project.runtimeLaneProfile.lanes.size(),
           label + ": runtimeLaneOrder size mismatch.");

    std::set<RuntimeLaneId> orderIds;
    for (const auto& laneId : project.runtimeLaneOrder)
    {
        expect(laneIds.count(laneId) == 1, label + ": runtimeLaneOrder contains unknown laneId: " + laneId);
        expect(orderIds.insert(laneId).second, label + ": duplicate laneId in runtimeLaneOrder: " + laneId);
    }

    for (const auto& lane : project.runtimeLaneProfile.lanes)
    {
        const auto* track = findTrackByLaneId(project, lane.laneId);
        if (lane.runtimeTrackType.has_value())
        {
            expect(track != nullptr, label + ": backed lane is missing track: " + lane.laneId);
            expect(track->runtimeTrackType.has_value(), label + ": backed track missing runtimeTrackType.");
            expect(track->type == *lane.runtimeTrackType, label + ": track.type mismatch for lane: " + lane.laneId);
            expect(*track->runtimeTrackType == *lane.runtimeTrackType,
                   label + ": runtimeTrackType mismatch for lane: " + lane.laneId);
        }
        else
        {
            expect(track == nullptr, label + ": unbacked lane must not create backing track: " + lane.laneId);
        }
    }

    for (const auto& track : project.tracks)
    {
        const auto* lane = findRuntimeLaneById(project.runtimeLaneProfile, track.laneId);
        expect(lane != nullptr, label + ": track points to missing laneId: " + track.laneId);
        expect(lane->runtimeTrackType.has_value(), label + ": track must point to backed lane: " + track.laneId);
        expect(*lane->runtimeTrackType == track.type, label + ": lane/track binding mismatch for laneId: " + track.laneId);
    }
}

std::map<RuntimeLaneId, int> noteCountsByLane(const PatternProject& project)
{
    std::map<RuntimeLaneId, int> counts;
    for (const auto& track : project.tracks)
    {
        const int count = track.type == TrackType::Sub808
            ? static_cast<int>((!track.sub808Notes.empty() ? track.sub808Notes : toSub808NoteEvents(track.notes)).size())
            : static_cast<int>(track.notes.size());
        counts[track.laneId] = count;
    }
    return counts;
}

void testSerializationRoundTripSmoke()
{
    auto project = createDefaultProject();
    project.params.bars = 4;
    project.params.bpm = 138.0f;

    std::rotate(project.runtimeLaneOrder.begin(), project.runtimeLaneOrder.begin() + 3, project.runtimeLaneOrder.end());

    auto* kick = findTrackByType(project, TrackType::Kick);
    auto* snare = findTrackByType(project, TrackType::Snare);
    auto* sub = findTrackByType(project, TrackType::Sub808);
    expect(kick != nullptr && snare != nullptr && sub != nullptr, "Default project is missing required tracks.");

    kick->notes = {
        NoteEvent { 36, 0, 1, 114, 12, false, "anchor", false, false, false },
        NoteEvent { 36, 8, 2, 96, -24, false, "support", false, false, false }
    };
    snare->notes = {
        NoteEvent { 38, 4, 1, 109, 0, false, "backbeat", false, false, false },
        NoteEvent { 38, 12, 1, 101, 18, false, "response", false, false, false }
    };
    sub->sub808Notes = {
        Sub808NoteEvent { 34, 0, 8, 108, 0, "root", false, false, false },
        Sub808NoteEvent { 37, 8, 4, 101, 30, "lift", true, false, true }
    };
    sub->notes = toLegacyNoteEvents(sub->sub808Notes);

    kick->laneVolume = 1.1f;
    sub->laneVolume = 0.92f;

    project.globalSound.pan = -0.21f;
    project.globalSound.width = 1.35f;
    project.globalSound.drive = 0.42f;
    project.globalSound.reverb = 0.31f;

    const auto selectedLaneId = kick->laneId;
    const auto soundLaneId = sub->laneId;
    project.selectedTrackIndex = findTrackIndexByLaneId(project, selectedLaneId);
    project.soundModuleTrackIndex = findTrackIndexByLaneId(project, soundLaneId);

    const auto beforeLaneOrder = project.runtimeLaneOrder;
    const auto beforeLaneIds = [&project]()
    {
        std::vector<RuntimeLaneId> out;
        out.reserve(project.runtimeLaneProfile.lanes.size());
        for (const auto& lane : project.runtimeLaneProfile.lanes)
            out.push_back(lane.laneId);
        return out;
    }();
    const auto beforeCounts = noteCountsByLane(project);

    const auto restored = roundTrip(project);
    expectRuntimeLaneIntegrity(restored, "serialization_roundtrip");

    expect(restored.tracks.size() == project.tracks.size(), "Track count changed after roundtrip.");
    expect(restored.runtimeLaneOrder == beforeLaneOrder, "runtimeLaneOrder changed after roundtrip.");

    std::vector<RuntimeLaneId> restoredLaneIds;
    restoredLaneIds.reserve(restored.runtimeLaneProfile.lanes.size());
    for (const auto& lane : restored.runtimeLaneProfile.lanes)
        restoredLaneIds.push_back(lane.laneId);
    expect(restoredLaneIds == beforeLaneIds, "Lane identities changed after roundtrip.");

    expect(noteCountsByLane(restored) == beforeCounts, "Per-lane note counts changed after roundtrip.");

    expect(restored.selectedTrackIndex >= 0 && restored.selectedTrackIndex < static_cast<int>(restored.tracks.size()),
           "Selected track index became invalid after roundtrip.");
    expect(restored.tracks[static_cast<size_t>(restored.selectedTrackIndex)].laneId == selectedLaneId,
           "Selected track lane changed after roundtrip.");
    expect(restored.soundModuleTrackIndex >= 0 && restored.soundModuleTrackIndex < static_cast<int>(restored.tracks.size()),
           "Sound module track index became invalid after roundtrip.");
    expect(restored.tracks[static_cast<size_t>(restored.soundModuleTrackIndex)].laneId == soundLaneId,
           "Sound module lane changed after roundtrip.");

    expect(nearlyEqual(restored.globalSound.pan, project.globalSound.pan), "Global sound pan changed after roundtrip.");
    expect(nearlyEqual(restored.globalSound.width, project.globalSound.width), "Global sound width changed after roundtrip.");
    expect(nearlyEqual(restored.globalSound.drive, project.globalSound.drive), "Global sound drive changed after roundtrip.");
    expect(nearlyEqual(restored.globalSound.reverb, project.globalSound.reverb), "Global sound reverb changed after roundtrip.");
}

void testRuntimeLaneLifecycle()
{
    auto project = createDefaultProject();
    expectRuntimeLaneIntegrity(project, "lifecycle_initial");

    expect(RuntimeLaneLifecycle::renameLane(project, TrackRegistry::defaultRuntimeLaneId(TrackType::Kick), "Main Kick"),
           "renameLane should succeed for Kick.");
    expectRuntimeLaneIntegrity(project, "lifecycle_after_rename");

    auto customLane = RuntimeLaneLifecycle::createCustomLaneDefinition(project, "Texture Vox");
    const auto customLaneId = customLane.laneId;
    expect(RuntimeLaneLifecycle::addLane(project, std::move(customLane), 2), "addLane should succeed for custom lane.");
    expectRuntimeLaneIntegrity(project, "lifecycle_after_custom_add");
    expect(findTrackByLaneId(project, customLaneId) == nullptr, "Custom lane must remain unbacked.");

    const auto ghostKickLaneId = TrackRegistry::defaultRuntimeLaneId(TrackType::GhostKick);
    expect(RuntimeLaneLifecycle::deleteLane(project, ghostKickLaneId), "deleteLane should remove GhostKick.");
    expectRuntimeLaneIntegrity(project, "lifecycle_after_delete_backed_lane");
    expect(findTrackByLaneId(project, ghostKickLaneId) == nullptr, "Deleted backed lane must remove its track.");

    expect(RuntimeLaneLifecycle::addRegistryLane(project, TrackType::GhostKick, 1),
           "addRegistryLane should restore GhostKick.");
    expectRuntimeLaneIntegrity(project, "lifecycle_after_add_registry_lane");
    expect(findTrackByLaneId(project, ghostKickLaneId) != nullptr, "Restored registry lane must recreate its backing track.");

    expect(RuntimeLaneLifecycle::deleteAllLanes(project), "deleteAllLanes should report changes.");
    expect(project.runtimeLaneProfile.lanes.empty(), "deleteAllLanes should clear runtime lane profile.");
    expect(project.runtimeLaneOrder.empty(), "deleteAllLanes should clear runtime lane order.");
    expect(project.tracks.empty(), "deleteAllLanes should clear tracks.");
    expect(project.selectedTrackIndex == -1, "deleteAllLanes should reset selectedTrackIndex.");
    expect(project.soundModuleTrackIndex == -1, "deleteAllLanes should reset soundModuleTrackIndex.");
}

void testValidateInvariants()
{
    auto project = createDefaultProject();
    project.params.bars = 4;

    expect(project.runtimeLaneProfile.lanes.size() >= 2, "Default runtime lane profile must contain at least two lanes.");
    project.runtimeLaneProfile.lanes[1].laneId = project.runtimeLaneProfile.lanes[0].laneId;
    project.runtimeLaneOrder = {
        project.runtimeLaneProfile.lanes[0].laneId,
        "missing:lane",
        project.runtimeLaneProfile.lanes[0].laneId
    };

    project.selectedTrackIndex = 999;
    project.soundModuleTrackIndex = 999;
    project.previewStartStep = -12;
    project.previewLoopTicks = juce::Range<int>(project.params.bars * 16 * ticksPerStep() + 120,
                                                project.params.bars * 16 * ticksPerStep() + 960);

    project.globalSound.pan = 3.0f;
    project.globalSound.width = -1.0f;
    project.globalSound.eqTone = 4.0f;
    project.globalSound.compression = 2.0f;
    project.globalSound.reverb = -0.5f;
    project.globalSound.gate = 5.0f;
    project.globalSound.transient = 9.0f;
    project.globalSound.drive = -3.0f;

    auto* kick = findTrackByType(project, TrackType::Kick);
    auto* sub = findTrackByType(project, TrackType::Sub808);
    expect(kick != nullptr && sub != nullptr, "Default project must contain Kick and Sub808 tracks.");

    kick->notes = {
        NoteEvent { 0, -5, 0, 200, 2000, false, " dirty ", true, true, true }
    };
    kick->laneVolume = 9.0f;
    kick->sound.pan = -8.0f;
    kick->sound.width = 4.0f;
    kick->sound.eqTone = 8.0f;
    kick->sound.compression = -1.0f;
    kick->sound.reverb = 5.0f;
    kick->sound.gate = -4.0f;
    kick->sound.transient = 7.0f;
    kick->sound.drive = -2.0f;

    sub->sub808Notes.clear();
    sub->notes = {
        NoteEvent { -7, 9999, 0, 999, -3000, false, " sub ", true, true, true }
    };
    sub->sub808Settings.glideTimeMs = 9000;
    sub->sub808Settings.overlapMode = static_cast<Sub808OverlapMode>(99);
    sub->sub808Settings.scaleSnapPolicy = static_cast<Sub808ScaleSnapPolicy>(99);

    PatternProjectSerialization::validate(project);
    expectRuntimeLaneIntegrity(project, "validate_invariants");

    expect(project.previewStartStep == 0, "previewStartStep should clamp to zero.");
    expect(project.previewLoopTicks.has_value(), "previewLoopTicks should remain normalized and valid.");
    expect(project.previewLoopTicks->getStart() >= 0, "preview loop start must be non-negative.");
    expect(project.previewLoopTicks->getEnd() <= project.params.bars * 16 * ticksPerStep(),
           "preview loop end must clamp to project length.");

    expect(project.selectedTrackIndex >= 0 && project.selectedTrackIndex < static_cast<int>(project.tracks.size()),
           "selectedTrackIndex must clamp into valid range.");
    expect(project.soundModuleTrackIndex >= -1 && project.soundModuleTrackIndex < static_cast<int>(project.tracks.size()),
           "soundModuleTrackIndex must clamp into valid range.");

    expect(nearlyEqual(project.globalSound.pan, 1.0f), "globalSound.pan should clamp.");
    expect(nearlyEqual(project.globalSound.width, 0.0f), "globalSound.width should clamp.");
    expect(nearlyEqual(project.globalSound.eqTone, 1.0f), "globalSound.eqTone should clamp.");
    expect(nearlyEqual(project.globalSound.compression, 1.0f), "globalSound.compression should clamp.");
    expect(nearlyEqual(project.globalSound.reverb, 0.0f), "globalSound.reverb should clamp.");
    expect(nearlyEqual(project.globalSound.gate, 1.0f), "globalSound.gate should clamp.");
    expect(nearlyEqual(project.globalSound.transient, 1.0f), "globalSound.transient should clamp.");
    expect(nearlyEqual(project.globalSound.drive, 0.0f), "globalSound.drive should clamp.");

    kick = findTrackByType(project, TrackType::Kick);
    sub = findTrackByType(project, TrackType::Sub808);
    expect(kick != nullptr && sub != nullptr, "Tracks must survive validation.");
    expect(static_cast<int>(kick->notes.size()) == 1, "Kick note should survive validation.");
    expect(kick->notes.front().pitch == TrackRegistry::find(TrackType::Kick)->defaultMidiNote,
           "Kick note pitch should normalize to default MIDI note when zero.");
    expect(kick->notes.front().step == 0, "Kick step should clamp to zero.");
    expect(kick->notes.front().length == 1, "Kick length should clamp to one.");
    expect(kick->notes.front().velocity == 127, "Kick velocity should clamp to 127.");
    expect(kick->notes.front().microOffset == 960, "Kick microOffset should clamp to 960.");
    expect(kick->notes.front().semanticRole == "dirty", "Kick semanticRole should trim.");
    expect(!kick->notes.front().isSlide && !kick->notes.front().isLegato && !kick->notes.front().glideToNext,
           "Non-Sub808 note articulation flags must reset.");
    expect(nearlyEqual(kick->laneVolume, 1.5f), "laneVolume should clamp to 1.5.");
    expect(nearlyEqual(kick->sound.pan, -1.0f), "Track sound pan should clamp.");
    expect(nearlyEqual(kick->sound.width, 2.0f), "Track sound width should clamp.");
    expect(nearlyEqual(kick->sound.eqTone, 1.0f), "Track sound eqTone should clamp.");
    expect(nearlyEqual(kick->sound.compression, 0.0f), "Track sound compression should clamp.");
    expect(nearlyEqual(kick->sound.reverb, 1.0f), "Track sound reverb should clamp.");
    expect(nearlyEqual(kick->sound.gate, 0.0f), "Track sound gate should clamp.");
    expect(nearlyEqual(kick->sound.transient, 1.0f), "Track sound transient should clamp.");
    expect(nearlyEqual(kick->sound.drive, 0.0f), "Track sound drive should clamp.");

    expect(!sub->sub808Notes.empty(), "Sub808 notes should be restored from legacy notes during validation.");
        expect(sub->sub808Notes.front().pitch == TrackRegistry::find(TrackType::Sub808)->defaultMidiNote,
            "Sub808 pitch should normalize to the track default MIDI note when zero-clamped.");
    expect(sub->sub808Notes.front().step == 255, "Sub808 step should clamp to maxStep.");
    expect(sub->sub808Notes.front().length == 1, "Sub808 length should clamp to one.");
    expect(sub->sub808Notes.front().velocity == 127, "Sub808 velocity should clamp to 127.");
    expect(sub->sub808Notes.front().microOffset == -960, "Sub808 microOffset should clamp to -960.");
    expect(sub->sub808Notes.front().semanticRole == "sub", "Sub808 semanticRole should trim.");
    expect(sub->sub808Settings.glideTimeMs == 4000, "Sub808 glideTimeMs should clamp to 4000.");
    expect(static_cast<int>(sub->sub808Settings.overlapMode) == 2, "Sub808 overlapMode should clamp.");
    expect(static_cast<int>(sub->sub808Settings.scaleSnapPolicy) == 2, "Sub808 scaleSnapPolicy should clamp.");
    expect(static_cast<int>(sub->notes.size()) == static_cast<int>(sub->sub808Notes.size()),
           "Sub808 legacy note mirror should stay aligned with sub808Notes.");
}

void testDefaultTrackRegistryConsistency()
{
    const auto& tracks = TrackRegistry::all();
    expect(tracks.size() == 11, "TrackRegistry::all() must expose 11 registry tracks.");

    std::set<juce::String> defaultIds;
    for (const auto& info : tracks)
    {
        const auto defaultId = TrackRegistry::defaultRuntimeLaneId(info.type);
        expect(defaultId.startsWith("registry:"), "Default runtime lane ids must use registry: prefix.");
        expect(defaultIds.insert(defaultId).second, "Default runtime lane ids must be unique.");
    }

    const auto profile = TrackRegistry::createDefaultRuntimeLaneProfile();
    const auto directStates = TrackRegistry::createDefaultTrackStates();
    const auto profileStates = TrackRegistry::createDefaultTrackStates(profile);

    expect(profile.lanes.size() == tracks.size(), "Default runtime lane profile size must match registry size.");
    expect(directStates.size() == tracks.size(), "Direct default track states size must match registry size.");
    expect(profileStates.size() == tracks.size(), "Profile-based default track states size must match registry size.");
    expect(directStates.size() == profileStates.size(), "Default track state factories must stay aligned.");

    for (const auto& info : tracks)
    {
        const auto expectedLaneId = TrackRegistry::defaultRuntimeLaneId(info.type);
        const auto* lane = findRuntimeLaneForTrack(profile, info.type);
        expect(lane != nullptr, "Default runtime lane profile is missing a registry lane.");
        expect(lane->laneId == expectedLaneId, "Runtime lane id must match TrackRegistry defaultRuntimeLaneId.");
        expect(lane->laneName == info.displayName, "Runtime lane display name mismatch.");
        expect(lane->runtimeTrackType.has_value() && *lane->runtimeTrackType == info.type,
               "Runtime lane type binding mismatch.");

        const auto* state = [&profileStates, &expectedLaneId, info]() -> const TrackState*
        {
            for (const auto& track : profileStates)
            {
                if (track.type == info.type)
                    return &track;
            }
            return nullptr;
        }();
        expect(state != nullptr, "Default track state missing for registry track.");
        expect(state->laneId == expectedLaneId, "Default track state's laneId must match runtime lane id.");
        expect(state->runtimeTrackType.has_value() && *state->runtimeTrackType == info.type,
               "Default track state's runtimeTrackType mismatch.");
        expect(state->enabled == info.enabledByDefault, "Default track enabled state mismatch.");
    }
}

void testStyleLabMetadataExportConsistency()
{
    auto project = createDefaultProject();
    auto customLane = RuntimeLaneLifecycle::createCustomLaneDefinition(project, "Reference Vox");
    const auto customLaneId = customLane.laneId;
    expect(RuntimeLaneLifecycle::addLane(project, std::move(customLane), 1), "Failed to add custom lane for metadata test.");

    auto* kick = findTrackByType(project, TrackType::Kick);
    expect(kick != nullptr, "Kick track missing for metadata test.");
    kick->laneVolume = 1.17f;
    kick->selectedSampleName = "MetadataKick";
    kick->notes = {
        NoteEvent { 36, 0, 1, 110, 0, false, "anchor", false, false, false }
    };

    PatternProjectSerialization::validate(project);
    const auto state = StyleLabReferenceService::createDefaultState(project, "Drill", "BrooklynDrill", 4, 140);
    juce::String conflictMessage;
    const auto metadataJson = StyleLabReferenceService::buildReferenceMetadataJson(project, state, &conflictMessage);

    expect(metadataJson.isNotEmpty(), "Metadata JSON must not be empty.");
    const auto metadataRoot = parseJson(metadataJson);
    const auto& laneLayout = requireArray(propertyOf(metadataRoot, "laneLayout", "metadataRoot"), "metadataRoot.laneLayout");

    expect(!laneLayout.isEmpty(), "Metadata laneLayout must not be empty.");
    expect(propertyOf(metadataRoot, "conflictMessage", "metadataRoot").isString(), "conflictMessage must be a string.");
    expect(conflictMessage.isNotEmpty(), "Custom lane export should emit a non-empty conflict message.");

    const auto* kickLaneEntry = findObjectByStringProperty(laneLayout,
                                                           "laneId",
                                                           TrackRegistry::defaultRuntimeLaneId(TrackType::Kick));
    expect(kickLaneEntry != nullptr, "Metadata laneLayout must contain Kick lane.");
    expect(propertyOf(*kickLaneEntry, "bindingKind", "kickLane").toString() == "backed",
           "Kick lane must remain backed in metadata export.");
    expect(static_cast<bool>(propertyOf(*kickLaneEntry, "hasBackingTrack", "kickLane")),
           "Kick lane must report hasBackingTrack=true.");
    expect(static_cast<bool>(propertyOf(*kickLaneEntry, "laneParamsAvailable", "kickLane")),
           "Kick lane must report laneParamsAvailable=true.");

    const auto* customLaneEntry = findObjectByStringProperty(laneLayout, "laneId", customLaneId);
    expect(customLaneEntry != nullptr, "Metadata laneLayout must contain custom lane.");
    expect(propertyOf(*customLaneEntry, "bindingKind", "customLane").toString() == "unbacked",
           "Custom lane must export as unbacked.");
    expect(!static_cast<bool>(propertyOf(*customLaneEntry, "hasBackingTrack", "customLane")),
           "Custom lane must report hasBackingTrack=false.");
    expect(!static_cast<bool>(propertyOf(*customLaneEntry, "laneParamsAvailable", "customLane")),
           "Custom lane must report laneParamsAvailable=false.");
}

    void testRegistryVsStyleLabConsistency()
    {
        const auto profile = TrackRegistry::createDefaultRuntimeLaneProfile();

        PatternProject emptyProject;
        emptyProject.runtimeLaneProfile.lanes.clear();
        emptyProject.runtimeLaneOrder.clear();
        emptyProject.tracks.clear();

        const auto fallbackState = StyleLabReferenceService::createDefaultState(emptyProject, "Boom Bap", "Classic", 4, 92);
        expect(static_cast<int>(fallbackState.laneDefinitions.size()) == static_cast<int>(profile.lanes.size()),
            "StyleLab fallback lane count must match TrackRegistry default profile.");

        for (const auto& info : TrackRegistry::all())
        {
         const auto* runtimeLane = findRuntimeLaneForTrack(profile, info.type);
         expect(runtimeLane != nullptr, "TrackRegistry default profile is missing registry lane.");

         const auto fallbackIt = std::find_if(fallbackState.laneDefinitions.begin(),
                               fallbackState.laneDefinitions.end(),
                               [type = info.type](const StyleLabLaneDefinition& lane)
                               {
                                return lane.runtimeTrackType.has_value() && *lane.runtimeTrackType == type;
                               });
         expect(fallbackIt != fallbackState.laneDefinitions.end(), "StyleLab fallback is missing registry lane definition.");

         expect(runtimeLane->groupName == LaneDefaults::defaultGroupForTrack(info.type),
             "Runtime lane group must come from LaneDefaults.");
         expect(fallbackIt->groupName == LaneDefaults::defaultGroupForTrack(info.type),
             "StyleLab fallback group must come from LaneDefaults.");
         expect(runtimeLane->dependencyName == LaneDefaults::defaultDependencyForTrack(info.type),
             "Runtime lane dependency must come from LaneDefaults.");
         expect(fallbackIt->dependencyName == LaneDefaults::defaultDependencyForTrack(info.type),
             "StyleLab fallback dependency must come from LaneDefaults.");
         expect(runtimeLane->generationPriority == LaneDefaults::defaultPriorityForTrack(info.type),
             "Runtime lane priority must come from LaneDefaults.");
         expect(fallbackIt->generationPriority == LaneDefaults::defaultPriorityForTrack(info.type),
             "StyleLab fallback priority must come from LaneDefaults.");
         expect(runtimeLane->isCore == LaneDefaults::defaultCoreForTrack(info.type),
             "Runtime lane isCore must come from LaneDefaults.");
         expect(fallbackIt->isCore == LaneDefaults::defaultCoreForTrack(info.type),
             "StyleLab fallback isCore must come from LaneDefaults.");

         expect(runtimeLane->groupName == fallbackIt->groupName,
             "Runtime lane and StyleLab fallback group mismatch.");
         expect(runtimeLane->dependencyName == fallbackIt->dependencyName,
             "Runtime lane and StyleLab fallback dependency mismatch.");
         expect(runtimeLane->generationPriority == fallbackIt->generationPriority,
             "Runtime lane and StyleLab fallback priority mismatch.");
         expect(runtimeLane->isCore == fallbackIt->isCore,
             "Runtime lane and StyleLab fallback isCore mismatch.");
        }
    }

    void testLaneDefaultsNoBehaviorRegressionSmoke()
    {
        const auto profile = TrackRegistry::createDefaultRuntimeLaneProfile();
        const auto defaultStates = TrackRegistry::createDefaultTrackStates(profile);

        struct ExpectedLaneDefaults
        {
         TrackType type;
         const char* laneName;
         const char* laneId;
         int priority;
         bool enabledByDefault;
        };

        static const std::array<ExpectedLaneDefaults, 11> expectedLanes {{
         { TrackType::HiHat, "HiHat", "registry:HiHat", 86, true },
         { TrackType::HatFX, "Hat Accent", "registry:HatFX", 56, false },
         { TrackType::OpenHat, "OpenHat", "registry:OpenHat", 74, true },
         { TrackType::Snare, "Snare", "registry:Snare", 92, true },
         { TrackType::ClapGhostSnare, "Clap Ghost", "registry:Clap/GhostSnare", 60, true },
         { TrackType::Kick, "Kick", "registry:Kick", 96, true },
         { TrackType::GhostKick, "Kick Ghost", "registry:GhostKick", 58, true },
         { TrackType::Ride, "Ride", "registry:Ride", 42, false },
         { TrackType::Cymbal, "Cymbal", "registry:Cymbal", 38, false },
         { TrackType::Perc, "Perc", "registry:Perc", 68, true },
         { TrackType::Sub808, "Sub808", "registry:Sub808", 82, false }
        }};

        expect(profile.lanes.size() == expectedLanes.size(), "Default runtime profile lane count changed.");
        expect(defaultStates.size() == expectedLanes.size(), "Default track state count changed.");

        for (const auto& expected : expectedLanes)
        {
         const auto* lane = findRuntimeLaneForTrack(profile, expected.type);
         expect(lane != nullptr, "Expected default runtime lane is missing.");
         expect(lane->laneName == expected.laneName, "Default lane name changed.");
         expect(lane->laneId == expected.laneId, "Default lane id changed.");
         expect(lane->generationPriority == expected.priority, "Default lane priority changed.");

         const auto* state = [&defaultStates, expected]() -> const TrackState*
         {
             for (const auto& track : defaultStates)
             {
              if (track.type == expected.type)
                  return &track;
             }
             return nullptr;
         }();
         expect(state != nullptr, "Expected default track state is missing.");
         expect(state->laneId == expected.laneId, "Default track state laneId changed.");
         expect(state->runtimeTrackType.has_value() && *state->runtimeTrackType == expected.type,
             "Default track state runtimeTrackType changed.");
         expect(state->enabled == expected.enabledByDefault, "Default track enabled state changed.");
        }
    }

        void testProjectLaneAccessLookupConsistency()
        {
            auto project = createDefaultProject();
            expectRuntimeLaneIntegrity(project, "project_lane_access_consistency");

            for (const auto& lane : project.runtimeLaneProfile.lanes)
            {
             const auto* resolvedLane = ProjectLaneAccess::findLaneDefinition(project, lane.laneId);
             expect(resolvedLane != nullptr, "ProjectLaneAccess must find every lane by laneId.");
             expect(resolvedLane->laneId == lane.laneId, "Lane lookup returned mismatched laneId.");
             expect(ProjectLaneAccess::isBackedLane(project, lane.laneId) == lane.runtimeTrackType.has_value(),
                 "Backed lane classification mismatch.");
             expect(ProjectLaneAccess::isUnbackedLane(project, lane.laneId) == !lane.runtimeTrackType.has_value(),
                 "Unbacked lane classification mismatch.");

             const auto resolvedType = ProjectLaneAccess::backingTrackTypeForLaneId(project, lane.laneId);
             expect(resolvedType == lane.runtimeTrackType, "Backing track type lookup mismatch.");

             const auto* trackByLane = ProjectLaneAccess::findTrackState(project, lane.laneId);
             if (lane.runtimeTrackType.has_value())
             {
                 const auto* trackByType = ProjectLaneAccess::findTrackState(project, *lane.runtimeTrackType);
                 expect(trackByLane != nullptr, "Backed lane must resolve to a track.");
                 expect(trackByType != nullptr, "Backed track type must resolve to a track.");
                 expect(trackByLane == trackByType, "LaneId and TrackType lookups must resolve the same backing track.");
                 expect(trackByLane->laneId == lane.laneId, "Resolved track must retain the laneId binding.");
                 expect(ProjectLaneAccess::canonicalLaneIdForTrack(project, *lane.runtimeTrackType) == lane.laneId,
                     "Canonical laneId must match runtime lane binding.");
             }
             else
             {
                 expect(trackByLane == nullptr, "Unbacked lane must not resolve to a track.");
             }
            }
        }

        void testProjectLaneAccessCustomLaneSafety()
        {
            auto project = createDefaultProject();
            auto customLane = RuntimeLaneLifecycle::createCustomLaneDefinition(project, "Texture Vox");
            const auto customLaneId = customLane.laneId;
            expect(RuntimeLaneLifecycle::addLane(project, std::move(customLane), 3),
                "Failed to add custom lane for ProjectLaneAccess safety test.");

            expect(ProjectLaneAccess::findLaneDefinition(project, customLaneId) != nullptr,
                "Custom lane must be discoverable by laneId.");
            expect(ProjectLaneAccess::findTrackState(project, customLaneId) == nullptr,
                "Custom unbacked lane must not resolve to a backing track.");
            expect(!ProjectLaneAccess::hasBackingTrackType(project, customLaneId),
                "Custom lane must not expose a backing track type.");
            expect(ProjectLaneAccess::backingTrackTypeForLaneId(project, customLaneId) == std::nullopt,
                "Custom lane backing track type lookup must return nullopt.");
            expect(ProjectLaneAccess::isUnbackedLane(project, customLaneId),
                "Custom lane must classify as unbacked.");
            expect(!ProjectLaneAccess::isBackedLane(project, customLaneId),
                "Custom lane must not classify as backed.");

            const RuntimeLaneId missingLaneId = "missing:lane";
            expect(ProjectLaneAccess::findLaneDefinition(project, missingLaneId) == nullptr,
                "Missing lane lookup must return nullptr.");
            expect(ProjectLaneAccess::findTrackState(project, missingLaneId) == nullptr,
                "Missing lane track lookup must return nullptr.");
            expect(ProjectLaneAccess::backingTrackTypeForLaneId(project, missingLaneId) == std::nullopt,
                "Missing lane backing type lookup must return nullopt.");
            expect(!ProjectLaneAccess::isBackedLane(project, missingLaneId),
                "Missing lane must not classify as backed.");
            expect(!ProjectLaneAccess::isUnbackedLane(project, missingLaneId),
                "Missing lane must not classify as unbacked.");

            for (const auto& info : TrackRegistry::all())
            {
             expect(ProjectLaneAccess::canonicalLaneIdForTrack(project, info.type) == TrackRegistry::defaultRuntimeLaneId(info.type),
                 "Canonical laneId lookup must preserve registry lane ids in the default profile.");
            }
        }

        void testProjectLaneAccessOrderStability()
        {
            auto project = createDefaultProject();
            auto customLane = RuntimeLaneLifecycle::createCustomLaneDefinition(project, "Order Test");
            const auto customLaneId = customLane.laneId;
            expect(RuntimeLaneLifecycle::addLane(project, std::move(customLane), 1),
                "Failed to add custom lane for order stability test.");

            std::map<TrackType, RuntimeLaneId> canonicalLaneIds;
            for (const auto& info : TrackRegistry::all())
             canonicalLaneIds.emplace(info.type, ProjectLaneAccess::canonicalLaneIdForTrack(project, info.type));

            std::reverse(project.runtimeLaneProfile.lanes.begin(), project.runtimeLaneProfile.lanes.end());
            std::rotate(project.runtimeLaneOrder.begin(), project.runtimeLaneOrder.begin() + 2, project.runtimeLaneOrder.end());
            std::reverse(project.tracks.begin(), project.tracks.end());

            for (const auto& [type, laneId] : canonicalLaneIds)
            {
             const auto* lane = ProjectLaneAccess::findLaneDefinition(project, laneId);
             const auto* track = ProjectLaneAccess::findTrackState(project, laneId);
             expect(lane != nullptr, "Lane lookup must remain stable after order changes.");
             expect(track != nullptr, "Track lookup by laneId must remain stable after order changes.");
             expect(track->type == type, "Track lookup returned wrong TrackType after order changes.");
             expect(ProjectLaneAccess::backingTrackTypeForLaneId(project, laneId) == type,
                 "Backing type lookup must remain stable after order changes.");
             expect(ProjectLaneAccess::canonicalLaneIdForTrack(project, type) == laneId,
                 "Canonical laneId lookup must remain stable after order changes.");
            }

            expect(ProjectLaneAccess::findLaneDefinition(project, customLaneId) != nullptr,
                "Custom lane must survive order changes.");
            expect(ProjectLaneAccess::findTrackState(project, customLaneId) == nullptr,
                "Custom unbacked lane must stay unbacked after order changes.");
            expect(ProjectLaneAccess::isUnbackedLane(project, customLaneId),
                "Custom lane classification must remain stable after order changes.");
        }

            void testProjectStateMutationParity()
            {
                auto project = createDefaultProject();
                const auto originalTrackCount = project.tracks.size();
                const auto kickLaneId = TrackRegistry::defaultRuntimeLaneId(TrackType::Kick);
                const auto snareLaneId = TrackRegistry::defaultRuntimeLaneId(TrackType::Snare);

                ProjectStateController::setTrackMuted(project, kickLaneId, true);
                ProjectStateController::setTrackSolo(project, kickLaneId, true);
                ProjectStateController::setTrackLocked(project, kickLaneId, true);
                ProjectStateController::setTrackEnabled(project, snareLaneId, false);
                ProjectStateController::setTrackLaneVolume(project, kickLaneId, 9.0f);

                auto* kick = ProjectLaneAccess::findTrackState(project, TrackType::Kick);
                auto* snare = ProjectLaneAccess::findTrackState(project, TrackType::Snare);
                expect(kick != nullptr && snare != nullptr, "Required backed tracks missing in mutation parity test.");
                expect(kick->muted, "Kick muted flag should update.");
                expect(kick->solo, "Kick solo flag should update.");
                expect(kick->locked, "Kick lock flag should update.");
                expect(!snare->enabled, "Snare enabled flag should update.");
                expect(nearlyEqual(kick->laneVolume, 1.5f), "Lane volume should clamp to 1.5.");
                expect(project.tracks.size() == originalTrackCount, "Mutation calls must not change track count.");

                ProjectStateController::setTrackLocked(project, kickLaneId, false);
                ProjectStateController::setTrackEnabled(project, snareLaneId, true);

                ProjectStateController::setTrackNotes(project,
                                    kickLaneId,
                                    {
                                        NoteEvent { 36, 6, 2, 120, 18, false, "anchor", false, false, false },
                                        NoteEvent { 36, 0, 1, 100, 0, false, "support", false, false, false }
                                    });
                ProjectStateController::setTrackNotes(project,
                                    snareLaneId,
                                    {
                                        NoteEvent { 38, 4, 1, 110, 0, false, "backbeat", false, false, false }
                                    });

                kick = ProjectLaneAccess::findTrackState(project, TrackType::Kick);
                snare = ProjectLaneAccess::findTrackState(project, TrackType::Snare);
                expect(static_cast<int>(kick->notes.size()) == 2, "Kick note edit should apply to the target lane.");
                expect(static_cast<int>(snare->notes.size()) == 1, "Snare note edit should apply to the target lane.");
                expect(kick->notes.front().step == 0, "Kick notes should remain sorted after mutation.");

                ProjectStateController::clearTrack(project, snareLaneId);
                expect(snare->notes.empty(), "clearTrack should clear only the targeted lane notes.");
                expect(static_cast<int>(kick->notes.size()) == 2, "clearTrack on snare must not touch kick notes.");

                ProjectStateController::setSelectedTrack(project, kickLaneId);
                expect(project.selectedTrackIndex >= 0, "Selected track index must be set.");
                expect(project.tracks[static_cast<size_t>(project.selectedTrackIndex)].type == TrackType::Kick,
                    "Selected track should resolve to Kick.");

                ProjectStateController::setSoundModuleTrack(project, TrackType::Kick);
                expect(project.soundModuleTrackIndex >= 0, "Sound module track index must be set.");
                expect(project.tracks[static_cast<size_t>(project.soundModuleTrackIndex)].type == TrackType::Kick,
                    "Sound module track should resolve to Kick.");

                SoundLayerState laneSound;
                laneSound.pan = 0.33f;
                laneSound.drive = 0.72f;
                ProjectStateController::setTrackSoundLayer(project, kickLaneId, laneSound);
                expect(nearlyEqual(kick->sound.pan, laneSound.pan), "Track sound layer pan should update.");
                expect(nearlyEqual(kick->sound.drive, laneSound.drive), "Track sound layer drive should update.");

                SoundLayerState globalSound;
                globalSound.width = 1.4f;
                globalSound.reverb = 0.25f;
                ProjectStateController::setGlobalSoundLayer(project, globalSound);
                expect(nearlyEqual(project.globalSound.width, globalSound.width), "Global sound width should update.");
                expect(nearlyEqual(project.globalSound.reverb, globalSound.reverb), "Global sound reverb should update.");

                ProjectStateController::setPreviewStartStep(project, 999);
                expect(project.previewStartStep == project.params.bars * 16 - 1, "Preview start step should clamp.");
                ProjectStateController::setPreviewPlaybackMode(project, PreviewPlaybackMode::LoopRange);
                expect(project.previewPlaybackMode == PreviewPlaybackMode::LoopRange, "Preview playback mode should update.");
                ProjectStateController::setPreviewLoopRegion(project, juce::Range<int>(-100, project.params.bars * 16 * ticksPerStep() + 500));
                expect(project.previewLoopTicks.has_value(), "Preview loop region should be stored.");
                expect(project.previewLoopTicks->getStart() == 0, "Preview loop start should clamp to zero.");
                expect(project.previewLoopTicks->getEnd() == project.params.bars * 16 * ticksPerStep(), "Preview loop end should clamp to total length.");
            }

            void testProjectStateSnapshotRestoreStability()
            {
                auto project = createDefaultProject();
                auto snapshot = createDefaultProject();

                std::rotate(snapshot.runtimeLaneOrder.begin(), snapshot.runtimeLaneOrder.begin() + 2, snapshot.runtimeLaneOrder.end());

                auto* kick = ProjectLaneAccess::findTrackState(snapshot, TrackType::Kick);
                auto* sub = ProjectLaneAccess::findTrackState(snapshot, TrackType::Sub808);
                expect(kick != nullptr && sub != nullptr, "Snapshot restore test requires Kick and Sub808 tracks.");

                kick->notes = {
                 NoteEvent { 36, 0, 1, 112, 0, false, "anchor", false, false, false },
                 NoteEvent { 36, 8, 2, 98, 24, false, "support", false, false, false }
                };
                sub->sub808Notes = {
                 Sub808NoteEvent { 34, 0, 8, 108, 0, "root", false, false, false },
                 Sub808NoteEvent { 37, 8, 4, 100, 30, "lift", true, false, true }
                };
                sub->notes = toLegacyNoteEvents(sub->sub808Notes);
                snapshot.selectedTrackIndex = 999;
                snapshot.soundModuleTrackIndex = 999;

                const auto expectedLaneOrder = snapshot.runtimeLaneOrder;
                const auto expectedKickNotes = kick->notes;
                const auto expectedSubNotes = sub->sub808Notes;
                const auto liveParams = project.params;

                ProjectStateController::restoreEditorProjectSnapshot(project, snapshot, liveParams);
                expectRuntimeLaneIntegrity(project, "project_state_snapshot_restore");

                expect(project.runtimeLaneOrder == expectedLaneOrder, "runtimeLaneOrder must survive snapshot restore.");
                const auto* restoredKick = ProjectLaneAccess::findTrackState(project, TrackType::Kick);
                const auto* restoredSub = ProjectLaneAccess::findTrackState(project, TrackType::Sub808);
                expect(restoredKick != nullptr && restoredSub != nullptr, "Restored project is missing required tracks.");
                    expect(vectorEquals(restoredKick->notes, expectedKickNotes, noteEventEquals),
                        "Kick notes must survive snapshot restore.");
                    expect(vectorEquals(restoredSub->sub808Notes, expectedSubNotes, sub808NoteEventEquals),
                        "Sub808 notes must survive snapshot restore.");
                expect(project.selectedTrackIndex >= 0 && project.selectedTrackIndex < static_cast<int>(project.tracks.size()),
                    "Selected track index must be valid after restore validation.");
                expect(project.soundModuleTrackIndex >= -1 && project.soundModuleTrackIndex < static_cast<int>(project.tracks.size()),
                    "Sound module track index must be valid after restore validation.");
                expect(project.params.genre == liveParams.genre && project.params.bars == liveParams.bars,
                    "Live params must win during snapshot restore.");
            }

            void testProjectStateLaneSafety()
            {
                auto project = createDefaultProject();
                auto customLane = RuntimeLaneLifecycle::createCustomLaneDefinition(project, "Unsafe Target");
                const auto customLaneId = customLane.laneId;
                expect(RuntimeLaneLifecycle::addLane(project, std::move(customLane), 2),
                    "Failed to add custom lane for lane safety test.");

                const RuntimeLaneId kickLaneId = TrackRegistry::defaultRuntimeLaneId(TrackType::Kick);
                const RuntimeLaneId snareLaneId = TrackRegistry::defaultRuntimeLaneId(TrackType::Snare);
                const RuntimeLaneId missingLaneId = "missing:lane";

                const auto before = project;
                ProjectStateController::setTrackMuted(project, kickLaneId, true);
                const auto* kick = ProjectLaneAccess::findTrackState(project, TrackType::Kick);
                const auto* snare = ProjectLaneAccess::findTrackState(project, TrackType::Snare);
                expect(kick != nullptr && snare != nullptr, "Lane safety test requires Kick and Snare tracks.");
                expect(kick->muted, "Targeted lane mute should apply.");
                expect(snare->muted == ProjectLaneAccess::findTrackState(before, TrackType::Snare)->muted,
                    "Muting one lane must not change other lanes.");

                ProjectStateController::setTrackMuted(project, customLaneId, true);
                ProjectStateController::setTrackLocked(project, customLaneId, true);
                ProjectStateController::setTrackEnabled(project, customLaneId, false);
                ProjectStateController::setTrackNotes(project, customLaneId, { NoteEvent { 36, 0, 1, 100, 0, false, "x", false, false, false } });
                ProjectStateController::clearTrack(project, customLaneId);
                expect(ProjectLaneAccess::findTrackState(project, customLaneId) == nullptr,
                    "Unbacked lane operations must not synthesize a backing track.");

                const auto snapshotBeforeInvalid = project;
                ProjectStateController::setTrackSolo(project, missingLaneId, true);
                ProjectStateController::setTrackMuted(project, missingLaneId, true);
                ProjectStateController::setTrackLocked(project, missingLaneId, true);
                ProjectStateController::setTrackEnabled(project, missingLaneId, false);
                ProjectStateController::setTrackLaneVolume(project, missingLaneId, 0.1f);
                ProjectStateController::setTrackNotes(project, missingLaneId, { NoteEvent { 40, 2, 1, 90, 0, false, "missing", false, false, false } });
                ProjectStateController::clearTrack(project, missingLaneId);
                expect(project.runtimeLaneOrder == snapshotBeforeInvalid.runtimeLaneOrder,
                    "Invalid laneId operations must not corrupt runtimeLaneOrder.");
                expect(project.tracks.size() == snapshotBeforeInvalid.tracks.size(),
                    "Invalid laneId operations must not change track count.");
                expect(project.selectedTrackIndex == snapshotBeforeInvalid.selectedTrackIndex,
                    "Invalid laneId operations must not change selected track.");
            }

                void testDrillReferenceRollClusterPreservation()
                {
                    const auto drillSubstyleName = StyleDefinitionLoader::substyleNameFor(GenreType::Drill, 0);

                    auto referenceProject = createDefaultProject();
                    referenceProject.params.genre = GenreType::Drill;
                    referenceProject.params.drillSubstyle = 0;

                    auto* referenceHat = findTrackByType(referenceProject, TrackType::HiHat);
                    auto* referenceKick = findTrackByType(referenceProject, TrackType::Kick);
                    auto* referenceSnare = findTrackByType(referenceProject, TrackType::Snare);
                    expect(referenceHat != nullptr && referenceKick != nullptr && referenceSnare != nullptr,
                        "Drill roll cluster preservation test requires HiHat, Kick and Snare tracks.");

                    referenceHat->laneRole = "ref_hat_rolls";
                    referenceHat->laneVolume = 0.66f;
                    referenceHat->selectedSampleName = "ReferenceHatOnly";
                    referenceHat->enabled = false;
                    referenceHat->notes = {
                     { 42, 0, 1, 94, 0, false },
                     { 42, 2, 1, 66, 0, false },
                     { 42, 4, 1, 98, 0, false },
                     { 42, 7, 1, 72, 0, false },
                     { 42, 7, 1, 78, 120, false },
                     { 42, 8, 1, 92, 0, false },
                     { 42, 10, 1, 62, 0, false },
                     { 42, 12, 1, 96, 0, false },
                     { 42, 15, 1, 70, 0, false }
                    };
                    referenceKick->notes = { { 36, 0, 1, 112, 0, false, "keep_project_only" } };
                    referenceSnare->notes = { { 38, 4, 1, 108, 0, false, "keep_project_only" } };

                    PatternProjectSerialization::validate(referenceProject);
                    const auto state = StyleLabReferenceService::createDefaultState(referenceProject, "Drill", drillSubstyleName, 4, 142);
                    const auto metadataJson = StyleLabReferenceService::buildReferenceMetadataJson(referenceProject, state);
                    auto metadataRoot = parseJson(metadataJson);

                    auto& rootRuntimeLaneOrder = const_cast<juce::Array<juce::var>&>(requireArray(propertyOf(metadataRoot, "runtimeLaneOrder", "metadataRoot"), "metadataRoot.runtimeLaneOrder"));
                    auto& rootLaneLayout = const_cast<juce::Array<juce::var>&>(requireArray(propertyOf(metadataRoot, "laneLayout", "metadataRoot"), "metadataRoot.laneLayout"));
                    auto referenceProjectVar = propertyOf(metadataRoot, "referenceProject", "metadataRoot");
                    auto* referenceProjectObject = requireObject(referenceProjectVar, "referenceProject");

                    const auto* hatLane = findRuntimeLaneForTrack(referenceProject.runtimeLaneProfile, TrackType::HiHat);
                    expect(hatLane != nullptr, "HiHat lane missing from reference runtime profile.");

                    const auto* hatLayout = findObjectByStringProperty(rootLaneLayout, "laneId", hatLane->laneId);
                    expect(hatLayout != nullptr, "HiHat lane missing from metadata laneLayout.");

                    juce::Array<juce::var> filteredOrder;
                    filteredOrder.add(hatLane->laneId);
                    rootRuntimeLaneOrder = filteredOrder;

                    juce::Array<juce::var> filteredLayout;
                    filteredLayout.add(*hatLayout);
                    rootLaneLayout = filteredLayout;

                    auto* hatLayoutObject = filteredLayout.getReference(0).getDynamicObject();
                    expect(hatLayoutObject != nullptr, "Filtered HiHat layout must remain an object.");
                    auto* laneParamsObject = requireObject(hatLayoutObject->getProperty("laneParams"), "filteredHatLayout.laneParams");
                    laneParamsObject->setProperty("enabled", false);
                    laneParamsObject->setProperty("laneRole", "ref_hat_rolls");
                    laneParamsObject->setProperty("laneVolume", 0.66f);
                    laneParamsObject->setProperty("selectedSampleName", "ReferenceHatOnly");

                    referenceProjectObject->setProperty("totalRuntimeLaneCount", 1);
                    referenceProjectObject->setProperty("backedLaneCount", 1);
                    referenceProjectObject->setProperty("unbackedLaneCount", 0);

                    auto tempRoot = juce::File::getSpecialLocation(juce::File::tempDirectory)
                     .getChildFile("DRUMENGINE_CoreTests")
                     .getChildFile("DrillRollClusterPreservation");
                    tempRoot.deleteRecursively();
                    const auto referenceDirectory = tempRoot
                     .getChildFile(StyleDefinitionLoader::genreDisplayName(GenreType::Drill))
                     .getChildFile(drillSubstyleName)
                     .getChildFile("2099-01-01_00-00-00_roll_cluster");
                    expect(referenceDirectory.createDirectory(), "Failed to create drill roll cluster temp directory.");
                    const auto metadataFile = referenceDirectory.getChildFile("metadata.json");
                    expect(metadataFile.replaceWithText(juce::JSON::toString(metadataRoot)),
                        "Failed to write drill roll cluster metadata.json.");

                    juce::String loadError;
                    const auto loaded = StyleDefinitionLoader::loadLatestForStyle(StyleDefinitionLoader::genreDisplayName(GenreType::Drill),
                                                       drillSubstyleName,
                                                       tempRoot,
                                                       &loadError);
                    expect(loaded.has_value(), "Failed to load drill roll cluster reference: " + loadError);
                    expect(loaded->referenceHatSkeleton.has_value() && loaded->referenceHatSkeleton->available,
                        "Drill roll cluster reference must expose ReferenceHatSkeleton.");
                    expect(!loaded->referenceHatSkeleton->rollClusters.empty(),
                        "Drill roll cluster reference must preserve extracted roll clusters during load.");

                    auto influenceTarget = createDefaultProject();
                    influenceTarget.params.genre = GenreType::Drill;
                    influenceTarget.params.drillSubstyle = 0;
                    expect(DrillStyleInfluence::applyResolvedStyle(*loaded, influenceTarget, &loadError),
                        "DrillStyleInfluence failed for roll cluster preservation test: " + loadError);
                    expect(influenceTarget.styleInfluence.referenceHatSkeleton.available,
                        "Drill style influence must propagate ReferenceHatSkeleton.");
                    expect(!influenceTarget.styleInfluence.referenceHatSkeleton.rollClusters.empty(),
                        "Drill style influence must preserve roll clusters from the loaded reference.");

                    tempRoot.deleteRecursively();
                }

                void testDrillHatMotionMicroTimingResponse()
                {
                    GeneratorParams params;
                    params.bars = 4;
                    params.bpm = 142.0f;
                    params.densityAmount = 0.64f;

                    StyleInfluenceState lowInfluence;
                    StyleInfluenceState highInfluence;
                    lowInfluence.hatMotionWeight = 0.54f;
                    highInfluence.hatMotionWeight = 1.52f;
                    laneBiasFor(lowInfluence, TrackType::HiHat).activityWeight = 0.38f;
                    laneBiasFor(highInfluence, TrackType::HiHat).activityWeight = 1.48f;

                    const auto& style = getDrillProfile(0);
                    std::vector<DrillPhraseRole> phrase {
                        DrillPhraseRole::Statement,
                        DrillPhraseRole::Response,
                        DrillPhraseRole::Tension,
                        DrillPhraseRole::Ending
                    };

                    TrackState snare = makeTrack(TrackType::Snare);
                    snare.notes = {
                        { 38, 4, 1, 110, 0, false },
                        { 38, 12, 1, 112, 0, false },
                        { 38, 20, 1, 110, 0, false },
                        { 38, 28, 1, 112, 0, false }
                    };
                    TrackState clap = makeTrack(TrackType::ClapGhostSnare);
                    TrackState kick = makeTrack(TrackType::Kick);
                    kick.notes = {
                        { 36, 0, 1, 110, 0, false },
                        { 36, 6, 1, 102, 0, false },
                        { 36, 10, 1, 104, 0, false },
                        { 36, 16, 1, 110, 0, false },
                        { 36, 22, 1, 100, 0, false },
                        { 36, 26, 1, 104, 0, false }
                    };

                    TrackState lowHat = makeTrack(TrackType::HiHat);
                    TrackState highHat = makeTrack(TrackType::HiHat);
                    DrillHatGenerator generator;
                    std::mt19937 lowRng(402);
                    std::mt19937 highRng(402);

                    generator.generate(lowHat, params, style, lowInfluence, phrase, nullptr, &snare, &clap, &kick, lowRng);
                    generator.generate(highHat, params, style, highInfluence, phrase, nullptr, &snare, &clap, &kick, highRng);

                    expect(!lowHat.notes.empty() && !highHat.notes.empty(),
                        "Drill hat motion test requires generated hats.");
                    expect(highHat.notes.size() >= lowHat.notes.size(),
                        "Higher Drill hat activity should not reduce generated hat count.");
                    expect(countAbsoluteMicroOffset(highHat) > countAbsoluteMicroOffset(lowHat),
                        "Higher Drill hat motion must increase total hat microtiming displacement.");
                }

int runTest(const char* name, const std::function<void()>& test)
{
    try
    {
        test();
        std::cout << "[PASS] " << name << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[FAIL] " << name << ": " << error.what() << std::endl;
        return 1;
    }
}
} // namespace
} // namespace bbg

int main()
{
    using namespace bbg;

    int failures = 0;
    failures += runTest("Serialization roundtrip smoke", testSerializationRoundTripSmoke);
    failures += runTest("Runtime lane lifecycle", testRuntimeLaneLifecycle);
    failures += runTest("Validate invariants", testValidateInvariants);
    failures += runTest("Default track registry consistency", testDefaultTrackRegistryConsistency);
    failures += runTest("StyleLab metadata export consistency", testStyleLabMetadataExportConsistency);
    failures += runTest("Registry vs StyleLab consistency", testRegistryVsStyleLabConsistency);
    failures += runTest("Lane defaults no behavior regression", testLaneDefaultsNoBehaviorRegressionSmoke);
    failures += runTest("Project lane access lookup consistency", testProjectLaneAccessLookupConsistency);
    failures += runTest("Project lane access custom lane safety", testProjectLaneAccessCustomLaneSafety);
    failures += runTest("Project lane access order stability", testProjectLaneAccessOrderStability);
    failures += runTest("Project state mutation parity", testProjectStateMutationParity);
    failures += runTest("Project state snapshot restore stability", testProjectStateSnapshotRestoreStability);
    failures += runTest("Project state lane safety", testProjectStateLaneSafety);
    failures += runTest("Drill reference roll cluster preservation", testDrillReferenceRollClusterPreservation);
    failures += runTest("Drill hat motion microtiming response", testDrillHatMotionMicroTimingResponse);

    if (failures == 0)
    {
        std::cout << "All core tests passed." << std::endl;
        return 0;
    }

    std::cerr << failures << " core test(s) failed." << std::endl;
    return 1;
}