#include "DrillStyleProfile.h"

namespace bbg
{
const std::array<DrillStyleProfile, 4>& getDrillProfiles()
{
    static const std::array<DrillStyleProfile, 4> profiles = {{
        { "UKDrill", DrillSubstyle::UKDrill, 51.0f, 1.06f, 1.04f, 0.84f, 0.14f, 0.52f, 0.05f, 0.12f, 0.92f, 94, 124, 102, 127, 56, 98, 92, 126 },
        { "BrooklynDrill", DrillSubstyle::BrooklynDrill, 51.0f, 1.12f, 1.06f, 0.9f, 0.14f, 0.58f, 0.06f, 0.14f, 0.94f, 96, 126, 106, 127, 58, 102, 94, 127 },
        { "NYDrill", DrillSubstyle::NYDrill, 50.8f, 0.9f, 0.92f, 0.66f, 0.12f, 0.50f, 0.05f, 0.10f, 0.86f, 94, 122, 102, 126, 54, 94, 92, 124 },
        { "DarkDrill", DrillSubstyle::DarkDrill, 50.5f, 0.84f, 0.86f, 0.82f, 0.10f, 0.48f, 0.04f, 0.08f, 0.90f, 94, 124, 104, 127, 50, 86, 94, 126 }
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
} // namespace bbg
