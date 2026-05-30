#include "app/EditorWindowLifecycleHandler.hpp"

#if defined(_WIN32)
#include "kb/editor/docking/DockTypes.hpp"

namespace kb::editor {

EditorWindowLifecycleHandler::EditorWindowLifecycleHandler(HWND& mainWindow, bool& running, EditorDockModel& dockModel, EditorFloatingWindowManager& floatingWindows) noexcept
    : mainWindow_(mainWindow)
    , running_(running)
    , dockModel_(dockModel)
    , floatingWindows_(floatingWindows) {}

LRESULT EditorWindowLifecycleHandler::HandleClose(HWND messageWindow) {
    if (const std::uint32_t panelId = floatingWindows_.Queries().PanelId(messageWindow); panelId != 0) {
        floatingWindows_.Commands().Destroy(panelId);
        dockModel_.Commands().DockPanelTo(panelId, DockDropPreview{ .zone = DockDropZone::Bottom });
        InvalidateRect(mainWindow_, nullptr, FALSE);
        return 0;
    }

    running_ = false;
    DestroyWindow(mainWindow_);
    mainWindow_ = nullptr;
    PostQuitMessage(0);
    return 0;
}

LRESULT EditorWindowLifecycleHandler::HandleDestroy(HWND messageWindow) {
    if (floatingWindows_.Queries().IsFloatingWindow(messageWindow)) {
        floatingWindows_.Lifecycle().OnDestroyed(messageWindow);
        return 0;
    }

    if (mainWindow_ != nullptr && messageWindow == mainWindow_) {
        mainWindow_ = nullptr;
        running_ = false;
        PostQuitMessage(0);
    }

    return 0;
}

} // namespace kb::editor

#endif
