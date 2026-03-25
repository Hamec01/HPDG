#include "StyleInfluence.h"

#include "../Core/PatternProjectSerialization.h"

namespace bbg
{
namespace
{
float clampUnit(float value)
{
    return juce::jlimit(0.0f, 1.0f, value);
}

float clampWeight(float value)
{
    return juce::jlimit(0.0f, 2.0f, value);
}

float asFloat(const juce::var& value, float fallbackValue)
{
    if (value.isInt() || value.isInt64() || value.isDouble() || value.isBool())
        return static_cast<float>(static_cast<double>(value));
    return fallbackValue;
}

float hintValue(const juce::NamedValueSet& hints, const juce::Identifier& key, float fallbackValue)
{
    if (const auto* value = hints.getVarPointer(key))
        return asFloat(*value, fallbackValue);
    return fallbackValue;
}

float laneHintValue(const ResolvedStyleDefinition& definition,
                    TrackType type,
                    const juce::Identifier& key,
                    float fallbackValue)
{
    for (const auto& lane : definition.lanes)
    {
        if (lane.runtimeTrackType.has_value() && *lane.runtimeTrackType == type)
            return hintValue(lane.skeletonHints, key, fallbackValue);
    }

    return fallbackValue;
}

void blendParam(float& target, float desired, float strength)
{
    const auto mix = clampUnit(strength);
    target = juce::jlimit(0.0f, 1.0f, target + (desired - target) * mix);
}

void blendWeight(float& target, float desired, float strength)
{
    const auto mix = clampUnit(strength);
    target = clampWeight(target + (clampWeight(desired) - target) * mix);
}

void blendSwing(float& swingPercent, float desiredNormalized, float strength)
{
    const auto mix = clampUnit(strength);
    const auto desiredSwing = juce::jlimit(50.0f, 66.0f, 50.0f + clampUnit(desiredNormalized) * 14.0f);
    swingPercent = juce::jlimit(50.0f, 75.0f, swingPercent + (desiredSwing - swingPercent) * mix);
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

float normalizedSwing(float swingPercent)
{
    return clampUnit((swingPercent - 50.0f) / 14.0f);
}

void resetMusicalBiasState(PatternProject& project)
{
    project.styleInfluence = {};
}

void applyBoomBapMusicalHints(const ResolvedStyleDefinition& definition, PatternProject& project)
{
    auto& params = project.params;
    auto& styleInfluence = project.styleInfluence;
    const auto sharedSwing = hintValue(definition.styleHints, "groove.swing", normalizedSwing(params.swingPercent));
    const auto sharedTiming = hintValue(definition.styleHints, "groove.timing", params.timingAmount);
    const auto sharedHumanize = hintValue(definition.styleHints, "groove.humanize", params.humanizeAmount);
    const auto sharedDensity = hintValue(definition.styleHints, "groove.density", params.densityAmount);

    const auto looseness = hintValue(definition.styleHints, "boom_bap.groove_looseness", sharedHumanize);
    const auto percSparsity = hintValue(definition.styleHints, "boom_bap.perc_sparsity", 0.4f);
    const auto clapFocus = hintValue(definition.styleHints, "boom_bap.clap_focus", 0.7f);
    const auto kickBias = laneHintValue(definition, TrackType::Kick, "lane.densityBias", 1.0f);
    const auto hatBias = laneHintValue(definition, TrackType::HiHat, "lane.densityBias", 1.0f);

    blendSwing(params.swingPercent, juce::jmax(sharedSwing, hintValue(definition.styleHints, "boom_bap.swing_feel", sharedSwing)), 0.8f);
    blendParam(params.timingAmount, clampUnit(sharedTiming * 0.45f + looseness * 0.55f), 0.75f);
    blendParam(params.humanizeAmount, clampUnit(sharedHumanize * 0.35f + looseness * 0.65f), 0.85f);
    blendParam(params.densityAmount,
               clampUnit(sharedDensity * 0.7f + kickBias * 0.08f + hatBias * 0.06f - percSparsity * 0.18f),
               0.7f);

    blendWeight(laneBiasFor(styleInfluence, TrackType::Kick).activityWeight, 0.96f + kickBias * 0.14f, 0.55f);
    blendWeight(laneBiasFor(styleInfluence, TrackType::HiHat).activityWeight, 0.92f + hatBias * 0.10f, 0.45f);
    blendWeight(laneBiasFor(styleInfluence, TrackType::ClapGhostSnare).balanceWeight, 1.0f + clapFocus * 0.24f, 0.8f);
    blendWeight(laneBiasFor(styleInfluence, TrackType::Perc).activityWeight, 0.56f + (1.0f - percSparsity) * 0.18f, 0.75f);
    blendWeight(laneBiasFor(styleInfluence, TrackType::OpenHat).activityWeight, 0.66f + (1.0f - percSparsity) * 0.16f, 0.7f);
    blendWeight(styleInfluence.supportAccentWeight, 1.0f + clapFocus * 0.18f - percSparsity * 0.08f, 0.7f);
}

void applyRapMusicalHints(const ResolvedStyleDefinition& definition, PatternProject& project)
{
    auto& params = project.params;
    auto& styleInfluence = project.styleInfluence;
    const auto sharedSwing = hintValue(definition.styleHints, "groove.swing", normalizedSwing(params.swingPercent));
    const auto sharedTiming = hintValue(definition.styleHints, "groove.timing", params.timingAmount);
    const auto sharedHumanize = hintValue(definition.styleHints, "groove.humanize", params.humanizeAmount);
    const auto sharedDensity = hintValue(definition.styleHints, "groove.density", params.densityAmount);

    const auto looseness = hintValue(definition.styleHints, "rap.groove_looseness", 0.2f);
    const auto supportDensity = hintValue(definition.styleHints, "rap.support_density", 0.35f);
    const auto accentPush = hintValue(definition.styleHints, "rap.accent_push", 0.55f);

    blendSwing(params.swingPercent, sharedSwing, 0.4f);
    blendParam(params.timingAmount, clampUnit(sharedTiming * 0.65f + looseness * 0.20f), 0.45f);
    blendParam(params.humanizeAmount, clampUnit(sharedHumanize * 0.6f + looseness * 0.25f), 0.5f);
    blendParam(params.densityAmount, clampUnit(sharedDensity * 0.72f + supportDensity * 0.18f), 0.55f);

    blendWeight(laneBiasFor(styleInfluence, TrackType::ClapGhostSnare).balanceWeight, 1.0f + accentPush * 0.14f, 0.65f);
    blendWeight(laneBiasFor(styleInfluence, TrackType::OpenHat).activityWeight, 0.88f + supportDensity * 0.12f, 0.5f);
    blendWeight(laneBiasFor(styleInfluence, TrackType::Perc).activityWeight, 0.82f + supportDensity * 0.12f, 0.55f);
    blendWeight(laneBiasFor(styleInfluence, TrackType::Ride).activityWeight, 0.78f + supportDensity * 0.14f, 0.6f);
    blendWeight(styleInfluence.supportAccentWeight, 0.94f + accentPush * 0.10f + supportDensity * 0.08f, 0.6f);
}

void applyTrapMusicalHints(const ResolvedStyleDefinition& definition, PatternProject& project)
{
    auto& params = project.params;
    auto& styleInfluence = project.styleInfluence;
    const auto sharedSwing = hintValue(definition.styleHints, "groove.swing", normalizedSwing(params.swingPercent));
    const auto sharedTiming = hintValue(definition.styleHints, "groove.timing", params.timingAmount);
    const auto sharedHumanize = hintValue(definition.styleHints, "groove.humanize", params.humanizeAmount);
    const auto sharedDensity = hintValue(definition.styleHints, "groove.density", params.densityAmount);

    const auto hatSubdivision = hintValue(definition.styleHints, "trap.hat_subdivision", 0.55f);
    const auto bounce = hintValue(definition.styleHints, "trap.bounce", 0.5f);
    const auto emphasis808 = hintValue(definition.styleHints, "trap.emphasis_808", 0.75f);
    const auto openHatProfile = hintValue(definition.styleHints, "trap.open_hat_profile", 0.35f);

    blendSwing(params.swingPercent, clampUnit(sharedSwing * 0.6f + bounce * 0.2f), 0.3f);
    blendParam(params.timingAmount, clampUnit(sharedTiming * 0.7f + bounce * 0.18f), 0.45f);
    blendParam(params.humanizeAmount, clampUnit(sharedHumanize * 0.55f + hatSubdivision * 0.10f), 0.35f);
    blendParam(params.densityAmount, clampUnit(sharedDensity * 0.58f + hatSubdivision * 0.18f + emphasis808 * 0.16f), 0.75f);

    blendWeight(laneBiasFor(styleInfluence, TrackType::HiHat).activityWeight, 1.0f + hatSubdivision * 0.22f, 0.8f);
    blendWeight(laneBiasFor(styleInfluence, TrackType::HatFX).activityWeight, 0.96f + bounce * 0.24f, 0.75f);
    blendWeight(laneBiasFor(styleInfluence, TrackType::Sub808).balanceWeight, 1.0f + emphasis808 * 0.30f, 0.85f);
    blendWeight(laneBiasFor(styleInfluence, TrackType::OpenHat).activityWeight, 0.92f + openHatProfile * 0.24f, 0.75f);
    blendWeight(styleInfluence.bounceWeight, 1.0f + bounce * 0.22f, 0.8f);
    blendWeight(styleInfluence.lowEndCouplingWeight, 1.0f + emphasis808 * 0.18f, 0.7f);
}

void applyDrillMusicalHints(const ResolvedStyleDefinition& definition, PatternProject& project)
{
    auto& params = project.params;
    auto& styleInfluence = project.styleInfluence;
    styleInfluence.referenceHatSkeleton = definition.referenceHatSkeleton.value_or(ReferenceHatSkeleton {});
    styleInfluence.referenceHatCorpus = definition.referenceHatCorpus.value_or(ReferenceHatCorpus {});
    styleInfluence.referenceKickCorpus = definition.referenceKickCorpus.value_or(ReferenceKickCorpus {});
    const auto sharedSwing = hintValue(definition.styleHints, "groove.swing", normalizedSwing(params.swingPercent));
    const auto sharedTiming = hintValue(definition.styleHints, "groove.timing", params.timingAmount);
    const auto sharedHumanize = hintValue(definition.styleHints, "groove.humanize", params.humanizeAmount);
    const auto sharedDensity = hintValue(definition.styleHints, "groove.density", params.densityAmount);

    const auto anchorRigidity = hintValue(definition.styleHints, "drill.anchor_rigidity", 0.7f);
    const auto hatMotion = hintValue(definition.styleHints, "drill.hat_motion", 0.5f);
    const auto kick808Coupling = hintValue(definition.styleHints, "drill.kick_808_coupling", 0.65f);
    const auto gapIntent = hintValue(definition.styleHints, "drill.gap_intent", 0.3f);
    const auto refRollLength = hintValue(definition.styleHints, "drill.ref_hat_roll_length", hatMotion * 0.52f);
    const auto refDensityVariation = hintValue(definition.styleHints, "drill.ref_hat_density_variation", hatMotion * 0.44f);
    const auto refAccentAlternation = hintValue(definition.styleHints, "drill.ref_hat_accent_alternation", hatMotion * 0.40f);
    const auto refGapIntent = hintValue(definition.styleHints, "drill.ref_hat_gap_intent", gapIntent);
    const auto refBurst = hintValue(definition.styleHints, "drill.ref_hat_burst", hatMotion * 0.48f);
    const auto refTriplet = hintValue(definition.styleHints, "drill.ref_hat_triplet", hatMotion * 0.42f);
    const auto styleAwareMotion = clampUnit(hatMotion * 0.48f
                                            + refRollLength * 0.14f
                                            + refDensityVariation * 0.10f
                                            + refAccentAlternation * 0.08f
                                            + refBurst * 0.12f
                                            + refTriplet * 0.08f);

    blendSwing(params.swingPercent, clampUnit(sharedSwing * 0.4f), 0.25f);
    blendParam(params.timingAmount, clampUnit(sharedTiming * 0.45f + (1.0f - anchorRigidity) * 0.18f), 0.55f);
    blendParam(params.humanizeAmount, clampUnit(sharedHumanize * 0.35f + styleAwareMotion * 0.08f + (1.0f - anchorRigidity) * 0.10f), 0.5f);
    blendParam(params.densityAmount,
               clampUnit(sharedDensity * 0.60f
                         + styleAwareMotion * 0.08f
                         + refDensityVariation * 0.08f
                         + refBurst * 0.08f
                         + kick808Coupling * 0.10f
                         - refGapIntent * 0.10f),
               0.7f);

    blendWeight(laneBiasFor(styleInfluence, TrackType::HiHat).activityWeight, 0.94f + styleAwareMotion * 0.18f + refDensityVariation * 0.06f, 0.7f);
    blendWeight(laneBiasFor(styleInfluence, TrackType::HatFX).activityWeight, 0.96f + styleAwareMotion * 0.16f + refBurst * 0.10f, 0.72f);
    blendWeight(laneBiasFor(styleInfluence, TrackType::Sub808).balanceWeight, 1.0f + kick808Coupling * 0.26f, 0.8f);
    blendWeight(laneBiasFor(styleInfluence, TrackType::Perc).activityWeight, 0.78f - refGapIntent * 0.16f, 0.7f);
    blendWeight(styleInfluence.lowEndCouplingWeight, 1.0f + kick808Coupling * 0.24f, 0.85f);
    blendWeight(styleInfluence.hatMotionWeight, 0.94f + styleAwareMotion * 0.26f + refBurst * 0.06f, 0.8f);
    blendWeight(styleInfluence.anchorRigidityWeight, 1.0f + anchorRigidity * 0.20f, 0.85f);
    blendWeight(styleInfluence.drillHatRollLengthWeight, 0.88f + refRollLength * 0.44f, 0.9f);
    blendWeight(styleInfluence.drillHatDensityVariationWeight, 0.86f + refDensityVariation * 0.46f, 0.9f);
    blendWeight(styleInfluence.drillHatAccentPatternWeight, 0.86f + refAccentAlternation * 0.44f, 0.9f);
    blendWeight(styleInfluence.drillHatGapIntentWeight, 0.84f + refGapIntent * 0.44f, 0.9f);
    blendWeight(styleInfluence.drillHatBurstWeight, 0.86f + refBurst * 0.48f, 0.9f);
    blendWeight(styleInfluence.drillHatTripletWeight, 0.84f + refTriplet * 0.50f, 0.9f);
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

juce::String defaultLaneRoleForTrack(TrackType type)
{
    switch (type)
    {
        case TrackType::Kick: return "core_pulse";
        case TrackType::Snare: return "backbeat";
        case TrackType::HiHat: return "carrier";
        case TrackType::OpenHat: return "accent";
        case TrackType::ClapGhostSnare: return "support";
        case TrackType::GhostKick: return "support";
        case TrackType::HatFX: return "accent_fx";
        case TrackType::Ride: return "support";
        case TrackType::Cymbal: return "crash";
        case TrackType::Perc: return "texture";
        case TrackType::Sub808: return "bass_anchor";
        default: break;
    }

    return "lane";
}
}

ResolvedStyleDefinition StyleInfluenceHelpers::resolveForProject(GenreType genre,
                                                                 int substyleIndex,
                                                                 juce::String* statusMessage)
{
    return StyleDefinitionResolver::resolve(genre, substyleIndex, statusMessage);
}

bool StyleInfluenceHelpers::applyToProject(const ResolvedStyleDefinition& definition,
                                           PatternProject& project,
                                           const StyleInfluenceApplicationOptions& options,
                                           juce::String* errorMessage)
{
    PatternProject next = project;
    const bool referenceInfluenceOnly = definition.loadedFromReference;

    if (options.applyRuntimeLaneSkeleton && !referenceInfluenceOnly)
    {
        RuntimeLaneProfile nextProfile;
        nextProfile.genre = definition.genreName;
        nextProfile.substyle = definition.substyleName;
        nextProfile.lanes.reserve(definition.lanes.size());

        next.runtimeLaneOrder.clear();
        next.runtimeLaneOrder.reserve(definition.lanes.size());

        for (const auto& sourceLane : definition.lanes)
        {
            RuntimeLaneDefinition lane;
            lane.laneId = sourceLane.laneId;
            lane.laneName = sourceLane.laneName;
            lane.groupName = sourceLane.groupName;
            lane.dependencyName = sourceLane.dependencyName;
            lane.generationPriority = sourceLane.generationPriority;
            lane.isCore = sourceLane.isCore;
            lane.isVisibleInEditor = sourceLane.isVisibleInEditor;
            lane.enabledByDefault = sourceLane.laneParams.available ? sourceLane.laneParams.enabled : sourceLane.enabledByDefault;
            lane.supportsDragExport = sourceLane.supportsDragExport;
            lane.isGhostTrack = sourceLane.isGhostTrack;
            lane.defaultMidiNote = sourceLane.defaultMidiNote;
            lane.isRuntimeRegistryLane = sourceLane.isRuntimeRegistryLane;
            lane.runtimeTrackType = sourceLane.runtimeTrackType;
            nextProfile.lanes.push_back(std::move(lane));
            next.runtimeLaneOrder.push_back(sourceLane.laneId);
        }

        next.runtimeLaneProfile = std::move(nextProfile);
        PatternProjectSerialization::validate(next);
    }

    for (const auto& sourceLane : definition.lanes)
    {
        TrackState* track = findTrackByLaneId(next, sourceLane.laneId);
        if (track == nullptr && sourceLane.runtimeTrackType.has_value())
            track = findTrackByRuntimeType(next, *sourceLane.runtimeTrackType);

        if (track == nullptr)
        {
            if (referenceInfluenceOnly)
                continue;

            if (errorMessage != nullptr)
                *errorMessage = "Resolved style lane is missing backing track: " + sourceLane.laneName;
            return false;
        }

        if (!referenceInfluenceOnly)
        {
            track->laneId = sourceLane.laneId;
            track->runtimeTrackType = sourceLane.runtimeTrackType;
        }

        if (options.applyEnabledState)
            track->enabled = sourceLane.laneParams.available ? sourceLane.laneParams.enabled : sourceLane.enabledByDefault;

        if (sourceLane.laneParams.available)
        {
            if (options.applyMuteState)
                track->muted = sourceLane.laneParams.muted;
            if (options.applySoloState)
                track->solo = sourceLane.laneParams.solo;
            if (options.applyLockState)
                track->locked = sourceLane.laneParams.locked;
            if (options.applyLaneVolume)
                track->laneVolume = sourceLane.laneParams.laneVolume;
            if (options.applySampleSelection)
            {
                track->selectedSampleIndex = sourceLane.laneParams.selectedSampleIndex;
                track->selectedSampleName = sourceLane.laneParams.selectedSampleName;
            }
            if (options.applySoundLayer)
                track->sound = sourceLane.laneParams.sound;
            if (options.applyLaneRole)
            {
                track->laneRole = sourceLane.laneParams.laneRole.isNotEmpty()
                    ? sourceLane.laneParams.laneRole
                    : defaultLaneRoleForTrack(*sourceLane.runtimeTrackType);
            }
        }
        else if (!referenceInfluenceOnly && options.applyLaneRole && track->laneRole.isEmpty())
        {
            track->laneRole = defaultLaneRoleForTrack(*sourceLane.runtimeTrackType);
        }
    }

    PatternProjectSerialization::validate(next);
    project = std::move(next);
    if (errorMessage != nullptr)
        *errorMessage = {};
    return true;
}

StyleInfluenceApplicationOptions BoomBapStyleInfluence::applicationOptions()
{
    StyleInfluenceApplicationOptions options;
    options.applyLaneVolume = true;
    options.applySampleSelection = true;
    options.applySoundLayer = true;
    return options;
}

bool BoomBapStyleInfluence::applyResolvedStyle(const ResolvedStyleDefinition& definition,
                                               PatternProject& project,
                                               juce::String* errorMessage)
{
    if (!StyleInfluenceHelpers::applyToProject(definition, project, applicationOptions(), errorMessage))
        return false;

    resetMusicalBiasState(project);
    applyBoomBapMusicalHints(definition, project);
    if (errorMessage != nullptr)
        *errorMessage = {};
    return true;
}

bool BoomBapStyleInfluence::apply(PatternProject& project, juce::String* errorMessage)
{
    auto definition = StyleInfluenceHelpers::resolveForProject(GenreType::BoomBap, project.params.boombapSubstyle, errorMessage);
    return applyResolvedStyle(definition, project, errorMessage);
}

StyleInfluenceApplicationOptions RapStyleInfluence::applicationOptions()
{
    StyleInfluenceApplicationOptions options;
    options.applyLaneVolume = true;
    options.applySampleSelection = true;
    options.applySoundLayer = true;
    return options;
}

bool RapStyleInfluence::applyResolvedStyle(const ResolvedStyleDefinition& definition,
                                           PatternProject& project,
                                           juce::String* errorMessage)
{
    if (!StyleInfluenceHelpers::applyToProject(definition, project, applicationOptions(), errorMessage))
        return false;

    resetMusicalBiasState(project);
    applyRapMusicalHints(definition, project);
    if (errorMessage != nullptr)
        *errorMessage = {};
    return true;
}

bool RapStyleInfluence::apply(PatternProject& project, juce::String* errorMessage)
{
    auto definition = StyleInfluenceHelpers::resolveForProject(GenreType::Rap, project.params.rapSubstyle, errorMessage);
    return applyResolvedStyle(definition, project, errorMessage);
}

StyleInfluenceApplicationOptions TrapStyleInfluence::applicationOptions()
{
    StyleInfluenceApplicationOptions options;
    options.applyLaneVolume = true;
    options.applySampleSelection = true;
    options.applySoundLayer = true;
    return options;
}

bool TrapStyleInfluence::applyResolvedStyle(const ResolvedStyleDefinition& definition,
                                            PatternProject& project,
                                            juce::String* errorMessage)
{
    if (!StyleInfluenceHelpers::applyToProject(definition, project, applicationOptions(), errorMessage))
        return false;

    resetMusicalBiasState(project);
    applyTrapMusicalHints(definition, project);
    if (errorMessage != nullptr)
        *errorMessage = {};
    return true;
}

bool TrapStyleInfluence::apply(PatternProject& project, juce::String* errorMessage)
{
    auto definition = StyleInfluenceHelpers::resolveForProject(GenreType::Trap, project.params.trapSubstyle, errorMessage);
    return applyResolvedStyle(definition, project, errorMessage);
}

StyleInfluenceApplicationOptions DrillStyleInfluence::applicationOptions()
{
    StyleInfluenceApplicationOptions options;
    options.applyLaneVolume = true;
    options.applySampleSelection = true;
    options.applySoundLayer = true;
    return options;
}

bool DrillStyleInfluence::applyResolvedStyle(const ResolvedStyleDefinition& definition,
                                             PatternProject& project,
                                             juce::String* errorMessage)
{
    if (!StyleInfluenceHelpers::applyToProject(definition, project, applicationOptions(), errorMessage))
        return false;

    resetMusicalBiasState(project);
    applyDrillMusicalHints(definition, project);
    if (errorMessage != nullptr)
        *errorMessage = {};
    return true;
}

bool DrillStyleInfluence::apply(PatternProject& project, juce::String* errorMessage)
{
    auto definition = StyleInfluenceHelpers::resolveForProject(GenreType::Drill, project.params.drillSubstyle, errorMessage);
    return applyResolvedStyle(definition, project, errorMessage);
}
} // namespace bbg
