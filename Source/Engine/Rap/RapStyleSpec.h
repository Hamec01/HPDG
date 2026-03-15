#pragma once

#include <algorithm>

#include "RapStyleProfile.h"

namespace bbg
{
struct RapStyleSpec
{
    float swingMin = 0.50f;
    float swingMax = 0.58f;

    float timingAmountMin = 0.24f;
    float timingAmountMax = 0.40f;

    float humanizeAmountMin = 0.20f;
    float humanizeAmountMax = 0.40f;

    float velocityAmountMin = 0.28f;
    float velocityAmountMax = 0.50f;

    float densityAmountMin = 0.28f;
    float densityAmountMax = 0.58f;

    float kickDensityMin = 0.30f;
    float kickDensityMax = 0.58f;

    float hatDensityMin = 0.34f;
    float hatDensityMax = 0.66f;

    float ghostKickDensityMin = 0.04f;
    float ghostKickDensityMax = 0.16f;

    float clapGhostDensityMin = 0.04f;
    float clapGhostDensityMax = 0.20f;

    float openHatDensityMin = 0.02f;
    float openHatDensityMax = 0.14f;

    float percDensityMin = 0.02f;
    float percDensityMax = 0.16f;

    float sub808DensityMin = 0.00f;
    float sub808DensityMax = 0.18f;

    bool hatFxEnabledByDefault = false;
    bool openHatUseful = false;
    bool percUseful = false;
    bool rideRare = true;
    bool cymbalRare = true;

    bool sub808EnabledByDefault = false;
    bool sub808AllowSlides = false;
    bool sub808FollowKickMoreOften = false;

