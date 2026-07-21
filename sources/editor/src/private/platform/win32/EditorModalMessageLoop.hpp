#pragma once

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdint>

namespace kb::editor {

// How a modal dialog's message pump ended.
enum class EditorModalLoopExit : std::uint8_t {
    Completed, // the dialog closed itself (OK / Cancel / WM_CLOSE)
    Quit,      // the queue delivered WM_QUIT: the app is shutting down under the dialog
    Abandoned, // the dialog window was destroyed from outside, so it will never finish on its own
    Error,     // GetMessageW failed; the queue is unusable
};

// The one message pump every editor dialog runs, so they all end the same way.
//
// A hand-written `while (!done && GetMessageW(...) > 0)` loop has three holes:
//
//  - WM_QUIT stops the loop but is consumed. The editor still shuts down, because the quit paths clear the
//    application's own `running` flag first, but the exit code is lost and any other consumer of the quit
//    (a nesting loop, a future host) never sees it. Re-posting is the honest thing to do.
//  - The loop exits with the dialog still alive, its GWLP_USERDATA pointing at state that is about to leave
//    the caller's scope. The exit code tells the caller the dialog did not close itself, so it can tear the
//    window down first.
//  - Worse: if the window is destroyed from outside (its owner is closed, which destroys owned windows),
//    the dialog's "done" flag is never set and no quit ever arrives, so the pump spins forever and the
//    editor freezes. Watching the handle is the only way out of that.
//
// `dialogNavigation` turns on IsDialogMessageW (tab/Enter/Escape between child controls). Windows that
// handle their own keys must leave it off, or dialog navigation eats their keystrokes.
template <typename FinishedPredicate>
[[nodiscard]] inline EditorModalLoopExit RunEditorModalMessageLoop(
    HWND window,
    bool dialogNavigation,
    FinishedPredicate finished) {
    MSG message{};
    while (!finished()) {
        if (window != nullptr && IsWindow(window) == 0) {
            return EditorModalLoopExit::Abandoned;
        }
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result == 0) {
            PostQuitMessage(static_cast<int>(message.wParam));
            return EditorModalLoopExit::Quit;
        }
        if (result == -1) {
            return EditorModalLoopExit::Error;
        }
        if (!dialogNavigation || window == nullptr || IsWindow(window) == 0 ||
            IsDialogMessageW(window, &message) == 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    return EditorModalLoopExit::Completed;
}

} // namespace kb::editor

#endif
