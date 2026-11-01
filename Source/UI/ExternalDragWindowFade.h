#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace bbg
{
// Drag-out of a MIDI / WAV file into the DAW. Once the dragged file leaves the plugin window, that window
// is moved outside its parent's visible area so the drop target behind it is reachable; it comes back
// (repainted) when the drag ends. The host-owned window is never hidden, restyled or reordered.
// Works on how hosts build plugin windows, not on host names (Windows):
//   - the plugin's frame = the outermost window around the editor that is not much bigger than it
//     (the host's plugin window with its title bar, FL Studio's TWPControl), never the DAW's main window.
// History (2026-10-19): a translucent / behind-siblings frame still took the drop in FL Studio, and
// hiding it on the press, with no repaint on the way back, left a blank window.
namespace ExternalDragWindowFade
{
// Windows: runs the native Shell drag loop on the message thread until drop/cancel;
// other platforms use JUCE's asynchronous implementation. onEnd runs on the message thread.
bool performFileDrag(const juce::String& path, juce::Component* source, std::function<void()> onEnd = {});

// True while a drag-out started here runs: HPDG's own drop zones must not take that file back.
bool isDragOutInProgress();

// Restores a temporarily moved host frame before an editor is destroyed or rebuilt. The native OLE drag
// still owns its normal lifetime and completion callback; this only guarantees that no host window state
// remains changed when the JUCE source component disappears.
void editorClosing(juce::Component* editor);

#if JUCE_WINDOWS
// For tests: the frame chosen for an editor window, and one move-away / restore round.
void* findPluginFrame(void* editorWindow);
bool moveAwayAndRestoreForTest(void* frame, bool& wasMoved);
#endif
} // namespace ExternalDragWindowFade
} // namespace bbg
