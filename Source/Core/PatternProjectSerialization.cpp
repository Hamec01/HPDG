#include "PatternProjectSerialization.h"

#include <algorithm>
#include <map>
#include <unordered_map>

#include "Sub808Types.h"

#include "TrackRegistry.h"
#include "../Utils/TimingHelpers.h"

namespace bbg
{
namespace
{
constexpr auto kPatternProjectNode = "PATTERN_PROJECT";
constexpr auto kTrackNode = "TRACK";
constexpr auto kNoteNode = "NOTE";
constexpr auto kRuntimeLaneProfileNode = "RUNTIME_LANE_PROFILE";
constexpr auto kRuntimeLaneNode = "RUNTIME_LANE";
constexpr auto kRuntimeLaneOrderNode = "RUNTIME_LANE_ORDER";
constexpr auto kRuntimeLaneOrderEntryNode = "RUNTIME_LANE_ORDER_ENTRY";
constexpr auto kAuthoringNode = "AUTHORING";
constexpr auto kTrackPreferencesNode = "TRACK_PREFERENCES";
constexpr auto kTrackPreferenceNode = "TRACK_PREFERENCE";
constexpr auto kNoteMetadataNode = "NOTE_METADATA";
constexpr auto kLaneNoteMetadataNode = "LANE_NOTE_METADATA";
constexpr auto kNoteAuthoringNode = "NOTE_AUTHORING";
constexpr auto kPhraseBlocksNode = "PHRASE_BLOCKS";
constexpr auto kPhraseBlockNode = "PHRASE_BLOCK";

int safeInt(const juce::ValueTree& node, const juce::Identifier& key, int fallback)
{
    const auto v = node.getProperty(key);
    return v.isVoid() ? fallback : static_cast<int>(v);
}

bool safeBool(const juce::ValueTree& node, const juce::Identifier& key, bool fallback)
{
    const auto v = node.getProperty(key);
    return v.isVoid() ? fallback : static_cast<bool>(v);
}

float safeFloat(const juce::ValueTree& node, const juce::Identifier& key, float fallback)
{
    const auto v = node.getProperty(key);
    return v.isVoid() ? fallback : static_cast<float>(v);
}

juce::String safeString(const juce::ValueTree& node, const juce::Identifier& key, const juce::String& fallback = {})
{
    const auto v = node.getProperty(key);
    return v.isVoid() ? fallback : v.toString();
}

void serializeSoundLayer(juce::ValueTree& node, const SoundLayerState& sound, const juce::String& prefix)
{
    node.setProperty(prefix + "_pan", sound.pan, nullptr);
    node.setProperty(prefix + "_width", sound.width, nullptr);
    node.setProperty(prefix + "_eq_tone", sound.eqTone, nullptr);
    node.setProperty(prefix + "_compression", sound.compression, nullptr);
    node.setProperty(prefix + "_reverb", sound.reverb, nullptr);
    node.setProperty(prefix + "_gate", sound.gate, nullptr);
    node.setProperty(prefix + "_transient", sound.transient, nullptr);
    node.setProperty(prefix + "_drive", sound.drive, nullptr);
}

void deserializeSoundLayer(const juce::ValueTree& node, SoundLayerState& sound, const juce::String& prefix)
{
    sound.pan = safeFloat(node, prefix + "_pan", sound.pan);
    sound.width = safeFloat(node, prefix + "_width", sound.width);
    sound.eqTone = safeFloat(node, prefix + "_eq_tone", sound.eqTone);
    sound.compression = safeFloat(node, prefix + "_compression", sound.compression);
    sound.reverb = safeFloat(node, prefix + "_reverb", sound.reverb);
    sound.gate = safeFloat(node, prefix + "_gate", sound.gate);
    sound.transient = safeFloat(node, prefix + "_transient", sound.transient);
    sound.drive = safeFloat(node, prefix + "_drive", sound.drive);
}

juce::ValueTree serializeRuntimeLane(const RuntimeLaneDefinition& lane)
{
    juce::ValueTree node(kRuntimeLaneNode);
    node.setProperty("lane_id", lane.laneId, nullptr);
    node.setProperty("lane_name", lane.laneName, nullptr);
    node.setProperty("group_name", lane.groupName, nullptr);
    node.setProperty("dependency_name", lane.dependencyName, nullptr);
    node.setProperty("generation_priority", lane.generationPriority, nullptr);
    node.setProperty("is_core", lane.isCore, nullptr);
    node.setProperty("is_visible_in_editor", lane.isVisibleInEditor, nullptr);
    node.setProperty("enabled_by_default", lane.enabledByDefault, nullptr);
    node.setProperty("supports_drag_export", lane.supportsDragExport, nullptr);
    node.setProperty("is_ghost_track", lane.isGhostTrack, nullptr);
    node.setProperty("default_midi_note", lane.defaultMidiNote, nullptr);
    node.setProperty("is_runtime_registry_lane", lane.isRuntimeRegistryLane, nullptr);
    if (lane.runtimeTrackType.has_value())
        node.setProperty("runtime_track_type", static_cast<int>(*lane.runtimeTrackType), nullptr);
    return node;
}

juce::ValueTree serializeRuntimeLaneProfile(const RuntimeLaneProfile& profile)
{
    juce::ValueTree node(kRuntimeLaneProfileNode);
    node.setProperty("genre", profile.genre, nullptr);
    node.setProperty("substyle", profile.substyle, nullptr);

    for (const auto& lane : profile.lanes)
        node.addChild(serializeRuntimeLane(lane), -1, nullptr);

    return node;
}

RuntimeLaneProfile deserializeRuntimeLaneProfile(const juce::ValueTree& patternNode)
{
    RuntimeLaneProfile profile;
    const auto profileNode = patternNode.getChildWithName(kRuntimeLaneProfileNode);
    if (!profileNode.isValid())
        return profile;

    profile.genre = safeString(profileNode, "genre");
    profile.substyle = safeString(profileNode, "substyle");

    for (int i = 0; i < profileNode.getNumChildren(); ++i)
    {
        const auto laneNode = profileNode.getChild(i);
        if (!laneNode.hasType(kRuntimeLaneNode))
            continue;

        RuntimeLaneDefinition lane;
        lane.laneId = safeString(laneNode, "lane_id").trim();
        lane.laneName = safeString(laneNode, "lane_name").trim();
        lane.groupName = safeString(laneNode, "group_name").trim();
        lane.dependencyName = safeString(laneNode, "dependency_name").trim();
        lane.generationPriority = safeInt(laneNode, "generation_priority", lane.generationPriority);
        lane.isCore = safeBool(laneNode, "is_core", lane.isCore);
        lane.isVisibleInEditor = safeBool(laneNode, "is_visible_in_editor", lane.isVisibleInEditor);
        lane.enabledByDefault = safeBool(laneNode, "enabled_by_default", lane.enabledByDefault);
        lane.supportsDragExport = safeBool(laneNode, "supports_drag_export", lane.supportsDragExport);
        lane.isGhostTrack = safeBool(laneNode, "is_ghost_track", lane.isGhostTrack);
        lane.defaultMidiNote = safeInt(laneNode, "default_midi_note", lane.defaultMidiNote);
        lane.isRuntimeRegistryLane = safeBool(laneNode, "is_runtime_registry_lane", lane.isRuntimeRegistryLane);

        const auto runtimeTrackTypeValue = laneNode.getProperty("runtime_track_type");
        if (!runtimeTrackTypeValue.isVoid())
            lane.runtimeTrackType = static_cast<TrackType>(static_cast<int>(runtimeTrackTypeValue));

        lane.editorCapabilities = makeLaneEditorCapabilities(lane.runtimeTrackType);

        profile.lanes.push_back(std::move(lane));
    }

    return profile;
}

void normalizeRuntimeLaneProfile(RuntimeLaneProfile& profile)
{
    if (profile.lanes.empty())
        profile = TrackRegistry::createDefaultRuntimeLaneProfile();

    juce::StringArray usedIds;
    for (auto& lane : profile.lanes)
    {
        if (lane.runtimeTrackType.has_value() && lane.laneId.isEmpty())
            lane.laneId = TrackRegistry::defaultRuntimeLaneId(*lane.runtimeTrackType);

        if (lane.laneId.isEmpty())
            lane.laneId = juce::Uuid().toString();

        while (usedIds.contains(lane.laneId))
            lane.laneId = lane.laneId + "_dup";
        usedIds.add(lane.laneId);

        if (lane.laneName.isEmpty())
        {
            if (lane.runtimeTrackType.has_value())
            {
                if (const auto* info = TrackRegistry::find(*lane.runtimeTrackType); info != nullptr)
                    lane.laneName = info->displayName;
            }

            if (lane.laneName.isEmpty())
                lane.laneName = "Lane";
        }

        lane.groupName = lane.groupName.trim();
        lane.dependencyName = lane.dependencyName.trim();
        lane.generationPriority = std::clamp(lane.generationPriority, 0, 100);
        lane.defaultMidiNote = std::clamp(lane.defaultMidiNote, 0, 127);
        lane.editorCapabilities = makeLaneEditorCapabilities(lane.runtimeTrackType);
    }
}

std::vector<RuntimeLaneId> deserializeRuntimeLaneOrder(const juce::ValueTree& patternNode)
{
    std::vector<RuntimeLaneId> order;
    const auto orderNode = patternNode.getChildWithName(kRuntimeLaneOrderNode);
    if (!orderNode.isValid())
        return order;

    for (int i = 0; i < orderNode.getNumChildren(); ++i)
    {
        const auto entryNode = orderNode.getChild(i);
        if (!entryNode.hasType(kRuntimeLaneOrderEntryNode))
            continue;

        const auto laneId = safeString(entryNode, "lane_id").trim();
        if (laneId.isNotEmpty())
            order.push_back(laneId);
    }

    return order;
}

juce::ValueTree serializeRuntimeLaneOrder(const std::vector<RuntimeLaneId>& order)
{
    juce::ValueTree node(kRuntimeLaneOrderNode);
    for (const auto& laneId : order)
    {
        juce::ValueTree entry(kRuntimeLaneOrderEntryNode);
        entry.setProperty("lane_id", laneId, nullptr);
        node.addChild(entry, -1, nullptr);
    }

    return node;
}

juce::ValueTree serializeAuthoringState(const PatternAuthoringState& authoring)
{
    juce::ValueTree node(kAuthoringNode);

    juce::ValueTree trackPrefsNode(kTrackPreferencesNode);
    for (const auto& [laneId, prefs] : authoring.trackPreferencesByLane)
    {
        if (laneId.isEmpty())
            continue;

        juce::ValueTree prefNode(kTrackPreferenceNode);
        prefNode.setProperty("lane_id", laneId, nullptr);
        prefNode.setProperty("preferred_density", prefs.preferredDensity, nullptr);
        prefNode.setProperty("anchor_preserve_ratio", prefs.anchorPreserveRatio, nullptr);
        prefNode.setProperty("allowed_silence", prefs.allowedSilence, nullptr);
        prefNode.setProperty("humanize_amount", prefs.humanizeAmount, nullptr);
        prefNode.setProperty("repetition_tolerance", prefs.repetitionTolerance, nullptr);
        prefNode.setProperty("fill_aggressiveness", prefs.fillAggressiveness, nullptr);
        trackPrefsNode.addChild(prefNode, -1, nullptr);
    }
    node.addChild(trackPrefsNode, -1, nullptr);

    juce::ValueTree noteMetadataNode(kNoteMetadataNode);
    for (const auto& [laneId, noteStates] : authoring.noteMetadataByLane)
    {
        if (laneId.isEmpty() || noteStates.empty())
            continue;

        juce::ValueTree laneNode(kLaneNoteMetadataNode);
        laneNode.setProperty("lane_id", laneId, nullptr);
        for (const auto& state : noteStates)
        {
            juce::ValueTree noteNode(kNoteAuthoringNode);
            noteNode.setProperty("step", state.noteKey.step, nullptr);
            noteNode.setProperty("micro_offset", state.noteKey.microOffset, nullptr);
            noteNode.setProperty("pitch", state.noteKey.pitch, nullptr);
            noteNode.setProperty("length", state.noteKey.length, nullptr);
            noteNode.setProperty("is_ghost", state.noteKey.isGhost, nullptr);
            noteNode.setProperty("anchor_locked", state.anchorLocked, nullptr);
            noteNode.setProperty("importance_weight", state.importanceWeight, nullptr);
            laneNode.addChild(noteNode, -1, nullptr);
        }

        noteMetadataNode.addChild(laneNode, -1, nullptr);
    }
    node.addChild(noteMetadataNode, -1, nullptr);

    juce::ValueTree phraseBlocksNode(kPhraseBlocksNode);
    for (const auto& block : authoring.phraseBlocks)
    {
        if (block.tickRange.getLength() <= 0)
            continue;

        juce::ValueTree blockNode(kPhraseBlockNode);
        blockNode.setProperty("start_tick", block.tickRange.getStart(), nullptr);
        blockNode.setProperty("end_tick", block.tickRange.getEnd(), nullptr);
        blockNode.setProperty("role", block.role, nullptr);
        phraseBlocksNode.addChild(blockNode, -1, nullptr);
    }
    node.addChild(phraseBlocksNode, -1, nullptr);

    return node;
}

PatternAuthoringState deserializeAuthoringState(const juce::ValueTree& patternNode)
{
    PatternAuthoringState authoring;
    const auto authoringNode = patternNode.getChildWithName(kAuthoringNode);
    if (!authoringNode.isValid())
        return authoring;

    const auto trackPrefsNode = authoringNode.getChildWithName(kTrackPreferencesNode);
    for (int i = 0; i < trackPrefsNode.getNumChildren(); ++i)
    {
        const auto prefNode = trackPrefsNode.getChild(i);
        if (!prefNode.hasType(kTrackPreferenceNode))
            continue;

        const auto laneId = safeString(prefNode, "lane_id").trim();
        if (laneId.isEmpty())
            continue;

        TrackAuthoringPreferences prefs;
        prefs.preferredDensity = safeFloat(prefNode, "preferred_density", prefs.preferredDensity);
        prefs.anchorPreserveRatio = safeFloat(prefNode, "anchor_preserve_ratio", prefs.anchorPreserveRatio);
        prefs.allowedSilence = safeFloat(prefNode, "allowed_silence", prefs.allowedSilence);
        prefs.humanizeAmount = safeFloat(prefNode, "humanize_amount", prefs.humanizeAmount);
        prefs.repetitionTolerance = safeFloat(prefNode, "repetition_tolerance", prefs.repetitionTolerance);
        prefs.fillAggressiveness = safeFloat(prefNode, "fill_aggressiveness", prefs.fillAggressiveness);
        authoring.trackPreferencesByLane[laneId] = prefs;
    }

    const auto noteMetadataNode = authoringNode.getChildWithName(kNoteMetadataNode);
    for (int i = 0; i < noteMetadataNode.getNumChildren(); ++i)
    {
        const auto laneNode = noteMetadataNode.getChild(i);
        if (!laneNode.hasType(kLaneNoteMetadataNode))
            continue;

        const auto laneId = safeString(laneNode, "lane_id").trim();
        if (laneId.isEmpty())
            continue;

        auto& laneStates = authoring.noteMetadataByLane[laneId];
        for (int noteIndex = 0; noteIndex < laneNode.getNumChildren(); ++noteIndex)
        {
            const auto noteNode = laneNode.getChild(noteIndex);
            if (!noteNode.hasType(kNoteAuthoringNode))
                continue;

            NoteAuthoringState state;
            state.noteKey.step = safeInt(noteNode, "step", 0);
            state.noteKey.microOffset = safeInt(noteNode, "micro_offset", 0);
            state.noteKey.pitch = safeInt(noteNode, "pitch", 36);
            state.noteKey.length = safeInt(noteNode, "length", 1);
            state.noteKey.isGhost = safeBool(noteNode, "is_ghost", false);
            state.anchorLocked = safeBool(noteNode, "anchor_locked", false);
            state.importanceWeight = safeInt(noteNode, "importance_weight", 50);
            laneStates.push_back(state);
        }
    }

    const auto phraseBlocksNode = authoringNode.getChildWithName(kPhraseBlocksNode);
    for (int i = 0; i < phraseBlocksNode.getNumChildren(); ++i)
    {
        const auto blockNode = phraseBlocksNode.getChild(i);
        if (!blockNode.hasType(kPhraseBlockNode))
            continue;

        const int startTick = safeInt(blockNode, "start_tick", -1);
        const int endTick = safeInt(blockNode, "end_tick", -1);
        if (startTick < 0 || endTick <= startTick)
            continue;

        PhraseBlock block;
        block.tickRange = juce::Range<int>(startTick, endTick);
        block.role = safeString(blockNode, "role").trim();
        authoring.phraseBlocks.push_back(std::move(block));
    }

    return authoring;
}

void normalizeRuntimeLaneOrder(const RuntimeLaneProfile& profile, std::vector<RuntimeLaneId>& order)
{
    juce::StringArray knownLaneIds;
    for (const auto& lane : profile.lanes)
        knownLaneIds.add(lane.laneId);

    std::vector<RuntimeLaneId> normalized;
    normalized.reserve(profile.lanes.size());

    juce::StringArray seen;
    for (const auto& laneId : order)
    {
        if (laneId.isEmpty() || !knownLaneIds.contains(laneId) || seen.contains(laneId))
            continue;

        normalized.push_back(laneId);
        seen.add(laneId);
    }

    for (const auto& lane : profile.lanes)
    {
        if (seen.contains(lane.laneId))
            continue;

        normalized.push_back(lane.laneId);
        seen.add(lane.laneId);
    }

    order.swap(normalized);
}

void sanitizeSoundLayerState(SoundLayerState& sound)
{
    sound.pan = std::clamp(sound.pan, -1.0f, 1.0f);
    sound.width = std::clamp(sound.width, 0.0f, 2.0f);
    sound.eqTone = std::clamp(sound.eqTone, -1.0f, 1.0f);
    sound.compression = std::clamp(sound.compression, 0.0f, 1.0f);
    sound.reverb = std::clamp(sound.reverb, 0.0f, 1.0f);
    sound.gate = std::clamp(sound.gate, 0.0f, 1.0f);
    sound.transient = std::clamp(sound.transient, 0.0f, 1.0f);
    sound.drive = std::clamp(sound.drive, 0.0f, 1.0f);
}

void sanitizeStyleInfluenceState(PatternProject& project)
{
    for (auto& laneBias : project.styleInfluence.laneBiases)
    {
        laneBias.activityWeight = std::clamp(laneBias.activityWeight, 0.0f, 2.0f);
        laneBias.balanceWeight = std::clamp(laneBias.balanceWeight, 0.0f, 2.0f);
    }

    project.styleInfluence.supportAccentWeight = std::clamp(project.styleInfluence.supportAccentWeight, 0.0f, 2.0f);
    project.styleInfluence.lowEndCouplingWeight = std::clamp(project.styleInfluence.lowEndCouplingWeight, 0.0f, 2.0f);
    project.styleInfluence.hatMotionWeight = std::clamp(project.styleInfluence.hatMotionWeight, 0.0f, 2.0f);
    project.styleInfluence.bounceWeight = std::clamp(project.styleInfluence.bounceWeight, 0.0f, 2.0f);
    project.styleInfluence.anchorRigidityWeight = std::clamp(project.styleInfluence.anchorRigidityWeight, 0.0f, 2.0f);
    project.styleInfluence.drillHatRollLengthWeight = std::clamp(project.styleInfluence.drillHatRollLengthWeight, 0.0f, 2.0f);
    project.styleInfluence.drillHatDensityVariationWeight = std::clamp(project.styleInfluence.drillHatDensityVariationWeight, 0.0f, 2.0f);
    project.styleInfluence.drillHatAccentPatternWeight = std::clamp(project.styleInfluence.drillHatAccentPatternWeight, 0.0f, 2.0f);
    project.styleInfluence.drillHatGapIntentWeight = std::clamp(project.styleInfluence.drillHatGapIntentWeight, 0.0f, 2.0f);
    project.styleInfluence.drillHatBurstWeight = std::clamp(project.styleInfluence.drillHatBurstWeight, 0.0f, 2.0f);
    project.styleInfluence.drillHatTripletWeight = std::clamp(project.styleInfluence.drillHatTripletWeight, 0.0f, 2.0f);
}

void reconcileTracksWithRuntimeLanes(PatternProject& project)
{
    if (project.tracks.empty() && !project.runtimeLaneProfile.lanes.empty())
        project.tracks = TrackRegistry::createDefaultTrackStates(project.runtimeLaneProfile);

    // Current runtime/editor path still supports only lanes backed by TrackType.
    std::map<juce::String, TrackState> incomingByLaneId;
    std::unordered_map<TrackType, TrackState> incomingByType;
    for (auto& track : project.tracks)
    {
        if (track.laneId.isEmpty())
        {
            if (const auto* lane = findRuntimeLaneForTrack(project.runtimeLaneProfile, track.type); lane != nullptr)
                track.laneId = lane->laneId;
            else
                track.laneId = TrackRegistry::defaultRuntimeLaneId(track.type);
        }

        if (!track.runtimeTrackType.has_value())
            track.runtimeTrackType = track.type;

        incomingByLaneId.emplace(track.laneId, track);
        incomingByType.emplace(track.type, track);
    }

    std::vector<TrackState> canonical;
    canonical.reserve(project.runtimeLaneProfile.lanes.size());

    for (const auto& lane : project.runtimeLaneProfile.lanes)
    {
        if (!lane.runtimeTrackType.has_value())
            continue;

        auto itByLaneId = incomingByLaneId.find(lane.laneId);
        if (itByLaneId != incomingByLaneId.end())
        {
            canonical.push_back(std::move(itByLaneId->second));
            continue;
        }

        auto itByType = incomingByType.find(*lane.runtimeTrackType);
        if (itByType != incomingByType.end())
        {
            auto restored = std::move(itByType->second);
            restored.laneId = lane.laneId;
            restored.runtimeTrackType = lane.runtimeTrackType;
            canonical.push_back(std::move(restored));
            continue;
        }

        TrackState fallback;
        fallback.type = *lane.runtimeTrackType;
        fallback.laneId = lane.laneId;
        fallback.runtimeTrackType = lane.runtimeTrackType;
        fallback.enabled = lane.enabledByDefault;
        canonical.push_back(std::move(fallback));
    }

    project.tracks = std::move(canonical);
}

void sanitizeGlobalSoundState(PatternProject& project)
{
    project.generationCounter = std::max(0, project.generationCounter);
    project.mutationCounter = std::max(0, project.mutationCounter);
    project.phraseLengthBars = std::clamp(project.phraseLengthBars, 1, 16);
    sanitizeSoundLayerState(project.globalSound);
}

void sanitizeTrackStates(PatternProject& project)
{
    for (auto& track : project.tracks)
    {
        if (track.laneId.isEmpty())
        {
            if (const auto* lane = findRuntimeLaneForTrack(project.runtimeLaneProfile, track.type); lane != nullptr)
                track.laneId = lane->laneId;
            else
                track.laneId = TrackRegistry::defaultRuntimeLaneId(track.type);
        }

        if (!track.runtimeTrackType.has_value())
            track.runtimeTrackType = track.type;

        track.sub808Settings.glideTimeMs = std::clamp(track.sub808Settings.glideTimeMs, 0, 4000);
        track.sub808Settings.overlapMode = static_cast<Sub808OverlapMode>(juce::jlimit(0,
                                                                                        2,
                                                                                        static_cast<int>(track.sub808Settings.overlapMode)));
        track.sub808Settings.scaleSnapPolicy = static_cast<Sub808ScaleSnapPolicy>(juce::jlimit(0,
                                                                                                2,
                                                                                                static_cast<int>(track.sub808Settings.scaleSnapPolicy)));
        track.templateId = std::max(0, track.templateId);
        track.variationId = std::max(0, track.variationId);
        track.mutationDepth = std::clamp(track.mutationDepth, 0.0f, 1.0f);
        track.laneVolume = std::clamp(track.laneVolume, 0.0f, 1.5f);
        track.selectedSampleIndex = std::max(0, track.selectedSampleIndex);
        sanitizeSoundLayerState(track.sound);
    }
}

void sanitizeTrackNotes(PatternProject& project)
{
    const int maxStep = 16 * 16 - 1;

    for (auto& track : project.tracks)
    {
        const auto* info = TrackRegistry::find(track.type);

        for (auto& note : track.notes)
        {
            note.pitch = std::clamp(note.pitch, 0, 127);
            if (note.pitch == 0 && info != nullptr)
                note.pitch = info->defaultMidiNote;

            note.step = std::clamp(note.step, 0, maxStep);
            note.length = std::clamp(note.length, 1, 64);
            note.velocity = std::clamp(note.velocity, 1, 127);
            note.microOffset = std::clamp(note.microOffset, -960, 960);
            note.semanticRole = note.semanticRole.trim();
            if (track.type != TrackType::Sub808)
            {
                note.isSlide = false;
                note.isLegato = false;
                note.glideToNext = false;
            }
        }

        if (track.type == TrackType::Sub808)
        {
            if (track.sub808Notes.empty() && !track.notes.empty())
                track.sub808Notes = toSub808NoteEvents(track.notes);

            for (auto& note : track.sub808Notes)
            {
                note.pitch = std::clamp(note.pitch, 0, 127);
                note.step = std::clamp(note.step, 0, maxStep);
                note.length = std::clamp(note.length, 1, 64);
                note.velocity = std::clamp(note.velocity, 1, 127);
                note.microOffset = std::clamp(note.microOffset, -960, 960);
                note.semanticRole = note.semanticRole.trim();
            }

            std::sort(track.sub808Notes.begin(), track.sub808Notes.end(), [](const Sub808NoteEvent& a, const Sub808NoteEvent& b)
            {
                if (a.step != b.step)
                    return a.step < b.step;
                return a.pitch < b.pitch;
            });

            track.notes = toLegacyNoteEvents(track.sub808Notes);
        }
        else
        {
            track.sub808Notes.clear();
        }

        std::sort(track.notes.begin(), track.notes.end(), [](const NoteEvent& a, const NoteEvent& b)
        {
            if (a.step != b.step)
                return a.step < b.step;
            return a.pitch < b.pitch;
        });
    }
}

void sanitizeAuthoringState(PatternProject& project)
{
    const int maxStep = 16 * 16 - 1;
    const int totalTicks = std::max(1, project.params.bars * 16 * ticksPerStep());

    for (auto it = project.authoring.trackPreferencesByLane.begin(); it != project.authoring.trackPreferencesByLane.end();)
    {
        if (it->first.isEmpty())
        {
            it = project.authoring.trackPreferencesByLane.erase(it);
            continue;
        }

        auto& prefs = it->second;
        prefs.preferredDensity = std::clamp(prefs.preferredDensity, 0.0f, 1.0f);
        prefs.anchorPreserveRatio = std::clamp(prefs.anchorPreserveRatio, 0.0f, 1.0f);
        prefs.allowedSilence = std::clamp(prefs.allowedSilence, 0.0f, 1.0f);
        prefs.humanizeAmount = std::clamp(prefs.humanizeAmount, 0.0f, 1.0f);
        prefs.repetitionTolerance = std::clamp(prefs.repetitionTolerance, 0.0f, 1.0f);
        prefs.fillAggressiveness = std::clamp(prefs.fillAggressiveness, 0.0f, 1.0f);
        ++it;
    }

    for (auto it = project.authoring.noteMetadataByLane.begin(); it != project.authoring.noteMetadataByLane.end();)
    {
        if (it->first.isEmpty())
        {
            it = project.authoring.noteMetadataByLane.erase(it);
            continue;
        }

        auto& noteStates = it->second;
        for (auto& state : noteStates)
        {
            state.noteKey.step = std::clamp(state.noteKey.step, 0, maxStep);
            state.noteKey.microOffset = std::clamp(state.noteKey.microOffset, -960, 960);
            state.noteKey.pitch = std::clamp(state.noteKey.pitch, 0, 127);
            state.noteKey.length = std::clamp(state.noteKey.length, 1, 64);
            state.importanceWeight = std::clamp(state.importanceWeight, 0, 100);
        }

        noteStates.erase(std::remove_if(noteStates.begin(), noteStates.end(), [](const NoteAuthoringState& state)
        {
            return state.noteKey.length <= 0;
        }),
                         noteStates.end());

        if (noteStates.empty())
            it = project.authoring.noteMetadataByLane.erase(it);
        else
            ++it;
    }

    project.authoring.phraseBlocks.erase(std::remove_if(project.authoring.phraseBlocks.begin(),
                                                        project.authoring.phraseBlocks.end(),
                                                        [totalTicks](PhraseBlock& block)
    {
        const int startTick = juce::jlimit(0, juce::jmax(0, totalTicks - 1), block.tickRange.getStart());
        const int endTick = juce::jlimit(startTick + 1, totalTicks, block.tickRange.getEnd());
        block.tickRange = juce::Range<int>(startTick, endTick);
        block.role = block.role.trim();
        return block.tickRange.getLength() <= 0;
    }),
                                        project.authoring.phraseBlocks.end());
}

void sanitizePreviewState(PatternProject& project)
{
    project.previewStartStep = std::max(0, project.previewStartStep);
    project.previewPlaybackMode = static_cast<PreviewPlaybackMode>(juce::jlimit(0,
                                                                                1,
                                                                                static_cast<int>(project.previewPlaybackMode)));

    const int totalTicks = std::max(1, project.params.bars * 16 * ticksPerStep());
    if (project.previewLoopTicks.has_value())
    {
        const int startTick = juce::jlimit(0, totalTicks - 1, project.previewLoopTicks->getStart());
        const int endTick = juce::jlimit(startTick + 1, totalTicks, project.previewLoopTicks->getEnd());
        if (endTick > startTick)
            project.previewLoopTicks = juce::Range<int>(startTick, endTick);
        else
            project.previewLoopTicks.reset();
    }
}

void sanitizeSelectionIndices(PatternProject& project)
{
    if (project.tracks.empty())
    {
        project.selectedTrackIndex = -1;
        project.soundModuleTrackIndex = -1;
    }
    else
    {
        project.selectedTrackIndex = std::clamp(project.selectedTrackIndex, 0, static_cast<int>(project.tracks.size()) - 1);
        project.soundModuleTrackIndex = std::clamp(project.soundModuleTrackIndex, -1, static_cast<int>(project.tracks.size()) - 1);
    }
}
} // namespace

juce::ValueTree PatternProjectSerialization::serialize(const PatternProject& project)
{
    juce::ValueTree root(kPatternProjectNode);
    root.setProperty("schema_version", kPatternSchemaVersion, nullptr);
    root.setProperty("selected_track_index", project.selectedTrackIndex, nullptr);
    root.setProperty("sound_module_track_index", project.soundModuleTrackIndex, nullptr);
    root.setProperty("generation_counter", project.generationCounter, nullptr);
    root.setProperty("mutation_counter", project.mutationCounter, nullptr);
    root.setProperty("phrase_length_bars", project.phraseLengthBars, nullptr);
    root.setProperty("phrase_role_summary", project.phraseRoleSummary, nullptr);
    root.setProperty("preview_start_step", project.previewStartStep, nullptr);
    root.setProperty("preview_playback_mode", static_cast<int>(project.previewPlaybackMode), nullptr);
    root.setProperty("preview_loop_start_tick", project.previewLoopTicks.has_value() ? project.previewLoopTicks->getStart() : -1, nullptr);
    root.setProperty("preview_loop_end_tick", project.previewLoopTicks.has_value() ? project.previewLoopTicks->getEnd() : -1, nullptr);
    root.setProperty("bars", project.params.bars, nullptr);
    serializeSoundLayer(root, project.globalSound, "global_sound");
    root.addChild(serializeRuntimeLaneProfile(project.runtimeLaneProfile), -1, nullptr);
    root.addChild(serializeRuntimeLaneOrder(project.runtimeLaneOrder), -1, nullptr);
    root.addChild(serializeAuthoringState(project.authoring), -1, nullptr);

    for (const auto& track : project.tracks)
        root.addChild(serializeTrack(track), -1, nullptr);

    return root;
}

bool PatternProjectSerialization::deserialize(const juce::ValueTree& rootState, PatternProject& projectOut)
{
    const auto patternNode = rootState.getChildWithName(kPatternProjectNode);
    if (!patternNode.isValid())
    {
        projectOut = createDefaultProject();
        validate(projectOut);
        return false;
    }

    PatternProject restored;
    restored.runtimeLaneProfile = deserializeRuntimeLaneProfile(patternNode);
    normalizeRuntimeLaneProfile(restored.runtimeLaneProfile);
    restored.runtimeLaneOrder = deserializeRuntimeLaneOrder(patternNode);
    normalizeRuntimeLaneOrder(restored.runtimeLaneProfile, restored.runtimeLaneOrder);
    restored.tracks = TrackRegistry::createDefaultTrackStates(restored.runtimeLaneProfile);
    restored.selectedTrackIndex = safeInt(patternNode, "selected_track_index", 0);
    restored.soundModuleTrackIndex = safeInt(patternNode, "sound_module_track_index", -1);
    restored.generationCounter = safeInt(patternNode, "generation_counter", 0);
    restored.mutationCounter = safeInt(patternNode, "mutation_counter", 0);
    restored.phraseLengthBars = safeInt(patternNode, "phrase_length_bars", restored.params.bars);
    restored.phraseRoleSummary = patternNode.getProperty("phrase_role_summary", {}).toString();
    restored.previewStartStep = safeInt(patternNode, "preview_start_step", 0);
    restored.previewPlaybackMode = static_cast<PreviewPlaybackMode>(safeInt(patternNode, "preview_playback_mode", 0));
    const int previewLoopStartTick = safeInt(patternNode, "preview_loop_start_tick", -1);
    const int previewLoopEndTick = safeInt(patternNode, "preview_loop_end_tick", -1);
    if (previewLoopStartTick >= 0 && previewLoopEndTick > previewLoopStartTick)
        restored.previewLoopTicks = juce::Range<int>(previewLoopStartTick, previewLoopEndTick);
    deserializeSoundLayer(patternNode, restored.globalSound, "global_sound");
    restored.authoring = deserializeAuthoringState(patternNode);

    std::map<juce::String, int> trackIndexByLaneId;
    std::unordered_map<TrackType, int> trackIndexByType;
    for (int i = 0; i < static_cast<int>(restored.tracks.size()); ++i)
    {
        const auto& track = restored.tracks[static_cast<size_t>(i)];
        if (track.laneId.isNotEmpty())
            trackIndexByLaneId[track.laneId] = i;
        trackIndexByType[track.type] = i;
    }

    for (int i = 0; i < patternNode.getNumChildren(); ++i)
    {
        const auto trackNode = patternNode.getChild(i);
        if (!trackNode.hasType(kTrackNode))
            continue;

        const auto typeInt = safeInt(trackNode, "type", static_cast<int>(TrackType::Kick));
        const auto type = static_cast<TrackType>(typeInt);
        const auto laneId = safeString(trackNode, "lane_id").trim();

        int trackSlot = -1;
        if (laneId.isNotEmpty())
        {
            auto itByLaneId = trackIndexByLaneId.find(laneId);
            if (itByLaneId != trackIndexByLaneId.end())
                trackSlot = itByLaneId->second;
        }

        if (trackSlot < 0)
        {
            auto itByType = trackIndexByType.find(type);
            if (itByType != trackIndexByType.end())
                trackSlot = itByType->second;
        }

        if (trackSlot < 0)
            continue; // Unknown tracks from future schemas are safely ignored.

        auto& track = restored.tracks[static_cast<size_t>(trackSlot)];
        if (laneId.isNotEmpty())
            track.laneId = laneId;
        track.runtimeTrackType = type;
        track.enabled = safeBool(trackNode, "enabled", track.enabled);
        track.muted = safeBool(trackNode, "muted", false);
        track.solo = safeBool(trackNode, "solo", false);
        track.locked = safeBool(trackNode, "locked", false);
        track.templateId = safeInt(trackNode, "template_id", 0);
        track.variationId = safeInt(trackNode, "variation_id", 0);
        track.mutationDepth = static_cast<float>(trackNode.getProperty("mutation_depth", 0.0));
        track.subProfile = trackNode.getProperty("sub_profile", {}).toString();
        track.laneRole = trackNode.getProperty("lane_role", {}).toString();
        track.laneVolume = static_cast<float>(trackNode.getProperty("lane_volume", track.laneVolume));
        track.selectedSampleIndex = safeInt(trackNode, "selected_sample_index", 0);
        track.selectedSampleName = trackNode.getProperty("selected_sample_name", {}).toString();
        deserializeSoundLayer(trackNode, track.sound, "sound");
        track.notes.clear();
        track.sub808Notes.clear();
        track.sub808Settings.mono = safeBool(trackNode, "sub808_mono", true);
        track.sub808Settings.cutItself = safeBool(trackNode, "sub808_cut_itself", true);
        track.sub808Settings.glideTimeMs = safeInt(trackNode, "sub808_glide_time_ms", 120);
        track.sub808Settings.overlapMode = static_cast<Sub808OverlapMode>(juce::jlimit(0,
                                                  2,
                                                  safeInt(trackNode, "sub808_overlap_mode", 0)));
        track.sub808Settings.scaleSnapPolicy = static_cast<Sub808ScaleSnapPolicy>(juce::jlimit(0,
                                                    2,
                                                    safeInt(trackNode, "sub808_scale_snap_policy", 2)));

        for (int n = 0; n < trackNode.getNumChildren(); ++n)
        {
            const auto noteNode = trackNode.getChild(n);
            if (!noteNode.hasType(kNoteNode))
                continue;

            NoteEvent note;
            note.pitch = safeInt(noteNode, "pitch", TrackRegistry::find(track.type) != nullptr ? TrackRegistry::find(track.type)->defaultMidiNote : 36);
            note.step = safeInt(noteNode, "step", 0);
            note.length = safeInt(noteNode, "length", 1);
            note.velocity = safeInt(noteNode, "velocity", 100);
            note.microOffset = safeInt(noteNode, "micro_offset", 0);
            note.isGhost = safeBool(noteNode, "is_ghost", false);
            note.semanticRole = noteNode.getProperty("semantic_role", {}).toString().trim();
            note.isSlide = safeBool(noteNode, "is_slide", false);
            note.isLegato = safeBool(noteNode, "is_legato", false);
            note.glideToNext = safeBool(noteNode, "glide_to_next", false);

            if (track.type == TrackType::Sub808)
                track.sub808Notes.push_back(toSub808NoteEvent(note));
            else
                track.notes.push_back(note);
        }

        if (track.type == TrackType::Sub808)
            track.notes = toLegacyNoteEvents(track.sub808Notes);
    }

    validate(restored);
    projectOut = std::move(restored);
    return true;
}

void PatternProjectSerialization::validate(PatternProject& project)
{
    const bool hasExplicitlyEmptyRuntimeLaneState = project.runtimeLaneProfile.lanes.empty()
        && project.runtimeLaneOrder.empty()
        && project.tracks.empty();

    if (!hasExplicitlyEmptyRuntimeLaneState)
        normalizeRuntimeLaneProfile(project.runtimeLaneProfile);

    if (project.runtimeLaneProfile.lanes.empty())
        project.runtimeLaneOrder.clear();
    else
        normalizeRuntimeLaneOrder(project.runtimeLaneProfile, project.runtimeLaneOrder);

    sanitizeStyleInfluenceState(project);
    reconcileTracksWithRuntimeLanes(project);
    sanitizeGlobalSoundState(project);
    sanitizeTrackStates(project);
    sanitizeTrackNotes(project);
    sanitizeAuthoringState(project);
    sanitizePreviewState(project);
    sanitizeSelectionIndices(project);
}

juce::ValueTree PatternProjectSerialization::serializeTrack(const TrackState& track)
{
    juce::ValueTree node(kTrackNode);
    node.setProperty("type", static_cast<int>(track.type), nullptr);
    node.setProperty("lane_id", track.laneId, nullptr);
    node.setProperty("enabled", track.enabled, nullptr);
    node.setProperty("muted", track.muted, nullptr);
    node.setProperty("solo", track.solo, nullptr);
    node.setProperty("locked", track.locked, nullptr);
    node.setProperty("template_id", track.templateId, nullptr);
    node.setProperty("variation_id", track.variationId, nullptr);
    node.setProperty("mutation_depth", track.mutationDepth, nullptr);
    node.setProperty("sub_profile", track.subProfile, nullptr);
    node.setProperty("lane_role", track.laneRole, nullptr);
    node.setProperty("lane_volume", track.laneVolume, nullptr);
    node.setProperty("selected_sample_index", track.selectedSampleIndex, nullptr);
    node.setProperty("selected_sample_name", track.selectedSampleName, nullptr);
    node.setProperty("sub808_mono", track.sub808Settings.mono, nullptr);
    node.setProperty("sub808_cut_itself", track.sub808Settings.cutItself, nullptr);
    node.setProperty("sub808_glide_time_ms", track.sub808Settings.glideTimeMs, nullptr);
    node.setProperty("sub808_overlap_mode", static_cast<int>(track.sub808Settings.overlapMode), nullptr);
    node.setProperty("sub808_scale_snap_policy", static_cast<int>(track.sub808Settings.scaleSnapPolicy), nullptr);
    serializeSoundLayer(node, track.sound, "sound");

    if (track.type == TrackType::Sub808)
    {
        const auto serializedSub808Notes = track.sub808Notes.empty() ? toSub808NoteEvents(track.notes) : track.sub808Notes;
        for (const auto& note : serializedSub808Notes)
            node.addChild(serializeNote(toLegacyNoteEvent(note)), -1, nullptr);
    }
    else
    {
        for (const auto& note : track.notes)
            node.addChild(serializeNote(note), -1, nullptr);
    }

    return node;
}

juce::ValueTree PatternProjectSerialization::serializeNote(const NoteEvent& note)
{
    juce::ValueTree node(kNoteNode);
    node.setProperty("pitch", note.pitch, nullptr);
    node.setProperty("step", note.step, nullptr);
    node.setProperty("length", note.length, nullptr);
    node.setProperty("velocity", note.velocity, nullptr);
    node.setProperty("micro_offset", note.microOffset, nullptr);
    node.setProperty("is_ghost", note.isGhost, nullptr);
    node.setProperty("semantic_role", note.semanticRole, nullptr);
    node.setProperty("is_slide", note.isSlide, nullptr);
    node.setProperty("is_legato", note.isLegato, nullptr);
    node.setProperty("glide_to_next", note.glideToNext, nullptr);
    return node;
}
} // namespace bbg
