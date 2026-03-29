#pragma once

#include <algorithm>
#include <optional>

#include "PatternProjectSerialization.h"
#include "ProjectLaneAccess.h"
#include "SoundTargetDescriptor.h"
#include "TrackRegistry.h"
#include "../Utils/TimingHelpers.h"

namespace bbg
{
namespace ProjectStateController
{
namespace detail
{
inline int floorDiv(int a, int b)
{
    const int q = a / b;
    const int r = a % b;
    return (r != 0 && ((r > 0) != (b > 0))) ? (q - 1) : q;
}

inline void normalizeNoteEvent(NoteEvent& note, int bars)
{
    const int maxTicks = bars * 16 * ticksPerStep();

    note.pitch = juce::jlimit(0, 127, note.pitch);
    note.velocity = juce::jlimit(1, 127, note.velocity);
    note.length = juce::jlimit(1, 64, note.length);

    int ticks = note.step * ticksPerStep() + note.microOffset;
    ticks = juce::jlimit(0, juce::jmax(0, maxTicks - 1), ticks);

    int step = floorDiv(ticks, ticksPerStep());
    int micro = ticks - step * ticksPerStep();
    if (micro > ticksPerStep() / 2)
    {
        micro -= ticksPerStep();
        ++step;
    }

    note.step = juce::jlimit(0, bars * 16 - 1, step);
    note.microOffset = juce::jlimit(-960, 960, micro);
}

inline void normalizeSub808NoteEvent(Sub808NoteEvent& note, int bars)
{
    const int maxTicks = bars * 16 * ticksPerStep();

    note.pitch = juce::jlimit(0, 127, note.pitch);
    note.velocity = juce::jlimit(1, 127, note.velocity);
    note.length = juce::jlimit(1, 64, note.length);

    int ticks = note.step * ticksPerStep() + note.microOffset;
    ticks = juce::jlimit(0, juce::jmax(0, maxTicks - 1), ticks);

    int step = floorDiv(ticks, ticksPerStep());
    int micro = ticks - step * ticksPerStep();
    if (micro > ticksPerStep() / 2)
    {
        micro -= ticksPerStep();
        ++step;
    }

    note.step = juce::jlimit(0, bars * 16 - 1, step);
    note.microOffset = juce::jlimit(-960, 960, micro);
    note.semanticRole = note.semanticRole.trim();
}
} // namespace detail

inline void setPreviewStartStep(PatternProject& project, int step)
{
    const int maxStep = juce::jmax(0, project.params.bars * 16 - 1);
    project.previewStartStep = juce::jlimit(0, maxStep, step);
}

inline void setPreviewPlaybackMode(PatternProject& project, PreviewPlaybackMode mode)
{
    project.previewPlaybackMode = mode;
}

inline void restoreEditorProjectSnapshot(PatternProject& project,
                                         const PatternProject& snapshot,
                                         const GeneratorParams& liveParams)
{
    project = snapshot;
    project.params = liveParams;
    PatternProjectSerialization::validate(project);
}

inline void setPreviewLoopRegion(PatternProject& project, const std::optional<juce::Range<int>>& tickRange)
{
    if (tickRange.has_value() && tickRange->getLength() > 0)
    {
        const int totalTicks = juce::jmax(1, project.params.bars * 16 * ticksPerStep());
        const int startTick = juce::jlimit(0, totalTicks - 1, tickRange->getStart());
        const int endTick = juce::jlimit(startTick + 1, totalTicks, tickRange->getEnd());
        if (endTick > startTick)
            project.previewLoopTicks = juce::Range<int>(startTick, endTick);
        else
            project.previewLoopTicks.reset();
    }
    else
    {
        project.previewLoopTicks.reset();
    }
}

inline void setSub808TrackNotes(PatternProject& project, TrackType trackType, const std::vector<Sub808NoteEvent>& notes)
{
    auto* state = ProjectLaneAccess::findTrackState(project, trackType);
    if (state == nullptr || state->locked || !state->enabled || trackType != TrackType::Sub808)
        return;

    state->sub808Notes = notes;
    state->notes = toLegacyNoteEvents(state->sub808Notes);

    const int bars = juce::jmax(1, project.params.bars);
    for (auto& note : state->sub808Notes)
        detail::normalizeSub808NoteEvent(note, bars);

    std::sort(state->sub808Notes.begin(), state->sub808Notes.end(), [](const Sub808NoteEvent& a, const Sub808NoteEvent& b)
    {
        if (a.step != b.step)
            return a.step < b.step;
        return a.pitch < b.pitch;
    });

    state->notes = toLegacyNoteEvents(state->sub808Notes);
}

inline void setSub808TrackNotes(PatternProject& project, const RuntimeLaneId& laneId, const std::vector<Sub808NoteEvent>& notes)
{
    if (const auto type = ProjectLaneAccess::backingTrackTypeForLaneId(project, laneId); type.has_value())
        setSub808TrackNotes(project, *type, notes);
}

inline void setSub808LaneSettings(PatternProject& project, TrackType trackType, const Sub808LaneSettings& settings)
{
    auto* state = ProjectLaneAccess::findTrackState(project, trackType);
    if (state == nullptr || trackType != TrackType::Sub808)
        return;

    state->sub808Settings.mono = settings.mono;
    state->sub808Settings.cutItself = settings.cutItself;
    state->sub808Settings.glideTimeMs = juce::jlimit(0, 4000, settings.glideTimeMs);
    state->sub808Settings.overlapMode = static_cast<Sub808OverlapMode>(juce::jlimit(0, 2, static_cast<int>(settings.overlapMode)));
    state->sub808Settings.scaleSnapPolicy = static_cast<Sub808ScaleSnapPolicy>(juce::jlimit(0, 2, static_cast<int>(settings.scaleSnapPolicy)));
}

inline void setSub808LaneSettings(PatternProject& project, const RuntimeLaneId& laneId, const Sub808LaneSettings& settings)
{
    if (const auto type = ProjectLaneAccess::backingTrackTypeForLaneId(project, laneId); type.has_value())
        setSub808LaneSettings(project, *type, settings);
}

inline void setTrackSolo(PatternProject& project, TrackType trackType, bool value)
{
    if (auto* state = ProjectLaneAccess::findTrackState(project, trackType))
        state->solo = value;
}

inline void setTrackSolo(PatternProject& project, const RuntimeLaneId& laneId, bool value)
{
    if (const auto type = ProjectLaneAccess::backingTrackTypeForLaneId(project, laneId); type.has_value())
        setTrackSolo(project, *type, value);
}

inline void setTrackMuted(PatternProject& project, TrackType trackType, bool value)
{
    if (auto* state = ProjectLaneAccess::findTrackState(project, trackType))
        state->muted = value;
}

inline void setTrackMuted(PatternProject& project, const RuntimeLaneId& laneId, bool value)
{
    if (const auto type = ProjectLaneAccess::backingTrackTypeForLaneId(project, laneId); type.has_value())
        setTrackMuted(project, *type, value);
}

inline void setTrackLocked(PatternProject& project, TrackType trackType, bool value)
{
    if (auto* state = ProjectLaneAccess::findTrackState(project, trackType))
        state->locked = value;
}

inline void setTrackLocked(PatternProject& project, const RuntimeLaneId& laneId, bool value)
{
    if (const auto type = ProjectLaneAccess::backingTrackTypeForLaneId(project, laneId); type.has_value())
        setTrackLocked(project, *type, value);
}

inline void setTrackEnabled(PatternProject& project, TrackType trackType, bool value)
{
    if (auto* state = ProjectLaneAccess::findTrackState(project, trackType))
        state->enabled = value;
}

inline void setTrackEnabled(PatternProject& project, const RuntimeLaneId& laneId, bool value)
{
    if (const auto type = ProjectLaneAccess::backingTrackTypeForLaneId(project, laneId); type.has_value())
        setTrackEnabled(project, *type, value);
}

inline void setTrackLaneVolume(PatternProject& project, TrackType trackType, float volume)
{
    if (auto* state = ProjectLaneAccess::findTrackState(project, trackType))
        state->laneVolume = juce::jlimit(0.0f, 1.5f, volume);
}

inline void setTrackLaneVolume(PatternProject& project, const RuntimeLaneId& laneId, float volume)
{
    if (const auto type = ProjectLaneAccess::backingTrackTypeForLaneId(project, laneId); type.has_value())
        setTrackLaneVolume(project, *type, volume);
}

inline void setTrackNotes(PatternProject& project, TrackType trackType, const std::vector<NoteEvent>& notes)
{
    if (trackType == TrackType::Sub808)
    {
        setSub808TrackNotes(project, trackType, toSub808NoteEvents(notes));
        return;
    }

    auto* state = ProjectLaneAccess::findTrackState(project, trackType);
    if (state == nullptr || state->locked || !state->enabled)
        return;

    state->notes = notes;
    const int bars = juce::jmax(1, project.params.bars);
    for (auto& note : state->notes)
        detail::normalizeNoteEvent(note, bars);

    std::sort(state->notes.begin(), state->notes.end(), [](const NoteEvent& a, const NoteEvent& b)
    {
        if (a.step != b.step)
            return a.step < b.step;
        return a.pitch < b.pitch;
    });
}

inline void setTrackNotes(PatternProject& project, const RuntimeLaneId& laneId, const std::vector<NoteEvent>& notes)
{
    if (const auto type = ProjectLaneAccess::backingTrackTypeForLaneId(project, laneId); type.has_value())
        setTrackNotes(project, *type, notes);
}

inline void setSelectedTrack(PatternProject& project, TrackType trackType)
{
    for (size_t i = 0; i < project.tracks.size(); ++i)
    {
        if (project.tracks[i].type == trackType)
        {
            project.selectedTrackIndex = static_cast<int>(i);
            return;
        }
    }
}

inline void setSelectedTrack(PatternProject& project, const RuntimeLaneId& laneId)
{
    if (const auto type = ProjectLaneAccess::backingTrackTypeForLaneId(project, laneId); type.has_value())
        setSelectedTrack(project, *type);
}

inline void setSoundModuleTrack(PatternProject& project, const std::optional<TrackType>& trackType)
{
    if (!trackType.has_value())
    {
        project.soundModuleTrackIndex = -1;
        return;
    }

    for (size_t i = 0; i < project.tracks.size(); ++i)
    {
        if (project.tracks[i].type == *trackType)
        {
            project.soundModuleTrackIndex = static_cast<int>(i);
            return;
        }
    }

    project.soundModuleTrackIndex = -1;
}

inline void setSoundModuleTarget(PatternProject& project, const SoundTargetDescriptor& descriptor)
{
    const auto resolved = SoundTargetController::sanitizeDescriptor(project, descriptor);
    if (resolved.isGlobal())
    {
        project.soundModuleTrackIndex = -1;
        return;
    }

    if (resolved.kind == SoundTargetDescriptorKind::BackedRuntimeLane)
    {
        for (size_t index = 0; index < project.tracks.size(); ++index)
        {
            if (project.tracks[index].laneId == resolved.laneId)
            {
                project.soundModuleTrackIndex = static_cast<int>(index);
                return;
            }
        }
    }

    setSoundModuleTrack(project, SoundTargetController::toLegacyTrackTypeAlias(resolved));
}

inline void setTrackSoundLayer(PatternProject& project, TrackType trackType, const SoundLayerState& state)
{
    if (auto* trackState = ProjectLaneAccess::findTrackState(project, trackType))
        trackState->sound = state;
}

inline void setTrackSoundLayer(PatternProject& project, const RuntimeLaneId& laneId, const SoundLayerState& state)
{
    if (const auto type = ProjectLaneAccess::backingTrackTypeForLaneId(project, laneId); type.has_value())
        setTrackSoundLayer(project, *type, state);
}

inline void setGlobalSoundLayer(PatternProject& project, const SoundLayerState& state)
{
    project.globalSound = state;
}

inline void setSoundLayerForTarget(PatternProject& project, const SoundTargetDescriptor& descriptor, const SoundLayerState& state)
{
    const auto resolved = SoundTargetController::sanitizeDescriptor(project, descriptor);
    if (resolved.isGlobal())
    {
        setGlobalSoundLayer(project, state);
        return;
    }

    if (resolved.kind == SoundTargetDescriptorKind::BackedRuntimeLane)
    {
        if (auto* trackState = ProjectLaneAccess::findTrackState(project, resolved.laneId))
        {
            trackState->sound = state;
            return;
        }
    }

    if (const auto legacyTrackType = SoundTargetController::toLegacyTrackTypeAlias(resolved); legacyTrackType.has_value())
        setTrackSoundLayer(project, *legacyTrackType, state);
}

inline void clearTrack(PatternProject& project, TrackType trackType)
{
    if (auto* state = ProjectLaneAccess::findTrackState(project, trackType))
        state->notes.clear();
}

inline void clearTrack(PatternProject& project, const RuntimeLaneId& laneId)
{
    if (const auto type = ProjectLaneAccess::backingTrackTypeForLaneId(project, laneId); type.has_value())
        clearTrack(project, *type);
}
}
} // namespace bbg