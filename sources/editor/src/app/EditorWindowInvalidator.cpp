#include "app/EditorWindowInvalidator.hpp"

#if defined(_WIN32)

namespace kb::editor {

void EditorWindowInvalidator::InvalidateMainAndSource(HWND mainWindow, HWND sourceWindow) noexcept {
    InvalidateRect(mainWindow, nullptr, FALSE);
    if (sourceWindow != mainWindow) {
        InvalidateRect(sourceWindow, nullptr, FALSE);
    }
}

} // namespace kb::editor

#endif
