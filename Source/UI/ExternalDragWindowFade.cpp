#include "ExternalDragWindowFade.h"

#include <atomic>
#include <memory>
#include <thread>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <shlobj.h>
 #include <shobjidl.h>
 #include <shlguid.h>
 #pragma comment(lib, "shell32.lib")
 #pragma comment(lib, "ole32.lib")
 #pragma comment(lib, "uuid.lib")
#endif

namespace bbg::ExternalDragWindowFade
{
namespace
{
// Drags out started here and not ended yet.
std::atomic<int> dragsInProgress { 0 };

#if JUCE_WINDOWS
constexpr int kMaxFrameChromePx = 160;     // a frame wider / taller than the editor by more is the DAW's area, not the plugin window
constexpr int kLeaveMarginPx = 8;          // the cursor must be this far outside the frame before it moves away
constexpr int kPollMs = 30;
constexpr int kReleaseGraceMs = 1500;      // button up this long with the drag not ended: restore the window anyway
constexpr int kOffscreenMarginPx = 64;

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

struct WindowPlacement
{
    HWND window = nullptr;
    HWND parent = nullptr;
    POINT position {};
    int width = 0;
    int height = 0;
    bool child = false;
    bool moved = false;
};

WindowPlacement moveFrameAway(HWND frame, bool asynchronous)
{
    WindowPlacement state;
    RECT rect {};
    if (frame == nullptr || !IsWindow(frame) || !GetWindowRect(frame, &rect))
        return state;

    state.window = frame;
    state.child = isChildWindow(frame);
    state.parent = state.child ? GetParent(frame) : nullptr;
    state.position = { rect.left, rect.top };
    if (state.child && (state.parent == nullptr || !ScreenToClient(state.parent, &state.position)))
        return {};

    state.width = rect.right - rect.left;
    state.height = rect.bottom - rect.top;
    const int x = state.child
        ? -state.width - kOffscreenMarginPx
        : GetSystemMetrics(SM_XVIRTUALSCREEN) - state.width - kOffscreenMarginPx;
    const int y = state.child ? -state.height - kOffscreenMarginPx : rect.top;
    UINT flags = SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE;
    if (asynchronous)
        flags |= SWP_ASYNCWINDOWPOS;
    state.moved = SetWindowPos(frame, nullptr, x, y, 0, 0, flags) != FALSE;
    return state;
}

void restoreFrame(const WindowPlacement& state, bool asynchronous)
{
    if (!state.moved || state.window == nullptr || !IsWindow(state.window))
        return;

    // Do not apply stale coordinates if the host has intentionally reparented or recreated the frame.
    if (isChildWindow(state.window) != state.child || (state.child && GetParent(state.window) != state.parent))
        return;

    UINT flags = SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE;
    if (asynchronous)
        flags |= SWP_ASYNCWINDOWPOS;
    SetWindowPos(state.window, nullptr, state.position.x, state.position.y, 0, 0, flags);
    RedrawWindow(state.window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
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
    DragWatcher(juce::Component* source, HWND frameToMove)
        : editor(source != nullptr ? source->getTopLevelComponent() : nullptr), frame(frameToMove)
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
        if (movedAway.load())
        {
            restoreFrame(placement, false);
            log("drag: window position restored");
        }
        repaintEditor(editor);
    }

    bool belongsTo(juce::Component* component) const
    {
        auto* top = component != nullptr ? component->getTopLevelComponent() : nullptr;
        return top != nullptr && editor.getComponent() == top;
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
            if (!movedAway.load())
            {
                const bool outside = haveGeometry
                    && (cursor.x < r.left - kLeaveMarginPx || cursor.x > r.right + kLeaveMarginPx
                        || cursor.y < r.top - kLeaveMarginPx || cursor.y > r.bottom + kLeaveMarginPx);
                if (outside && down)
                {
                    placement = moveFrameAway(frame, true);
                    movedAway = placement.moved;
                    log("drag: cursor left the window, moved away " + juce::String(placement.moved ? 1 : 0));
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
                // The drag end did not come: restore the window from here and let the editor repaint.
                restoreFrame(placement, true);
                log("drag: no end callback, window position restored");
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
    WindowPlacement placement;
    std::atomic<bool> stopRequested { false };
    std::atomic<bool> movedAway { false };
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

bool moveAwayAndRestoreForTest(void* frameHandle, bool& wasMoved)
{
    const auto frame = static_cast<HWND>(frameHandle);
    RECT rectBefore {};
    GetWindowRect(frame, &rectBefore);
    const auto parentBefore = GetParent(frame);
    const auto styleBefore = GetWindowLongPtr(frame, GWL_STYLE);
    const auto exStyleBefore = GetWindowLongPtr(frame, GWL_EXSTYLE);
    const bool visibleBefore = IsWindowVisible(frame) != FALSE;
    const auto placement = moveFrameAway(frame, false);
    RECT movedRect {};
    GetWindowRect(frame, &movedRect);
    wasMoved = placement.moved
        && IsWindowVisible(frame) != FALSE
        && EqualRect(&rectBefore, &movedRect) == FALSE;
    restoreFrame(placement, false);
    RECT after {};
    GetWindowRect(frame, &after);
    return GetParent(frame) == parentBefore
        && GetWindowLongPtr(frame, GWL_STYLE) == styleBefore
        && GetWindowLongPtr(frame, GWL_EXSTYLE) == exStyleBefore
        && (IsWindowVisible(frame) != FALSE) == visibleBefore
        && EqualRect(&rectBefore, &after) != FALSE;
}
#endif

bool performFileDrag(const juce::String& path, juce::Component* source, std::function<void()> onEnd)
{
#if JUCE_WINDOWS
    // Run OLE on the window's thread. JUCE's worker-based implementation can leave
    // an in-process host waiting on a cross-apartment transfer during shutdown.
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread()
        || !juce::File(path).existsAsFile())
        return false;
    int expected = 0;
    if (!dragsInProgress.compare_exchange_strong(expected, 1))
        return false;

    struct DragScope
    {
        ~DragScope()
        {
            activeWatcher.reset();
            dragsInProgress = 0;
        }
    } scope;

    const HRESULT oleResult = OleInitialize(nullptr);
    if (FAILED(oleResult))
    {
        log("drag: OleInitialize failed hr=" + juce::String::toHexString(static_cast<int>(oleResult)));
        return false;
    }
    struct OleScope { ~OleScope() { OleUninitialize(); } } oleScope;

    IShellItem* item = nullptr;
    IDataObject* data = nullptr;
    HRESULT result = SHCreateItemFromParsingName(path.toWideCharPointer(), nullptr, IID_PPV_ARGS(&item));
    if (SUCCEEDED(result))
    {
        result = item->BindToHandler(nullptr, BHID_DataObject, IID_PPV_ARGS(&data));
        item->Release();
    }
    if (FAILED(result))
    {
        log("drag: shell file data failed hr=" + juce::String::toHexString(static_cast<int>(result)));
        return false;
    }

    const auto editor = editorWindow(source);
    const auto frame = pluginFrame(editor);
    log("drag: shell UI-thread begin | frame " + describe(frame) + " | " + path);
    activeWatcher = std::make_unique<DragWatcher>(source, frame);
    DWORD effect = DROPEFFECT_NONE;
    // The Shell supplies a standard IDropSource and a real file IDataObject.
    result = SHDoDragDrop(editor, data, nullptr, DROPEFFECT_COPY, &effect);
    data->Release();
    activeWatcher.reset();
    dragsInProgress = 0;
    log("drag: shell end hr=" + juce::String::toHexString(static_cast<int>(result))
        + " effect=" + juce::String(static_cast<int>(effect)));
    if (onEnd)
        onEnd();
    return result == DRAGDROP_S_DROP || result == DRAGDROP_S_CANCEL;
#else
    ++dragsInProgress;
    const bool started = juce::DragAndDropContainer::performExternalDragDropOfFiles({ path }, false, source,
                                                                                     [onEnd]
                                                                                     {
                                                                                         --dragsInProgress;
                                                                                         if (onEnd)
                                                                                             onEnd();
                                                                                     });
    if (!started)
    {
        --dragsInProgress;
    }
    return started;
#endif
}

bool isDragOutInProgress()
{
    return dragsInProgress.load() > 0;
}

void editorClosing(juce::Component* editor)
{
#if JUCE_WINDOWS
    if (activeWatcher != nullptr && activeWatcher->belongsTo(editor))
    {
        log("drag: editor closing, restore native frame");
        activeWatcher.reset();
    }
#else
    juce::ignoreUnused(editor);
#endif
}
} // namespace bbg::ExternalDragWindowFade
