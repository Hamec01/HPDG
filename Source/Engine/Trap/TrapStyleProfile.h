#pragma once

#include <array>

#include <juce_core/juce_core.h>

namespace bbg
{
enum class TrapSubstyle
{
    ATLClassic = 0,
    DarkTrap,
    CloudTrap,
    RageTrap,
    MemphisTrap,
    LuxuryTrap
};

enum class Trap808Mode
{
    LayerMostKicks = 0,
    SelectiveFollow,
    SparseCounter,
    PhraseAnchored
};

enum class TrapPitchMotion
{
    VeryStatic = 0,
    StaticWithAccentMoves,
    ControlledMovement,
    EnergeticControlledMovement
};

enum class TrapSlideMode
{
    Sparse = 0,
    Moderate,
    Aggressive
};

struct TrapStyleSpec
{
    float kickDensityMin = 0.25f;
    float kickDensityMax = 0.72f;

    float subDensityMin = 0.18f;
    float subDensityMax = 0.66f;

    float hatFxDensityMin = 0.12f;
    float hatFxDensityMax = 0.58f;

    float ghostKickDensityMin = 0.02f;
    float ghostKickDensityMax = 0.2f;

    float followKickProbability = 0.68f;
    float sustainProbability = 0.5f;
    float counterGapProbability = 0.2f;
    float phraseEdgeAnchorProbability = 0.58f;
    float skipIfDenseHatFxProbability = 0.12f;

    bool preferTriplets = true;
    bool preferFastBursts = true;
    bool allowOpenHatFrequently = true;
    bool allowCymbalTransitions = false;
    bool preferSparseSpace = false;

    Trap808Mode rhythmMode = Trap808Mode::SelectiveFollow;
    TrapPitchMotion pitchMotion = TrapPitchMotion::StaticWithAccentMoves;
    TrapSlideMode slideMode = TrapSlideMode::Moderate;
};

struct TrapStyleProfile
{
    juce::String name;
    TrapSubstyle substyle = TrapSubstyle::ATLClassic;

    float swingPercent = 52.0f;
    float kickDensityBias = 1.0f;
    float hatDensityBias = 1.0f;

    float hatFxIntensity = 0.6f;
    float openHatChance = 0.18f;
    float clapLayerChance = 0.65f;
    float ghostKickChance = 0.08f;
    float percChance = 0.16f;
    float sub808Activity = 0.85f;

    int kickVelocityMin = 92;
    int kickVelocityMax = 122;
    int snareVelocityMin = 98;
    int snareVelocityMax = 124;
    int hatVelocityMin = 56;
    int hatVelocityMax = 104;
    int sub808VelocityMin = 90;
    int sub808VelocityMax = 123;
};

const std::array<TrapStyleProfile, 6>& getTrapProfiles();
const TrapStyleProfile& getTrapProfile(int index);
TrapStyleSpec getTrapStyleSpec(TrapSubstyle substyle);
} // namespace bbg
