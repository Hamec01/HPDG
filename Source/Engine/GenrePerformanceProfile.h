#pragma once

#include <array>

#include "StyleDefaults.h"

namespace bbg
{
struct LanePerformanceProfile
{
    float swingWeight = 1.0f;
    float timingScale = 1.0f;
    float zeroTimingWeight = 1.0f;
    float humanizeTimingWeight = 1.0f;
    float humanizeVelocityWeight = 1.0f;
    float velocityWeight = 1.0f;
    float densityWeight = 1.0f;
    float anchorSwingProtection = 1.0f;
    float anchorTimingProtection = 1.0f;
    float supportTimingBoost = 1.0f;
    float supportHumanizeBoost = 1.0f;
    float lateSwingBias = 1.0f;
};

struct GenrePerformanceProfile
{
    GenreType genre = GenreType::BoomBap;
    int substyleIndex = 0;
    juce::String substyleName;
    float timingScaleAmount = 0.65f;
    float swingTicksPerPercent = 5.0f;
    float zeroTimingTicks = 14.0f;
    float humanizeTimingTicks = 16.0f;
    float humanizeVelocityRange = 8.0f;
    float velocityRange = 14.0f;
    float anchorSwingTightness = 0.90f;
    float anchorTimingTightness = 0.82f;
    float supportLateBias = 0.0f;
    float accentVelocityDepth = 1.0f;
    std::array<LanePerformanceProfile, kTrackTypeCount> lanes {};
};

namespace detail
{
inline GenrePerformanceProfile makeBasePerformanceProfile()
{
    GenrePerformanceProfile profile;

    profile.lanes[trackTypeIndex(TrackType::HiHat)] = { 1.20f, 1.15f, 1.00f, 1.00f, 0.85f, 0.70f, 1.00f, 0.45f, 0.42f, 1.14f, 1.08f, 1.00f };
    profile.lanes[trackTypeIndex(TrackType::OpenHat)] = { 0.75f, 0.95f, 0.75f, 0.75f, 0.80f, 0.85f, 0.80f, 0.65f, 0.55f, 1.02f, 1.00f, 0.82f };
    profile.lanes[trackTypeIndex(TrackType::Snare)] = { 0.14f, 0.70f, 0.40f, 0.40f, 0.60f, 0.95f, 0.85f, 1.00f, 0.96f, 0.82f, 0.82f, 0.24f };
    profile.lanes[trackTypeIndex(TrackType::ClapGhostSnare)] = { 0.36f, 0.90f, 0.75f, 0.90f, 0.70f, 0.70f, 1.05f, 0.74f, 0.64f, 1.08f, 1.08f, 0.72f };
    profile.lanes[trackTypeIndex(TrackType::Kick)] = { 0.10f, 0.70f, 0.50f, 0.45f, 0.55f, 1.00f, 0.90f, 1.00f, 0.98f, 0.78f, 0.74f, 0.12f };
    profile.lanes[trackTypeIndex(TrackType::GhostKick)] = { 0.22f, 1.00f, 0.90f, 0.95f, 0.80f, 0.75f, 1.10f, 0.80f, 0.72f, 1.04f, 1.10f, 0.46f };
    profile.lanes[trackTypeIndex(TrackType::Ride)] = { 1.10f, 1.05f, 0.90f, 0.90f, 0.80f, 0.80f, 0.75f, 0.42f, 0.46f, 1.10f, 1.04f, 1.04f };
    profile.lanes[trackTypeIndex(TrackType::Cymbal)] = { 0.08f, 0.70f, 0.65f, 0.55f, 0.65f, 0.75f, 0.60f, 0.86f, 0.74f, 0.86f, 0.82f, 0.18f };
    profile.lanes[trackTypeIndex(TrackType::Perc)] = { 0.92f, 1.00f, 1.00f, 0.95f, 0.90f, 0.80f, 1.00f, 0.40f, 0.44f, 1.12f, 1.08f, 0.96f };
    profile.lanes[trackTypeIndex(TrackType::HatFX)] = { 0.82f, 1.05f, 0.95f, 0.95f, 0.85f, 0.75f, 0.95f, 0.54f, 0.52f, 1.08f, 1.06f, 0.90f };
    profile.lanes[trackTypeIndex(TrackType::Sub808)] = { 0.05f, 0.60f, 0.45f, 0.45f, 0.45f, 0.90f, 0.85f, 1.00f, 0.94f, 0.78f, 0.76f, 0.10f };

    return profile;
}
} // namespace detail

inline GenrePerformanceProfile getGenrePerformanceProfile(const GeneratorParams& params)
{
    auto profile = detail::makeBasePerformanceProfile();
    profile.genre = params.genre;
    profile.substyleIndex = getSelectedSubstyleIndex(params);

    const auto& style = getGenreStyleDefaults(params.genre, profile.substyleIndex);
    profile.substyleName = style.substyleName;

    switch (params.genre)
    {
        case GenreType::BoomBap:
            profile.timingScaleAmount = 0.88f;
            profile.swingTicksPerPercent = 7.0f;
            profile.zeroTimingTicks = 13.0f;
            profile.humanizeTimingTicks = 15.0f;
            profile.humanizeVelocityRange = 7.0f;
            profile.velocityRange = 18.0f;
            profile.anchorSwingTightness = 0.78f;
            profile.anchorTimingTightness = 0.62f;
            profile.supportLateBias = 0.05f;
            profile.accentVelocityDepth = 1.24f;
            profile.lanes[trackTypeIndex(TrackType::HiHat)].swingWeight = 1.42f;
            profile.lanes[trackTypeIndex(TrackType::Ride)].swingWeight = 1.48f;
            profile.lanes[trackTypeIndex(TrackType::Perc)].swingWeight = 1.24f;
            profile.lanes[trackTypeIndex(TrackType::HatFX)].swingWeight = 1.08f;
            profile.lanes[trackTypeIndex(TrackType::OpenHat)].swingWeight = 0.62f;
            profile.lanes[trackTypeIndex(TrackType::Kick)].swingWeight = 0.12f;
            profile.lanes[trackTypeIndex(TrackType::Snare)].swingWeight = 0.10f;
            break;

        case GenreType::Rap:
            profile.timingScaleAmount = 0.66f;
            profile.swingTicksPerPercent = 3.8f;
            profile.zeroTimingTicks = 11.0f;
            profile.humanizeTimingTicks = 12.0f;
            profile.humanizeVelocityRange = 6.5f;
            profile.velocityRange = 15.0f;
            profile.anchorSwingTightness = 0.86f;
            profile.anchorTimingTightness = 0.72f;
            profile.supportLateBias = 0.04f;
            profile.accentVelocityDepth = 1.08f;
            profile.lanes[trackTypeIndex(TrackType::HiHat)].swingWeight = 0.72f;
            profile.lanes[trackTypeIndex(TrackType::OpenHat)].swingWeight = 0.40f;
            profile.lanes[trackTypeIndex(TrackType::Perc)].swingWeight = 0.44f;
            profile.lanes[trackTypeIndex(TrackType::Kick)].swingWeight = 0.08f;
            profile.lanes[trackTypeIndex(TrackType::Snare)].swingWeight = 0.08f;
            if (style.substyleName == "EastCoast")
            {
                profile.swingTicksPerPercent = 4.4f;
                profile.lanes[trackTypeIndex(TrackType::HiHat)].swingWeight = 0.86f;
            }
            else if (style.substyleName == "RnBRap")
            {
                profile.swingTicksPerPercent = 4.8f;
                profile.timingScaleAmount = 0.72f;
                profile.lanes[trackTypeIndex(TrackType::HiHat)].swingWeight = 0.94f;
                profile.lanes[trackTypeIndex(TrackType::OpenHat)].swingWeight = 0.52f;
            }
            else if (style.substyleName == "GermanStreetRap" || style.substyleName == "RussianRap" || style.substyleName == "HardcoreRap")
            {
                profile.swingTicksPerPercent = 3.0f;
                profile.lanes[trackTypeIndex(TrackType::HiHat)].swingWeight = 0.54f;
            }
            break;

        case GenreType::Trap:
            profile.timingScaleAmount = 0.48f;
            profile.swingTicksPerPercent = 1.2f;
            profile.zeroTimingTicks = 8.0f;
            profile.humanizeTimingTicks = 8.0f;
            profile.humanizeVelocityRange = 5.0f;
            profile.velocityRange = 11.0f;
            profile.anchorSwingTightness = 0.98f;
            profile.anchorTimingTightness = 0.90f;
            profile.supportLateBias = 0.02f;
            profile.accentVelocityDepth = 0.92f;
            profile.lanes[trackTypeIndex(TrackType::HiHat)].swingWeight = 0.24f;
            profile.lanes[trackTypeIndex(TrackType::HatFX)].swingWeight = 0.20f;
            profile.lanes[trackTypeIndex(TrackType::Perc)].swingWeight = 0.16f;
            profile.lanes[trackTypeIndex(TrackType::OpenHat)].swingWeight = 0.10f;
            profile.lanes[trackTypeIndex(TrackType::Kick)].swingWeight = 0.02f;
            profile.lanes[trackTypeIndex(TrackType::Snare)].swingWeight = 0.02f;
            profile.lanes[trackTypeIndex(TrackType::Sub808)].swingWeight = 0.01f;
            profile.lanes[trackTypeIndex(TrackType::HiHat)].timingScale = 1.12f;
            profile.lanes[trackTypeIndex(TrackType::HatFX)].timingScale = 1.08f;
            profile.lanes[trackTypeIndex(TrackType::HiHat)].supportTimingBoost = 1.08f;
            break;

        case GenreType::Drill:
            profile.timingScaleAmount = 0.56f;
            profile.swingTicksPerPercent = 0.9f;
            profile.zeroTimingTicks = 7.0f;
            profile.humanizeTimingTicks = 7.5f;
            profile.humanizeVelocityRange = 5.0f;
            profile.velocityRange = 10.0f;
            profile.anchorSwingTightness = 1.0f;
            profile.anchorTimingTightness = 0.92f;
            profile.supportLateBias = 0.16f;
            profile.accentVelocityDepth = 0.92f;
            profile.lanes[trackTypeIndex(TrackType::Kick)].swingWeight = 0.0f;
            profile.lanes[trackTypeIndex(TrackType::Snare)].swingWeight = 0.0f;
            profile.lanes[trackTypeIndex(TrackType::Sub808)].swingWeight = 0.0f;
            profile.lanes[trackTypeIndex(TrackType::HiHat)].swingWeight = 0.22f;
            profile.lanes[trackTypeIndex(TrackType::HatFX)].swingWeight = 0.18f;
            profile.lanes[trackTypeIndex(TrackType::ClapGhostSnare)].swingWeight = 0.16f;
            profile.lanes[trackTypeIndex(TrackType::Perc)].swingWeight = 0.12f;
            profile.lanes[trackTypeIndex(TrackType::HiHat)].anchorSwingProtection = 0.85f;
            profile.lanes[trackTypeIndex(TrackType::HiHat)].timingScale = 1.18f;
            profile.lanes[trackTypeIndex(TrackType::HatFX)].timingScale = 1.12f;
            profile.lanes[trackTypeIndex(TrackType::ClapGhostSnare)].timingScale = 1.05f;
            profile.lanes[trackTypeIndex(TrackType::HiHat)].supportTimingBoost = 1.16f;
            profile.lanes[trackTypeIndex(TrackType::HatFX)].supportTimingBoost = 1.12f;
            profile.lanes[trackTypeIndex(TrackType::ClapGhostSnare)].supportTimingBoost = 1.10f;
            profile.lanes[trackTypeIndex(TrackType::HiHat)].supportHumanizeBoost = 1.12f;
            profile.lanes[trackTypeIndex(TrackType::ClapGhostSnare)].supportHumanizeBoost = 1.08f;
            break;
    }

    return profile;
}
} // namespace bbg