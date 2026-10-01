#pragma once

#include <array>
#include <cstdint>
#include <random>
#include <optional>
#include <vector>

#include <juce_core/juce_core.h>

#include "BoomBapStyleProfile.h"
#include "BoomBapGenerationContext.h"
#include "../GenerationModel/PatternFeatureVector.h"
#include "../GenerationModel/StyleTargetModel.h"

namespace bbg
{
namespace BoomBapClassicLanes
{
constexpr int HiHat = 0;
constexpr int HatAccent = 1;
constexpr int OpenHat = 2;
constexpr int Snare = 3;
constexpr int ClapGhost = 4;
constexpr int Kick = 5;
constexpr int KickGhost = 6;
constexpr int Ride = 7;
constexpr int Cymbal = 8;
constexpr int Perc = 9;
constexpr int Sub808 = 10;
constexpr int Count = 11;

constexpr std::array<const char*, Count> Names {
    "HiHat",
    "Hat Accent",
    "OpenHat",
    "Snare",
    "Clap Ghost",
    "Kick",
    "Kick Ghost",
    "Ride",
    "Cymbal",
    "Perc",
    "Sub808"
};
} // namespace BoomBapClassicLanes

enum class BoomBapClassicRole
{
    Anchor,
    Backbeat,
    Syncopated,
    Pickup,
    Response,
    Support,
    Ghost,
    GhostBeforeSnare,
    GhostAfterSnare,
    ClapLayer,
    FillSupport,
    PickupToKick,
    ResponseFromKick,
    FillKick,
    Accent,
    WeakPulse,
    StrongPulse,
    Turnaround,
    Ornament,
    Fill,
    Ending
};

struct BoomBapClassicAlgebraParams
{
    int seed = 1;
    int bars = 4;
    float bpm = 90.0f;
    float density = 0.55f;
    float swing = 0.58f;
    float humanize = 0.35f;
    float variation = 0.35f;
    int candidateCount = 64;
    BoomBapSubstyle substyle = BoomBapSubstyle::Classic;
    std::optional<BoomBapGrooveArchetype> forcedArchetype;
    std::optional<RarePhraseEvent> forcedRareEvent;
    std::optional<BoomBapFillType> forcedFillType;
};

struct BoomBapClassicAlgebraNote
{
    int laneIndex = BoomBapClassicLanes::Kick;
    int barIndex = 0;
    int tick64 = 0;
    int length = 1;
    int velocity = 100;
    int microTimingTicks = 0; // PPQ subtick offset, not a whole 1/64-grid move.
    BoomBapTiming::TimingBreakdown timing;
    BoomBapClassicRole role = BoomBapClassicRole::Support;
    juce::String roleString = "support";
    int anchorLane = -1;
    int anchorTick64 = -1;
    int priority = 40;
};

struct BoomBapClassicScoreBreakdown
{
    float backbeatScore = 0.0f;
    float kickAnchorScore = 0.0f;
    float grooveScore = 0.0f;
    float breakResemblanceScore = 0.0f;
    float microPlausibilityScore = 0.0f;
    float negativeSpaceScore = 0.0f;
    float lowEndDisciplineScore = 0.0f;
    float variationScore = 0.0f;
    float densityBalanceScore = 0.0f;
    float velocityHumanityScore = 0.0f;
    float trapLeakPenalty = 0.0f;
    float trapLeakConfidence = 0.0f;
    bool hardTrapLeak = false;
    float earlySnarePenalty = 0.0f;
    float overHumanizePenalty = 0.0f;
    float sub808OverusePenalty = 0.0f;
    float conflictPenalty = 0.0f;
    float spamPenalty = 0.0f;
    float quality = 0.0f;
    float similarity12 = 0.0f;
    float similarity13 = 0.0f;
    float similarity14 = 0.0f;
    float ghostContextQuality = 0.0f;
    float ghostVelocityQuality = 0.0f;
    float kickConversationQuality = 0.0f;
    float hatMotifCoherence = 0.0f;
    float rareEventQuality = 0.0f;
    float fillQuality = 0.0f;
    float dropoutQuality = 0.0f;
    float novelty = 0.0f;
};

struct BoomBapClassicAlgebraPattern
{
    std::array<std::vector<BoomBapClassicAlgebraNote>, BoomBapClassicLanes::Count> notesByLane;
    std::array<juce::String, 4> phraseRoles {
        "statement",
        "confirmation",
        "development",
        "turnaround"
    };
    juce::StringArray repairsApplied;
    int selectedCandidateIndex = 0;
    BoomBapClassicScoreBreakdown score;
    PatternFeatureVector features;
    StyleTargetMatch styleMatch;
    float selectionQuality = 0.0f;
    float kickMotifFidelity = 1.0f; // share of the seeded kick motif played in bar 1
    BoomBapGenerationContext context;
    RarePhraseEvent realizedEvent = RarePhraseEvent::None;
    BoomBapFillType fillType = BoomBapFillType::None;
    int orphanGhostCount = 0;
    int repairedGhostCount = 0;
    int orphanKickGhostCount = 0;
    float maxGhostVelocityRatio = 0.0f;
    int nearBestPoolSize = 0;
    float bestCandidateQuality = 0.0f;
    float selectedCandidateQuality = 0.0f;
    juce::String debugSummary;

    std::vector<BoomBapClassicAlgebraNote> allNotes() const;
};

class BoomBapClassicPatternScorer
{
public:
    BoomBapClassicScoreBreakdown score(const BoomBapClassicAlgebraPattern& pattern,
                                       const BoomBapClassicAlgebraParams& params,
                                       const BoomBapStyleProfile& profile) const;
};

class BoomBapClassicAlgebraGenerator
{
public:
    BoomBapClassicAlgebraPattern generate(const BoomBapClassicAlgebraParams& params) const;

    static const char* roleToString(BoomBapClassicRole role);
    static int ticksPerBar();
    static int ticksPerBeat();
    static int ticksPerSixteenth();
    static int ticksPerEighth();

private:
    BoomBapClassicAlgebraPattern generateCandidate(const BoomBapClassicAlgebraParams& params, const BoomBapGenerationContext& context, int candidateIndex) const;
    void generateBar(BoomBapClassicAlgebraPattern& pattern,
                     const BoomBapClassicAlgebraParams& params,
                     int candidateIndex,
                     int barIndex,
                     std::mt19937& rng,
                     const BoomBapGenerationContext& context,
                     const std::vector<int>* kickSkeletonToAnswer = nullptr) const;
    void cloneBarWithSmallMutation(BoomBapClassicAlgebraPattern& pattern,
                                   const BoomBapClassicAlgebraParams& params,
                                   std::mt19937& rng,
                                   const BoomBapGenerationContext& context) const;
    void deriveBarFromStatement(BoomBapClassicAlgebraPattern& pattern,
                                const BoomBapClassicAlgebraParams& params,
                                std::mt19937& rng,
                                int sourceBar,
                                int targetBar,
                                const BoomBapGenerationContext& context) const;
    void applyRarePhraseEvent(BoomBapClassicAlgebraPattern& pattern,
                              const BoomBapClassicAlgebraParams& params,
                              std::mt19937& rng) const;
    void validateAndRepair(BoomBapClassicAlgebraPattern& pattern,
                           const BoomBapClassicAlgebraParams& params) const;
    juce::String buildDebugSummary(const BoomBapClassicAlgebraPattern& pattern,
                                   const BoomBapClassicAlgebraParams& params) const;
};
} // namespace bbg
