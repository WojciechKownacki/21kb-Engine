#include "app/EditorSkeletalMeshTreeSearchInputHandler.hpp"

#if defined(_WIN32)
#include "app/EditorTextInputShortcuts.hpp"

#include <optional>
#include <string>

namespace kb::editor {

EditorSkeletalMeshTreeSearchInputHandler::EditorSkeletalMeshTreeSearchInputHandler(HWND mainWindow, EditorSceneContext& sceneContext) noexcept
    : mainWindow_(mainWindow)
    , sceneContext_(sceneContext) {}

bool EditorSkeletalMeshTreeSearchInputHandler::HandleChar(HWND messageWindow, WPARAM wparam) const {
    if (!sceneContext_.IsSkeletalMeshEditorTreeSearchFocused() || wparam < 0x20U) return false;
    sceneContext_.AppendSkeletalMeshEditorTreeSearchText(static_cast<wchar_t>(wparam));
    Invalidate(messageWindow);
    return true;
}

bool EditorSkeletalMeshTreeSearchInputHandler::HandleKeyDown(HWND messageWindow, WPARAM wparam) const {
    if (!sceneContext_.IsSkeletalMeshEditorTreeSearchFocused()) return false;
    switch (EditorTextInputShortcuts::Resolve(wparam)) {
    case EditorTextInputShortcut::SelectAll:
        sceneContext_.SelectAllSkeletalMeshEditorTreeSearch();
        break;
    case EditorTextInputShortcut::Copy:
        static_cast<void>(EditorTextInputShortcuts::CopyToClipboard(messageWindow, sceneContext_.SkeletalMeshEditorTreeFilter()));
        break;
    case EditorTextInputShortcut::Cut:
        static_cast<void>(EditorTextInputShortcuts::CopyToClipboard(messageWindow, sceneContext_.SkeletalMeshEditorTreeFilter()));
        sceneContext_.ClearSkeletalMeshEditorTreeSearch();
        break;
    case EditorTextInputShortcut::Paste:
        if (const std::optional<std::string> text = EditorTextInputShortcuts::PasteFromClipboard(messageWindow); text.has_value()) {
            sceneContext_.InsertSkeletalMeshEditorTreeSearchText(*text);
        }
        break;
    case EditorTextInputShortcut::None:
        if (wparam == VK_BACK) {
            sceneContext_.BackspaceSkeletalMeshEditorTreeSearch();
        } else if (wparam == VK_ESCAPE) {
            sceneContext_.ClearSkeletalMeshEditorTreeSearch();
            sceneContext_.FocusSkeletalMeshEditorTreeSearch(false);
        } else {
            return false;
        }
        break;
    }
    Invalidate(messageWindow);
    return true;
}

void EditorSkeletalMeshTreeSearchInputHandler::Invalidate(HWND messageWindow) const {
    InvalidateRect(mainWindow_, nullptr, FALSE);
    if (messageWindow != mainWindow_) InvalidateRect(messageWindow, nullptr, FALSE);
}

} // namespace kb::editor

#endif
