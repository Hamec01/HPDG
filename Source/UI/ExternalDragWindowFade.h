#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace bbg
{
// Drag-out of a MIDI / WAV file into the DAW. Once the dragged file leaves the plugin window, that window
// is hidden so the drop target behind it is visible and reachable; it comes back (repainted) when the
// drag ends. Works on how hosts build plugin windows, not on host names (Windows):
//   - the plugin's frame = the outermost window around the editor that is not much bigger than it
//     (the host's plugin window with its title bar, FL Studio's TWPControl), never the DAW's main window.
// History (2026-10-19): a translucent / behind-siblings frame still took the drop in FL Studio, and
// hiding it on the press, with no repaint on the way back, left a blank window.
namespace ExternalDragWindowFade
{
// Starts the external file drag; `onEnd` (may be empty) runs on the message thread when it ends.
bool performFileDrag(const juce::String& path, juce::Component* source, std::function<void()> onEnd = {});

// True while a drag-out started here runs: HPDG's own drop zones must not take that file back.
bool isDragOutInProgress();

#if JUCE_WINDOWS
// For tests: the frame chosen for an editor window, and one hide / show round.
void* findPluginFrame(void* editorWindow);
bool hideAndShowForTest(void* frame, bool& wasHidden);
#endif
} // namespace ExternalDragWindowFade
} // namespace bbg
