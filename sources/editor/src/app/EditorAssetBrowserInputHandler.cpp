#include "app/EditorAssetBrowserInputHandler.hpp"

#if defined(_WIN32)

namespace kb::editor {

EditorAssetBrowserInputHandler::EditorAssetBrowserInputHandler(HWND mainWindow, EditorSceneContext& sceneContext) noexcept
    : mainWindow_(mainWindow)
    , sceneContext_(sceneContext) {}

bool EditorAssetBrowserInputHandler::HandleChar(HWND messageWindow, WPARAM wparam) const {
    if (sceneContext_.AssetBrowser().IsTextEditing()) {
        if (wparam == VK_BACK || wparam == VK_ESCAPE || wparam == VK_RETURN) {
            return false;
        }
        sceneContext_.AssetBrowser().AppendTextEdit(static_cast<wchar_t>(wparam));
        Invalidate(messageWindow);
        return true;
    }

    if (!sceneContext_.AssetBrowser().IsSearchFocused()) {
        return false;
    }

    if (wparam == VK_BACK || wparam == VK_ESCAPE || wparam == VK_RETURN) {
        return false;
    }

    sceneContext_.AssetBrowser().AppendSearchText(static_cast<wchar_t>(wparam));
    Invalidate(messageWindow);
    return true;
}

bool EditorAssetBrowserInputHandler::HandleKeyDown(HWND messageWindow, WPARAM wparam) const {
    if (sceneContext_.AssetBrowser().IsDeleteConfirmOpen()) {
        switch (wparam) {
        case VK_RETURN:
            sceneContext_.AssetBrowser().CloseDeleteConfirm();
            static_cast<void>(sceneContext_.DeleteSelectedAssetBrowserItem());
            Invalidate(messageWindow);
            return true;
        case VK_ESCAPE:
            sceneContext_.AssetBrowser().CloseDeleteConfirm();
            Invalidate(messageWindow);
            return true;
        default:
            return false;
        }
    }

    if (sceneContext_.AssetBrowser().IsTextEditing()) {
        switch (wparam) {
        case VK_BACK:
            sceneContext_.AssetBrowser().BackspaceTextEdit();
            Invalidate(messageWindow);
            return true;
        case VK_RETURN:
            static_cast<void>(sceneContext_.CommitAssetTextEdit());
            Invalidate(messageWindow);
            return true;
        case VK_ESCAPE:
            sceneContext_.CancelAssetTextEdit();
            Invalidate(messageWindow);
            return true;
        default:
            return false;
        }
    }

    if (!sceneContext_.AssetBrowser().IsSearchFocused()) {
        if (sceneContext_.AssetBrowser().IsContextMenuOpen()) {
            if (wparam == VK_ESCAPE) {
                sceneContext_.AssetBrowser().CloseContextMenu();
                Invalidate(messageWindow);
                return true;
            }
            return false;
        }
        if (wparam == VK_F2 && sceneContext_.BeginAssetRename()) {
            Invalidate(messageWindow);
            return true;
        }
        if (wparam == VK_DELETE && sceneContext_.AssetBrowser().IsSelectionFocused()) {
            if (sceneContext_.AssetBrowser().SelectionKind() == EditorAssetBrowserSelectionKind::Folder) {
                if (sceneContext_.AssetBrowser().OpenDeleteConfirm()) {
                    Invalidate(messageWindow);
                    return true;
                }
            } else if (sceneContext_.DeleteSelectedAssetBrowserItem()) {
                Invalidate(messageWindow);
                return true;
            }
        }
        return false;
    }

    switch (wparam) {
    case VK_BACK:
        sceneContext_.AssetBrowser().BackspaceSearch();
        Invalidate(messageWindow);
        return true;
    case VK_ESCAPE:
        sceneContext_.AssetBrowser().FocusSearch(false);
        Invalidate(messageWindow);
        return true;
    default:
        return false;
    }
}

void EditorAssetBrowserInputHandler::Invalidate(HWND messageWindow) const {
    InvalidateRect(mainWindow_, nullptr, FALSE);
    if (messageWindow != mainWindow_) {
        InvalidateRect(messageWindow, nullptr, FALSE);
    }
}

} // namespace kb::editor

#endif
