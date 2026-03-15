#pragma once

namespace bbg
{
enum class TrackType
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
    Sub808
};

inline const char* toString(TrackType type)
{
    switch (type)
    {
        case TrackType::HiHat:          return "HiHat";
        case TrackType::OpenHat:        return "OpenHat";
        case TrackType::Snare:          return "Snare";
        case TrackType::ClapGhostSnare: return "Clap/GhostSnare";
        case TrackType::Kick:           return "Kick";
        case TrackType::GhostKick:      return "GhostKick";
        case TrackType::Ride:           return "Ride";
        case TrackType::Cymbal:         return "Cymbal";
        case TrackType::Perc:           return "Perc";
        case TrackType::HatFX:          return "HatFX";
        case TrackType::Sub808:         return "Sub808";
        default:                        return "Unknown";
    }
}
} // namespace bbg
