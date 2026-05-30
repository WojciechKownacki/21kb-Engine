#include "app/EditorHierarchySearchInputHandler.hpp"

#if defined(_WIN32)

namespace kb::editor {

EditorHierarchySearchInputHandler::EditorHierarchySearchInputHandler(HWND mainWindow, EditorSceneContext& sceneContext) noexcept
    : mainWindow_(mainWindow)
    , sceneContext_(sceneContext) {}

bool EditorHierarchySearchInputHandler::HandleChar(HWND messageWindow, WPARAM wparam) const {
    if (!sceneContext_.IsHierarchySearchFocused()) {
        return false;
    }

    sceneContext_.AppendHierarchySearchText(static_cast<wchar_t>(wparam));
    Invalidate(messageWindow);
    return true;
}

bool EditorHierarchySearchInputHandler::HandleKeyDown(HWND messageWindow, WPARAM wparam) const {
    if (!sceneContext_.IsHierarchySearchFocused()) {
        return false;
    }

    if (wparam == VK_BACK) {
        sceneContext_.BackspaceHierarchySearch();
    } else if (wparam == VK_ESCAPE) {
        sceneContext_.ClearHierarchySearch();
        sceneContext_.FocusHierarchySearch(false);
    } else {
        return false;
    }

    Invalidate(messageWindow);
    return true;
}

void EditorHierarchySearchInputHandler::Invalidate(HWND messageWindow) const {
    InvalidateRect(mainWindow_, nullptr, FALSE);
    if (messageWindow != mainWindow_) {
        InvalidateRect(messageWindow, nullptr, FALSE);
    }
}

} // namespace kb::editor

#endif
