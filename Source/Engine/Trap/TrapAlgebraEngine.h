#pragma once

#include <array>
#include <random>
#include <vector>

#include <juce_core/juce_core.h>

namespace bbg
{
namespace TrapAlgebraLanes
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
} // namespace TrapAlgebraLanes

enum class TrapAlgebraSubstyle
{
    ATLClassic = 0,
    DarkTrap,
    CloudTrap,
    RageTrap,
    MemphisTrap,
    LuxuryTrap
};

enum class TrapAlgebraRole
{
    Anchor,
    Support,
    Ghost,
    Accent,
    Roll,
    Fill,
    Bass,
    Ending
};

struct TrapAlgebraParams
{
    int seed = 1;
    int bars = 4;
    float bpm = 140.0f;
    float density = 0.55f;
    float swing = 0.54f;
    float humanize = 0.35f;
    float variation = 0.45f;
    float temperature = 0.40f;
    float qMin = 0.62f;
    int candidateCount = 64;
    TrapAlgebraSubstyle substyle = TrapAlgebraSubstyle::ATLClassic;
};

struct TrapAlgebraNote
{
    int laneIndex = TrapAlgebraLanes::Kick;
    int barIndex = 0;
    int tick64 = 0;
    int durationTicks = 1;
    int velocity = 100;
    int microTimingTicks = 0; // PPQ subtick offset, not 1/64-grid ticks.
    TrapAlgebraRole role = TrapAlgebraRole::Support;
    juce::String roleString = "support";
};

struct TrapPatternCell
{
    bool active = false;
    int velocity = 0;
    int durationTicks = 0;
    int microTimingTicks = 0; // PPQ subtick offset, not 1/64-grid ticks.
    TrapAlgebraRole role = TrapAlgebraRole::Support;
    juce::String roleString;
};

class TrapPatternMatrix
{
public:
    explicit TrapPatternMatrix(int bars = 4);

    void reset(int newBars);
    int getBars() const;
    int getTotalTicks() const;

    bool setNote(int lane,
                 int tick64,
                 int velocity,
                 int durationTicks,
                 int microTimingTicks,
                 TrapAlgebraRole role);
    void clearNote(int lane, int tick64);
    bool hasNote(int lane, int tick64) const;
    bool isLaneActiveAt(int lane, int tick64) const;
    const TrapPatternCell* cellAt(int lane, int tick64) const;
    TrapPatternCell* cellAt(int lane, int tick64);

    int activeLaneCountAt(int tick64) const;
    int active808Ticks() const;
    int countLane(int lane) const;
    int countLaneInBar(int lane, int bar) const;
    std::vector<TrapAlgebraNote> notesForLane(int lane) const;
    std::vector<TrapAlgebraNote> notesInBar(int bar) const;
    std::vector<TrapAlgebraNote> allNotes() const;

private:
    int bars = 4;
    std::array<std::vector<TrapPatternCell>, TrapAlgebraLanes::Count> lanes;
};

struct TrapSubstyleWeights
{
    juce::String name = "ATLClassic";
    float snare = 1.40f;
    float kick808 = 1.50f;
    float hat = 1.20f;
    float roll = 0.90f;
    float negativeSpace = 1.10f;
    float variation = 0.80f;
    float groove = 0.70f;
    float overload = 1.60f;
    float mud = 1.40f;

    float targetHatDensity = 0.25f;
    float hatVarianceMin = 55.0f;
    int activeLaneMax = 4;

    float hatRate = 0.48f;
    float rollRate = 0.25f;
    float tripletBias = 0.15f;
    float kickIrregularity = 0.48f;
    float bassLegato = 0.35f;
    float bassDistortion = 0.35f;
    float drumDryness = 0.90f;
    float stereoAir = 0.25f;
    float melodicBrightness = 0.45f;
    float pianoOrgan = 0.90f;
    float tranceLead = 0.05f;
    float cowbell = 0.05f;
    float lofiNoise = 0.05f;
    float luxuryPolish = 0.35f;
};

struct TrapQualityBreakdown
{
    float snareBackboneScore = 0.0f;
    float kick808CouplingScore = 0.0f;
    float hiHatMovementScore = 0.0f;
    float rollQualityScore = 0.0f;
    float negativeSpaceScore = 0.0f;
    float barVariationScore = 0.0f;
    float grooveMicrotimingScore = 0.0f;
    float overloadPenalty = 0.0f;
    float mudPenalty = 0.0f;
    float spamPenalty = 0.0f;
    float energy = 0.0f;
    float quality = 0.0f;

    int snareMissingCount = 0;
    float kick808CouplingRatio = 0.0f;
    float sub808Density = 0.0f;
    float hatVelocityVariance = 0.0f;
    int rollCount = 0;
    float averageRollQuality = 0.0f;
    float d01 = 0.0f;
    float d02 = 0.0f;
    float d03 = 0.0f;
};

struct TrapAlgebraPattern
{
    TrapPatternMatrix matrix { 4 };
    TrapQualityBreakdown score;
    juce::StringArray repairsApplied;
    int selectedCandidateIndex = 0;
    bool acceptedByThreshold = false;
    juce::String debugSummary;
};

class TrapQualityScorer
{
public:
    TrapQualityBreakdown score(const TrapPatternMatrix& matrix,
                               const TrapAlgebraParams& params,
                               const TrapSubstyleWeights& weights) const;
};

class TrapEnergyModel
{
public:
    float energy(const TrapQualityBreakdown& score, const TrapSubstyleWeights& weights) const;
};

class TrapConstraintValidator
{
public:
    juce::StringArray validate(const TrapPatternMatrix& matrix,
                               const TrapAlgebraParams& params,
                               const TrapQualityBreakdown& score) const;
};

class TrapAlgebraEngine
{
public:
    TrapAlgebraPattern generate(const TrapAlgebraParams& params) const;

    static TrapSubstyleWeights weightsForSubstyle(TrapAlgebraSubstyle substyle);
    static const char* roleToString(TrapAlgebraRole role);
    static const char* substyleToString(TrapAlgebraSubstyle substyle);
    static int ticksPerBar();
    static int ticksForBars(int bars);

private:
    TrapAlgebraPattern generateCandidate(const TrapAlgebraParams& params, int candidateIndex) const;
    void generateSkeleton(TrapPatternMatrix& matrix, const TrapAlgebraParams& params, std::mt19937& rng) const;
    void generateBarAdditions(TrapPatternMatrix& matrix,
                              const TrapAlgebraParams& params,
                              int bar,
                              int candidateIndex,
                              std::mt19937& rng) const;
    void addRoll(TrapPatternMatrix& matrix,
                 const TrapAlgebraParams& params,
                 int bar,
                 bool barFill,
                 std::mt19937& rng) const;
    void repair(TrapAlgebraPattern& pattern,
                const TrapAlgebraParams& params,
                const TrapSubstyleWeights& weights) const;
    juce::String buildDebugSummary(const TrapAlgebraPattern& pattern,
                                   const TrapAlgebraParams& params,
                                   const TrapSubstyleWeights& weights) const;
};
} // namespace bbg
