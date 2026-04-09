#include "StyleInfluence.h"

#include "../Core/TrackSemantics.h"
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

void applyReferenceAssets(const ResolvedStyleDefinition& definition, PatternProject& project)
{
    project.styleInfluence.referenceHatSkeleton = definition.referenceHatSkeleton.value_or(ReferenceHatSkeleton {});
    project.styleInfluence.referenceHatCorpus = definition.referenceHatCorpus.value_or(ReferenceHatCorpus {});
    project.styleInfluence.referenceKickCorpus = definition.referenceKickCorpus.value_or(ReferenceKickCorpus {});
    project.styleInfluence.brooklynReferenceProfile = definition.brooklynReferenceProfile.value_or(BrooklynReferenceProfile {});
    project.styleInfluence.brooklynHatDiagnostics = {};
    project.styleInfluence.referenceDebugDiagnostics = definition.referenceDebugDiagnostics;
}

void resetMusicalBiasState(PatternProject& project)
{
    const auto referenceHatSkeleton = project.styleInfluence.referenceHatSkeleton;
    const auto referenceHatCorpus = project.styleInfluence.referenceHatCorpus;
    const auto referenceKickCorpus = project.styleInfluence.referenceKickCorpus;
    const auto brooklynReferenceProfile = project.styleInfluence.brooklynReferenceProfile;
    const auto referenceDebugDiagnostics = project.styleInfluence.referenceDebugDiagnostics;

    project.styleInfluence = {};
    project.styleInfluence.referenceHatSkeleton = referenceHatSkeleton;
    project.styleInfluence.referenceHatCorpus = referenceHatCorpus;
    project.styleInfluence.referenceKickCorpus = referenceKickCorpus;
    project.styleInfluence.brooklynReferenceProfile = brooklynReferenceProfile;
    project.styleInfluence.referenceDebugDiagnostics = referenceDebugDiagnostics;
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

    blendWeight(laneBiasFor(styleInfluence, TrackRole::Kick).activityWeight, 0.96f + kickBias * 0.14f, 0.55f);
    blendWeight(laneBiasFor(styleInfluence, TrackRole::HiHat).activityWeight, 0.92f + hatBias * 0.10f, 0.45f);
    blendWeight(laneBiasFor(styleInfluence, TrackRole::ClapGhostSnare).balanceWeight, 1.0f + clapFocus * 0.24f, 0.8f);
    blendWeight(laneBiasFor(styleInfluence, TrackRole::Perc).activityWeight, 0.56f + (1.0f - percSparsity) * 0.18f, 0.75f);
    blendWeight(laneBiasFor(styleInfluence, TrackRole::OpenHat).activityWeight, 0.66f + (1.0f - percSparsity) * 0.16f, 0.7f);
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

    blendWeight(laneBiasFor(styleInfluence, TrackRole::ClapGhostSnare).balanceWeight, 1.0f + accentPush * 0.14f, 0.65f);
    blendWeight(laneBiasFor(styleInfluence, TrackRole::OpenHat).activityWeight, 0.88f + supportDensity * 0.12f, 0.5f);
    blendWeight(laneBiasFor(styleInfluence, TrackRole::Perc).activityWeight, 0.82f + supportDensity * 0.12f, 0.55f);
    blendWeight(laneBiasFor(styleInfluence, TrackRole::Ride).activityWeight, 0.78f + supportDensity * 0.14f, 0.6f);
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

    blendWeight(laneBiasFor(styleInfluence, TrackRole::HiHat).activityWeight, 1.0f + hatSubdivision * 0.22f, 0.8f);
    blendWeight(laneBiasFor(styleInfluence, TrackRole::HatFX).activityWeight, 0.96f + bounce * 0.24f, 0.75f);
    blendWeight(laneBiasFor(styleInfluence, TrackRole::Bass).balanceWeight, 1.0f + emphasis808 * 0.30f, 0.85f);
    blendWeight(laneBiasFor(styleInfluence, TrackRole::OpenHat).activityWeight, 0.92f + openHatProfile * 0.24f, 0.75f);
    blendWeight(styleInfluence.bounceWeight, 1.0f + bounce * 0.22f, 0.8f);
    blendWeight(styleInfluence.lowEndCouplingWeight, 1.0f + emphasis808 * 0.18f, 0.7f);
}

