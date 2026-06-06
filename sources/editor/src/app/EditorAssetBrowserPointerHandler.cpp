#include "app/EditorAssetBrowserPointerHandler.hpp"

#if defined(_WIN32)
#include "app/EditorAssetBrowserContextMenuPointerHandler.hpp"
#include "app/EditorAssetBrowserDeleteConfirmPointerHandler.hpp"
#include "app/EditorAssetBrowserDoubleClickHandler.hpp"
#include "app/EditorAssetBrowserPointerPanelResolver.hpp"
#include "app/EditorAssetBrowserPrimaryClickHandler.hpp"
#include "app/EditorAssetBrowserThumbnailScaleDragHandler.hpp"
#include "assets/EditorAssetBrowserHitTester.hpp"
#include "assets/EditorAssetBrowserState.hpp"
#include "engine/scene/SceneAssets.hpp"

#include <algorithm>
#include <optional>

namespace kb::editor {
namespace {

[[nodiscard]] int AssetContentHeight(const EditorAssetBrowserLayoutRects& layout, const EditorAssetBrowserState& state, const kb::assets::AssetManager& manager) {
    const int itemCount = static_cast<int>(state.ChildFolderRows(manager).size() + state.AssetRows(manager).size())
        + (state.TextEditMode() == EditorAssetTextEditMode::NewFolder ? 1 : 0);
    if (state.ViewMode() == EditorAssetViewMode::Tiles) {
        constexpr int tileGap = 5;
        const int columns = EditorAssetBrowserLayout::AssetTileColumnCount(layout, state.ThumbnailScale());
        const int rows = (itemCount + columns - 1) / std::max(1, columns);
        return rows * (EditorAssetBrowserLayout::TileHeight(state.ThumbnailScale()) + tileGap);
    }
    return itemCount * EditorAssetBrowserLayout::RowHeight;
}

[[nodiscard]] int AssetViewportHeight(const EditorAssetBrowserLayoutRects& layout, const EditorAssetBrowserState& state) noexcept {
    const RECT viewport = EditorAssetBrowserLayout::AssetViewportRect(layout);
    int height = static_cast<int>(viewport.bottom - viewport.top);
    if (state.ViewMode() == EditorAssetViewMode::List) {
        height -= EditorAssetBrowserLayout::AssetHeaderHeight;
    }
    return std::max(1, height);
}

bool HandleScrollbarDrag(
    HWND sourceWindow,
    HWND mainWindow,
    int y,
    bool leftButtonDown,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext) {
    EditorAssetBrowserState& state = sceneContext.AssetBrowser();
    if (!state.IsTreeScrollbarDragging() && !state.IsContentScrollbarDragging()) {
        return false;
    }
    if (!leftButtonDown) {
        state.EndTreeScrollbarDrag();
        state.EndContentScrollbarDrag();
        return true;
    }
    const std::optional<RECT> content = EditorAssetBrowserPointerPanelResolver::ResolveContent(sourceWindow, mainWindow, dockModel, floatingWindows, metrics);
    if (!content.has_value()) {
        return true;
    }
    const EditorAssetBrowserLayoutRects layout = EditorAssetBrowserLayout::Build(*content, state.TreeWidth());
    kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();
    if (state.IsTreeScrollbarDragging()) {
        const RECT viewport = EditorAssetBrowserLayout::TreeViewportRect(layout);
        const int contentHeight = static_cast<int>(state.FolderRows(manager).size()) * EditorAssetBrowserLayout::RowHeight;
        const int viewportHeight = static_cast<int>(viewport.bottom - viewport.top);
        const int maxOffset = std::max(0, contentHeight - viewportHeight);
        const RECT track = EditorAssetBrowserLayout::TreeScrollbarTrackRect(layout);
        const RECT thumb = EditorAssetBrowserLayout::ScrollbarThumbRect(track, viewportHeight, contentHeight, state.TreeScrollOffset());
        state.DragTreeScrollbar(y, std::max(1, static_cast<int>((track.bottom - track.top) - (thumb.bottom - thumb.top))), maxOffset);
        return true;
    }
    const int contentHeight = AssetContentHeight(layout, state, manager);
    const int viewportHeight = AssetViewportHeight(layout, state);
    const int maxOffset = std::max(0, contentHeight - viewportHeight);
    RECT track = EditorAssetBrowserLayout::AssetScrollbarTrackRect(layout);
    if (state.ViewMode() == EditorAssetViewMode::List) {
        track.top += EditorAssetBrowserLayout::AssetHeaderHeight;
    }
    const RECT thumb = EditorAssetBrowserLayout::ScrollbarThumbRect(track, viewportHeight, contentHeight, state.ContentScrollOffset());
    state.DragContentScrollbar(y, std::max(1, static_cast<int>((track.bottom - track.top) - (thumb.bottom - thumb.top))), maxOffset);
    return true;
}

} // namespace

bool EditorAssetBrowserPointerHandler::HandlePointerDown(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext) {
    const std::optional<RECT> content = EditorAssetBrowserPointerPanelResolver::ResolveContent(sourceWindow, mainWindow, dockModel, floatingWindows, metrics);
    if (!content.has_value()) {
        sceneContext.AssetBrowser().FocusSearch(false);
        return false;
    }

    EditorAssetBrowserState& state = sceneContext.AssetBrowser();
    if (state.TextEditMode() == EditorAssetTextEditMode::NewFolder) {
        static_cast<void>(sceneContext.CommitAssetTextEdit());
    }

    kb::assets::AssetManager& manager = sceneContext.Scene().Assets().Manager();
    const EditorAssetBrowserHit hit = EditorAssetBrowserHitTester::HitTest(*content, x, y, state, manager);

    if (state.IsDeleteConfirmOpen()) {
        return EditorAssetBrowserDeleteConfirmPointerHandler::HandlePointerDown(hit, x, y, sceneContext);
    }

    if (const std::optional<bool> handled = EditorAssetBrowserContextMenuPointerHandler::HandleOpenMenuPointerDown(hit, sceneContext)) {
        return *handled;
    }

    return EditorAssetBrowserPrimaryClickHandler::HandlePointerDown(*content, hit, x, y, sceneContext);
}

bool EditorAssetBrowserPointerHandler::HandleRightButtonDown(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext) {
    const std::optional<RECT> content = EditorAssetBrowserPointerPanelResolver::ResolveContent(sourceWindow, mainWindow, dockModel, floatingWindows, metrics);
    if (!content.has_value()) {
        sceneContext.AssetBrowser().CloseContextMenu();
        return false;
    }

    return EditorAssetBrowserContextMenuPointerHandler::HandleRightButtonDown(*content, x, y, sceneContext);
}

bool EditorAssetBrowserPointerHandler::HandleDoubleClick(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext) {
    const std::optional<RECT> content = EditorAssetBrowserPointerPanelResolver::ResolveContent(sourceWindow, mainWindow, dockModel, floatingWindows, metrics);
    if (!content.has_value()) {
        return false;
    }

    return EditorAssetBrowserDoubleClickHandler::HandleDoubleClick(*content, x, y, sceneContext);
}

bool EditorAssetBrowserPointerHandler::HandlePointerMove(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    bool leftButtonDown,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext) {
    if (EditorAssetBrowserDeleteConfirmPointerHandler::HandlePointerMove(x, y, sceneContext)) {
        return true;
    }
    if (HandleScrollbarDrag(sourceWindow, mainWindow, y, leftButtonDown, dockModel, floatingWindows, metrics, sceneContext)) {
        return true;
    }
    if (!leftButtonDown && (sceneContext.AssetBrowser().IsTreeWidthDragging() || sceneContext.AssetBrowser().IsThumbnailScaleDragging())) {
        sceneContext.AssetBrowser().EndTreeWidthDrag();
        sceneContext.AssetBrowser().EndThumbnailScaleDrag();
        return true;
    }
    if (leftButtonDown && EditorAssetBrowserThumbnailScaleDragHandler::HandleTreeResizePointerMove(sourceWindow, mainWindow, x, dockModel, floatingWindows, metrics, sceneContext)) {
        return true;
    }

    if (sceneContext.AssetBrowser().IsContextMenuOpen() || sceneContext.AssetBrowser().IsDropActionMenuOpen() || sceneContext.AssetBrowser().IsFilterMenuOpen()) {
        const std::optional<RECT> content = EditorAssetBrowserPointerPanelResolver::ResolveContent(sourceWindow, mainWindow, dockModel, floatingWindows, metrics);
        if (content.has_value() && sceneContext.AssetBrowser().IsFilterMenuOpen()) {
            const EditorAssetBrowserHit hit = EditorAssetBrowserHitTester::HitTest(*content, x, y, sceneContext.AssetBrowser(), sceneContext.Scene().Assets().Manager());
            int hovered = -1;
            if (hit.kind == EditorAssetBrowserHitKind::FilterFolder) {
                hovered = 0;
            } else if (hit.kind == EditorAssetBrowserHitKind::FilterTemplate) {
                hovered = 1;
            }
            const bool hoveringFilterMenu = hit.kind == EditorAssetBrowserHitKind::FilterFolder
                || hit.kind == EditorAssetBrowserHitKind::FilterTemplate
                || hit.kind == EditorAssetBrowserHitKind::DropActionBody;
            if (sceneContext.AssetBrowser().SetFilterMenuHoveredIndex(hovered)) {
                return true;
            }
            static_cast<void>(hoveringFilterMenu);
        }
        if (content.has_value() && sceneContext.AssetBrowser().IsDropActionMenuOpen()) {
            const EditorAssetBrowserHit hit = EditorAssetBrowserHitTester::HitTest(*content, x, y, sceneContext.AssetBrowser(), sceneContext.Scene().Assets().Manager());
            const EditorAssetDropAction hovered = hit.kind == EditorAssetBrowserHitKind::DropActionCommand ? hit.dropAction : EditorAssetDropAction::None;
            if (sceneContext.AssetBrowser().SetDropActionHoveredCommand(hovered)) {
                return true;
            }
        }
        if (content.has_value() && EditorAssetBrowserContextMenuPointerHandler::HandlePointerMove(*content, x, y, sceneContext)) {
            return true;
        }
    }

    return leftButtonDown && EditorAssetBrowserThumbnailScaleDragHandler::HandlePointerMove(sourceWindow, mainWindow, x, dockModel, floatingWindows, metrics, sceneContext);
}

bool EditorAssetBrowserPointerHandler::HandlePointerUp(EditorSceneContext& sceneContext) noexcept {
    if (EditorAssetBrowserDeleteConfirmPointerHandler::HandlePointerUp(sceneContext)) {
        return true;
    }
    if (EditorAssetBrowserThumbnailScaleDragHandler::HandleTreeResizePointerUp(sceneContext)) {
        return true;
    }
    if (sceneContext.AssetBrowser().IsTreeScrollbarDragging() || sceneContext.AssetBrowser().IsContentScrollbarDragging()) {
        sceneContext.AssetBrowser().EndTreeScrollbarDrag();
        sceneContext.AssetBrowser().EndContentScrollbarDrag();
        return true;
    }
    return EditorAssetBrowserThumbnailScaleDragHandler::HandlePointerUp(sceneContext);
}

} // namespace kb::editor

#endif
