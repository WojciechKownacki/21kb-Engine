#include "app/EditorHierarchySearchInputHandler.hpp"

#if defined(_WIN32)
#include "app/EditorTextInputShortcuts.hpp"

#include <optional>
#include <string>

namespace kb::editor {
namespace {

[[nodiscard]] bool HandleRenameShortcut(HWND owner, EditorSceneContext& sceneContext, WPARAM key) {
    switch (EditorTextInputShortcuts::Resolve(key)) {
    case EditorTextInputShortcut::SelectAll:
        sceneContext.SelectAllHierarchyRename();
        return true;
    case EditorTextInputShortcut::Copy:
        static_cast<void>(EditorTextInputShortcuts::CopyToClipboard(owner, sceneContext.HierarchyRenameBuffer()));
        return true;
    case EditorTextInputShortcut::Cut:
        static_cast<void>(EditorTextInputShortcuts::CopyToClipboard(owner, sceneContext.HierarchyRenameBuffer()));
        sceneContext.ClearHierarchyRename();
        return true;
    case EditorTextInputShortcut::Paste:
        if (const std::optional<std::string> text = EditorTextInputShortcuts::PasteFromClipboard(owner); text.has_value()) {
            sceneContext.InsertHierarchyRenameText(*text);
        }
        return true;
    case EditorTextInputShortcut::None:
        return false;
    }
    return false;
}

[[nodiscard]] bool HandleSearchShortcut(HWND owner, EditorSceneContext& sceneContext, WPARAM key) {
    switch (EditorTextInputShortcuts::Resolve(key)) {
    case EditorTextInputShortcut::SelectAll:
        sceneContext.SelectAllHierarchySearch();
        return true;
    case EditorTextInputShortcut::Copy:
        static_cast<void>(EditorTextInputShortcuts::CopyToClipboard(owner, sceneContext.HierarchySearchQuery()));
        return true;
    case EditorTextInputShortcut::Cut:
        static_cast<void>(EditorTextInputShortcuts::CopyToClipboard(owner, sceneContext.HierarchySearchQuery()));
        sceneContext.ClearHierarchySearch();
        return true;
    case EditorTextInputShortcut::Paste:
        if (const std::optional<std::string> text = EditorTextInputShortcuts::PasteFromClipboard(owner); text.has_value()) {
            sceneContext.InsertHierarchySearchText(*text);
        }
        return true;
    case EditorTextInputShortcut::None:
        return false;
    }
    return false;
}

} // namespace

EditorHierarchySearchInputHandler::EditorHierarchySearchInputHandler(HWND mainWindow, EditorSceneContext& sceneContext) noexcept
    : mainWindow_(mainWindow)
    , sceneContext_(sceneContext) {}

bool EditorHierarchySearchInputHandler::HandleChar(HWND messageWindow, WPARAM wparam) const {
    if (sceneContext_.IsHierarchyRenaming()) {
        if (wparam == VK_BACK || wparam == VK_ESCAPE || wparam == VK_RETURN) {
            return false;
        }
        sceneContext_.AppendHierarchyRenameText(static_cast<wchar_t>(wparam));
        Invalidate(messageWindow);
        return true;
    }

    if (!sceneContext_.IsHierarchySearchFocused()) {
        return false;
    }

    sceneContext_.AppendHierarchySearchText(static_cast<wchar_t>(wparam));
    Invalidate(messageWindow);
    return true;
}

bool EditorHierarchySearchInputHandler::HandleKeyDown(HWND messageWindow, WPARAM wparam) const {
    if (sceneContext_.IsHierarchyRenaming()) {
        if (HandleRenameShortcut(messageWindow, sceneContext_, wparam)) {
            Invalidate(messageWindow);
            return true;
        }
        switch (wparam) {
        case VK_BACK:
            sceneContext_.BackspaceHierarchyRename();
            Invalidate(messageWindow);
            return true;
        case VK_RETURN:
            static_cast<void>(sceneContext_.CommitHierarchyRename());
            Invalidate(messageWindow);
            return true;
        case VK_ESCAPE:
            sceneContext_.CancelHierarchyRename();
            Invalidate(messageWindow);
            return true;
        default:
            return false;
        }
    }

    if (!sceneContext_.IsHierarchySearchFocused()) {
        if (wparam == VK_F2 && sceneContext_.BeginHierarchyRename()) {
            Invalidate(messageWindow);
            return true;
        }
        if (wparam == VK_DELETE && sceneContext_.DeleteSelectedHierarchyEntity()) {
            Invalidate(messageWindow);
            return true;
        }
        return false;
    }

    if (HandleSearchShortcut(messageWindow, sceneContext_, wparam)) {
        Invalidate(messageWindow);
        return true;
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
