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
constexpr auto kBaseNoteNode = "BASE_NOTE";
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

juce::ValueTree serializeNoteNode(const char* nodeType, const NoteEvent& note)
{
    juce::ValueTree node(nodeType);
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

void sanitizeGeneratorParams(GeneratorParams& params)
{
    params.bpm = std::clamp(params.bpm, 40.0f, 240.0f);
    params.swingPercent = std::clamp(params.swingPercent, 50.0f, 75.0f);
    params.velocityAmount = std::clamp(params.velocityAmount, 0.0f, 1.0f);
    params.timingAmount = std::clamp(params.timingAmount, 0.0f, 1.0f);
    params.humanizeAmount = std::clamp(params.humanizeAmount, 0.0f, 1.0f);
    params.densityAmount = std::clamp(params.densityAmount, 0.0f, 1.0f);
    params.bars = juce::jlimit(1, 16, params.bars);
    params.tempoInterpretationMode = std::max(0, params.tempoInterpretationMode);
    params.boombapSubstyle = std::max(0, params.boombapSubstyle);
    params.rapSubstyle = std::max(0, params.rapSubstyle);
    params.trapSubstyle = std::max(0, params.trapSubstyle);
    params.drillSubstyle = std::max(0, params.drillSubstyle);
    params.genre = static_cast<GenreType>(juce::jlimit(0, 3, static_cast<int>(params.genre)));
}

void serializePerformanceBaseParams(juce::ValueTree& node, const TrackState& track)
{
    if (!track.hasPerformanceBaseParams)
        return;

    const auto& params = track.performanceBaseParams;
    node.setProperty("performance_base_params_present", true, nullptr);
    node.setProperty("performance_base_bpm", params.bpm, nullptr);
    node.setProperty("performance_base_swing_percent", params.swingPercent, nullptr);
    node.setProperty("performance_base_velocity_amount", params.velocityAmount, nullptr);
    node.setProperty("performance_base_timing_amount", params.timingAmount, nullptr);
    node.setProperty("performance_base_humanize_amount", params.humanizeAmount, nullptr);
    node.setProperty("performance_base_density_amount", params.densityAmount, nullptr);
    node.setProperty("performance_base_bars", params.bars, nullptr);
    node.setProperty("performance_base_tempo_interpretation", params.tempoInterpretationMode, nullptr);
    node.setProperty("performance_base_genre", static_cast<int>(params.genre), nullptr);
    node.setProperty("performance_base_boombap_substyle", params.boombapSubstyle, nullptr);
    node.setProperty("performance_base_rap_substyle", params.rapSubstyle, nullptr);
    node.setProperty("performance_base_trap_substyle", params.trapSubstyle, nullptr);
    node.setProperty("performance_base_drill_substyle", params.drillSubstyle, nullptr);
}

void deserializePerformanceBaseParams(const juce::ValueTree& node, TrackState& track)
{
    track.hasPerformanceBaseParams = safeBool(node, "performance_base_params_present", false);
    if (!track.hasPerformanceBaseParams)
        return;

    auto& params = track.performanceBaseParams;
    params.bpm = safeFloat(node, "performance_base_bpm", params.bpm);
    params.swingPercent = safeFloat(node, "performance_base_swing_percent", params.swingPercent);
    params.velocityAmount = safeFloat(node, "performance_base_velocity_amount", params.velocityAmount);
    params.timingAmount = safeFloat(node, "performance_base_timing_amount", params.timingAmount);
    params.humanizeAmount = safeFloat(node, "performance_base_humanize_amount", params.humanizeAmount);
    params.densityAmount = safeFloat(node, "performance_base_density_amount", params.densityAmount);
    params.bars = safeInt(node, "performance_base_bars", params.bars);
    params.tempoInterpretationMode = safeInt(node, "performance_base_tempo_interpretation", params.tempoInterpretationMode);
    params.genre = static_cast<GenreType>(safeInt(node, "performance_base_genre", static_cast<int>(params.genre)));
    params.boombapSubstyle = safeInt(node, "performance_base_boombap_substyle", params.boombapSubstyle);
    params.rapSubstyle = safeInt(node, "performance_base_rap_substyle", params.rapSubstyle);
    params.trapSubstyle = safeInt(node, "performance_base_trap_substyle", params.trapSubstyle);
    params.drillSubstyle = safeInt(node, "performance_base_drill_substyle", params.drillSubstyle);
    sanitizeGeneratorParams(params);
}

void sanitizeLegacyNotes(std::vector<NoteEvent>& notes, TrackType trackType, int maxStep)
{
    const auto* info = TrackRegistry::find(trackType);

    for (auto& note : notes)
    {
        note.pitch = std::clamp(note.pitch, 0, 127);
        if (note.pitch == 0 && info != nullptr)
            note.pitch = info->defaultMidiNote;

        note.step = std::clamp(note.step, 0, maxStep);
        note.length = std::clamp(note.length, 1, 64);
        note.velocity = std::clamp(note.velocity, 1, 127);
        note.microOffset = std::clamp(note.microOffset, -960, 960);
        note.semanticRole = note.semanticRole.trim();
        if (trackType != TrackType::Sub808)
        {
            note.isSlide = false;
            note.isLegato = false;
            note.glideToNext = false;
        }
    }

    std::sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b)
    {
        if (a.step != b.step)
            return a.step < b.step;
        return a.pitch < b.pitch;
    });
}

