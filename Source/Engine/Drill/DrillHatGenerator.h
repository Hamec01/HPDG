#pragma once

#include <random>
#include <vector>

#include "DrillGrooveBlueprint.h"
#include "DrillPhrasePlanner.h"
#include "DrillStyleProfile.h"
#include "../../Core/GeneratorParams.h"
#include "../../Core/TrackState.h"

namespace bbg
{
struct StyleInfluenceState;
struct ReferenceHatSkeleton;

class DrillHatGenerator
{
public:
    enum class BarRole
    {
        Statement = 0,
        Response,
        Transition,
        Ending
    };

    struct BarContext
    {
        int barIndex = 0;
        BarRole role = BarRole::Statement;
        double phraseEnergy = 1.0;
        bool isTransitionBar = false;

        std::vector<int> snareTargetTicks;
        std::vector<int> kickTicks;
        std::vector<int> phaseAnchorTicks;

        double kickDensity = 0.0;

        int existingRolls = 0;
        int existingAccents = 0;
    };

    struct HatGridFeature
    {
        int tick = 0;
        int step = 0;

        double metrical = 0.0;
        double phrase = 0.0;
        double offbeat = 0.0;
        double preSnare = 0.0;
        double preKick = 0.0;
        double localDensity = 0.0;
        double isolation = 0.0;
        double cooldownRoll = 1.0;
        double cooldownAccent = 1.0;

        double blueprintBackboneWeight = 0.5;
        bool blueprintFillAllowed = true;
        bool blueprintRollAllowed = true;
        bool blueprintAccentAllowed = true;
        bool reservedSilence = false;
        double majorEventCompetition = 0.0;
        double phraseEdgeWeight = 0.0;
        bool snareProtection = false;
        double referenceBackbone = 0.0;
        double referenceMotion = 0.0;
        double phaseAnchor = 0.0;
        bool referencePreSnareZone = false;
        bool referenceRollZone = false;
        bool referenceTripletZone = false;
    };

    enum class RollShape
    {
        Flat = 0,
        Crescendo,
        Decrescendo,
        Arch,
        AccentFirst,
        AccentLast
    };

    enum class RollExitType
    {
        None = 0,
        SingleTail,
        DoubleTail,
        PauseAfter
    };

    enum class MotionMode
    {
        Low = 0,
        Mid,
        High
    };

    struct RollSegmentState
    {
        int startTick = 0;
        int lenTicks = 0;
        int noteCount = 0;
        int targetSnareTick = -1;

        double subdivisionTicks = 0.0;
        double energy = 1.0;
        int groupSize = 1;
        bool burstCluster = false;
        bool tripletGroup = false;

        RollShape shape = RollShape::Flat;
        RollExitType exitType = RollExitType::None;
    };

    struct MotionProfile
    {
        MotionMode mode = MotionMode::Mid;
        double motionWeight = 1.0;
        double rollLengthWeight = 1.0;
        double densityVariationWeight = 1.0;
        double accentPatternWeight = 1.0;
        double gapIntentWeight = 1.0;
        double burstWeight = 1.0;
        double tripletWeight = 1.0;
        double averageRollNotes = 3.0;
        double rollsPerBarTarget = 0.45;
        double densitySwing = 0.0;
        int maxMotionEventsPerBar = 2;
        bool preferBurstClusters = false;
        bool preferAccentGroups = true;
        bool sparseMode = false;
        bool expressiveMode = false;
    };

    enum class HatEntity
    {
        Backbone = 0,
        Fill,
        Roll,
        Accent
    };

    struct HatEvent
    {
        int tick = 0;
        int velocity = 0;
        int microOffset = 0;
        HatEntity entity = HatEntity::Backbone;
        bool accent = false;
        bool rollPeak = false;
        bool rollExit = false;
    };

