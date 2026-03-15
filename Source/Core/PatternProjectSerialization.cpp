#include "PatternProjectSerialization.h"

#include <algorithm>
#include <unordered_map>

#include "TrackRegistry.h"

namespace bbg
{
namespace
{
constexpr auto kPatternProjectNode = "PATTERN_PROJECT";
constexpr auto kTrackNode = "TRACK";
constexpr auto kNoteNode = "NOTE";

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
} // namespace

juce::ValueTree PatternProjectSerialization::serialize(const PatternProject& project)
{
    juce::ValueTree root(kPatternProjectNode);
    root.setProperty("schema_version", kPatternSchemaVersion, nullptr);
    root.setProperty("selected_track_index", project.selectedTrackIndex, nullptr);
    root.setProperty("generation_counter", project.generationCounter, nullptr);
    root.setProperty("mutation_counter", project.mutationCounter, nullptr);
    root.setProperty("phrase_length_bars", project.phraseLengthBars, nullptr);
    root.setProperty("phrase_role_summary", project.phraseRoleSummary, nullptr);
    root.setProperty("preview_start_step", project.previewStartStep, nullptr);
    root.setProperty("bars", project.params.bars, nullptr);

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
    restored.tracks = TrackRegistry::createDefaultTrackStates();
    restored.selectedTrackIndex = safeInt(patternNode, "selected_track_index", 0);
    restored.generationCounter = safeInt(patternNode, "generation_counter", 0);
    restored.mutationCounter = safeInt(patternNode, "mutation_counter", 0);
    restored.phraseLengthBars = safeInt(patternNode, "phrase_length_bars", restored.params.bars);
    restored.phraseRoleSummary = patternNode.getProperty("phrase_role_summary", {}).toString();
    restored.previewStartStep = safeInt(patternNode, "preview_start_step", 0);

    std::unordered_map<TrackType, int> trackIndex;
    for (int i = 0; i < static_cast<int>(restored.tracks.size()); ++i)
        trackIndex[restored.tracks[static_cast<size_t>(i)].type] = i;

    for (int i = 0; i < patternNode.getNumChildren(); ++i)
    {
        const auto trackNode = patternNode.getChild(i);
        if (!trackNode.hasType(kTrackNode))
            continue;

        const auto typeInt = safeInt(trackNode, "type", static_cast<int>(TrackType::Kick));
        const auto type = static_cast<TrackType>(typeInt);

        auto it = trackIndex.find(type);
        if (it == trackIndex.end())
            continue; // Unknown tracks from future schemas are safely ignored.

        auto& track = restored.tracks[static_cast<size_t>(it->second)];
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
        track.notes.clear();

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

            track.notes.push_back(note);
        }
    }

    validate(restored);
    projectOut = std::move(restored);
    return true;
}

void PatternProjectSerialization::validate(PatternProject& project)
{
    if (project.tracks.empty())
        project.tracks = TrackRegistry::createDefaultTrackStates();

    // Canonicalize track order and ensure all required tracks exist.
    std::unordered_map<TrackType, TrackState> incoming;
    for (auto& track : project.tracks)
        incoming.emplace(track.type, std::move(track));

    std::vector<TrackState> canonical;
    canonical.reserve(TrackRegistry::all().size());

    for (const auto& info : TrackRegistry::all())
    {
        auto it = incoming.find(info.type);
        if (it != incoming.end())
        {
            canonical.push_back(std::move(it->second));
        }
        else
        {
            TrackState fallback;
            fallback.type = info.type;
            fallback.enabled = info.enabledByDefault;
            canonical.push_back(std::move(fallback));
        }
    }

    project.tracks = std::move(canonical);

    const int maxStep = std::max(16, project.params.bars * 16 * 4);

    project.generationCounter = std::max(0, project.generationCounter);
    project.mutationCounter = std::max(0, project.mutationCounter);
    project.phraseLengthBars = std::clamp(project.phraseLengthBars, 1, 16);
    project.previewStartStep = std::max(0, project.previewStartStep);

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
        }

        track.templateId = std::max(0, track.templateId);
        track.variationId = std::max(0, track.variationId);
        track.mutationDepth = std::clamp(track.mutationDepth, 0.0f, 1.0f);
        track.laneVolume = std::clamp(track.laneVolume, 0.0f, 1.5f);
        track.selectedSampleIndex = std::max(0, track.selectedSampleIndex);

        std::sort(track.notes.begin(), track.notes.end(), [](const NoteEvent& a, const NoteEvent& b)
        {
            if (a.step != b.step)
                return a.step < b.step;
            return a.pitch < b.pitch;
        });
    }

    project.selectedTrackIndex = std::clamp(project.selectedTrackIndex, 0, static_cast<int>(project.tracks.size()) - 1);
}

juce::ValueTree PatternProjectSerialization::serializeTrack(const TrackState& track)
{
    juce::ValueTree node(kTrackNode);
    node.setProperty("type", static_cast<int>(track.type), nullptr);
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

    for (const auto& note : track.notes)
        node.addChild(serializeNote(note), -1, nullptr);

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
    return node;
}
} // namespace bbg
