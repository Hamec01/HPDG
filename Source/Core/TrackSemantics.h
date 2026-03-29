#pragma once

#include "TrackType.h"

namespace bbg
{
enum class TrackRole
{
    HiHat,
    OpenHat,
    Snare,
    ClapGhostSnare,
    Kick,
    GhostKick,
    Ride,
    Cymbal,
    Perc,
    HatFX,
    Bass,
    Unknown
};

enum class TrackFamily
{
    HatFamily,
    SnareFamily,
    KickFamily,
    CymbalFamily,
    BassFamily,
    Unknown
};

constexpr TrackRole roleFromTrackType(TrackType type)
{
    switch (type)
    {
        case TrackType::HiHat:          return TrackRole::HiHat;
        case TrackType::OpenHat:        return TrackRole::OpenHat;
        case TrackType::Snare:          return TrackRole::Snare;
        case TrackType::ClapGhostSnare: return TrackRole::ClapGhostSnare;
        case TrackType::Kick:           return TrackRole::Kick;
        case TrackType::GhostKick:      return TrackRole::GhostKick;
        case TrackType::Ride:           return TrackRole::Ride;
        case TrackType::Cymbal:         return TrackRole::Cymbal;
        case TrackType::Perc:           return TrackRole::Perc;
        case TrackType::HatFX:          return TrackRole::HatFX;
        case TrackType::Sub808:         return TrackRole::Bass;
        default:                        return TrackRole::Unknown;
    }
}

constexpr TrackFamily familyFromTrackType(TrackType type)
{
    switch (type)
    {
        case TrackType::HiHat:
        case TrackType::OpenHat:
        case TrackType::Perc:
        case TrackType::HatFX:
            return TrackFamily::HatFamily;
        case TrackType::Snare:
        case TrackType::ClapGhostSnare:
            return TrackFamily::SnareFamily;
        case TrackType::Kick:
        case TrackType::GhostKick:
            return TrackFamily::KickFamily;
        case TrackType::Ride:
        case TrackType::Cymbal:
            return TrackFamily::CymbalFamily;
        case TrackType::Sub808:
            return TrackFamily::BassFamily;
        default:
            return TrackFamily::Unknown;
    }
}

constexpr bool isHatFamily(TrackType type)
{
    return familyFromTrackType(type) == TrackFamily::HatFamily;
}

constexpr bool isKickFamily(TrackType type)
{
    return familyFromTrackType(type) == TrackFamily::KickFamily;
}

constexpr bool isSnareFamily(TrackType type)
{
    return familyFromTrackType(type) == TrackFamily::SnareFamily;
}

constexpr bool isBassFamily(TrackType type)
{
    return familyFromTrackType(type) == TrackFamily::BassFamily;
}
} // namespace bbg