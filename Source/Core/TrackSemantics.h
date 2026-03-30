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

constexpr bool hasCanonicalTrackTypeForRole(TrackRole role)
{
    switch (role)
    {
        case TrackRole::HiHat:
        case TrackRole::OpenHat:
        case TrackRole::ClapGhostSnare:
        case TrackRole::Kick:
        case TrackRole::Ride:
        case TrackRole::Perc:
        case TrackRole::HatFX:
        case TrackRole::Bass:
            return true;
        default:
            return false;
    }
}

constexpr TrackType canonicalTrackTypeForRole(TrackRole role)
{
    switch (role)
    {
        case TrackRole::HiHat:          return TrackType::HiHat;
        case TrackRole::OpenHat:        return TrackType::OpenHat;
        case TrackRole::ClapGhostSnare: return TrackType::ClapGhostSnare;
        case TrackRole::Kick:           return TrackType::Kick;
        case TrackRole::Ride:           return TrackType::Ride;
        case TrackRole::Perc:           return TrackType::Perc;
        case TrackRole::HatFX:          return TrackType::HatFX;
        case TrackRole::Bass:           return TrackType::Sub808;
        default:                        return TrackType::HiHat;
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

constexpr const char* defaultLaneRoleForTrackType(TrackType type)
{
    switch (type)
    {
        case TrackType::Kick:           return "core_pulse";
        case TrackType::Snare:          return "backbeat";
        case TrackType::HiHat:          return "carrier";
        case TrackType::OpenHat:        return "accent";
        case TrackType::ClapGhostSnare: return "support";
        case TrackType::GhostKick:      return "support";
        case TrackType::HatFX:          return "accent_fx";
        case TrackType::Ride:           return "support";
        case TrackType::Cymbal:         return "crash";
        case TrackType::Perc:           return "texture";
        case TrackType::Sub808:         return "bass_anchor";
        default:                        return "lane";
    }
}
} // namespace bbg