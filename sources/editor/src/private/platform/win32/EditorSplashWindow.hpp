#pragma once

#include <memory>

namespace kb::editor {

#if defined(_WIN32)

// The editor's startup screen: a borderless, centred window that shows what EditorStartupProgress
// reports until startup is done. It is on a thread of its own with its own message loop, so it keeps
// repainting (and Windows keeps calling it responsive) while the main thread is busy loading; the
// loading itself stays on the main thread and only the finished text and number cross over.
class EditorSplashWindow {
public:
    // Opens the window and becomes the progress sink; returns once it is on screen.
    EditorSplashWindow();
    // Closes it if it is still open.
    ~EditorSplashWindow();
    EditorSplashWindow(const EditorSplashWindow&) = delete;
    EditorSplashWindow& operator=(const EditorSplashWindow&) = delete;

    // Stops being the sink and closes the window. Safe to call twice.
    void Close();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#endif

} // namespace kb::editor
