#include "TrackRegistry.h"

namespace bbg
{
namespace
{
juce::String defaultGroupForTrack(TrackType type)
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

juce::String defaultDependencyForTrack(TrackType type)
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

int defaultPriorityForTrack(TrackType type)
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

bool defaultCoreForTrack(TrackType type)
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
} // namespace

const std::array<TrackInfo, 11>& TrackRegistry::all()
{
    static const std::array<TrackInfo, 11> kTracks = {{
        { TrackType::HiHat, "HiHat", 42, true, false, true, true },
        { TrackType::HatFX, "Hat Accent", 44, false, true, true, true },
        { TrackType::OpenHat, "OpenHat", 46, true, false, true, true },
        { TrackType::Snare, "Snare", 38, true, false, true, true },
        { TrackType::ClapGhostSnare, "Clap Ghost", 39, true, true, true, true },
        { TrackType::Kick, "Kick", 36, true, false, true, true },
        { TrackType::GhostKick, "Kick Ghost", 35, true, true, true, true },
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

juce::String TrackRegistry::defaultRuntimeLaneId(TrackType type)
{
    return "registry:" + juce::String(toString(type));
}

RuntimeLaneProfile TrackRegistry::createDefaultRuntimeLaneProfile()
{
    RuntimeLaneProfile profile;
    profile.lanes.reserve(all().size());

    for (const auto& info : all())
    {
        RuntimeLaneDefinition lane;
        lane.laneId = defaultRuntimeLaneId(info.type);
        lane.laneName = info.displayName;
        lane.groupName = defaultGroupForTrack(info.type);
        lane.dependencyName = defaultDependencyForTrack(info.type);
        lane.generationPriority = defaultPriorityForTrack(info.type);
        lane.isCore = defaultCoreForTrack(info.type);
        lane.isVisibleInEditor = info.visibleInUI;
        lane.enabledByDefault = info.enabledByDefault;
        lane.supportsDragExport = info.supportsDragExport;
        lane.isGhostTrack = info.isGhostTrack;
        lane.defaultMidiNote = info.defaultMidiNote;
        lane.isRuntimeRegistryLane = true;
        lane.runtimeTrackType = info.type;
        lane.editorCapabilities = makeLaneEditorCapabilities(lane.runtimeTrackType);
        profile.lanes.push_back(std::move(lane));
    }

    return profile;
}

std::vector<TrackState> TrackRegistry::createDefaultTrackStates()
{
    return createDefaultTrackStates(createDefaultRuntimeLaneProfile());
}

std::vector<TrackState> TrackRegistry::createDefaultTrackStates(const RuntimeLaneProfile& profile)
{
    std::vector<TrackState> tracks;
    tracks.reserve(profile.lanes.size());

    for (const auto& lane : profile.lanes)
    {
        if (!lane.runtimeTrackType.has_value())
            continue;

        TrackState state;
        state.type = *lane.runtimeTrackType;
        state.laneId = lane.laneId;
        state.runtimeTrackType = lane.runtimeTrackType;
        state.enabled = lane.enabledByDefault;
        tracks.push_back(std::move(state));
    }

    return tracks;
}
} // namespace bbg
