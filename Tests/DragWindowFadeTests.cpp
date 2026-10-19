// The drag-out window step back (Source/UI/ExternalDragWindowFade.cpp) against real Win32 windows laid
// out the way hosts build plugin windows: a frame inside the DAW's main window next to a sibling
// (FL Studio's workspace) and a top-level frame (Ableton, Reaper, Cubase, Studio One, Bitwig).
#include <iostream>

#include <juce_gui_basics/juce_gui_basics.h>

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>

#include "../Source/UI/ExternalDragWindowFade.h"

namespace
{
int failures = 0;

void expect(bool condition, const char* message)
{
    std::cout << (condition ? "[PASS] " : "[FAIL] ") << message << "\n";
    failures += condition ? 0 : 1;
}

HWND make(HWND parent, int x, int y, int w, int h)
{
    const DWORD style = parent != nullptr ? (WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS) : (WS_OVERLAPPEDWINDOW | WS_VISIBLE);
    return CreateWindowExW(0, L"STATIC", L"", style, x, y, w, h, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    using namespace bbg::ExternalDragWindowFade;

    // Plugin window inside the DAW's main window (FL Studio).
    const HWND daw = make(nullptr, 0, 0, 1600, 1000);
    const HWND workspace = make(daw, 0, 0, 1560, 940);
    const HWND playlist = make(workspace, 0, 0, 1500, 900);
    const HWND frame = make(workspace, 100, 100, 640, 460);
    const HWND editor = make(frame, 10, 40, 600, 400);
    expect(findPluginFrame(editor) == frame, "Docked: the frame is the host's plugin window, not the workspace or the DAW.");
    bool faded = false, behind = false;
    expect(fadeAndRestoreForTest(frame, faded, behind), "Docked: the window style is restored after the drag.");
    expect(behind, "Docked: the plugin window goes behind its siblings (the playlist shows).");
    std::cout << "  docked translucent: " << (faded ? "yes" : "no (layered child windows not available here)") << "\n";
    expect(GetWindow(frame, GW_HWNDPREV) == nullptr, "Docked: the plugin window is back on top after the drag.");
    (void) playlist;

    // A floating plugin window (top-level).
    const HWND floating = make(nullptr, 200, 200, 640, 470);
    const HWND floatingEditor = make(floating, 10, 40, 600, 400);
    expect(findPluginFrame(floatingEditor) == floating, "Floating: the frame is the top-level plugin window.");
    expect(fadeAndRestoreForTest(floating, faded, behind), "Floating: the window style is restored after the drag.");
    expect(faded, "Floating: the plugin window turns translucent.");

    // An editor that fills its own top-level window.
    const HWND bare = make(nullptr, 300, 300, 600, 400);
    expect(findPluginFrame(bare) == bare, "Bare: a top-level editor is its own frame.");

    for (const HWND w : { editor, frame, playlist, workspace, daw, floatingEditor, floating, bare })
        DestroyWindow(w);
    return failures == 0 ? 0 : 1;
}
