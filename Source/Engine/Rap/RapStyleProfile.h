#pragma once

#include <array>

#include <juce_core/juce_core.h>

namespace bbg
{
enum class RapSubstyle
{
    EastCoast = 0,
    WestCoast,
    DirtySouthClassic,
    GermanStreetRap,
    RussianRap,
    RnBRap,
    LofiRap,
    HardcoreRap
};

struct RapStyleProfile
{
    juce::String name;
    RapSubstyle substyle = RapSubstyle::EastCoast;

    float swingPercent = 53.0f; // 50..58 for rap pocket
    float kickDensityBias = 1.0f;
    float hatDensityBias = 1.0f;

    float openHatChance = 0.15f;
    float ghostSnareChance = 0.08f;
    float ghostKickChance = 0.08f;
    float clapLayerChance = 0.65f;
    float rideChance = 0.05f;
    float percChance = 0.10f;

    float relaxedTiming = 0.0f; // 0=tight, 1=looser pocket

    int kickVelocityMin = 90;
    int kickVelocityMax = 116;
    int snareVelocityMin = 96;
    int snareVelocityMax = 122;
    int hatVelocityMin = 58;
    int hatVelocityMax = 96;
    int ghostVelocityMin = 32;
    int ghostVelocityMax = 58;
    int percVelocityMin = 46;
    int percVelocityMax = 82;
};

const std::array<RapStyleProfile, 7>& getRapProfiles();
const RapStyleProfile& getRapProfile(int index);
} // namespace bbg