void sanitizeSub808Notes(std::vector<Sub808NoteEvent>& notes, int maxStep)
{
    for (auto& note : notes)
    {
        note.pitch = std::clamp(note.pitch, 0, 127);
        note.step = std::clamp(note.step, 0, maxStep);
        note.length = std::clamp(note.length, 1, 64);
        note.velocity = std::clamp(note.velocity, 1, 127);
        note.microOffset = std::clamp(note.microOffset, -960, 960);
        note.semanticRole = note.semanticRole.trim();
    }

    std::sort(notes.begin(), notes.end(), [](const Sub808NoteEvent& a, const Sub808NoteEvent& b)
    {
        if (a.step != b.step)
            return a.step < b.step;
        return a.pitch < b.pitch;
    });
}

void serializeSoundLayer(juce::ValueTree& node, const SoundLayerState& sound, const juce::String& prefix)
{
    auto serializedSound = sound;
    reconcileLegacySoundLayerState(serializedSound);

    node.setProperty(prefix + "_pan", serializedSound.pan, nullptr);
    node.setProperty(prefix + "_width", serializedSound.width, nullptr);
    node.setProperty(prefix + "_stereo_field_enabled", serializedSound.stereoFieldEnabled, nullptr);
    node.setProperty(prefix + "_stereo_field_order", serializedSound.stereoFieldOrder, nullptr);
    node.setProperty(prefix + "_stereo_field_focus", serializedSound.stereoFieldFocus, nullptr);
    node.setProperty(prefix + "_stereo_field_edge", serializedSound.stereoFieldEdge, nullptr);
    node.setProperty(prefix + "_stereo_field_mono_safe", serializedSound.stereoFieldMonoSafe, nullptr);
    node.setProperty(prefix + "_stereo_field_low_center_protect", serializedSound.stereoFieldLowCenterProtect, nullptr);
    node.setProperty(prefix + "_stereo_field_air_spread", serializedSound.stereoFieldAirSpread, nullptr);
    node.setProperty(prefix + "_eq_tone", serializedSound.eqTone, nullptr);
    node.setProperty(prefix + "_eq_selected_band", serializedSound.eq.selectedBand, nullptr);
    for (int bandIndex = 0; bandIndex < kEqBandCount; ++bandIndex)
    {
        const auto& band = serializedSound.eq.bands[static_cast<size_t>(bandIndex)];
        const auto bandPrefix = prefix + "_eq_band_" + juce::String(bandIndex);
        node.setProperty(bandPrefix + "_enabled", band.enabled, nullptr);
        node.setProperty(bandPrefix + "_freq_hz", band.freqHz, nullptr);
        node.setProperty(bandPrefix + "_gain_db", band.gainDb, nullptr);
        node.setProperty(bandPrefix + "_q", band.q, nullptr);
        node.setProperty(bandPrefix + "_shape", static_cast<int>(band.shape), nullptr);
    }

    node.setProperty(prefix + "_compressor_enabled", serializedSound.compressor.enabled, nullptr);
    node.setProperty(prefix + "_compressor_order", serializedSound.compressor.order, nullptr);
    node.setProperty(prefix + "_compressor_ratio", serializedSound.compressor.ratio, nullptr);
    node.setProperty(prefix + "_compressor_threshold_db", serializedSound.compressor.thresholdDb, nullptr);
    node.setProperty(prefix + "_compressor_mix", serializedSound.compressor.mix, nullptr);
    node.setProperty(prefix + "_compressor_attack_ms", serializedSound.compressor.attackMs, nullptr);
    node.setProperty(prefix + "_compressor_release_ms", serializedSound.compressor.releaseMs, nullptr);
    node.setProperty(prefix + "_compressor_saturation", serializedSound.compressor.saturation, nullptr);
    node.setProperty(prefix + "_compressor_input_trim_db", serializedSound.compressor.inputTrimDb, nullptr);
    node.setProperty(prefix + "_compressor_output_trim_db", serializedSound.compressor.outputTrimDb, nullptr);
    node.setProperty(prefix + "_compressor_auto_makeup", serializedSound.compressor.autoMakeup, nullptr);
    node.setProperty(prefix + "_compressor_character", static_cast<int>(serializedSound.compressor.character), nullptr);
    node.setProperty(prefix + "_compressor_saturation_mode", static_cast<int>(serializedSound.compressor.saturationMode), nullptr);

    node.setProperty(prefix + "_drum_reverb_enabled", serializedSound.drumReverb.enabled, nullptr);
    node.setProperty(prefix + "_drum_reverb_mix", serializedSound.drumReverb.mix, nullptr);
    node.setProperty(prefix + "_drum_reverb_predelay_ms", serializedSound.drumReverb.predelayMs, nullptr);
    node.setProperty(prefix + "_drum_reverb_size", serializedSound.drumReverb.size, nullptr);
    node.setProperty(prefix + "_drum_reverb_er_tail", serializedSound.drumReverb.erTail, nullptr);

    node.setProperty(prefix + "_drum_transient_attack", serializedSound.drumTransient.attack, nullptr);
    node.setProperty(prefix + "_drum_transient_sustain", serializedSound.drumTransient.sustain, nullptr);
    node.setProperty(prefix + "_drum_transient_gain_db", serializedSound.drumTransient.gainDb, nullptr);
    node.setProperty(prefix + "_drum_transient_smooth", serializedSound.drumTransient.smooth, nullptr);
    node.setProperty(prefix + "_drum_transient_limit", serializedSound.drumTransient.limit, nullptr);

    node.setProperty(prefix + "_monsta_fx_enabled", serializedSound.monstaFx.enabled, nullptr);
    node.setProperty(prefix + "_monsta_fx_order", serializedSound.monstaFx.order, nullptr);
    node.setProperty(prefix + "_monsta_fx_dry", serializedSound.monstaFx.dry, nullptr);
    node.setProperty(prefix + "_monsta_fx_wet", serializedSound.monstaFx.wet, nullptr);
    node.setProperty(prefix + "_monsta_fx_chaos_seed", static_cast<int64_t>(serializedSound.monstaFx.chaosSeed), nullptr);

    node.setProperty(prefix + "_compression", serializedSound.compression, nullptr);
    node.setProperty(prefix + "_reverb", serializedSound.reverb, nullptr);
    node.setProperty(prefix + "_gate", serializedSound.gate, nullptr);
    node.setProperty(prefix + "_transient", serializedSound.transient, nullptr);
    node.setProperty(prefix + "_drive", serializedSound.drive, nullptr);
}