void applyDrillMusicalHints(const ResolvedStyleDefinition& definition, PatternProject& project)
{
    auto& params = project.params;
    auto& styleInfluence = project.styleInfluence;

    const auto sharedSwing = hintValue(definition.styleHints, "groove.swing", normalizedSwing(params.swingPercent));
    const auto sharedTiming = hintValue(definition.styleHints, "groove.timing", params.timingAmount);
    const auto sharedHumanize = hintValue(definition.styleHints, "groove.humanize", params.humanizeAmount);
    const auto sharedDensity = hintValue(definition.styleHints, "groove.density", params.densityAmount);

    const auto hatDensityBias = laneHintValue(definition, TrackType::HiHat, "lane.densityBias", 1.0f);
    const auto hatVariationBias = laneHintValue(definition, TrackType::HiHat, "lane.rgVariationIntensity", 0.8f);
    const auto hatFxIntensity = laneHintValue(definition, TrackType::HatFX, "lane.hatFxIntensity", 0.4f);
    const auto supportDensity = laneHintValue(definition, TrackType::ClapGhostSnare, "lane.noteProbability", 0.25f);
    const auto lowEndActivity = laneHintValue(definition, TrackType::Sub808, "lane.sub808Activity", 0.85f);

    const auto hatMotion = hintValue(definition.styleHints,
                                     "drill.hat_motion",
                                     clampUnit(hatDensityBias * 0.52f + hatVariationBias * 0.30f + hatFxIntensity * 0.18f));
    const auto gapIntent = hintValue(definition.styleHints,
                                     "drill.gap_intent",
                                     clampUnit(0.28f + (1.0f - clampUnit(sharedDensity)) * 0.18f + hatFxIntensity * 0.20f));
    const auto supportAccent = hintValue(definition.styleHints,
                                         "drill.support_accent",
                                         clampUnit(0.24f + supportDensity * 0.46f + hatFxIntensity * 0.30f));
    const auto lowEndCoupling = hintValue(definition.styleHints,
                                          "drill.low_end_coupling",
                                          clampUnit(0.28f + lowEndActivity * 0.60f + sharedDensity * 0.10f));
    const auto rollLength = hintValue(definition.styleHints,
                                      "drill.ref_hat_roll_length",
                                      hintValue(definition.styleHints,
                                                "drill.hat_roll_length",
                                                clampUnit(hatVariationBias * 0.62f + hatFxIntensity * 0.22f)));
    const auto densityVariation = hintValue(definition.styleHints,
                                            "drill.ref_hat_density_variation",
                                            hintValue(definition.styleHints,
                                                      "drill.hat_density_variation",
                                                      clampUnit(hatVariationBias * 0.72f + gapIntent * 0.12f)));
    const auto accentPattern = hintValue(definition.styleHints,
                                         "drill.ref_hat_accent_alternation",
                                         hintValue(definition.styleHints,
                                                   "drill.hat_accent_pattern",
                                                   clampUnit(hatFxIntensity * 0.54f + supportDensity * 0.30f + gapIntent * 0.10f)));
    const auto burst = hintValue(definition.styleHints,
                                 "drill.ref_hat_burst",
                                 hintValue(definition.styleHints,
                                           "drill.hat_burst",
                                           clampUnit(hatFxIntensity * 0.58f + densityVariation * 0.24f + hatMotion * 0.10f)));
    const auto triplet = hintValue(definition.styleHints,
                                   "drill.ref_hat_triplet",
                                   hintValue(definition.styleHints,
                                             "drill.hat_triplet",
                                             clampUnit(rollLength * 0.28f + densityVariation * 0.34f + hatMotion * 0.18f)));

    blendSwing(params.swingPercent, clampUnit(sharedSwing * 0.32f + gapIntent * 0.04f), 0.18f);
    blendParam(params.timingAmount, clampUnit(sharedTiming * 0.72f + hatMotion * 0.10f + gapIntent * 0.08f), 0.35f);
    blendParam(params.humanizeAmount,
               clampUnit(sharedHumanize * 0.66f + densityVariation * 0.12f + hatMotion * 0.08f),
               0.35f);
    blendParam(params.densityAmount,
               clampUnit(sharedDensity * 0.58f + hatDensityBias * 0.14f + hatFxIntensity * 0.08f + lowEndActivity * 0.10f),
               0.55f);

    blendWeight(styleInfluence.hatMotionWeight, 0.88f + hatMotion * 0.60f, 0.85f);
    blendWeight(styleInfluence.supportAccentWeight, 0.90f + supportAccent * 0.45f, 0.8f);
    blendWeight(styleInfluence.lowEndCouplingWeight, 0.92f + lowEndCoupling * 0.55f, 0.85f);
    blendWeight(styleInfluence.drillHatRollLengthWeight, 0.82f + rollLength * 0.75f, 0.85f);
    blendWeight(styleInfluence.drillHatDensityVariationWeight, 0.86f + densityVariation * 0.70f, 0.8f);
    blendWeight(styleInfluence.drillHatAccentPatternWeight, 0.88f + accentPattern * 0.65f, 0.8f);
    blendWeight(styleInfluence.drillHatGapIntentWeight, 0.86f + gapIntent * 0.60f, 0.8f);
    blendWeight(styleInfluence.drillHatBurstWeight, 0.82f + burst * 0.70f, 0.8f);
    blendWeight(styleInfluence.drillHatTripletWeight, 0.82f + triplet * 0.72f, 0.8f);
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
                    : defaultLaneRoleForTrackType(*sourceLane.runtimeTrackType);
            }
        }
        else if (!referenceInfluenceOnly && options.applyLaneRole && track->laneRole.isEmpty())
        {
            track->laneRole = defaultLaneRoleForTrackType(*sourceLane.runtimeTrackType);
        }
    }

    applyReferenceAssets(definition, next);

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
