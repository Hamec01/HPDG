#include "TrapStyleProfile.h"

namespace bbg
{
const std::array<TrapStyleProfile, 6>& getTrapProfiles()
{
    static const std::array<TrapStyleProfile, 6> profiles = {{
        { "ATLClassic", TrapSubstyle::ATLClassic, 52.0f, 1.0f, 1.06f, 0.76f, 0.18f, 0.66f, 0.08f, 0.18f, 0.88f, 92, 122, 98, 124, 58, 104, 92, 123 },
        { "DarkTrap", TrapSubstyle::DarkTrap, 51.0f, 0.86f, 0.80f, 0.58f, 0.12f, 0.60f, 0.06f, 0.10f, 0.96f, 96, 124, 100, 126, 54, 92, 96, 124 },
        { "CloudTrap", TrapSubstyle::CloudTrap, 53.0f, 0.84f, 0.92f, 0.68f, 0.24f, 0.56f, 0.06f, 0.14f, 0.78f, 88, 112, 94, 116, 52, 92, 84, 112 },
        { "RageTrap", TrapSubstyle::RageTrap, 52.5f, 1.22f, 1.24f, 1.08f, 0.20f, 0.72f, 0.10f, 0.20f, 0.92f, 100, 127, 106, 127, 64, 118, 96, 126 },
        { "MemphisTrap", TrapSubstyle::MemphisTrap, 51.5f, 0.88f, 0.88f, 0.52f, 0.14f, 0.62f, 0.07f, 0.12f, 0.84f, 94, 122, 98, 124, 56, 98, 92, 122 },
        { "LuxuryTrap", TrapSubstyle::LuxuryTrap, 52.0f, 0.96f, 1.0f, 0.74f, 0.20f, 0.66f, 0.08f, 0.14f, 0.86f, 92, 118, 98, 122, 58, 104, 90, 120 }
    }};

    return profiles;
}

const TrapStyleProfile& getTrapProfile(int index)
{
    const auto& profiles = getTrapProfiles();
    if (index < 0 || index >= static_cast<int>(profiles.size()))
        return profiles.front();

    return profiles[static_cast<size_t>(index)];
}

TrapStyleSpec getTrapStyleSpec(TrapSubstyle substyle)
{
    TrapStyleSpec spec;

    switch (substyle)
    {
        case TrapSubstyle::ATLClassic:
            spec.kickDensityMin = 0.34f;
            spec.kickDensityMax = 0.68f;
            spec.hatDensityMin = 0.42f;
            spec.hatDensityMax = 0.78f;
            spec.subDensityMin = 0.24f;
            spec.subDensityMax = 0.62f;
            spec.hatFxDensityMin = 0.2f;
            spec.hatFxDensityMax = 0.56f;
            spec.ghostKickDensityMin = 0.03f;
            spec.ghostKickDensityMax = 0.16f;
            spec.followKickProbability = 0.72f;
            spec.sustainProbability = 0.5f;
            spec.counterGapProbability = 0.2f;
            spec.phraseEdgeAnchorProbability = 0.62f;
            spec.skipIfDenseHatFxProbability = 0.12f;
            spec.majorBurstPerBarMax = 2.4f;
            spec.burstWindowPrimaryBias = 1.1f;
            spec.burstWindowSecondaryBias = 0.64f;
            spec.preferTriplets = true;
            spec.preferFastBursts = true;
            spec.preferPhraseEdgeBursts = true;
            spec.preferKickReactionBursts = true;
            spec.allowOpenHatFrequently = true;
            spec.allowCymbalTransitions = false;
            spec.preferSparseSpace = false;
            spec.rhythmMode = Trap808Mode::SelectiveFollow;
            spec.pitchMotion = TrapPitchMotion::StaticWithAccentMoves;
            spec.slideMode = TrapSlideMode::Moderate;
            spec.hatDensityMode = TrapHatDensityMode::Medium;
            spec.hatFxMode = TrapHatFxMode::Medium;
            spec.rollPreference = TrapRollPreference::Balanced32and64;
            spec.velocityContourPreference = TrapVelocityContourPreference::MostlyRampUp;
            break;

        case TrapSubstyle::DarkTrap:
            spec.kickDensityMin = 0.24f;
            spec.kickDensityMax = 0.54f;
            spec.hatDensityMin = 0.26f;
            spec.hatDensityMax = 0.6f;
            spec.subDensityMin = 0.14f;
            spec.subDensityMax = 0.48f;
            spec.hatFxDensityMin = 0.12f;
            spec.hatFxDensityMax = 0.34f;
            spec.ghostKickDensityMin = 0.02f;
            spec.ghostKickDensityMax = 0.12f;
            spec.followKickProbability = 0.58f;
            spec.sustainProbability = 0.68f;
            spec.counterGapProbability = 0.1f;
            spec.phraseEdgeAnchorProbability = 0.72f;
            spec.skipIfDenseHatFxProbability = 0.26f;
            spec.majorBurstPerBarMax = 1.5f;
            spec.burstWindowPrimaryBias = 1.16f;
            spec.burstWindowSecondaryBias = 0.42f;
            spec.preferTriplets = true;
            spec.preferFastBursts = false;
            spec.preferPhraseEdgeBursts = true;
            spec.preferKickReactionBursts = false;
            spec.allowOpenHatFrequently = false;
            spec.allowCymbalTransitions = false;
            spec.preferSparseSpace = true;
            spec.rhythmMode = Trap808Mode::PhraseAnchored;
            spec.pitchMotion = TrapPitchMotion::VeryStatic;
            spec.slideMode = TrapSlideMode::Sparse;
            spec.hatDensityMode = TrapHatDensityMode::Sparse;
            spec.hatFxMode = TrapHatFxMode::Low;
            spec.rollPreference = TrapRollPreference::Mostly32;
            spec.velocityContourPreference = TrapVelocityContourPreference::MostlyRampDown;
            break;

        case TrapSubstyle::CloudTrap:
            spec.kickDensityMin = 0.28f;
            spec.kickDensityMax = 0.58f;
            spec.hatDensityMin = 0.32f;
            spec.hatDensityMax = 0.72f;
            spec.subDensityMin = 0.22f;
            spec.subDensityMax = 0.56f;
            spec.hatFxDensityMin = 0.18f;
            spec.hatFxDensityMax = 0.42f;
            spec.ghostKickDensityMin = 0.03f;
            spec.ghostKickDensityMax = 0.14f;
            spec.followKickProbability = 0.64f;
            spec.sustainProbability = 0.56f;
            spec.counterGapProbability = 0.16f;
            spec.phraseEdgeAnchorProbability = 0.58f;
            spec.skipIfDenseHatFxProbability = 0.18f;
            spec.majorBurstPerBarMax = 2.0f;
            spec.burstWindowPrimaryBias = 0.96f;
            spec.burstWindowSecondaryBias = 0.74f;
            spec.preferTriplets = true;
            spec.preferFastBursts = false;
            spec.preferPhraseEdgeBursts = true;
            spec.preferKickReactionBursts = false;
            spec.allowOpenHatFrequently = true;
            spec.allowCymbalTransitions = false;
            spec.preferSparseSpace = false;
            spec.rhythmMode = Trap808Mode::SelectiveFollow;
            spec.pitchMotion = TrapPitchMotion::ControlledMovement;
            spec.slideMode = TrapSlideMode::Moderate;
            spec.hatDensityMode = TrapHatDensityMode::Medium;
            spec.hatFxMode = TrapHatFxMode::Medium;
            spec.rollPreference = TrapRollPreference::Smooth32Triplet;
            spec.velocityContourPreference = TrapVelocityContourPreference::BalancedContours;
            break;

        case TrapSubstyle::RageTrap:
            spec.kickDensityMin = 0.42f;
            spec.kickDensityMax = 0.8f;
            spec.hatDensityMin = 0.58f;
            spec.hatDensityMax = 0.92f;
            spec.subDensityMin = 0.3f;
            spec.subDensityMax = 0.72f;
            spec.hatFxDensityMin = 0.34f;
            spec.hatFxDensityMax = 0.8f;
            spec.ghostKickDensityMin = 0.04f;
            spec.ghostKickDensityMax = 0.24f;
            spec.followKickProbability = 0.82f;
            spec.sustainProbability = 0.38f;
            spec.counterGapProbability = 0.3f;
            spec.phraseEdgeAnchorProbability = 0.56f;
            spec.skipIfDenseHatFxProbability = 0.08f;
            spec.majorBurstPerBarMax = 3.4f;
            spec.burstWindowPrimaryBias = 1.24f;
            spec.burstWindowSecondaryBias = 0.78f;
            spec.preferTriplets = true;
            spec.preferFastBursts = true;
            spec.preferPhraseEdgeBursts = true;
            spec.preferKickReactionBursts = true;
            spec.allowOpenHatFrequently = true;
            spec.allowCymbalTransitions = true;
            spec.preferSparseSpace = false;
            spec.rhythmMode = Trap808Mode::LayerMostKicks;
            spec.pitchMotion = TrapPitchMotion::EnergeticControlledMovement;
            spec.slideMode = TrapSlideMode::Aggressive;
            spec.hatDensityMode = TrapHatDensityMode::Dense;
            spec.hatFxMode = TrapHatFxMode::High;
            spec.rollPreference = TrapRollPreference::Aggressive32and64;
            spec.velocityContourPreference = TrapVelocityContourPreference::SharpAlternating;
            break;

        case TrapSubstyle::MemphisTrap:
            spec.kickDensityMin = 0.3f;
            spec.kickDensityMax = 0.62f;
            spec.hatDensityMin = 0.38f;
            spec.hatDensityMax = 0.74f;
            spec.subDensityMin = 0.22f;
            spec.subDensityMax = 0.58f;
            spec.hatFxDensityMin = 0.2f;
            spec.hatFxDensityMax = 0.5f;
            spec.ghostKickDensityMin = 0.03f;
            spec.ghostKickDensityMax = 0.16f;
            spec.followKickProbability = 0.68f;
            spec.sustainProbability = 0.52f;
            spec.counterGapProbability = 0.18f;
            spec.phraseEdgeAnchorProbability = 0.66f;
            spec.skipIfDenseHatFxProbability = 0.16f;
            spec.majorBurstPerBarMax = 2.2f;
            spec.burstWindowPrimaryBias = 1.12f;
            spec.burstWindowSecondaryBias = 0.62f;
            spec.preferTriplets = true;
            spec.preferFastBursts = true;
            spec.preferPhraseEdgeBursts = true;
            spec.preferKickReactionBursts = true;
            spec.allowOpenHatFrequently = true;
            spec.allowCymbalTransitions = false;
            spec.preferSparseSpace = false;
            spec.rhythmMode = Trap808Mode::PhraseAnchored;
            spec.pitchMotion = TrapPitchMotion::StaticWithAccentMoves;
            spec.slideMode = TrapSlideMode::Moderate;
            spec.hatDensityMode = TrapHatDensityMode::Medium;
            spec.hatFxMode = TrapHatFxMode::Medium;
            spec.rollPreference = TrapRollPreference::Balanced32and64;
            spec.velocityContourPreference = TrapVelocityContourPreference::SharpAlternating;
            break;

        case TrapSubstyle::LuxuryTrap:
            spec.kickDensityMin = 0.32f;
            spec.kickDensityMax = 0.6f;
            spec.hatDensityMin = 0.34f;
            spec.hatDensityMax = 0.68f;
            spec.subDensityMin = 0.22f;
            spec.subDensityMax = 0.56f;
            spec.hatFxDensityMin = 0.16f;
            spec.hatFxDensityMax = 0.42f;
            spec.ghostKickDensityMin = 0.02f;
            spec.ghostKickDensityMax = 0.12f;
            spec.followKickProbability = 0.66f;
            spec.sustainProbability = 0.56f;
            spec.counterGapProbability = 0.14f;
            spec.phraseEdgeAnchorProbability = 0.62f;
            spec.skipIfDenseHatFxProbability = 0.2f;
            spec.majorBurstPerBarMax = 1.8f;
            spec.burstWindowPrimaryBias = 1.0f;
            spec.burstWindowSecondaryBias = 0.56f;
            spec.preferTriplets = true;
            spec.preferFastBursts = false;
            spec.preferPhraseEdgeBursts = true;
            spec.preferKickReactionBursts = false;
            spec.allowOpenHatFrequently = true;
            spec.allowCymbalTransitions = true;
            spec.preferSparseSpace = false;
            spec.rhythmMode = Trap808Mode::SelectiveFollow;
            spec.pitchMotion = TrapPitchMotion::ControlledMovement;
            spec.slideMode = TrapSlideMode::Moderate;
            spec.hatDensityMode = TrapHatDensityMode::Medium;
            spec.hatFxMode = TrapHatFxMode::Low;
            spec.rollPreference = TrapRollPreference::Mostly32;
            spec.velocityContourPreference = TrapVelocityContourPreference::CleanCurated;
            break;
    }

    return spec;
}
} // namespace bbg