void deserializeSoundLayer(const juce::ValueTree& node, SoundLayerState& sound, const juce::String& prefix)
{
    sound.pan = safeFloat(node, prefix + "_pan", sound.pan);
    sound.width = safeFloat(node, prefix + "_width", sound.width);
    sound.stereoFieldEnabled = safeBool(node, prefix + "_stereo_field_enabled", sound.stereoFieldEnabled);
    sound.stereoFieldOrder = safeInt(node, prefix + "_stereo_field_order", sound.stereoFieldOrder);
    sound.stereoFieldFocus = safeFloat(node, prefix + "_stereo_field_focus", sound.stereoFieldFocus);
    sound.stereoFieldEdge = safeFloat(node, prefix + "_stereo_field_edge", sound.stereoFieldEdge);
    sound.stereoFieldMonoSafe = safeBool(node, prefix + "_stereo_field_mono_safe", sound.stereoFieldMonoSafe);
    sound.stereoFieldLowCenterProtect = safeFloat(node,
                                                  prefix + "_stereo_field_low_center_protect",
                                                  sound.stereoFieldLowCenterProtect);
    sound.stereoFieldAirSpread = safeFloat(node, prefix + "_stereo_field_air_spread", sound.stereoFieldAirSpread);
    sound.eqTone = safeFloat(node, prefix + "_eq_tone", sound.eqTone);
    const bool hasEqState = !node.getProperty(prefix + "_eq_selected_band").isVoid()
        || !node.getProperty(prefix + "_eq_band_0_freq_hz").isVoid();
    sound.eq = createDefaultEqState();
    if (hasEqState)
    {
        sound.eq.selectedBand = clampEqBandIndex(safeInt(node, prefix + "_eq_selected_band", sound.eq.selectedBand));
        for (int bandIndex = 0; bandIndex < kEqBandCount; ++bandIndex)
        {
            auto& band = sound.eq.bands[static_cast<size_t>(bandIndex)];
            const auto bandPrefix = prefix + "_eq_band_" + juce::String(bandIndex);
            band.enabled = safeBool(node, bandPrefix + "_enabled", band.enabled);
            band.freqHz = safeFloat(node, bandPrefix + "_freq_hz", band.freqHz);
            band.gainDb = safeFloat(node, bandPrefix + "_gain_db", band.gainDb);
            band.q = safeFloat(node, bandPrefix + "_q", band.q);
            band.shape = static_cast<EqBandShape>(juce::jlimit(0,
                                                               2,
                                                               safeInt(node, bandPrefix + "_shape", static_cast<int>(band.shape))));
        }
        sound.eqTone = legacyEqToneFromEqState(sound.eq);
    }
    else
    {
        applyLegacyEqToneToEqState(sound.eq, sound.eqTone);
    }

    const bool hasCompressorState = !node.getProperty(prefix + "_compressor_enabled").isVoid()
        || !node.getProperty(prefix + "_compressor_ratio").isVoid()
        || !node.getProperty(prefix + "_compressor_threshold_db").isVoid()
        || !node.getProperty(prefix + "_compressor_mix").isVoid();

    const float legacyCompression = safeFloat(node, prefix + "_compression", sound.compression);
    if (hasCompressorState)
    {
        sound.compressor = createDefaultCompressorState();
        sound.compressor.enabled = safeBool(node, prefix + "_compressor_enabled", sound.compressor.enabled);
        sound.compressor.order = safeInt(node, prefix + "_compressor_order", sound.compressor.order);
        sound.compressor.ratio = safeFloat(node, prefix + "_compressor_ratio", sound.compressor.ratio);
        sound.compressor.thresholdDb = safeFloat(node, prefix + "_compressor_threshold_db", sound.compressor.thresholdDb);
        sound.compressor.mix = safeFloat(node, prefix + "_compressor_mix", sound.compressor.mix);
        sound.compressor.attackMs = safeFloat(node, prefix + "_compressor_attack_ms", sound.compressor.attackMs);
        sound.compressor.releaseMs = safeFloat(node, prefix + "_compressor_release_ms", sound.compressor.releaseMs);
        sound.compressor.saturation = safeFloat(node, prefix + "_compressor_saturation", sound.compressor.saturation);
        sound.compressor.inputTrimDb = safeFloat(node, prefix + "_compressor_input_trim_db", sound.compressor.inputTrimDb);
        sound.compressor.outputTrimDb = safeFloat(node, prefix + "_compressor_output_trim_db", sound.compressor.outputTrimDb);
        sound.compressor.autoMakeup = safeBool(node, prefix + "_compressor_auto_makeup", sound.compressor.autoMakeup);
        sound.compressor.character = clampDrumCompressorCharacter(safeInt(node,
                                                                         prefix + "_compressor_character",
                                                                         static_cast<int>(sound.compressor.character)));
        sound.compressor.saturationMode = clampDrumSaturationMode(safeInt(node,
                                                                          prefix + "_compressor_saturation_mode",
                                                                          static_cast<int>(sound.compressor.saturationMode)));
    }
    else
    {
        applyLegacyCompressionToCompressorState(sound.compressor, legacyCompression);
    }

    const bool hasDrumReverbState = !node.getProperty(prefix + "_drum_reverb_enabled").isVoid()
        || !node.getProperty(prefix + "_drum_reverb_mix").isVoid()
        || !node.getProperty(prefix + "_drum_reverb_predelay_ms").isVoid()
        || !node.getProperty(prefix + "_drum_reverb_size").isVoid()
        || !node.getProperty(prefix + "_drum_reverb_er_tail").isVoid();

    const float legacyReverb = safeFloat(node, prefix + "_reverb", sound.reverb);
    if (hasDrumReverbState)
    {
        sound.drumReverb = createDefaultDrumReverbState();
        sound.drumReverb.enabled = safeBool(node, prefix + "_drum_reverb_enabled", sound.drumReverb.enabled);
        sound.drumReverb.mix = safeFloat(node, prefix + "_drum_reverb_mix", sound.drumReverb.mix);
        sound.drumReverb.predelayMs = safeFloat(node, prefix + "_drum_reverb_predelay_ms", sound.drumReverb.predelayMs);
        sound.drumReverb.size = safeFloat(node, prefix + "_drum_reverb_size", sound.drumReverb.size);
        sound.drumReverb.erTail = safeFloat(node, prefix + "_drum_reverb_er_tail", sound.drumReverb.erTail);
        sanitizeDrumReverbState(sound.drumReverb);
        sound.reverb = legacyReverbFromDrumReverbState(sound.drumReverb);
    }
    else
    {
        sound.reverb = legacyReverb;
        applyLegacyReverbToDrumReverbState(sound.drumReverb, legacyReverb);
    }

    const bool hasDrumTransientState = !node.getProperty(prefix + "_drum_transient_attack").isVoid()
        || !node.getProperty(prefix + "_drum_transient_sustain").isVoid()
        || !node.getProperty(prefix + "_drum_transient_gain_db").isVoid();

    const float legacyTransient = safeFloat(node, prefix + "_transient", sound.transient);
    const float legacyDrive = safeFloat(node, prefix + "_drive", sound.drive);
    if (hasDrumTransientState)
    {
        sound.drumTransient = createDefaultDrumTransientState();
        sound.drumTransient.attack = safeFloat(node, prefix + "_drum_transient_attack", sound.drumTransient.attack);
        sound.drumTransient.sustain = safeFloat(node, prefix + "_drum_transient_sustain", sound.drumTransient.sustain);
        sound.drumTransient.gainDb = safeFloat(node, prefix + "_drum_transient_gain_db", sound.drumTransient.gainDb);
        sound.drumTransient.smooth = safeBool(node, prefix + "_drum_transient_smooth", sound.drumTransient.smooth);
        sound.drumTransient.limit = safeBool(node, prefix + "_drum_transient_limit", sound.drumTransient.limit);
        sanitizeDrumTransientState(sound.drumTransient);
        sound.transient = legacyTransientFromDrumTransientState(sound.drumTransient);
        sound.drive = legacyDriveFromDrumTransientState(sound.drumTransient);
    }
    else
    {
        sound.transient = legacyTransient;
        sound.drive = legacyDrive;
        applyLegacyTransientToDrumTransientState(sound.drumTransient, legacyTransient, legacyDrive);
    }

    const bool hasMonstaFxState = !node.getProperty(prefix + "_monsta_fx_enabled").isVoid()
        || !node.getProperty(prefix + "_monsta_fx_dry").isVoid()
        || !node.getProperty(prefix + "_monsta_fx_wet").isVoid()
        || !node.getProperty(prefix + "_monsta_fx_chaos_seed").isVoid();

    sound.monstaFx = createDefaultMonstaFxState();
    if (hasMonstaFxState)
    {
        sound.monstaFx.enabled = safeBool(node, prefix + "_monsta_fx_enabled", sound.monstaFx.enabled);
        sound.monstaFx.order = safeInt(node, prefix + "_monsta_fx_order", sound.monstaFx.order);
        sound.monstaFx.dry = safeFloat(node, prefix + "_monsta_fx_dry", sound.monstaFx.dry);
        sound.monstaFx.wet = safeFloat(node, prefix + "_monsta_fx_wet", sound.monstaFx.wet);
        sound.monstaFx.chaosSeed = static_cast<std::uint32_t>(safeInt(node,
                                          prefix + "_monsta_fx_chaos_seed",
                                          static_cast<int>(sound.monstaFx.chaosSeed)));
    }

    sound.monstaFx.pendingChaosReseed = false;
    sanitizeMonstaFxState(sound.monstaFx);

    sound.compression = legacyCompression;
    sound.gate = safeFloat(node, prefix + "_gate", sound.gate);
    reconcileLegacySoundLayerState(sound);
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
    sound.stereoFieldOrder = std::clamp(sound.stereoFieldOrder, 0, 3);
    sound.stereoFieldFocus = std::clamp(sound.stereoFieldFocus, 0.0f, 1.0f);
    sound.stereoFieldEdge = std::clamp(sound.stereoFieldEdge, 0.0f, 1.0f);
    sound.stereoFieldLowCenterProtect = std::clamp(sound.stereoFieldLowCenterProtect, 0.0f, 1.0f);
    sound.stereoFieldAirSpread = std::clamp(sound.stereoFieldAirSpread, 0.0f, 1.0f);
    sound.eq.selectedBand = clampEqBandIndex(sound.eq.selectedBand);
    for (auto& band : sound.eq.bands)
    {
        band.freqHz = std::clamp(band.freqHz, 20.0f, 20000.0f);
        band.gainDb = std::clamp(band.gainDb, -24.0f, 24.0f);
        band.q = std::clamp(band.q, 0.1f, 10.0f);
        band.shape = static_cast<EqBandShape>(std::clamp(static_cast<int>(band.shape), 0, 2));
    }
    sound.compression = std::clamp(sound.compression, 0.0f, 1.0f);
    sanitizeDrumReverbState(sound.drumReverb);
    sanitizeDrumTransientState(sound.drumTransient);
    sound.reverb = std::clamp(sound.reverb, 0.0f, 1.0f);
    sound.gate = std::clamp(sound.gate, 0.0f, 1.0f);
    sound.transient = std::clamp(sound.transient, 0.0f, 1.0f);
    sound.drive = std::clamp(sound.drive, 0.0f, 1.0f);
    reconcileLegacySoundLayerState(sound);
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

        if (track.hasPerformanceBaseParams)
            sanitizeGeneratorParams(track.performanceBaseParams);
    }
}

