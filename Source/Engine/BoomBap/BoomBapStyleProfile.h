#pragma once

#include <array>

#include <juce_core/juce_core.h>
#include "BoomBapTiming.h"

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

    struct Pocket
    {
        BoomBapTiming::TimingDistribution kickAnchor { 0.0f, 1.5f, -3.0f, 3.0f };
        BoomBapTiming::TimingDistribution kickSyncopated { 0.0f, 2.5f, -6.0f, 6.0f };
        BoomBapTiming::TimingDistribution kickPickup { -1.0f, 3.0f, -8.0f, 6.0f };
        BoomBapTiming::TimingDistribution snareBeat2 { 10.0f, 2.5f, 4.0f, 16.0f };
        BoomBapTiming::TimingDistribution snareBeat4 { 8.0f, 2.5f, 3.0f, 14.0f };
        BoomBapTiming::TimingDistribution ghostBeforeSnare { -3.0f, 4.0f, -12.0f, 7.0f };
        BoomBapTiming::TimingDistribution ghostAfterSnare { 3.0f, 4.0f, -7.0f, 12.0f };
        BoomBapTiming::TimingDistribution hatStrong { 0.0f, 1.5f, -4.0f, 4.0f };
        BoomBapTiming::TimingDistribution hatWeak { 1.0f, 1.5f, -3.0f, 5.0f };
        BoomBapTiming::TimingDistribution openHat { 1.0f, 2.5f, -5.0f, 7.0f };
        BoomBapTiming::TimingDistribution ride { 0.0f, 2.0f, -5.0f, 5.0f };
        BoomBapTiming::TimingDistribution percussion { 0.0f, 3.0f, -8.0f, 8.0f };
        float humanJitterSigmaPPQ = 1.25f;
        float humanJitterLimitPPQ = 4.0f;
    } pocket;

    struct SimilarityTargets
    {
        float confirmationLow = 0.75f, confirmationHigh = 0.95f;
        float developmentLow = 0.60f, developmentHigh = 0.85f;
        float turnaroundLow = 0.50f, turnaroundHigh = 0.80f;
        float tolerance = 0.16f;
        std::array<float, 5> familyWeights { 1.30f, 2.0f, 0.65f, 0.30f, 0.25f }; // kick/snare/hats/ghost/perc
    } similarity;

    struct ScoreWeights
    {
        float backbeat = 1.4f, metricSupport = 1.2f, groove = 1.3f, breakResemblance = 1.0f;
        float microPlausibility = 0.8f, negativeSpace = 0.9f, lowEnd = 0.8f;
        float variation = 0.9f, density = 0.8f, velocity = 0.7f;
        float trapLeak = 1.2f, earlySnare = 1.0f, overHumanize = 0.8f;
        float sub808 = 0.9f, conflict = 1.2f, spam = 1.0f;
    } scorer;
};

const std::array<BoomBapStyleProfile, 6>& getBoomBapProfiles();
const BoomBapStyleProfile& getBoomBapProfile(int index);
const BoomBapStyleProfile& getBoomBapProfile(BoomBapSubstyle substyle);
int getSubstyleMask(BoomBapSubstyle substyle);
float interpretedReferenceTempo(const BoomBapStyleProfile& style);
} // namespace bbg
