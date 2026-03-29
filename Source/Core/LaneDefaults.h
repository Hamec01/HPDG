#pragma once

#include <juce_core/juce_core.h>

#include "TrackType.h"

namespace bbg::LaneDefaults
{
inline juce::String defaultGroupForTrack(TrackType type)
{
    switch (type)
    {
        case TrackType::Kick:
        case TrackType::GhostKick:
            return "Kick";
        case TrackType::Snare:
        case TrackType::ClapGhostSnare:
            return "Snare";
        case TrackType::HiHat:
        case TrackType::OpenHat:
        case TrackType::HatFX:
            return "Hats";
        case TrackType::Ride:
        case TrackType::Cymbal:
            return "Texture";
        case TrackType::Perc:
            return "Perc";
        case TrackType::Sub808:
            return "Bass";
        default:
            break;
    }

    return "Custom";
}

inline juce::String defaultDependencyForTrack(TrackType type)
{
    switch (type)
    {
        case TrackType::GhostKick: return "Kick";
        case TrackType::ClapGhostSnare: return "Snare";
        case TrackType::HatFX: return "HiHat";
        case TrackType::Sub808: return "Kick";
        default: break;
    }

    return {};
}

inline int defaultPriorityForTrack(TrackType type)
{
    switch (type)
    {
        case TrackType::Kick: return 96;
        case TrackType::Snare: return 92;
        case TrackType::HiHat: return 86;
        case TrackType::Sub808: return 82;
        case TrackType::OpenHat: return 74;
        case TrackType::Perc: return 68;
        case TrackType::ClapGhostSnare: return 60;
        case TrackType::GhostKick: return 58;
        case TrackType::HatFX: return 56;
        case TrackType::Ride: return 42;
        case TrackType::Cymbal: return 38;
        default: break;
    }

    return 50;
}

inline bool defaultCoreForTrack(TrackType type)
{
    switch (type)
    {
        case TrackType::GhostKick:
        case TrackType::ClapGhostSnare:
        case TrackType::HatFX:
        case TrackType::Ride:
        case TrackType::Cymbal:
            return false;
        default:
            break;
    }

    return true;
}
} // namespace bbg::LaneDefaults