#include "DrillStyleProfile.h"

namespace bbg
{
namespace
{
DrillHatBaseSpec makeUKDrillHatBaseSpec()
{
    DrillHatBaseSpec s;
    s.primaryMode = DrillHatBaseMode::TwoTwoOneSkippy;
    s.secondaryMode = DrillHatBaseMode::BrokenGarageLike;
    s.baseDensityMin = 0.34f;
    s.baseDensityMax = 0.52f;
    s.omissionProbability = 0.26f;
    s.tripletInsertProbability = 0.22f;
    s.extra16Probability = 0.12f;
    s.startOfBarBias = 0.10f;
    s.midBarBias = 0.08f;
    s.endOfBarBias = 0.20f;
    s.phraseEdgeBias = 0.26f;
    s.microTimingMin = -6.0f;
    s.microTimingMax = 7.0f;
    s.baseVelocityMin = 58.0f;
    s.baseVelocityMax = 84.0f;
    s.accentVelocityMin = 82.0f;
    s.accentVelocityMax = 96.0f;
    s.preferTwoTwoOne = true;
    s.preferTripletCarriers = true;
    s.preferSilence = false;
    return s;
}

DrillHatBaseSpec makeBrooklynDrillHatBaseSpec()
{
    DrillHatBaseSpec s;
    s.primaryMode = DrillHatBaseMode::DenseBrooklynCarrier;
    s.secondaryMode = DrillHatBaseMode::TwoTwoOneSkippy;
    s.baseDensityMin = 0.42f;
    s.baseDensityMax = 0.64f;
    s.omissionProbability = 0.18f;
    s.tripletInsertProbability = 0.16f;
    s.extra16Probability = 0.22f;
    s.startOfBarBias = 0.12f;
    s.midBarBias = 0.10f;
    s.endOfBarBias = 0.18f;
    s.phraseEdgeBias = 0.20f;
    s.microTimingMin = -4.0f;
    s.microTimingMax = 5.0f;
    s.baseVelocityMin = 62.0f;
    s.baseVelocityMax = 88.0f;
    s.accentVelocityMin = 86.0f;
    s.accentVelocityMax = 100.0f;
    s.preferTwoTwoOne = true;
    s.preferTripletCarriers = false;
    s.preferSilence = false;
    return s;
}

DrillHatBaseSpec makeNYDrillHatBaseSpec()
{
    DrillHatBaseSpec s;
    s.primaryMode = DrillHatBaseMode::Sparse16WithCuts;
    s.secondaryMode = DrillHatBaseMode::HalfTimeTripletCarrier;
    s.baseDensityMin = 0.36f;
    s.baseDensityMax = 0.56f;
    s.omissionProbability = 0.22f;
    s.tripletInsertProbability = 0.18f;
    s.extra16Probability = 0.16f;
    s.startOfBarBias = 0.10f;
    s.midBarBias = 0.09f;
    s.endOfBarBias = 0.18f;
    s.phraseEdgeBias = 0.22f;
    s.microTimingMin = -5.0f;
    s.microTimingMax = 6.0f;
    s.baseVelocityMin = 60.0f;
    s.baseVelocityMax = 86.0f;
    s.accentVelocityMin = 84.0f;
    s.accentVelocityMax = 98.0f;
    s.preferTwoTwoOne = false;
    s.preferTripletCarriers = true;
    s.preferSilence = false;
    return s;
}

DrillHatBaseSpec makeDarkDrillHatBaseSpec()
{
    DrillHatBaseSpec s;
    s.primaryMode = DrillHatBaseMode::DarkSparseCarrier;
    s.secondaryMode = DrillHatBaseMode::Sparse16WithCuts;
    s.baseDensityMin = 0.22f;
    s.baseDensityMax = 0.42f;
    s.omissionProbability = 0.34f;
    s.tripletInsertProbability = 0.10f;
    s.extra16Probability = 0.08f;
    s.startOfBarBias = 0.12f;
    s.midBarBias = 0.06f;
    s.endOfBarBias = 0.22f;
    s.phraseEdgeBias = 0.30f;
    s.microTimingMin = -6.0f;
    s.microTimingMax = 8.0f;
    s.baseVelocityMin = 56.0f;
    s.baseVelocityMax = 80.0f;
    s.accentVelocityMin = 80.0f;
    s.accentVelocityMax = 94.0f;
    s.preferTwoTwoOne = false;
    s.preferTripletCarriers = false;
    s.preferSilence = true;
    return s;
}

DrillHatFxStyleProfile makeUKDrillHatFxStyleProfile()
{
    DrillHatFxStyleProfile s;
    s.banks = {
        { DrillHatFxBank::ShortTripletCluster, 12 },
        { DrillHatFxBank::ExtendedTripletCluster, 4 },
        { DrillHatFxBank::SkippyBurst, 8 },
        { DrillHatFxBank::TresilloEdgeBurst, 7 },
        { DrillHatFxBank::PreSnareTriplet, 11 },
        { DrillHatFxBank::KickReactionSkips, 6 },
        { DrillHatFxBank::PhraseEdgeCluster, 9 },
        { DrillHatFxBank::DarkFadeBurst, 3 },
        { DrillHatFxBank::AngularJabBurst, 7 },
        { DrillHatFxBank::Tight32Burst, 2 }
    };
    s.maxTinyPerBar = 1;
    s.maxMediumPerBar = 1;
    s.maxMajorPerBar = 0;
    s.maxMajorPerTwoBars = 1;
    s.allowEndOfBar = true;
    s.allowEndOfHalfBar = false;
    s.allowPreSnare = true;
    s.allowKickReaction = false;
    s.allowPhraseEdge = true;
    s.allowTransitionLift = false;
    return s;
}

DrillHatFxStyleProfile makeBrooklynDrillHatFxStyleProfile()
{
    DrillHatFxStyleProfile s;
    s.banks = {
        { DrillHatFxBank::ShortTripletCluster, 8 },
        { DrillHatFxBank::ExtendedTripletCluster, 4 },
        { DrillHatFxBank::SkippyBurst, 10 },
        { DrillHatFxBank::TresilloEdgeBurst, 5 },
        { DrillHatFxBank::PreSnareTriplet, 9 },
        { DrillHatFxBank::KickReactionSkips, 9 },
        { DrillHatFxBank::PhraseEdgeCluster, 8 },
        { DrillHatFxBank::DarkFadeBurst, 2 },
        { DrillHatFxBank::AngularJabBurst, 5 },
        { DrillHatFxBank::Tight32Burst, 5 }
    };
    s.maxTinyPerBar = 1;
    s.maxMediumPerBar = 1;
    s.maxMajorPerBar = 1;
    s.maxMajorPerTwoBars = 1;
    s.allowEndOfBar = true;
    s.allowEndOfHalfBar = false;
    s.allowPreSnare = true;
    s.allowKickReaction = true;
    s.allowPhraseEdge = true;
    s.allowTransitionLift = false;
    return s;
}

DrillHatFxStyleProfile makeNYDrillHatFxStyleProfile()
{
    DrillHatFxStyleProfile s;
    s.banks = {
        { DrillHatFxBank::ShortTripletCluster, 16 },
        { DrillHatFxBank::ExtendedTripletCluster, 7 },
        { DrillHatFxBank::SkippyBurst, 12 },
        { DrillHatFxBank::TresilloEdgeBurst, 10 },
        { DrillHatFxBank::PreSnareTriplet, 14 },
        { DrillHatFxBank::KickReactionSkips, 14 },
        { DrillHatFxBank::PhraseEdgeCluster, 14 },
        { DrillHatFxBank::DarkFadeBurst, 4 },
        { DrillHatFxBank::AngularJabBurst, 8 },
        { DrillHatFxBank::Tight32Burst, 10 }
    };
    s.maxTinyPerBar = 2;
    s.maxMediumPerBar = 1;
    s.maxMajorPerBar = 1;
    s.maxMajorPerTwoBars = 1;
    s.allowEndOfBar = true;
    s.allowEndOfHalfBar = false;
    s.allowPreSnare = true;
    s.allowKickReaction = true;
    s.allowPhraseEdge = true;
    s.allowTransitionLift = false;
    return s;
}

DrillHatFxStyleProfile makeDarkDrillHatFxStyleProfile()
{
    DrillHatFxStyleProfile s;
    s.banks = {
        { DrillHatFxBank::ShortTripletCluster, 6 },
        { DrillHatFxBank::ExtendedTripletCluster, 2 },
        { DrillHatFxBank::SkippyBurst, 6 },
        { DrillHatFxBank::TresilloEdgeBurst, 12 },
        { DrillHatFxBank::PreSnareTriplet, 6 },
        { DrillHatFxBank::KickReactionSkips, 5 },
        { DrillHatFxBank::PhraseEdgeCluster, 16 },
        { DrillHatFxBank::DarkFadeBurst, 18 },
        { DrillHatFxBank::AngularJabBurst, 14 },
        { DrillHatFxBank::Tight32Burst, 2 }
    };
    s.maxTinyPerBar = 1;
    s.maxMediumPerBar = 1;
    s.maxMajorPerBar = 1;
    s.maxMajorPerTwoBars = 1;
    s.allowEndOfBar = true;
    s.allowEndOfHalfBar = false;
    s.allowPreSnare = false;
    s.allowKickReaction = false;
    s.allowPhraseEdge = true;
    s.allowTransitionLift = false;
    return s;
}

Drill808StyleSpec makeUKDrill808Spec()
{
    Drill808StyleSpec s;
    s.rhythmMode = Drill808RhythmMode::PhraseAnchored;
    s.pitchMotion = Drill808PitchMotion::ControlledMovement;
    s.slideMode = Drill808SlideMode::Moderate;
    s.minStartsPerBar = 1;
    s.maxStartsPerBar = 3;
    s.anchorFollowProbability = 0.78f;
    s.supportFollowProbability = 0.18f;
    s.edgeAnchorProbability = 0.56f;
    s.pickupBassProbability = 0.12f;
    s.restartProbability = 0.24f;
    s.longHoldProbability = 0.58f;
    s.phraseHoldProbability = 0.34f;
    s.rejectIfDenseTopProbability = 0.76f;
    s.rootWeight = 0.70f;
    s.octaveWeight = 0.14f;
    s.fifthWeight = 0.08f;
    s.flat7Weight = 0.05f;
    s.minor3Weight = 0.03f;
    s.maxPitchChangesPer2Bars = 2;
    s.maxSlidesPer2Bars = 2;
    s.maxMajorRestartsPer2Bars = 3;
    s.preferRootOnPhraseEdge = true;
    s.allowOctaveJumps = true;
    s.allowFlat7 = true;
    s.allowMinor3 = true;
    s.allowLongSlideAtPhraseEdge = true;
    return s;
}

Drill808StyleSpec makeBrooklynDrill808Spec()
{
    Drill808StyleSpec s;
    s.rhythmMode = Drill808RhythmMode::FrontalPressure;
    s.pitchMotion = Drill808PitchMotion::AggressiveControlledMovement;
    s.slideMode = Drill808SlideMode::Strong;
    s.minStartsPerBar = 2;
    s.maxStartsPerBar = 4;
    s.anchorFollowProbability = 0.84f;
    s.supportFollowProbability = 0.28f;
    s.edgeAnchorProbability = 0.44f;
    s.pickupBassProbability = 0.18f;
    s.restartProbability = 0.38f;
    s.longHoldProbability = 0.42f;
    s.phraseHoldProbability = 0.20f;
    s.rejectIfDenseTopProbability = 0.62f;
    s.rootWeight = 0.64f;
    s.octaveWeight = 0.18f;
    s.fifthWeight = 0.10f;
    s.flat7Weight = 0.05f;
    s.minor3Weight = 0.03f;
    s.maxPitchChangesPer2Bars = 3;
    s.maxSlidesPer2Bars = 3;
    s.maxMajorRestartsPer2Bars = 5;
    s.preferRootOnPhraseEdge = false;
    s.allowOctaveJumps = true;
    s.allowFlat7 = true;
    s.allowMinor3 = true;
    s.allowLongSlideAtPhraseEdge = false;
    return s;
}

Drill808StyleSpec makeNYDrill808Spec()
{
    Drill808StyleSpec s;
    s.rhythmMode = Drill808RhythmMode::BrokenFollow;
    s.pitchMotion = Drill808PitchMotion::ControlledMovement;
    s.slideMode = Drill808SlideMode::Moderate;
    s.minStartsPerBar = 1;
    s.maxStartsPerBar = 3;
    s.anchorFollowProbability = 0.80f;
    s.supportFollowProbability = 0.22f;
    s.edgeAnchorProbability = 0.48f;
    s.pickupBassProbability = 0.14f;
    s.restartProbability = 0.28f;
    s.longHoldProbability = 0.50f;
    s.phraseHoldProbability = 0.26f;
    s.rejectIfDenseTopProbability = 0.70f;
    s.rootWeight = 0.68f;
    s.octaveWeight = 0.16f;
    s.fifthWeight = 0.10f;
    s.flat7Weight = 0.04f;
    s.minor3Weight = 0.02f;
    s.maxPitchChangesPer2Bars = 3;
    s.maxSlidesPer2Bars = 2;
    s.maxMajorRestartsPer2Bars = 4;
    s.preferRootOnPhraseEdge = true;
    s.allowOctaveJumps = true;
    s.allowFlat7 = true;
    s.allowMinor3 = false;
    s.allowLongSlideAtPhraseEdge = false;
    return s;
}

Drill808StyleSpec makeDarkDrill808Spec()
{
    Drill808StyleSpec s;
    s.rhythmMode = Drill808RhythmMode::SparseThreat;
    s.pitchMotion = Drill808PitchMotion::VeryStatic;
    s.slideMode = Drill808SlideMode::Sparse;
    s.minStartsPerBar = 1;
    s.maxStartsPerBar = 2;
    s.anchorFollowProbability = 0.72f;
    s.supportFollowProbability = 0.10f;
    s.edgeAnchorProbability = 0.62f;
    s.pickupBassProbability = 0.06f;
    s.restartProbability = 0.14f;
    s.longHoldProbability = 0.70f;
    s.phraseHoldProbability = 0.46f;
    s.rejectIfDenseTopProbability = 0.84f;
    s.rootWeight = 0.76f;
    s.octaveWeight = 0.14f;
    s.fifthWeight = 0.05f;
    s.flat7Weight = 0.03f;
    s.minor3Weight = 0.02f;
    s.maxPitchChangesPer2Bars = 1;
    s.maxSlidesPer2Bars = 1;
    s.maxMajorRestartsPer2Bars = 2;
    s.preferRootOnPhraseEdge = true;
    s.allowOctaveJumps = true;
    s.allowFlat7 = true;
    s.allowMinor3 = true;
    s.allowLongSlideAtPhraseEdge = true;
    return s;
}

DrillStyleProfile makeUKDrillProfile()
{
    DrillStyleProfile p;
    p.name = "UKDrill";
    p.substyle = DrillSubstyle::UKDrill;
    p.baseHat = 0.38;
    p.baseFill = 0.14;
    p.baseRoll = 0.11;
    p.baseAccent = 0.09;
    p.lambdaDensity = 1.08;
    p.minHatsPerBar = 5;
    p.maxHatsPerBar = 8;
    p.minRollsPer4Bars = 1;
    p.maxRollsPer4Bars = 3;
    p.minAccentsPerBar = 1;
    p.maxAccentsPerBar = 1;
    p.maxRollSegmentsPerBar = 1;
    p.maxAccentsPerBarHard = 1;
    p.rollCooldownTicks = 560;
    p.accentCooldownTicks = 360;
    p.minDistanceRollTicks = 280;
    p.minDistanceAccentTicks = 240;
    p.maxRollSegmentsPer2Bars = 1;
    p.maxConsecutiveRollBars = 1;
    p.preSnareBoostRoll = 1.38;
    p.preSnareBoostAccent = 1.08;
    p.endBarBoostRoll = 1.14;
    p.allowMidBarRoll = false;
    p.preferPreSnareRoll = true;
    p.preferEndBarRoll = true;
    p.tripletSubpulseWeight = 0.74;
    p.straight32SubpulseWeight = 0.86;
    p.phraseComplexityCurve = { 0.98, 0.94, 1.12, 1.28 };

    p.swingPercent = 51.0f;
    p.kickDensityBias = 1.06f;
    p.hatDensityBias = 0.90f;
    p.cleanHatMode = true;
    p.hatFxIntensity = 0.56f;
    p.openHatChance = 0.14f;
    p.clapLayerChance = 0.52f;
    p.ghostKickChance = 0.05f;
    p.percChance = 0.12f;
    p.sub808Activity = 0.92f;
    p.kickVelocityMin = 94;
    p.kickVelocityMax = 124;
    p.snareVelocityMin = 102;
    p.snareVelocityMax = 127;
    p.hatVelocityMin = 56;
    p.hatVelocityMax = 92;
    p.sub808VelocityMin = 92;
    p.sub808VelocityMax = 126;
    return p;
}

DrillStyleProfile makeBrooklynDrillProfile()
{
    DrillStyleProfile p;
    p.name = "BrooklynDrill";
    p.substyle = DrillSubstyle::BrooklynDrill;
    p.baseHat = 0.37;
    p.baseFill = 0.13;
    p.baseRoll = 0.11;
    p.baseAccent = 0.10;
    p.lambdaDensity = 1.04;
    p.minHatsPerBar = 5;
    p.maxHatsPerBar = 8;
    p.minRollsPer4Bars = 1;
    p.maxRollsPer4Bars = 2;
    p.minAccentsPerBar = 1;
    p.maxAccentsPerBar = 1;
    p.maxRollSegmentsPerBar = 1;
    p.maxAccentsPerBarHard = 1;
    p.rollCooldownTicks = 520;
    p.accentCooldownTicks = 340;
    p.minDistanceRollTicks = 250;
    p.minDistanceAccentTicks = 220;
    p.maxRollSegmentsPer2Bars = 1;
    p.maxConsecutiveRollBars = 1;
    p.preSnareBoostRoll = 1.36;
    p.preSnareBoostAccent = 1.08;
    p.endBarBoostRoll = 1.12;
    p.allowMidBarRoll = false;
    p.preferPreSnareRoll = true;
    p.preferEndBarRoll = true;
    p.tripletSubpulseWeight = 0.78;
    p.straight32SubpulseWeight = 0.80;
    p.phraseComplexityCurve = { 1.02, 0.98, 1.14, 1.22 };

    p.swingPercent = 51.0f;
    p.kickDensityBias = 1.12f;
    p.hatDensityBias = 0.90f;
    p.cleanHatMode = true;
    p.hatFxIntensity = 0.60f;
    p.openHatChance = 0.14f;
    p.clapLayerChance = 0.46f;
    p.ghostKickChance = 0.06f;
    p.percChance = 0.08f;
    p.sub808Activity = 0.94f;
    p.kickVelocityMin = 96;
    p.kickVelocityMax = 126;
    p.snareVelocityMin = 106;
    p.snareVelocityMax = 127;
    p.hatVelocityMin = 58;
    p.hatVelocityMax = 94;
    p.sub808VelocityMin = 94;
    p.sub808VelocityMax = 127;
    return p;
}

DrillStyleProfile makeNYDrillProfile()
{
    DrillStyleProfile p;
    p.name = "NYDrill";
    p.substyle = DrillSubstyle::NYDrill;
    p.baseHat = 0.40;
    p.baseFill = 0.17;
    p.baseRoll = 0.14;
    p.baseAccent = 0.13;
    p.lambdaDensity = 0.98;
    p.minHatsPerBar = 6;
    p.maxHatsPerBar = 9;
    p.minRollsPer4Bars = 1;
    p.maxRollsPer4Bars = 2;
    p.minAccentsPerBar = 1;
    p.maxAccentsPerBar = 2;
    p.maxRollSegmentsPerBar = 1;
    p.maxAccentsPerBarHard = 2;
    p.rollCooldownTicks = 470;
    p.accentCooldownTicks = 250;
    p.minDistanceRollTicks = 220;
    p.minDistanceAccentTicks = 165;
    p.maxRollSegmentsPer2Bars = 2;
    p.maxConsecutiveRollBars = 2;
    p.preSnareBoostRoll = 1.54;
    p.preSnareBoostAccent = 1.22;
    p.endBarBoostRoll = 1.18;
    p.allowMidBarRoll = false;
    p.preferPreSnareRoll = true;
    p.preferEndBarRoll = true;
    p.tripletSubpulseWeight = 0.82;
    p.straight32SubpulseWeight = 0.74;
    p.phraseComplexityCurve = { 0.96, 0.92, 1.08, 1.24 };

    p.swingPercent = 50.8f;
    p.kickDensityBias = 0.90f;
    p.hatDensityBias = 0.92f;
    p.hatFxIntensity = 0.66f;
    p.openHatChance = 0.12f;
    p.clapLayerChance = 0.50f;
    p.ghostKickChance = 0.05f;
    p.percChance = 0.10f;
    p.sub808Activity = 0.86f;
    p.kickVelocityMin = 94;
    p.kickVelocityMax = 122;
    p.snareVelocityMin = 102;
    p.snareVelocityMax = 126;
    p.hatVelocityMin = 54;
    p.hatVelocityMax = 94;
    p.sub808VelocityMin = 92;
    p.sub808VelocityMax = 124;
    return p;
}

DrillStyleProfile makeDarkDrillProfile()
{
    DrillStyleProfile p;
    p.name = "DarkDrill";
    p.substyle = DrillSubstyle::DarkDrill;
    p.baseHat = 0.36;
    p.baseFill = 0.12;
    p.baseRoll = 0.10;
    p.baseAccent = 0.10;
    p.lambdaDensity = 1.06;
    p.minHatsPerBar = 5;
    p.maxHatsPerBar = 8;
    p.minRollsPer4Bars = 1;
    p.maxRollsPer4Bars = 2;
    p.minAccentsPerBar = 0;
    p.maxAccentsPerBar = 2;
    p.maxRollSegmentsPerBar = 1;
    p.maxAccentsPerBarHard = 2;
    p.rollCooldownTicks = 560;
    p.accentCooldownTicks = 300;
    p.minDistanceRollTicks = 260;
    p.minDistanceAccentTicks = 180;
    p.maxRollSegmentsPer2Bars = 1;
    p.maxConsecutiveRollBars = 1;
    p.preSnareBoostRoll = 1.36;
    p.preSnareBoostAccent = 1.10;
    p.endBarBoostRoll = 1.24;
    p.allowMidBarRoll = false;
    p.preferPreSnareRoll = true;
    p.preferEndBarRoll = true;
    p.tripletSubpulseWeight = 0.76;
    p.straight32SubpulseWeight = 0.70;
    p.phraseComplexityCurve = { 0.90, 0.84, 0.98, 1.22 };

    p.swingPercent = 50.5f;
    p.kickDensityBias = 0.84f;
    p.hatDensityBias = 0.86f;
    p.hatFxIntensity = 0.82f;
    p.openHatChance = 0.10f;
    p.clapLayerChance = 0.48f;
    p.ghostKickChance = 0.04f;
    p.percChance = 0.08f;
    p.sub808Activity = 0.90f;
    p.kickVelocityMin = 94;
    p.kickVelocityMax = 124;
    p.snareVelocityMin = 104;
    p.snareVelocityMax = 127;
    p.hatVelocityMin = 50;
    p.hatVelocityMax = 86;
    p.sub808VelocityMin = 94;
    p.sub808VelocityMax = 126;
    return p;
}
}

const std::array<DrillStyleProfile, 4>& getDrillProfiles()
{
    static const std::array<DrillStyleProfile, 4> profiles = {{
        makeUKDrillProfile(),
        makeBrooklynDrillProfile(),
        makeNYDrillProfile(),
        makeDarkDrillProfile()
    }};

    return profiles;
}

const DrillStyleProfile& getDrillProfile(int index)
{
    const auto& profiles = getDrillProfiles();
    if (index < 0 || index >= static_cast<int>(profiles.size()))
        return profiles.front();

    return profiles[static_cast<size_t>(index)];
}

DrillStyleSpec getDrillStyleSpec(DrillSubstyle substyle)
{
    DrillStyleSpec spec;

    switch (substyle)
    {
        case DrillSubstyle::UKDrill:
            spec.kickDensityMin = 0.26f;
            spec.kickDensityMax = 0.58f;
            spec.subDensityMin = 0.14f;
            spec.subDensityMax = 0.48f;
            spec.hatFxDensityMin = 0.12f;
            spec.hatFxDensityMax = 0.34f;
            spec.ghostKickDensityMin = 0.02f;
            spec.ghostKickDensityMax = 0.12f;
            spec.followKickProbability = 0.56f;
            spec.sustainProbability = 0.64f;
            spec.counterGapProbability = 0.14f;
            spec.phraseEdgeAnchorProbability = 0.68f;
            spec.skipIfDenseHatFxProbability = 0.24f;
            spec.maxMajorEventsPerBar = 2;
            spec.maxMajorEventsPer2Bars = 3;
            spec.denseTopHitsThresholdPerBar = 15;
            spec.preferTriplets = true;
            spec.preferTresilloLikeBursts = true;
            spec.allowOpenHatFrequently = false;
            spec.allowCymbalTransitions = false;
            spec.preferSparseSpace = true;
            spec.rhythmMode = Drill808Mode::PhraseAnchored;
            spec.pitchMotion = DrillPitchMotion::VeryStatic;
            spec.slideMode = DrillSlideMode::Sparse;
            break;

        case DrillSubstyle::BrooklynDrill:
            spec.kickDensityMin = 0.34f;
            spec.kickDensityMax = 0.74f;
            spec.subDensityMin = 0.24f;
            spec.subDensityMax = 0.66f;
            spec.hatFxDensityMin = 0.22f;
            spec.hatFxDensityMax = 0.56f;
            spec.ghostKickDensityMin = 0.04f;
            spec.ghostKickDensityMax = 0.2f;
            spec.followKickProbability = 0.74f;
            spec.sustainProbability = 0.42f;
            spec.counterGapProbability = 0.26f;
            spec.phraseEdgeAnchorProbability = 0.58f;
            spec.skipIfDenseHatFxProbability = 0.1f;
            spec.maxMajorEventsPerBar = 3;
            spec.maxMajorEventsPer2Bars = 5;
            spec.denseTopHitsThresholdPerBar = 18;
            spec.preferTriplets = true;
            spec.preferTresilloLikeBursts = true;
            spec.allowOpenHatFrequently = true;
            spec.allowCymbalTransitions = true;
            spec.preferSparseSpace = false;
            spec.rhythmMode = Drill808Mode::SelectiveFollow;
            spec.pitchMotion = DrillPitchMotion::AggressiveControlledMovement;
            spec.slideMode = DrillSlideMode::Aggressive;
            break;

        case DrillSubstyle::NYDrill:
            spec.kickDensityMin = 0.3f;
            spec.kickDensityMax = 0.66f;
            spec.subDensityMin = 0.2f;
            spec.subDensityMax = 0.6f;
            spec.hatFxDensityMin = 0.16f;
            spec.hatFxDensityMax = 0.44f;
            spec.ghostKickDensityMin = 0.03f;
            spec.ghostKickDensityMax = 0.16f;
            spec.followKickProbability = 0.64f;
            spec.sustainProbability = 0.5f;
            spec.counterGapProbability = 0.2f;
            spec.phraseEdgeAnchorProbability = 0.62f;
            spec.skipIfDenseHatFxProbability = 0.14f;
            spec.maxMajorEventsPerBar = 3;
            spec.maxMajorEventsPer2Bars = 4;
            spec.denseTopHitsThresholdPerBar = 17;
            spec.preferTriplets = true;
            spec.preferTresilloLikeBursts = true;
            spec.allowOpenHatFrequently = true;
            spec.allowCymbalTransitions = false;
            spec.preferSparseSpace = false;
            spec.rhythmMode = Drill808Mode::SelectiveFollow;
            spec.pitchMotion = DrillPitchMotion::ControlledMovement;
            spec.slideMode = DrillSlideMode::Moderate;
            break;

        case DrillSubstyle::DarkDrill:
            spec.kickDensityMin = 0.2f;
            spec.kickDensityMax = 0.5f;
            spec.subDensityMin = 0.12f;
            spec.subDensityMax = 0.42f;
            spec.hatFxDensityMin = 0.08f;
            spec.hatFxDensityMax = 0.26f;
            spec.ghostKickDensityMin = 0.01f;
            spec.ghostKickDensityMax = 0.09f;
            spec.followKickProbability = 0.52f;
            spec.sustainProbability = 0.7f;
            spec.counterGapProbability = 0.08f;
            spec.phraseEdgeAnchorProbability = 0.72f;
            spec.skipIfDenseHatFxProbability = 0.28f;
            spec.maxMajorEventsPerBar = 2;
            spec.maxMajorEventsPer2Bars = 3;
            spec.denseTopHitsThresholdPerBar = 14;
            spec.preferTriplets = true;
            spec.preferTresilloLikeBursts = false;
            spec.allowOpenHatFrequently = false;
            spec.allowCymbalTransitions = false;
            spec.preferSparseSpace = true;
            spec.rhythmMode = Drill808Mode::SparseCounter;
            spec.pitchMotion = DrillPitchMotion::VeryStatic;
            spec.slideMode = DrillSlideMode::Sparse;
            break;
    }

    return spec;
}

DrillHatBaseSpec getDrillHatBaseSpec(DrillSubstyle substyle)
{
    switch (substyle)
    {
        case DrillSubstyle::UKDrill: return makeUKDrillHatBaseSpec();
        case DrillSubstyle::BrooklynDrill: return makeBrooklynDrillHatBaseSpec();
        case DrillSubstyle::NYDrill: return makeNYDrillHatBaseSpec();
        case DrillSubstyle::DarkDrill: return makeDarkDrillHatBaseSpec();
    }

    return makeUKDrillHatBaseSpec();
}

DrillHatFxStyleProfile getDrillHatFxStyleProfile(DrillSubstyle substyle)
{
    switch (substyle)
    {
        case DrillSubstyle::UKDrill: return makeUKDrillHatFxStyleProfile();
        case DrillSubstyle::BrooklynDrill: return makeBrooklynDrillHatFxStyleProfile();
        case DrillSubstyle::NYDrill: return makeNYDrillHatFxStyleProfile();
        case DrillSubstyle::DarkDrill: return makeDarkDrillHatFxStyleProfile();
    }

    return makeUKDrillHatFxStyleProfile();
}

Drill808StyleSpec getDrill808StyleSpec(DrillSubstyle substyle)
{
    switch (substyle)
    {
        case DrillSubstyle::UKDrill: return makeUKDrill808Spec();
        case DrillSubstyle::BrooklynDrill: return makeBrooklynDrill808Spec();
        case DrillSubstyle::NYDrill: return makeNYDrill808Spec();
        case DrillSubstyle::DarkDrill: return makeDarkDrill808Spec();
    }

    return makeUKDrill808Spec();
}
} // namespace bbg
