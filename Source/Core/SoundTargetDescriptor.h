#pragma once

#include <optional>

#include "PatternProject.h"

namespace bbg
{
enum class SoundTargetDescriptorKind
{
    Global,
    BackedRuntimeLane,
    LegacyTrackTypeAlias
};

struct SoundTargetDescriptor
{
    SoundTargetDescriptorKind kind = SoundTargetDescriptorKind::Global;
    RuntimeLaneId laneId;
    std::optional<TrackType> runtimeTrackType;
    std::optional<TrackType> legacyTrackTypeAlias;

    static SoundTargetDescriptor makeGlobal()
    {
        return {};
    }

    static SoundTargetDescriptor makeBackedRuntimeLane(const RuntimeLaneId& laneIdToUse, TrackType trackType)
    {
        SoundTargetDescriptor descriptor;
        descriptor.kind = SoundTargetDescriptorKind::BackedRuntimeLane;
        descriptor.laneId = laneIdToUse;
        descriptor.runtimeTrackType = trackType;
        return descriptor;
    }

    static SoundTargetDescriptor makeLegacyTrackTypeAlias(TrackType trackType)
    {
        SoundTargetDescriptor descriptor;
        descriptor.kind = SoundTargetDescriptorKind::LegacyTrackTypeAlias;
        descriptor.legacyTrackTypeAlias = trackType;
        return descriptor;
    }

    bool isGlobal() const
    {
        return kind == SoundTargetDescriptorKind::Global;
    }

    bool operator==(const SoundTargetDescriptor& other) const
    {
        return kind == other.kind
            && laneId == other.laneId
            && runtimeTrackType == other.runtimeTrackType
            && legacyTrackTypeAlias == other.legacyTrackTypeAlias;
    }

    bool operator!=(const SoundTargetDescriptor& other) const
    {
        return !(*this == other);
    }
};

class SoundTargetController
{
public:
    static SoundTargetDescriptor resolveProjectSelection(const PatternProject& project)
    {
        if (project.soundModuleTrackIndex < 0 || project.soundModuleTrackIndex >= static_cast<int>(project.tracks.size()))
            return SoundTargetDescriptor::makeGlobal();

        const auto& track = project.tracks[static_cast<size_t>(project.soundModuleTrackIndex)];
        if (track.laneId.isNotEmpty())
        {
            if (const auto* lane = findRuntimeLaneById(project.runtimeLaneProfile, track.laneId);
                lane != nullptr && lane->runtimeTrackType.has_value())
            {
                return SoundTargetDescriptor::makeBackedRuntimeLane(lane->laneId, *lane->runtimeTrackType);
            }
        }

        return SoundTargetDescriptor::makeLegacyTrackTypeAlias(track.type);
    }

    static SoundTargetDescriptor resolveLegacySelection(const PatternProject& project, const std::optional<TrackType>& target)
    {
        if (!target.has_value())
            return SoundTargetDescriptor::makeGlobal();

        for (const auto& track : project.tracks)
        {
            if (track.type != *target)
                continue;

            if (track.laneId.isNotEmpty())
            {
                if (const auto* lane = findRuntimeLaneById(project.runtimeLaneProfile, track.laneId);
                    lane != nullptr && lane->runtimeTrackType.has_value())
                {
                    return SoundTargetDescriptor::makeBackedRuntimeLane(lane->laneId, *lane->runtimeTrackType);
                }
            }
        }

        return SoundTargetDescriptor::makeLegacyTrackTypeAlias(*target);
    }

    static std::optional<TrackType> toLegacyTrackTypeAlias(const SoundTargetDescriptor& descriptor)
    {
        if (descriptor.kind == SoundTargetDescriptorKind::BackedRuntimeLane)
            return descriptor.runtimeTrackType;

        if (descriptor.kind == SoundTargetDescriptorKind::LegacyTrackTypeAlias)
            return descriptor.legacyTrackTypeAlias;

        return std::nullopt;
    }

    static SoundTargetDescriptor sanitizeDescriptor(const PatternProject& project, const SoundTargetDescriptor& descriptor)
    {
        if (descriptor.kind == SoundTargetDescriptorKind::Global)
            return SoundTargetDescriptor::makeGlobal();

        if (descriptor.kind == SoundTargetDescriptorKind::BackedRuntimeLane)
        {
            if (descriptor.laneId.isNotEmpty())
            {
                if (const auto* lane = findRuntimeLaneById(project.runtimeLaneProfile, descriptor.laneId);
                    lane != nullptr && lane->runtimeTrackType.has_value())
                {
                    return SoundTargetDescriptor::makeBackedRuntimeLane(lane->laneId, *lane->runtimeTrackType);
                }
            }

            if (descriptor.runtimeTrackType.has_value())
                return resolveLegacySelection(project, descriptor.runtimeTrackType);

            return SoundTargetDescriptor::makeGlobal();
        }

        if (descriptor.legacyTrackTypeAlias.has_value())
        {
            for (const auto& track : project.tracks)
            {
                if (track.type != *descriptor.legacyTrackTypeAlias)
                    continue;

                if (track.laneId.isNotEmpty())
                {
                    if (const auto* lane = findRuntimeLaneById(project.runtimeLaneProfile, track.laneId);
                        lane != nullptr && lane->runtimeTrackType.has_value())
                    {
                        return SoundTargetDescriptor::makeBackedRuntimeLane(lane->laneId, *lane->runtimeTrackType);
                    }
                }
            }

            return SoundTargetDescriptor::makeLegacyTrackTypeAlias(*descriptor.legacyTrackTypeAlias);
        }

        return SoundTargetDescriptor::makeGlobal();
    }

    static SoundLayerState resolveSoundState(const PatternProject& project, const SoundTargetDescriptor& descriptor)
    {
        const auto resolved = sanitizeDescriptor(project, descriptor);
        if (resolved.kind == SoundTargetDescriptorKind::BackedRuntimeLane)
        {
            for (const auto& track : project.tracks)
            {
                if (track.laneId == resolved.laneId)
                    return track.sound;
            }
        }
        else if (resolved.kind == SoundTargetDescriptorKind::LegacyTrackTypeAlias && resolved.legacyTrackTypeAlias.has_value())
        {
            for (const auto& track : project.tracks)
            {
                if (track.type == *resolved.legacyTrackTypeAlias)
                    return track.sound;
            }
        }

        return project.globalSound;
    }
};
} // namespace bbg
