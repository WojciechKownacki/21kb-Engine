#include "app/EditorWindowInvalidator.hpp"

#if defined(_WIN32)

#include "docking/EditorFloatingWindowManager.hpp"
#include "rendering/EditorPanelContentResolver.hpp"

namespace kb::editor {

void EditorWindowInvalidator::InvalidatePanel(HWND window, const RECT& panelRect) noexcept {
    // The back buffer keeps the previous frame and the painter is clipped to the dirty rect, so limiting
    // the invalidation to one panel means the rest of the editor - scene viewport included - is not
    // repainted at all. Graph interactions fire on every mouse move, so this is the difference between
    // repainting a panel and repainting the whole application per pointer event.
    if (window == nullptr || IsWindow(window) == 0) {
        return;
    }
    InvalidateRect(window, &panelRect, FALSE);
}

void EditorWindowInvalidator::InvalidateMainAndSource(HWND mainWindow, HWND sourceWindow) noexcept {
    InvalidateRect(mainWindow, nullptr, FALSE);
    if (sourceWindow != mainWindow) {
        InvalidateRect(sourceWindow, nullptr, FALSE);
    }
}

void EditorWindowInvalidator::InvalidateDockPanel(
    HWND mainWindow,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    DockPanelKind kind) noexcept {
    const auto invalidateForHost = [&](HWND window) {
        if (window == nullptr || IsWindow(window) == 0) {
            return;
        }
        if (const std::optional<RECT> panel = EditorPanelContentResolver::Resolve(
                kind, window, mainWindow, dockModel, floatingWindows, metrics)) {
            InvalidatePanel(window, *panel);
        }
    };

    invalidateForHost(mainWindow);
    for (HWND window : floatingWindows.Queries().Windows()) {
        invalidateForHost(window);
    }
}

} // namespace kb::editor

#endif