void sanitizeTrackNotes(PatternProject& project)
{
    const int maxStep = 16 * 16 - 1;

    for (auto& track : project.tracks)
    {
        sanitizeLegacyNotes(track.notes, track.type, maxStep);

        if (track.type == TrackType::Sub808)
        {
            if (track.sub808Notes.empty() && !track.notes.empty())
                track.sub808Notes = toSub808NoteEvents(track.notes);

            sanitizeSub808Notes(track.sub808Notes, maxStep);

            if (track.baseSub808Notes.empty())
            {
                if (!track.baseNotes.empty())
                    track.baseSub808Notes = toSub808NoteEvents(track.baseNotes);
                else if (!track.sub808Notes.empty())
                    track.baseSub808Notes = track.sub808Notes;
            }

            sanitizeSub808Notes(track.baseSub808Notes, maxStep);

            track.notes = toLegacyNoteEvents(track.sub808Notes);
            track.baseNotes = toLegacyNoteEvents(track.baseSub808Notes);
        }
        else
        {
            track.sub808Notes.clear();
            sanitizeLegacyNotes(track.baseNotes, track.type, maxStep);
            track.baseSub808Notes.clear();

            if (track.baseNotes.empty() && !track.notes.empty())
                track.baseNotes = track.notes;
        }
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
        deserializePerformanceBaseParams(trackNode, track);
        track.notes.clear();
        track.baseNotes.clear();
        track.sub808Notes.clear();
        track.baseSub808Notes.clear();
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
            const bool isVisibleNote = noteNode.hasType(kNoteNode);
            const bool isBaseNote = noteNode.hasType(kBaseNoteNode);
            if (!isVisibleNote && !isBaseNote)
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
            {
                if (isBaseNote)
                    track.baseSub808Notes.push_back(toSub808NoteEvent(note));
                else
                    track.sub808Notes.push_back(toSub808NoteEvent(note));
            }
            else
            {
                if (isBaseNote)
                    track.baseNotes.push_back(note);
                else
                    track.notes.push_back(note);
            }
        }

        if (track.type == TrackType::Sub808)
        {
            track.notes = toLegacyNoteEvents(track.sub808Notes);
            track.baseNotes = toLegacyNoteEvents(track.baseSub808Notes);
        }
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
    serializePerformanceBaseParams(node, track);

    if (track.type == TrackType::Sub808)
    {
        const auto serializedSub808Notes = track.sub808Notes.empty() ? toSub808NoteEvents(track.notes) : track.sub808Notes;
        const auto serializedBaseSub808Notes = track.baseSub808Notes.empty()
            ? (track.baseNotes.empty() ? serializedSub808Notes : toSub808NoteEvents(track.baseNotes))
            : track.baseSub808Notes;
        for (const auto& note : serializedSub808Notes)
            node.addChild(serializeNote(toLegacyNoteEvent(note)), -1, nullptr);
        for (const auto& note : serializedBaseSub808Notes)
            node.addChild(serializeNoteNode(kBaseNoteNode, toLegacyNoteEvent(note)), -1, nullptr);
    }
    else
    {
        for (const auto& note : track.notes)
            node.addChild(serializeNote(note), -1, nullptr);

        const auto& serializedBaseNotes = track.baseNotes.empty() ? track.notes : track.baseNotes;
        for (const auto& note : serializedBaseNotes)
            node.addChild(serializeNoteNode(kBaseNoteNode, note), -1, nullptr);
    }

    return node;
}

juce::ValueTree PatternProjectSerialization::serializeNote(const NoteEvent& note)
{
    return serializeNoteNode(kNoteNode, note);
}
} // namespace bbg
