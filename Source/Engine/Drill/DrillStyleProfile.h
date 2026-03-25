#pragma once

#include <array>
#include <vector>

#include <juce_core/juce_core.h>

namespace bbg
{
enum class DrillSubstyle
{
    UKDrill = 0,
    BrooklynDrill,
    NYDrill,
    DarkDrill
};

enum class Drill808Mode
{
    PhraseAnchored = 0,
    SelectiveFollow,
    SparseCounter
};

enum class DrillPitchMotion
{
    VeryStatic = 0,
    StaticWithAccentMoves,
    ControlledMovement,
    AggressiveControlledMovement
};

enum class DrillSlideMode
{
    Sparse = 0,
    Moderate,
    Aggressive
};

enum class DrillHatBaseMode
{
    TwoTwoOneSkippy = 0,
    HalfTimeTripletCarrier,
    Sparse16WithCuts,
    BrokenGarageLike,
    DenseBrooklynCarrier,
    DarkSparseCarrier
};

struct DrillHatBaseSpec
{
    DrillHatBaseMode primaryMode = DrillHatBaseMode::TwoTwoOneSkippy;
    DrillHatBaseMode secondaryMode = DrillHatBaseMode::Sparse16WithCuts;

    float baseDensityMin = 0.30f;
    float baseDensityMax = 0.55f;

    float omissionProbability = 0.20f;
    float tripletInsertProbability = 0.16f;
    float extra16Probability = 0.18f;

    float startOfBarBias = 0.10f;
    float midBarBias = 0.08f;
    float endOfBarBias = 0.18f;
    float phraseEdgeBias = 0.22f;

    float microTimingMin = -5.0f;
    float microTimingMax = 6.0f;

    float baseVelocityMin = 60.0f;
    float baseVelocityMax = 86.0f;
    float accentVelocityMin = 82.0f;
    float accentVelocityMax = 98.0f;

    bool preferTwoTwoOne = false;
    bool preferTripletCarriers = false;
    bool preferSilence = false;
};

enum class DrillHatFxBank
{
    ShortTripletCluster = 0,
    ExtendedTripletCluster,
    SkippyBurst,
    TresilloEdgeBurst,
    PreSnareTriplet,
    KickReactionSkips,
    PhraseEdgeCluster,
    DarkFadeBurst,
    AngularJabBurst,
    Tight32Burst
};

enum class DrillFxSize
{
    Tiny = 0,
    Medium,
    Major
};

struct DrillHatFxBankWeight
{
    DrillHatFxBank bank = DrillHatFxBank::ShortTripletCluster;
    int weight = 0;
};

struct DrillHatFxStyleProfile
{
    std::vector<DrillHatFxBankWeight> banks;

    int maxTinyPerBar = 1;
    int maxMediumPerBar = 1;
    int maxMajorPerBar = 0;
    int maxMajorPerTwoBars = 1;

    bool allowEndOfBar = false;
    bool allowEndOfHalfBar = false;
    bool allowPreSnare = false;
    bool allowKickReaction = false;
    bool allowPhraseEdge = false;
    bool allowTransitionLift = false;
};

enum class Drill808RhythmMode
{
    PhraseAnchored = 0,
    BrokenFollow,
    FrontalPressure,
    SparseThreat
};

enum class Drill808PitchMotion
{
    VeryStatic = 0,
    ControlledMovement,
    AggressiveControlledMovement
};

enum class Drill808SlideMode
{
    Sparse = 0,
    Moderate,
    Strong
};

struct Drill808StyleSpec
{
    Drill808RhythmMode rhythmMode = Drill808RhythmMode::PhraseAnchored;
    Drill808PitchMotion pitchMotion = Drill808PitchMotion::VeryStatic;
    Drill808SlideMode slideMode = Drill808SlideMode::Moderate;

    int minStartsPerBar = 1;
    int maxStartsPerBar = 3;

    float anchorFollowProbability = 0.78f;
    float supportFollowProbability = 0.22f;
    float edgeAnchorProbability = 0.48f;
    float pickupBassProbability = 0.14f;
    float restartProbability = 0.24f;
    float longHoldProbability = 0.56f;
    float phraseHoldProbability = 0.30f;
    float rejectIfDenseTopProbability = 0.72f;

    float rootWeight = 0.68f;
    float octaveWeight = 0.16f;
    float fifthWeight = 0.10f;
    float flat7Weight = 0.04f;
    float minor3Weight = 0.02f;

