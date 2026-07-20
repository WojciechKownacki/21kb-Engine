#include "app/EditorWindowInvalidator.hpp"

#if defined(_WIN32)

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

} // namespace kb::editor

#endif
