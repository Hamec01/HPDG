#pragma once

#include <array>

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

    float swingPercent = 51.0f;
    float kickDensityBias = 1.0f;
    float hatDensityBias = 1.0f;

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
} // namespace bbg
