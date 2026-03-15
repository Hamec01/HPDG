#pragma once

#include <algorithm>

namespace bbg
{
struct LofiRapStyleSpec
{
    float swingMin = 0.18f;
    float swingMax = 0.32f;

    float timingAmountMin = 0.28f;
    float timingAmountMax = 0.42f;

    float humanizeAmountMin = 0.35f;
    float humanizeAmountMax = 0.52f;

    float velocityAmountMin = 0.30f;
    float velocityAmountMax = 0.44f;

    float densityAmountMin = 0.28f;
    float densityAmountMax = 0.42f;

    float kickDensityMin = 0.26f;
    float kickDensityMax = 0.42f;

    float hatDensityMin = 0.42f;
    float hatDensityMax = 0.62f;

    float ghostHatDensityMin = 0.08f;
    float ghostHatDensityMax = 0.22f;

    float clapGhostDensityMin = 0.02f;
    float clapGhostDensityMax = 0.10f;

    float openHatDensityMin = 0.00f;
    float openHatDensityMax = 0.08f;

    float percDensityMin = 0.04f;
    float percDensityMax = 0.14f;

    bool useHatFxSparingly = true;
    bool useRideRarely = true;
    bool useCymbalRarely = true;
    bool useSub808Sparingly = true;
    bool preferTwoBarPhrases = true;
    bool preferLateSnare = true;
    bool preferLooseKick = true;
    bool preferSoftPhraseEndings = true;
};

inline const LofiRapStyleSpec& getLofiRapStyleSpec()
{
    static const LofiRapStyleSpec spec {};
    return spec;
}

inline float lofiMix(float minValue, float maxValue, float amount)
{
    return minValue + (maxValue - minValue) * std::clamp(amount, 0.0f, 1.0f);
}
} // namespace bbg