    void generate(TrackState& track,
                  const GeneratorParams& params,
                  const DrillStyleProfile& style,
                  const StyleInfluenceState& styleInfluence,
                  const std::vector<DrillPhraseRole>& phrase,
                  const DrillGrooveBlueprint* blueprint,
                  const TrackState* snareTrack,
                  const TrackState* clapTrack,
                  const TrackState* kickTrack,
                  std::mt19937& rng) const;

private:
    std::vector<int> collectTicks(const TrackState* track) const;
    std::vector<int> collectSnareTargets(const TrackState* snareTrack,
                                         const TrackState* clapTrack,
                                         int bars) const;

    std::vector<BarContext> buildBarContext(const std::vector<DrillPhraseRole>& phrase,
                                            const std::vector<int>& snareTargets,
                                            const std::vector<int>& kickTicks,
                                  const ReferenceHatSkeleton* referenceSkeleton,
                                  const DrillGrooveBlueprint* blueprint,
                                            const DrillStyleProfile& style,
                                            int bars) const;

    std::vector<HatGridFeature> buildGridFeatures(const std::vector<BarContext>& contexts,
                                      const ReferenceHatSkeleton* referenceSkeleton,
                                      const DrillGrooveBlueprint* blueprint,
                                                  int bars) const;

    void generateBackboneHats(std::vector<HatEvent>& hats,
                              const std::vector<BarContext>& contexts,
                              std::vector<HatGridFeature>& features,
                              const DrillStyleProfile& style,
                              const DrillHatBaseSpec& spec,
                              const ReferenceHatSkeleton* referenceSkeleton,
                              const DrillGrooveBlueprint* blueprint,
                              int bars,
                              std::mt19937& rng) const;

    void generateFillHats(std::vector<HatEvent>& hats,
                          const std::vector<BarContext>& contexts,
                          std::vector<HatGridFeature>& features,
                          const DrillStyleProfile& style,
                          const MotionProfile& motion,
                          const DrillHatBaseSpec& spec,
                          const DrillGrooveBlueprint* blueprint,
                          int bars,
                          std::mt19937& rng) const;

    std::vector<RollSegmentState> createRollSegments(const std::vector<HatEvent>& hats,
                                                     const std::vector<BarContext>& contexts,
                                                     std::vector<HatGridFeature>& features,
                                                     const DrillStyleProfile& style,
                                                     const MotionProfile& motion,
                                                     const DrillHatBaseSpec& spec,
                                                     const DrillGrooveBlueprint* blueprint,
                                                     int bars,
                                                     std::mt19937& rng) const;

    void renderRollSegments(std::vector<HatEvent>& hats,
                            const std::vector<RollSegmentState>& segments,
                            const DrillStyleProfile& style,
                            int bars) const;

    void assignHatAccents(std::vector<HatEvent>& hats,
                          const std::vector<RollSegmentState>& segments,
                          const std::vector<BarContext>& contexts,
                          std::vector<HatGridFeature>& features,
                          const DrillStyleProfile& style,
                          const MotionProfile& motion,
                          const DrillGrooveBlueprint* blueprint,
                          int bars,
                          std::mt19937& rng) const;

    void applyHardCapsCleanup(std::vector<HatEvent>& hats,
                              const std::vector<RollSegmentState>& segments,
                              const std::vector<BarContext>& contexts,
                              const DrillStyleProfile& style,
                              const MotionProfile& motion,
                              const DrillGrooveBlueprint* blueprint,
                              int bars) const;

    void applyVelocityShape(std::vector<HatEvent>& hats,
                            const std::vector<RollSegmentState>& segments,
                            const DrillStyleProfile& style,
                            const DrillHatBaseSpec& spec,
                            int bars,
                            std::mt19937& rng) const;

    void applyMicroTiming(std::vector<HatEvent>& hats,
                          const std::vector<RollSegmentState>& segments,
                          const DrillStyleProfile& style,
                          const DrillHatBaseSpec& spec,
                          const std::vector<BarContext>& contexts,
                          int bars,
                          std::mt19937& rng) const;

    void renderMidi(TrackState& track,
                    const std::vector<HatEvent>& hats,
                    int pitch,
                    int bars) const;
};
} // namespace bbg
