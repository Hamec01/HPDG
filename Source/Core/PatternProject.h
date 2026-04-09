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

enum class BrooklynReferenceBarRole
{
    Statement = 0,
    Response,
    Lift,
    Ending
};

struct BrooklynReferenceBarRoleProfile
{
    std::array<float, 16> kickStepWeight {};
    std::array<float, 16> hatStepWeight {};
    std::array<float, 16> openHatStepWeight {};
    std::array<float, 16> subStartStepWeight {};
    float avgKickHitsPerBar = 0.0f;
    float avgHatHitsPerBar = 0.0f;
    float avgOpenHatHitsPerBar = 0.0f;
    float avgSubStartsPerBar = 0.0f;
    float avgSubLengthSteps = 0.0f;
    float phraseEdgeKickRate = 0.0f;
    float preSnareAccentRate = 0.0f;
    float burstRate = 0.0f;
};

struct BrooklynReferenceProfile
{
    bool available = false;
    int sourceCount = 0;
    std::array<BrooklynReferenceBarRoleProfile, 4> roles {};

    const BrooklynReferenceBarRoleProfile& profileForRole(BrooklynReferenceBarRole role) const
    {
        return roles[static_cast<size_t>(role)];
    }
};

struct BrooklynHatReferenceDiagnostics
{
    bool available = false;
    juce::String primaryReferenceId;
    juce::String secondaryReferenceId;
    bool usedBlend = false;
    float similarityScore = 0.0f;
    int candidatePoolSize = 0;
};

enum class StyleLabReferenceZeroReason
{
    None = 0,
    NoRefsSelected,
    RefsDisabledByStyleSwitch,
    LaneMappingEmpty,
    IncompatibleReferenceSpan,
    FilteredByDensity,
    ParsingFailed
};

struct StyleLabReferenceLaneDiagnostics
{
    int requestedCount = 0;
    int resolvedCount = 0;
    StyleLabReferenceZeroReason zeroReason = StyleLabReferenceZeroReason::None;
    juce::String detail;
};

struct StyleLabReferenceDebugDiagnostics
{
    int candidateDirectoryCount = 0;
    int parseFailureCount = 0;
    int matchingRecordCount = 0;
    int selectedRecordCount = 0;
    StyleLabReferenceLaneDiagnostics hat;
    StyleLabReferenceLaneDiagnostics kick;
    juce::String loadMessage;
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
    BrooklynReferenceProfile brooklynReferenceProfile;
    BrooklynHatReferenceDiagnostics brooklynHatDiagnostics;
    StyleLabReferenceDebugDiagnostics referenceDebugDiagnostics;
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
    juce::String generationDebugReport;
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
