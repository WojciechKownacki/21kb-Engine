#include "assets/EditorAssetBrowserOverlayHitTester.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserGeometry.hpp"
#include "assets/EditorAssetBrowserState.hpp"

namespace kb::editor {

std::optional<EditorAssetBrowserHit> EditorAssetBrowserOverlayHitTester::HitTestDeleteConfirm(
    const RECT& content,
    int x,
    int y,
    const EditorAssetBrowserState& state,
    const RECT* overlayBounds) {
    if (!state.IsDeleteConfirmOpen()) {
        return std::nullopt;
    }

    const RECT bounds = overlayBounds != nullptr ? *overlayBounds : content;
    const RECT dialog = EditorAssetBrowserGeometry::DeleteConfirmRect(bounds, state.DeleteConfirmOffsetX(), state.DeleteConfirmOffsetY());
    if (EditorAssetBrowserGeometry::Contains(EditorAssetBrowserGeometry::DeleteConfirmAcceptRect(dialog), x, y)) {
        return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::DeleteConfirmAccept };
    }
    if (EditorAssetBrowserGeometry::Contains(EditorAssetBrowserGeometry::DeleteConfirmCancelRect(dialog), x, y)) {
        return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::DeleteConfirmCancel };
    }
    return EditorAssetBrowserGeometry::Contains(dialog, x, y)
        ? EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::DeleteConfirmBody }
        : EditorAssetBrowserHit{};
}

std::optional<EditorAssetBrowserHit> EditorAssetBrowserOverlayHitTester::HitTestContextMenu(
    const RECT& content,
    int x,
    int y,
    const EditorAssetBrowserState& state,
    const kb::assets::AssetManager& manager) {
    if (!state.IsContextMenuOpen()) {
        return std::nullopt;
    }

    const std::vector<EditorAssetContextMenuItem> items = state.ContextMenuItems(manager);
    if (items.empty()) {
        return std::nullopt;
    }

    const RECT menu = EditorAssetBrowserLayout::ContextMenuRect(content, state.ContextMenuX(), state.ContextMenuY(), static_cast<int>(items.size()));
    if (!EditorAssetBrowserGeometry::Contains(menu, x, y)) {
        return std::nullopt;
    }

    for (std::size_t index = 0; index < items.size(); ++index) {
        if (EditorAssetBrowserGeometry::Contains(EditorAssetBrowserLayout::ContextMenuItemRect(menu, static_cast<int>(index)), x, y)) {
            return EditorAssetBrowserHit{
                .kind = EditorAssetBrowserHitKind::ContextMenuCommand,
                .index = index,
                .command = items[index].command,
            };
        }
    }
    return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::ContextMenuBody };
}

std::optional<EditorAssetBrowserHit> EditorAssetBrowserOverlayHitTester::HitTestDropActionMenu(
    const RECT& content,
    int x,
    int y,
    const EditorAssetBrowserState& state) {
    if (!state.IsDropActionMenuOpen()) {
        return std::nullopt;
    }

    const RECT menu = EditorAssetBrowserLayout::ContextMenuRect(content, state.DropActionMenuX(), state.DropActionMenuY(), 2);
    if (!EditorAssetBrowserGeometry::Contains(menu, x, y)) {
        return EditorAssetBrowserHit{};
    }

    for (int index = 0; index < 2; ++index) {
        if (EditorAssetBrowserGeometry::Contains(EditorAssetBrowserLayout::ContextMenuItemRect(menu, index), x, y)) {
            return EditorAssetBrowserHit{
                .kind = EditorAssetBrowserHitKind::DropActionCommand,
                .index = static_cast<std::size_t>(index),
                .dropAction = index == 0 ? EditorAssetDropAction::MoveHere : EditorAssetDropAction::CopyHere,
            };
        }
    }
    return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::DropActionBody };
}

} // namespace kb::editor

#endif
