#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace bbg
{
// While a MIDI / WAV file is dragged out into the DAW, the plugin window steps back so the drop target
// behind it is visible and reachable (maintainer, 2026-10-17); restored when the drag ends. Works on
// how hosts build plugin windows, not on host names (Windows):
//   - the plugin's frame = the outermost window around the editor that is not much bigger than it
//     (the host's plugin window with its title bar), never the DAW's main window;
//   - a top-level frame (Ableton, Reaper, Cubase, Studio One, Bitwig, a detached FL window) turns almost
//     transparent and lets the mouse through;
//   - a frame inside the DAW's main window (FL Studio's workspace) turns translucent and goes behind
//     its sibling windows (playlist, channel rack).
namespace ExternalDragWindowFade
{
// Starts the external file drag with the window stepped back; `onEnd` (may be empty) runs when it ends.
bool performFileDrag(const juce::String& path, juce::Component* source, std::function<void()> onEnd = {});

#if JUCE_WINDOWS
// For tests: the frame chosen for an editor window, and one fade / restore round.
void* findPluginFrame(void* editorWindow);
bool fadeAndRestoreForTest(void* frame, bool& wasFaded, bool& wentBehindSiblings);
#endif
} // namespace ExternalDragWindowFade
} // namespace bbg
