#pragma once

#include <array>
#include <cstdint>
#include <random>
#include <vector>

#include <juce_core/juce_core.h>

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
    Support,
    Ghost,
    Accent,
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
    int candidateCount = 24;
};

struct BoomBapClassicAlgebraNote
{
    int laneIndex = BoomBapClassicLanes::Kick;
    int barIndex = 0;
    int tick64 = 0;
    int length = 1;
    int velocity = 100;
    int microTimingTicks = 0;
    BoomBapClassicRole role = BoomBapClassicRole::Support;
    juce::String roleString = "support";
};

struct BoomBapClassicScoreBreakdown
{
    float backbeatScore = 0.0f;
    float kickAnchorScore = 0.0f;
    float grooveScore = 0.0f;
    float variationScore = 0.0f;
    float densityBalanceScore = 0.0f;
    float velocityHumanityScore = 0.0f;
    float conflictPenalty = 0.0f;
    float spamPenalty = 0.0f;
    float quality = 0.0f;
};

struct BoomBapClassicAlgebraPattern
{
    std::array<std::vector<BoomBapClassicAlgebraNote>, BoomBapClassicLanes::Count> notesByLane;
    std::array<juce::String, 4> phraseRoles {
        "statement",
        "repeat_or_small_variation",
        "answer",
        "ending_or_fill"
    };
    juce::StringArray repairsApplied;
    int selectedCandidateIndex = 0;
    BoomBapClassicScoreBreakdown score;
    juce::String debugSummary;

    std::vector<BoomBapClassicAlgebraNote> allNotes() const;
};

class BoomBapClassicPatternScorer
{
public:
    BoomBapClassicScoreBreakdown score(const BoomBapClassicAlgebraPattern& pattern,
                                       const BoomBapClassicAlgebraParams& params) const;
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
    BoomBapClassicAlgebraPattern generateCandidate(const BoomBapClassicAlgebraParams& params, int candidateIndex) const;
    void generateBar(BoomBapClassicAlgebraPattern& pattern,
                     const BoomBapClassicAlgebraParams& params,
                     int candidateIndex,
                     int barIndex,
                     std::mt19937& rng,
                     const std::vector<int>* kickSkeletonToAnswer = nullptr) const;
    void cloneBarWithSmallMutation(BoomBapClassicAlgebraPattern& pattern,
                                   const BoomBapClassicAlgebraParams& params,
                                   std::mt19937& rng) const;
    void validateAndRepair(BoomBapClassicAlgebraPattern& pattern,
                           const BoomBapClassicAlgebraParams& params) const;
    juce::String buildDebugSummary(const BoomBapClassicAlgebraPattern& pattern,
                                   const BoomBapClassicAlgebraParams& params) const;
};
} // namespace bbg
