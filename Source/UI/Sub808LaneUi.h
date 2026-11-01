#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Core/RuntimeLaneProfile.h"
#include "Sub808EditorOverlay.h"

namespace bbg
{
class BoomBapGeneratorAudioProcessor;

// The bass lane's UI shared by HPDG's VST3 and Standalone windows.

// A click on a lane's sample name: all its samples; on the bass lane also the HPDG 808 presets by genre,
// the user's own basses (and deleting one of them) and the editor.
void showLaneSampleList(BoomBapGeneratorAudioProcessor& processor,
                        juce::Component& parent,
                        const RuntimeLaneId& laneId,
                        std::function<void()> refresh,
                        std::function<void()> openBassEditor);

// The HPDG 808 editor wired to the Sub808 lane. soundChanged: after every change it makes;
// close: the host deletes the overlay (asynchronously).
Sub808EditorOverlay::Host makeSub808EditorHost(BoomBapGeneratorAudioProcessor& processor,
                                               std::function<void()> soundChanged,
                                               std::function<void()> close);

// What the bass lane plays, for the strip: the HPDG 808 sound's name or the bank sample's.
juce::String sub808SoundName(BoomBapGeneratorAudioProcessor& processor, const Sub808SoundState& sound);
} // namespace bbg