    int phraseBarsPreferred = 2;
};

inline float rapMix(float minValue, float maxValue, float amount)
{
    return minValue + (maxValue - minValue) * std::clamp(amount, 0.0f, 1.0f);
}

inline RapStyleSpec getRapStyleSpec(RapSubstyle substyle)
{
    RapStyleSpec s;

    switch (substyle)
    {
        case RapSubstyle::EastCoast:
            s.swingMin = 0.50f; s.swingMax = 0.56f;
            s.timingAmountMin = 0.24f; s.timingAmountMax = 0.36f;
            s.humanizeAmountMin = 0.20f; s.humanizeAmountMax = 0.34f;
            s.kickDensityMin = 0.34f; s.kickDensityMax = 0.56f;
            s.hatDensityMin = 0.40f; s.hatDensityMax = 0.62f;
            s.openHatDensityMin = 0.01f; s.openHatDensityMax = 0.08f;
            s.percDensityMin = 0.01f; s.percDensityMax = 0.10f;
            s.sub808DensityMin = 0.00f; s.sub808DensityMax = 0.10f;
            s.openHatUseful = false;
            s.percUseful = true;
            s.sub808EnabledByDefault = false;
            break;

        case RapSubstyle::WestCoast:
            s.swingMin = 0.52f; s.swingMax = 0.58f;
            s.timingAmountMin = 0.26f; s.timingAmountMax = 0.40f;
            s.humanizeAmountMin = 0.24f; s.humanizeAmountMax = 0.38f;
            s.kickDensityMin = 0.32f; s.kickDensityMax = 0.54f;
            s.hatDensityMin = 0.40f; s.hatDensityMax = 0.62f;
            s.openHatDensityMin = 0.04f; s.openHatDensityMax = 0.16f;
            s.percDensityMin = 0.04f; s.percDensityMax = 0.14f;
            s.sub808DensityMin = 0.04f; s.sub808DensityMax = 0.18f;
            s.openHatUseful = true;
            s.percUseful = true;
            s.sub808EnabledByDefault = false;
            break;

        case RapSubstyle::DirtySouthClassic:
            s.swingMin = 0.50f; s.swingMax = 0.55f;
            s.timingAmountMin = 0.22f; s.timingAmountMax = 0.34f;
            s.humanizeAmountMin = 0.18f; s.humanizeAmountMax = 0.32f;
            s.kickDensityMin = 0.44f; s.kickDensityMax = 0.68f;
            s.hatDensityMin = 0.44f; s.hatDensityMax = 0.66f;
            s.openHatDensityMin = 0.06f; s.openHatDensityMax = 0.20f;
            s.percDensityMin = 0.04f; s.percDensityMax = 0.16f;
            s.sub808DensityMin = 0.14f; s.sub808DensityMax = 0.34f;
            s.openHatUseful = true;
            s.percUseful = true;
            s.sub808EnabledByDefault = true;
            s.sub808FollowKickMoreOften = true;
            break;

        case RapSubstyle::GermanStreetRap:
            s.swingMin = 0.50f; s.swingMax = 0.54f;
            s.timingAmountMin = 0.16f; s.timingAmountMax = 0.28f;
            s.humanizeAmountMin = 0.14f; s.humanizeAmountMax = 0.24f;
            s.kickDensityMin = 0.36f; s.kickDensityMax = 0.56f;
            s.hatDensityMin = 0.34f; s.hatDensityMax = 0.54f;
            s.openHatDensityMin = 0.01f; s.openHatDensityMax = 0.08f;
            s.percDensityMin = 0.00f; s.percDensityMax = 0.06f;
            s.sub808DensityMin = 0.06f; s.sub808DensityMax = 0.20f;
            s.openHatUseful = false;
            s.percUseful = false;
            s.sub808EnabledByDefault = false;
            break;

        case RapSubstyle::RussianRap:
            s.swingMin = 0.50f; s.swingMax = 0.56f;
            s.timingAmountMin = 0.22f; s.timingAmountMax = 0.34f;
            s.humanizeAmountMin = 0.18f; s.humanizeAmountMax = 0.30f;
            s.kickDensityMin = 0.30f; s.kickDensityMax = 0.52f;
            s.hatDensityMin = 0.36f; s.hatDensityMax = 0.58f;
            s.openHatDensityMin = 0.01f; s.openHatDensityMax = 0.10f;
            s.percDensityMin = 0.02f; s.percDensityMax = 0.10f;
            s.sub808DensityMin = 0.04f; s.sub808DensityMax = 0.18f;
            s.openHatUseful = false;
            s.percUseful = true;
            s.sub808EnabledByDefault = false;
            break;

        case RapSubstyle::RnBRap:
            s.swingMin = 0.52f; s.swingMax = 0.58f;
            s.timingAmountMin = 0.28f; s.timingAmountMax = 0.42f;
            s.humanizeAmountMin = 0.26f; s.humanizeAmountMax = 0.40f;
            s.kickDensityMin = 0.28f; s.kickDensityMax = 0.46f;
            s.hatDensityMin = 0.40f; s.hatDensityMax = 0.60f;
            s.openHatDensityMin = 0.06f; s.openHatDensityMax = 0.22f;
            s.percDensityMin = 0.06f; s.percDensityMax = 0.20f;
            s.sub808DensityMin = 0.06f; s.sub808DensityMax = 0.22f;
            s.hatFxEnabledByDefault = true;
            s.openHatUseful = true;
            s.percUseful = true;
            s.sub808EnabledByDefault = true;
            s.sub808AllowSlides = true;
            break;

        case RapSubstyle::HardcoreRap:
            s.swingMin = 0.50f; s.swingMax = 0.54f;
            s.timingAmountMin = 0.16f; s.timingAmountMax = 0.28f;
            s.humanizeAmountMin = 0.12f; s.humanizeAmountMax = 0.22f;
            s.kickDensityMin = 0.42f; s.kickDensityMax = 0.68f;
            s.hatDensityMin = 0.36f; s.hatDensityMax = 0.58f;
            s.openHatDensityMin = 0.01f; s.openHatDensityMax = 0.08f;
            s.percDensityMin = 0.00f; s.percDensityMax = 0.08f;
            s.sub808DensityMin = 0.00f; s.sub808DensityMax = 0.12f;
            s.openHatUseful = false;
            s.percUseful = true;
            s.sub808EnabledByDefault = false;
            s.sub808AllowSlides = false;
            s.sub808FollowKickMoreOften = false;
            break;

        case RapSubstyle::LofiRap:
        default:
            s.swingMin = 0.50f; s.swingMax = 0.56f;
            s.timingAmountMin = 0.24f; s.timingAmountMax = 0.36f;
            s.humanizeAmountMin = 0.22f; s.humanizeAmountMax = 0.34f;
            s.kickDensityMin = 0.30f; s.kickDensityMax = 0.50f;
            s.hatDensityMin = 0.38f; s.hatDensityMax = 0.58f;
            s.openHatDensityMin = 0.02f; s.openHatDensityMax = 0.10f;
            s.percDensityMin = 0.02f; s.percDensityMax = 0.12f;
            s.sub808DensityMin = 0.00f; s.sub808DensityMax = 0.12f;
            s.openHatUseful = false;
            s.percUseful = true;
            s.sub808EnabledByDefault = false;
            break;
    }

    return s;
}
} // namespace bbg
