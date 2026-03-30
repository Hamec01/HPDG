#pragma once

#include <array>
#include <optional>
#include <vector>

#include <juce_core/juce_core.h>

#include "../Analysis/SampleAwareGenerationContext.h"
#include "GeneratorParams.h"
#include "PatternAuthoringState.h"
#include "ReferenceHatSkeleton.h"
#include "RuntimeLaneProfile.h"
#include "TrackSemantics.h"
#include "TrackRegistry.h"
#include "TrackState.h"

namespace bbg
{
struct LaneMusicalBiasState
{
    float activityWeight = 1.0f;
    float balanceWeight = 1.0f;
};

struct StyleInfluenceState
{
    std::array<LaneMusicalBiasState, kTrackTypeCount> laneBiases {};
    float supportAccentWeight = 1.0f;
    float lowEndCouplingWeight = 1.0f;
    float hatMotionWeight = 1.0f;
    float bounceWeight = 1.0f;
    float anchorRigidityWeight = 1.0f;
    float drillHatRollLengthWeight = 1.0f;
    float drillHatDensityVariationWeight = 1.0f;
    float drillHatAccentPatternWeight = 1.0f;
    float drillHatGapIntentWeight = 1.0f;
    float drillHatBurstWeight = 1.0f;
    float drillHatTripletWeight = 1.0f;
    ReferenceHatSkeleton referenceHatSkeleton;
    ReferenceHatCorpus referenceHatCorpus;
    ReferenceKickCorpus referenceKickCorpus;
};

inline LaneMusicalBiasState& laneBiasFor(StyleInfluenceState& state, TrackType type)
{
    return state.laneBiases[trackTypeIndex(type)];
}

inline const LaneMusicalBiasState& laneBiasFor(const StyleInfluenceState& state, TrackType type)
{
    return state.laneBiases[trackTypeIndex(type)];
}

inline LaneMusicalBiasState& laneBiasFor(StyleInfluenceState& state, TrackRole role)
{
    jassert(hasCanonicalTrackTypeForRole(role));
    return laneBiasFor(state, canonicalTrackTypeForRole(role));
}

inline const LaneMusicalBiasState& laneBiasFor(const StyleInfluenceState& state, TrackRole role)
{
    jassert(hasCanonicalTrackTypeForRole(role));
    return laneBiasFor(state, canonicalTrackTypeForRole(role));
}

enum class PreviewPlaybackMode
{
    FromFlag = 0,
    LoopRange = 1
};

struct PatternProject
{
    // PatternProject owns generated musical content and per-track musical state.
    // APVTS owns plugin parameter state (tempo sync, knobs, genre choices, seed).
    GeneratorParams params;
    StyleInfluenceState styleInfluence;
    RuntimeLaneProfile runtimeLaneProfile;
    std::vector<RuntimeLaneId> runtimeLaneOrder;
    std::vector<TrackState> tracks;
    int selectedTrackIndex = 0;
    int soundModuleTrackIndex = -1;
    int generationCounter = 0;
    int mutationCounter = 0;
    int phraseLengthBars = 1;
    juce::String phraseRoleSummary;
    int previewStartStep = 0;
    PreviewPlaybackMode previewPlaybackMode = PreviewPlaybackMode::FromFlag;
    std::optional<juce::Range<int>> previewLoopTicks;
    SoundLayerState globalSound;
    PatternAuthoringState authoring;
    SampleAwareGenerationContext sampleContext;
};

inline PatternProject createDefaultProject()
{
    PatternProject project;
    project.runtimeLaneProfile = TrackRegistry::createDefaultRuntimeLaneProfile();
    for (const auto& lane : project.runtimeLaneProfile.lanes)
        project.runtimeLaneOrder.push_back(lane.laneId);
    project.tracks = TrackRegistry::createDefaultTrackStates(project.runtimeLaneProfile);
    project.phraseLengthBars = project.params.bars;
    return project;
}
} // namespace bbg
