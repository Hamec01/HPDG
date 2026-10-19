#include "ExternalDragWindowFade.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace bbg::ExternalDragWindowFade
{
namespace
{
#if JUCE_WINDOWS
constexpr BYTE kFadedAlpha = 56;           // about a fifth visible
constexpr double kMaxFrameAreaRatio = 2.0; // a frame bigger than this x the editor is the DAW, not the plugin window

// The window state before the step back, put back when the drag ends.
struct Faded
{
    HWND window = nullptr;
    LONG_PTR exStyle = 0;
    bool hadLayeredAlpha = false;
    BYTE alpha = 255;
    DWORD flags = 0;
    bool isChild = false;
    bool faded = false;
    bool behindSiblings = false;
};

double windowArea(HWND window)
{
    RECT r {};
    if (!GetWindowRect(window, &r))
        return 0.0;
    return static_cast<double>(r.right - r.left) * static_cast<double>(r.bottom - r.top);
}

bool isChildWindow(HWND window)
{
    return (GetWindowLongPtr(window, GWL_STYLE) & WS_CHILD) != 0;
}

// Outermost ancestor not much bigger than the editor; stops at the first top-level window.
HWND pluginFrame(HWND editor)
{
    if (editor == nullptr || !IsWindow(editor))
        return nullptr;
    const double editorArea = windowArea(editor);
    if (editorArea <= 0.0)
        return nullptr;
    HWND frame = editor;
    const HWND desktop = GetDesktopWindow();
    while (isChildWindow(frame))
    {
        const HWND parent = GetAncestor(frame, GA_PARENT);
        if (parent == nullptr || parent == desktop || windowArea(parent) > kMaxFrameAreaRatio * editorArea)
            break;
        frame = parent;
    }
    return frame;
}

void log(const juce::String& line)
{
    // A short trace in %TEMP% for checking a host where the window does not step back.
    juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("HPDG_drag.log")
        .appendText(juce::Time::getCurrentTime().toString(true, true) + " " + line + "\n", false, false, "\n");
}

juce::String describe(HWND window)
{
    if (window == nullptr)
        return "none";
    wchar_t className[128] {};
    GetClassNameW(window, className, 127);
    RECT r {};
    GetWindowRect(window, &r);
    return juce::String(juce::CharPointer_UTF16(reinterpret_cast<const juce::CharPointer_UTF16::CharType*>(className)))
           + " " + juce::String(r.right - r.left) + "x" + juce::String(r.bottom - r.top)
           + (isChildWindow(window) ? " child" : " top-level");
}

Faded stepBack(HWND window)
{
    Faded state;
    if (window == nullptr)
        return state;
    state.window = window;
    state.isChild = isChildWindow(window);
    state.exStyle = GetWindowLongPtr(window, GWL_EXSTYLE);
    if ((state.exStyle & WS_EX_LAYERED) != 0)
        state.hadLayeredAlpha = GetLayeredWindowAttributes(window, nullptr, &state.alpha, &state.flags) != 0;
    // Translucent and click-through (a layered child window needs Windows 8+; it may refuse).
    SetWindowLongPtr(window, GWL_EXSTYLE, state.exStyle | WS_EX_LAYERED | WS_EX_TRANSPARENT);
    state.faded = SetLayeredWindowAttributes(window, 0, kFadedAlpha, LWA_ALPHA) != 0;
    if (state.isChild)
        // Inside the DAW's main window: under the sibling windows (playlist, mixer ...).
        state.behindSiblings = SetWindowPos(window, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != 0;
    return state;
}

void restore(const Faded& state)
{
    if (state.window == nullptr || !IsWindow(state.window))
        return;
    if (state.hadLayeredAlpha)
        SetLayeredWindowAttributes(state.window, 0, state.alpha, state.flags);
    SetWindowLongPtr(state.window, GWL_EXSTYLE, state.exStyle);
    if (state.behindSiblings)
        SetWindowPos(state.window, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    RedrawWindow(state.window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

HWND editorWindow(juce::Component* source)
{
    auto* top = source != nullptr ? source->getTopLevelComponent() : nullptr;
    auto* peer = top != nullptr ? top->getPeer() : nullptr;
    return peer != nullptr ? static_cast<HWND>(peer->getNativeHandle()) : nullptr;
}
#endif
} // namespace

#if JUCE_WINDOWS
void* findPluginFrame(void* editorWindowHandle)
{
    return pluginFrame(static_cast<HWND>(editorWindowHandle));
}

bool fadeAndRestoreForTest(void* frame, bool& wasFaded, bool& wentBehindSiblings)
{
    const auto window = static_cast<HWND>(frame);
    const auto before = GetWindowLongPtr(window, GWL_EXSTYLE);
    const auto state = stepBack(window);
    wasFaded = state.faded;
    wentBehindSiblings = state.behindSiblings;
    restore(state);
    return GetWindowLongPtr(window, GWL_EXSTYLE) == before;
}
#endif

bool performFileDrag(const juce::String& path, juce::Component* source, std::function<void()> onEnd)
{
#if JUCE_WINDOWS
    const auto editor = editorWindow(source);
    const auto frame = pluginFrame(editor);
    const auto state = stepBack(frame);
    log("drag: editor " + describe(editor) + " | frame " + describe(frame) + " | faded " + juce::String(state.faded ? 1 : 0)
        + " behind " + juce::String(state.behindSiblings ? 1 : 0));
    // On Windows the drag runs on a worker thread; JUCE posts the end callback to the message thread.
    const bool started = juce::DragAndDropContainer::performExternalDragDropOfFiles({ path }, false, source,
                                                                                     [state, onEnd]
                                                                                     {
                                                                                         restore(state);
                                                                                         if (onEnd)
                                                                                             onEnd();
                                                                                     });
    if (!started)
        restore(state);
    return started;
#else
    return juce::DragAndDropContainer::performExternalDragDropOfFiles({ path }, false, source, std::move(onEnd));
#endif
}
} // namespace bbg::ExternalDragWindowFade
