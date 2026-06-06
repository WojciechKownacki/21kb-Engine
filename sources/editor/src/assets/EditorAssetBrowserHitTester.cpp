#include "assets/EditorAssetBrowserHitTester.hpp"

#if defined(_WIN32)
#include "assets/EditorAssetBrowserBreadcrumbHitTester.hpp"
#include "assets/EditorAssetBrowserChromeHitTester.hpp"
#include "assets/EditorAssetBrowserContentHitTester.hpp"
#include "assets/EditorAssetBrowserGeometry.hpp"
#include "assets/EditorAssetBrowserHitPayloadResolver.hpp"
#include "assets/EditorAssetBrowserOverlayHitTester.hpp"
#include "assets/EditorAssetBrowserState.hpp"
#include "assets/EditorAssetBrowserTreeHitTester.hpp"

namespace kb::editor {

EditorAssetBrowserHit EditorAssetBrowserHitTester::HitTest(
    const RECT& content,
    int x,
    int y,
    const EditorAssetBrowserState& state,
    const kb::assets::AssetManager& manager,
    const RECT* overlayBounds) {
    const EditorAssetBrowserLayoutRects layout = EditorAssetBrowserLayout::Build(content);
    if (const std::optional<EditorAssetBrowserHit> hit = EditorAssetBrowserOverlayHitTester::HitTestDeleteConfirm(content, x, y, state, overlayBounds)) {
        return *hit;
    }

    if (const std::optional<EditorAssetBrowserHit> hit = EditorAssetBrowserOverlayHitTester::HitTestDropActionMenu(content, x, y, state)) {
        return *hit;
    }

    if (state.IsFilterMenuOpen()) {
        const RECT filterMenu = EditorAssetBrowserLayout::FilterMenuRect(layout);
        if (EditorAssetBrowserGeometry::Contains(EditorAssetBrowserLayout::FilterMenuItemRect(filterMenu, 0), x, y)) {
            return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::FilterFolder };
        }
        if (EditorAssetBrowserGeometry::Contains(EditorAssetBrowserLayout::FilterMenuItemRect(filterMenu, 1), x, y)) {
            return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::FilterTemplate };
        }
        if (EditorAssetBrowserGeometry::Contains(filterMenu, x, y)) {
            return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::DropActionBody };
        }
    }

    if (!EditorAssetBrowserGeometry::Contains(layout.frame, x, y)) {
        return {};
    }

    if (const std::optional<EditorAssetBrowserHit> hit = EditorAssetBrowserOverlayHitTester::HitTestContextMenu(content, x, y, state, manager)) {
        return *hit;
    }

    if (const std::optional<EditorAssetBrowserHit> hit = EditorAssetBrowserChromeHitTester::HitTest(layout, x, y, state)) {
        return *hit;
    }

    if (const std::optional<EditorAssetBrowserHit> hit = EditorAssetBrowserTreeHitTester::HitTest(layout, x, y, state, manager)) {
        return *hit;
    }

    if (const std::optional<EditorAssetBrowserHit> hit = EditorAssetBrowserContentHitTester::HitTest(layout, x, y, state, manager)) {
        return *hit;
    }

    if (EditorAssetBrowserGeometry::Contains(layout.assetView, x, y) || EditorAssetBrowserGeometry::Contains(layout.tree, x, y)) {
        return EditorAssetBrowserHit{ .kind = EditorAssetBrowserHitKind::DropTarget };
    }
    return {};
}

std::optional<std::filesystem::path> EditorAssetBrowserHitTester::PrefabAssetAt(
    const RECT& content,
    int x,
    int y,
    const EditorAssetBrowserState& state,
    const kb::assets::AssetManager& manager) {
    return EditorAssetBrowserHitPayloadResolver::PrefabAssetAt(HitTest(content, x, y, state, manager), state, manager);
}

std::optional<kb::assets::AssetId> EditorAssetBrowserHitTester::AssetIdAt(
    const RECT& content,
    int x,
    int y,
    const EditorAssetBrowserState& state,
    const kb::assets::AssetManager& manager) {
    return EditorAssetBrowserHitPayloadResolver::AssetIdAt(HitTest(content, x, y, state, manager), state, manager);
}

std::optional<kb::assets::AssetMetadata> EditorAssetBrowserHitTester::AssetMetadataAt(
    const RECT& content,
    int x,
    int y,
    const EditorAssetBrowserState& state,
    const kb::assets::AssetManager& manager) {
    return EditorAssetBrowserHitPayloadResolver::AssetMetadataAt(HitTest(content, x, y, state, manager), state, manager);
}

std::optional<std::filesystem::path> EditorAssetBrowserHitTester::FolderAt(
    const RECT& content,
    int x,
    int y,
    const EditorAssetBrowserState& state,
    const kb::assets::AssetManager& manager) {
    return EditorAssetBrowserHitPayloadResolver::FolderAt(HitTest(content, x, y, state, manager), state, manager);
}

std::optional<std::filesystem::path> EditorAssetBrowserHitTester::FolderDropTargetAt(
    const RECT& content,
    int x,
    int y,
    const EditorAssetBrowserState& state,
    const kb::assets::AssetManager& manager) {
    return EditorAssetBrowserHitPayloadResolver::FolderDropTargetAt(HitTest(content, x, y, state, manager), state, manager);
}

std::optional<std::filesystem::path> EditorAssetBrowserHitTester::BreadcrumbFolderAt(
    const RECT& content,
    int x,
    int y,
    const EditorAssetBrowserState& state) {
    return EditorAssetBrowserBreadcrumbHitTester::FolderAt(EditorAssetBrowserLayout::Build(content), x, y, state);
}

bool EditorAssetBrowserHitTester::IsDropTarget(const RECT& content, int x, int y) noexcept {
    const EditorAssetBrowserLayoutRects layout = EditorAssetBrowserLayout::Build(content);
    return EditorAssetBrowserGeometry::Contains(layout.assetView, x, y) || EditorAssetBrowserGeometry::Contains(layout.tree, x, y);
}

} // namespace kb::editor

#endif
