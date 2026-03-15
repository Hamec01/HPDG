#include "TrackRegistry.h"

namespace bbg
{
const std::array<TrackInfo, 11>& TrackRegistry::all()
{
    static const std::array<TrackInfo, 11> kTracks = {{
        { TrackType::HiHat, "HiHat", 42, true, false, true, true },
        { TrackType::HatFX, "HatFX", 44, false, true, true, true },
        { TrackType::OpenHat, "OpenHat", 46, true, false, true, true },
        { TrackType::Snare, "Snare", 38, true, false, true, true },
        { TrackType::ClapGhostSnare, "Clap/Ghost", 39, true, true, true, true },
        { TrackType::Kick, "Kick", 36, true, false, true, true },
        { TrackType::GhostKick, "GhostKick", 35, true, true, true, true },
        { TrackType::Ride, "Ride", 51, false, false, true, true },
        { TrackType::Cymbal, "Cymbal", 49, false, false, true, true },
        { TrackType::Perc, "Perc", 50, true, false, true, true },
        { TrackType::Sub808, "Sub808", 34, false, false, true, true }
    }};

    return kTracks;
}

const TrackInfo* TrackRegistry::find(TrackType type)
{
    for (const auto& info : all())
    {
        if (info.type == type)
            return &info;
    }

    return nullptr;
}

std::vector<TrackState> TrackRegistry::createDefaultTrackStates()
{
    std::vector<TrackState> tracks;
    tracks.reserve(all().size());

    for (const auto& info : all())
    {
        TrackState state;
        state.type = info.type;
        state.enabled = info.enabledByDefault;
        tracks.push_back(std::move(state));
    }

    return tracks;
}
} // namespace bbg
