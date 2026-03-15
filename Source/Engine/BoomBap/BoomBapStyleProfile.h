#pragma once

#include <array>

#include <juce_core/juce_core.h>

namespace bbg
{
enum class BoomBapSubstyle
{
    Classic = 0,
    Dusty,
    Jazzy,
    Aggressive,
    LaidBack,
    BoomBapGold,
    RussianUnderground,
    LofiRap
};

enum class CarrierMode
{
    Hat = 0,
    Ride,
    Hybrid
};

struct BoomBapStyleProfile
{
    // Substyle data stays declarative so later genres can reuse the same decision model.
    juce::String name;
    BoomBapSubstyle substyle = BoomBapSubstyle::Classic;

    float swingPercent = 56.0f;
    float kickDensityBias = 1.0f;
    float hatDensityBias = 1.0f;
    float ghostDensityBias = 1.0f;
    float openHatDensityBias = 1.0f;
    float percDensityBias = 1.0f;

    float ghostKickChance = 0.2f;
    float openHatChance = 0.2f;
    float percChance = 0.25f;
    float clapLayerChance = 1.0f;
    float ghostSnareChance = 0.08f;

    float barVariationAmount = 0.25f;

    int grooveReferenceBpm = 96;
    float halfTimeReferenceBias = 0.0f;
    float hatCarrierPreference = 0.7f;
    float rideCarrierPreference = 0.1f;
    float hybridCarrierPreference = 0.2f;

    int snareLateBeat2Ticks = 16;
    int snareLateBeat4Ticks = 10;
    int clapLateTicks = 20;
    int kickTimingMaxTicks = 8;
    int hatTimingMaxTicks = 6;
    int ghostTimingMaxTicks = 16;

    int kickVelocityMin = 84;
    int kickVelocityMax = 110;
    int snareVelocityMin = 88;
    int snareVelocityMax = 112;
    int clapVelocityMin = 82;
    int clapVelocityMax = 106;
    int hatVelocityMin = 56;
    int hatVelocityMax = 100;
    int openHatVelocityMin = 78;
    int openHatVelocityMax = 108;
    int percVelocityMin = 45;
    int percVelocityMax = 85;
    int ghostVelocityMin = 34;
    int ghostVelocityMax = 58;

    float blueprintEnergyBias = 0.0f;
    float blueprintSwingBias = 0.0f;

    float laneGhostKickActivity = 0.56f;
    float laneOpenHatActivity = 0.48f;
    float lanePercActivity = 0.50f;
    float laneRideActivity = 0.26f;
    float laneClapActivity = 0.92f;
    float laneCymbalActivity = 0.24f;
};

const std::array<BoomBapStyleProfile, 8>& getBoomBapProfiles();
const BoomBapStyleProfile& getBoomBapProfile(int index);
int getSubstyleMask(BoomBapSubstyle substyle);
float interpretedReferenceTempo(const BoomBapStyleProfile& style);
} // namespace bbg