    int maxPitchChangesPer2Bars = 2;
    int maxSlidesPer2Bars = 1;
    int maxMajorRestartsPer2Bars = 3;

    bool preferRootOnPhraseEdge = true;
    bool allowOctaveJumps = true;
    bool allowFlat7 = true;
    bool allowMinor3 = false;
    bool allowLongSlideAtPhraseEdge = false;
};

struct DrillStyleSpec
{
    float kickDensityMin = 0.25f;
    float kickDensityMax = 0.65f;

    float subDensityMin = 0.15f;
    float subDensityMax = 0.55f;

    float hatFxDensityMin = 0.1f;
    float hatFxDensityMax = 0.45f;

    float ghostKickDensityMin = 0.02f;
    float ghostKickDensityMax = 0.16f;

    float followKickProbability = 0.58f;
    float sustainProbability = 0.48f;
    float counterGapProbability = 0.18f;
    float phraseEdgeAnchorProbability = 0.56f;
    float skipIfDenseHatFxProbability = 0.12f;

    int maxMajorEventsPerBar = 2;
    int maxMajorEventsPer2Bars = 3;
    int denseTopHitsThresholdPerBar = 16;

    bool preferTriplets = true;
    bool preferTresilloLikeBursts = true;
    bool allowOpenHatFrequently = false;
    bool allowCymbalTransitions = false;
    bool preferSparseSpace = true;

    Drill808Mode rhythmMode = Drill808Mode::PhraseAnchored;
    DrillPitchMotion pitchMotion = DrillPitchMotion::StaticWithAccentMoves;
    DrillSlideMode slideMode = DrillSlideMode::Sparse;
};

struct DrillStyleProfile
{
    juce::String name;
    DrillSubstyle substyle = DrillSubstyle::UKDrill;

    // Drill hat engine control surface.
    double baseHat = 0.46;
    double baseFill = 0.28;
    double baseRoll = 0.20;
    double baseAccent = 0.16;
    double lambdaDensity = 0.70;

    int minHatsPerBar = 8;
    int maxHatsPerBar = 12;

    int minRollsPer4Bars = 2;
    int maxRollsPer4Bars = 4;

    int minAccentsPerBar = 1;
    int maxAccentsPerBar = 2;

    int maxRollSegmentsPerBar = 1;
    int maxAccentsPerBarHard = 2;

    int rollCooldownTicks = 480;
    int accentCooldownTicks = 240;
    int minDistanceRollTicks = 240;
    int minDistanceAccentTicks = 160;

    int maxRollSegmentsPer2Bars = 2;
    int maxConsecutiveRollBars = 2;

    double preSnareBoostRoll = 1.90;
    double preSnareBoostAccent = 1.45;
    double endBarBoostRoll = 1.35;

    bool allowMidBarRoll = true;
    bool preferPreSnareRoll = true;
    bool preferEndBarRoll = true;

    double tripletSubpulseWeight = 0.82;
    double straight32SubpulseWeight = 0.74;

    std::array<double, 4> phraseComplexityCurve { 1.00, 0.94, 1.14, 1.28 };

    float swingPercent = 51.0f;
    float kickDensityBias = 1.0f;
    float hatDensityBias = 1.0f;
    bool cleanHatMode = false;

    float hatFxIntensity = 0.7f;
    float openHatChance = 0.14f;
    float clapLayerChance = 0.54f;
    float ghostKickChance = 0.06f;
    float percChance = 0.12f;
    float sub808Activity = 0.9f;

    int kickVelocityMin = 94;
    int kickVelocityMax = 124;
    int snareVelocityMin = 102;
    int snareVelocityMax = 127;
    int hatVelocityMin = 58;
    int hatVelocityMax = 106;
    int sub808VelocityMin = 92;
    int sub808VelocityMax = 126;
};

const std::array<DrillStyleProfile, 4>& getDrillProfiles();
const DrillStyleProfile& getDrillProfile(int index);
DrillStyleSpec getDrillStyleSpec(DrillSubstyle substyle);
DrillHatBaseSpec getDrillHatBaseSpec(DrillSubstyle substyle);
DrillHatFxStyleProfile getDrillHatFxStyleProfile(DrillSubstyle substyle);
Drill808StyleSpec getDrill808StyleSpec(DrillSubstyle substyle);
} // namespace bbg
