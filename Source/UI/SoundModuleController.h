#pragma once

#include <functional>

#include "../Core/TrackRegistry.h"
#include "../Plugin/PluginProcessor.h"
#include "SoundModuleComponent.h"

namespace bbg
{
class SoundModuleController
{
public:
    using MutationCallback = std::function<void(const std::function<void()>& mutation, bool refreshTrackRows)>;
    using RefreshCallback = std::function<void(bool refreshTrackRows)>;
    using GestureCallback = std::function<void()>;

    SoundModuleController(BoomBapGeneratorAudioProcessor& processor,
                          SoundModuleComponent& component)
        : audioProcessor(processor)
        , soundModule(component)
    {
        bindCallbacks();
    }

    ~SoundModuleController()
    {
        soundModule.onSoundTargetChanged = {};
        soundModule.onSoundLayerChanged = {};
        soundModule.onSoundLayerGestureStarted = {};
        soundModule.onSoundLayerGestureEnded = {};
    }

    void setHostCallbacks(MutationCallback mutationCallback,
                          RefreshCallback refreshCallback,
                          GestureCallback gestureStartedCallback = {},
                          GestureCallback gestureEndedCallback = {})
    {
        applyProjectMutation = std::move(mutationCallback);
        requestRefresh = std::move(refreshCallback);
        onGestureStarted = std::move(gestureStartedCallback);
        onGestureEnded = std::move(gestureEndedCallback);
    }

    void sync(const PatternProject& project)
    {
        soundModule.setState(buildViewState(project));
    }

    void syncFromProcessor()
    {
        sync(audioProcessor.getProjectSnapshot());
    }

private:
    static juce::String displayNameForTrack(const TrackState& track)
    {
        if (const auto* info = TrackRegistry::find(track.type); info != nullptr)
            return info->displayName;

        return "Track";
    }

    SoundModuleViewState buildViewState(const PatternProject& project) const
    {
        SoundModuleViewState state;
        state.selectedTarget = audioProcessor.getSoundModuleTarget();
        state.soundState = SoundTargetController::resolveSoundState(project, state.selectedTarget);
        state.targetOptions.push_back({ "All Tracks (Global)", SoundTargetDescriptor::makeGlobal() });

        for (const auto& track : project.tracks)
        {
            const auto* info = TrackRegistry::find(track.type);
            if (info == nullptr || !info->visibleInUI)
                continue;

            const auto descriptor = track.runtimeTrackType.has_value() && track.laneId.isNotEmpty()
                ? SoundTargetDescriptor::makeBackedRuntimeLane(track.laneId, *track.runtimeTrackType)
                : SoundTargetDescriptor::makeLegacyTrackTypeAlias(track.type);

            state.targetOptions.push_back({ displayNameForTrack(track), descriptor });
        }

        return state;
    }

    void bindCallbacks()
    {
        soundModule.onSoundTargetChanged = [this](const SoundTargetDescriptor& target)
        {
            audioProcessor.setSoundModuleTarget(target);

            if (requestRefresh)
                requestRefresh(false);
            else
                syncFromProcessor();
        };

        soundModule.onSoundLayerChanged = [this](const SoundTargetDescriptor& target, const SoundLayerState& state)
        {
            if (applyProjectMutation)
            {
                applyProjectMutation([this, target, state]
                {
                    audioProcessor.setSoundLayerForTarget(target, state);
                }, false);
                return;
            }

            audioProcessor.setSoundLayerForTarget(target, state);
            if (requestRefresh)
                requestRefresh(false);
            else
                syncFromProcessor();
        };

        soundModule.onSoundLayerGestureStarted = [this]
        {
            if (onGestureStarted)
                onGestureStarted();
        };

        soundModule.onSoundLayerGestureEnded = [this]
        {
            if (onGestureEnded)
                onGestureEnded();
        };
    }

    BoomBapGeneratorAudioProcessor& audioProcessor;
    SoundModuleComponent& soundModule;
    MutationCallback applyProjectMutation;
    RefreshCallback requestRefresh;
    GestureCallback onGestureStarted;
    GestureCallback onGestureEnded;
};
} // namespace bbg