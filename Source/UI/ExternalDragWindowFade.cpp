#include "ExternalDragWindowFade.h"

#include <atomic>
#include <memory>
#include <thread>

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
// Drags out started here and not ended yet.
std::atomic<int> dragsInProgress { 0 };

#if JUCE_WINDOWS
constexpr int kMaxFrameChromePx = 160;     // a frame wider / taller than the editor by more is the DAW's area, not the plugin window
constexpr int kLeaveMarginPx = 8;          // the cursor must be this far outside the frame before it hides
constexpr int kPollMs = 30;
constexpr int kReleaseGraceMs = 1500;      // button up this long with the drag not ended: show the window anyway

bool sizeOf(HWND window, int& width, int& height)
{
    RECT r {};
    if (!GetWindowRect(window, &r))
        return false;
    width = r.right - r.left;
    height = r.bottom - r.top;
    return width > 0 && height > 0;
}

bool isChildWindow(HWND window)
{
    return (GetWindowLongPtr(window, GWL_STYLE) & WS_CHILD) != 0;
}

// Outermost ancestor that is only the editor plus a title bar and borders; stops at the first top-level
// window. FL Studio puts its plugin window (title "HPDG (Master)") inside a TWPControl that spans the
// whole workspace (1548x950 around a 1460x520 editor); an area rule took that and hid the workspace.
HWND pluginFrame(HWND editor)
{
    int editorWidth = 0, editorHeight = 0;
    if (editor == nullptr || !IsWindow(editor) || !sizeOf(editor, editorWidth, editorHeight))
        return nullptr;
    HWND frame = editor;
    const HWND desktop = GetDesktopWindow();
    while (isChildWindow(frame))
    {
        const HWND parent = GetAncestor(frame, GA_PARENT);
        int width = 0, height = 0;
        if (parent == nullptr || parent == desktop || !sizeOf(parent, width, height)
            || width > editorWidth + kMaxFrameChromePx || height > editorHeight + kMaxFrameChromePx)
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

bool hideFrame(HWND frame)
{
    return SetWindowPos(frame, nullptr, 0, 0, 0, 0,
                        SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE) != 0;
}

void showFrame(HWND frame)
{
    if (frame == nullptr || !IsWindow(frame))
        return;
    SetWindowPos(frame, nullptr, 0, 0, 0, 0, SWP_SHOWWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    RedrawWindow(frame, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

HWND editorWindow(juce::Component* source)
{
    auto* top = source != nullptr ? source->getTopLevelComponent() : nullptr;
    auto* peer = top != nullptr ? top->getPeer() : nullptr;
    return peer != nullptr ? static_cast<HWND>(peer->getNativeHandle()) : nullptr;
}

bool buttonDown()
{
    return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 || (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
}

// Watches one drag on its own thread (the host's message loop may not run our timers while a drag is
// on): hides the frame once the cursor leaves it, asynchronously so the host's UI thread is never
// waited on. The drag end (message thread) shows it again; if that end never comes, the watcher shows
// it itself once the button has been up for a while.
class DragWatcher final
{
public:
    DragWatcher(juce::Component* source, HWND frameToHide)
        : editor(source != nullptr ? source->getTopLevelComponent() : nullptr), frame(frameToHide)
    {
        if (frame != nullptr)
            poller = std::thread([this] { run(); });
    }

    // Message thread: the drag ended (or a new one starts).
    ~DragWatcher()
    {
        stopRequested = true;
        if (poller.joinable())
            poller.join();
        if (hidden.load())
        {
            showFrame(frame);
            log("drag: window shown again");
        }
        repaintEditor(editor);
    }

private:
    void run()
    {
        bool first = true;
        int releasedForMs = 0;
        while (!stopRequested.load())
        {
            if (!IsWindow(frame))
                return;
            const bool down = buttonDown();
            POINT cursor {};
            RECT r {};
            const bool haveGeometry = GetCursorPos(&cursor) && GetWindowRect(frame, &r);
            if (first)
            {
                first = false;
                log("drag: watch start button " + juce::String(down ? 1 : 0) + " cursor " + juce::String(cursor.x) + ","
                    + juce::String(cursor.y) + " frame " + juce::String(r.left) + "," + juce::String(r.top) + " - "
                    + juce::String(r.right) + "," + juce::String(r.bottom));
            }
            if (!hidden.load())
            {
                const bool outside = haveGeometry
                    && (cursor.x < r.left - kLeaveMarginPx || cursor.x > r.right + kLeaveMarginPx
                        || cursor.y < r.top - kLeaveMarginPx || cursor.y > r.bottom + kLeaveMarginPx);
                if (outside && down)
                {
                    const bool ok = SetWindowPos(frame, nullptr, 0, 0, 0, 0,
                                                 SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE
                                                     | SWP_ASYNCWINDOWPOS) != 0;
                    hidden = ok;
                    log("drag: cursor left the window, hidden " + juce::String(ok ? 1 : 0));
                }
                else if (!down && (releasedForMs += kPollMs) >= kReleaseGraceMs)
                {
                    return; // dropped without leaving the window: nothing to hide
                }
            }
            else if (down)
            {
                releasedForMs = 0;
            }
            else if ((releasedForMs += kPollMs) >= kReleaseGraceMs)
            {
                // The drag end did not come: show the window from here and let the editor repaint.
                SetWindowPos(frame, nullptr, 0, 0, 0, 0,
                             SWP_SHOWWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
                hidden = false;
                log("drag: no end callback, window shown");
                juce::MessageManager::callAsync([safeEditor = editor] { repaintEditor(safeEditor); });
                return;
            }
            Sleep(static_cast<DWORD>(kPollMs));
        }
    }

    static void repaintEditor(const juce::Component::SafePointer<juce::Component>& editor)
    {
        if (editor == nullptr)
            return;
        editor->repaint();
        if (auto* peer = editor->getPeer())
            RedrawWindow(static_cast<HWND>(peer->getNativeHandle()), nullptr, nullptr,
                         RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        // Some hosts touch their plugin window right after a drop; paint once more a moment later.
        juce::Timer::callAfterDelay(150, [editor]
                                    {
                                        if (editor != nullptr)
                                            editor->repaint();
                                    });
    }

    juce::Component::SafePointer<juce::Component> editor;
    HWND frame = nullptr;
    std::atomic<bool> stopRequested { false };
    std::atomic<bool> hidden { false };
    std::thread poller;
};

std::unique_ptr<DragWatcher> activeWatcher;

#endif
} // namespace

#if JUCE_WINDOWS
void* findPluginFrame(void* editorWindowHandle)
{
    return pluginFrame(static_cast<HWND>(editorWindowHandle));
}

bool hideAndShowForTest(void* frameHandle, bool& wasHidden)
{
    const auto frame = static_cast<HWND>(frameHandle);
    const auto before = GetWindowLongPtr(frame, GWL_EXSTYLE);
    wasHidden = hideFrame(frame) && !IsWindowVisible(frame);
    showFrame(frame);
    return GetWindowLongPtr(frame, GWL_EXSTYLE) == before && IsWindowVisible(frame);
}
#endif

bool performFileDrag(const juce::String& path, juce::Component* source, std::function<void()> onEnd)
{
    ++dragsInProgress;
#if JUCE_WINDOWS
    const auto editor = editorWindow(source);
    const auto frame = pluginFrame(editor);
    juce::String chain;
    for (HWND w = editor; w != nullptr && w != GetDesktopWindow(); w = isChildWindow(w) ? GetAncestor(w, GA_PARENT) : nullptr)
        chain << (chain.isEmpty() ? "" : " < ") << describe(w);
    log("drag: frame " + describe(frame) + " | chain " + chain);
    activeWatcher.reset(); // a previous drag's window comes back first
    activeWatcher = std::make_unique<DragWatcher>(source, frame);
#endif
    // On Windows the drag runs on a worker thread; JUCE posts the end callback to the message thread.
    const bool started = juce::DragAndDropContainer::performExternalDragDropOfFiles({ path }, false, source,
                                                                                     [onEnd]
                                                                                     {
#if JUCE_WINDOWS
                                                                                         activeWatcher.reset();
#endif
                                                                                         --dragsInProgress;
                                                                                         if (onEnd)
                                                                                             onEnd();
                                                                                     });
    if (!started)
    {
#if JUCE_WINDOWS
        activeWatcher.reset();
#endif
        --dragsInProgress;
    }
    return started;
}

bool isDragOutInProgress()
{
    return dragsInProgress.load() > 0;
}
} // namespace bbg::ExternalDragWindowFade
