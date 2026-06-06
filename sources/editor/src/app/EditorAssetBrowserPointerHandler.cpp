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

#include <optional>

namespace kb::editor {

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
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext) {
    if (EditorAssetBrowserDeleteConfirmPointerHandler::HandlePointerMove(x, y, sceneContext)) {
        return true;
    }

    if (sceneContext.AssetBrowser().IsContextMenuOpen() || sceneContext.AssetBrowser().IsDropActionMenuOpen()) {
        const std::optional<RECT> content = EditorAssetBrowserPointerPanelResolver::ResolveContent(sourceWindow, mainWindow, dockModel, floatingWindows, metrics);
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

    return EditorAssetBrowserThumbnailScaleDragHandler::HandlePointerMove(sourceWindow, mainWindow, x, dockModel, floatingWindows, metrics, sceneContext);
}

bool EditorAssetBrowserPointerHandler::HandlePointerUp(EditorSceneContext& sceneContext) noexcept {
    if (EditorAssetBrowserDeleteConfirmPointerHandler::HandlePointerUp(sceneContext)) {
        return true;
    }
    return EditorAssetBrowserThumbnailScaleDragHandler::HandlePointerUp(sceneContext);
}

} // namespace kb::editor

#endif
