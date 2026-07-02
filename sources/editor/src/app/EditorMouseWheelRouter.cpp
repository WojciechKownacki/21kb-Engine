#include "app/EditorMouseWheelRouter.hpp"

#if defined(_WIN32)
#include "app/console/EditorConsolePointerController.hpp"
#include "app/inspector/EditorInspectorPointerController.hpp"
#include "app/plugins/EditorPluginsPointerController.hpp"
#include "app/project_files/EditorProjectFilesMouseWheelController.hpp"
#include "app/scene_viewport/EditorSceneViewportCameraController.hpp"
#include "assets/EditorAssetBrowserGeometry.hpp"
#include "rendering/EditorPanelContentResolver.hpp"
#include "rendering/HierarchyToolbarLayout.hpp"
#include "rendering/MaterialEditorPanelRenderer.hpp"
#include "scene/EditorHierarchyMetrics.hpp"
#include "scene/EditorSceneContext.hpp"

#include <algorithm>
#include <optional>

namespace kb::editor {
namespace {

[[nodiscard]] bool Contains(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

[[nodiscard]] int HierarchyMaxScroll(const RECT& hierarchyContent, const EditorSceneContext& sceneContext) {
    const RECT list = HierarchyToolbarLayout::Resolve(hierarchyContent).listContent;
    const int viewportHeight = std::max(0L, list.bottom - list.top);
    const int contentHeight = static_cast<int>(sceneContext.HierarchyRowCount()) * kHierarchyRowHeight;
    return std::max(0, contentHeight - viewportHeight);
}

} // namespace

EditorMouseWheelRouter::EditorMouseWheelRouter(
    HWND messageWindow,
    HWND mainWindow,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext,
    EditorSceneBgfxViewport& sceneViewport) noexcept
    : messageWindow_(messageWindow)
    , mainWindow_(mainWindow)
    , dockModel_(dockModel)
    , floatingWindows_(floatingWindows)
    , metrics_(metrics)
    , sceneContext_(sceneContext)
    , sceneViewport_(sceneViewport) {}

bool EditorMouseWheelRouter::HandleMouseWheel(int x, int y, int wheelDelta) {
    EditorProjectFilesMouseWheelController projectFilesWheel(sceneContext_);
    if (sceneContext_.AssetBrowser().IsDeleteConfirmOpen()) {
        RECT client{};
        GetClientRect(messageWindow_, &client);
        if (projectFilesWheel.HandleMouseWheel(client, x, y, wheelDelta)) {
            return true;
        }
        const RECT dialog = EditorAssetBrowserGeometry::DeleteConfirmRect(
            client,
            sceneContext_.AssetBrowser().DeleteConfirmOffsetX(),
            sceneContext_.AssetBrowser().DeleteConfirmOffsetY());
        if (EditorAssetBrowserGeometry::Contains(dialog, x, y)) {
            return true;
        }
    }

    const std::optional<RECT> consoleContent = EditorPanelContentResolver::Resolve(DockPanelKind::Console, messageWindow_, mainWindow_, dockModel_, floatingWindows_, metrics_);
    EditorConsolePointerController consolePointer(messageWindow_, sceneContext_);
    if (consoleContent.has_value() && consolePointer.HandleMouseWheel(*consoleContent, x, y, wheelDelta)) {
        return true;
    }

    const std::optional<RECT> hierarchyContent = EditorPanelContentResolver::Resolve(DockPanelKind::Hierarchy, messageWindow_, mainWindow_, dockModel_, floatingWindows_, metrics_);
    if (hierarchyContent.has_value() && Contains(*hierarchyContent, x, y)) {
        const int direction = wheelDelta > 0 ? 1 : -1;
        const int maxOffset = HierarchyMaxScroll(*hierarchyContent, sceneContext_);
        return sceneContext_.SetHierarchyScrollOffset(
            sceneContext_.HierarchyScrollOffset() - direction * kHierarchyRowHeight * 3,
            maxOffset);
    }

    const std::optional<RECT> materialEditorContent = EditorPanelContentResolver::Resolve(DockPanelKind::MaterialEditor, messageWindow_, mainWindow_, dockModel_, floatingWindows_, metrics_);
    if (materialEditorContent.has_value() && Contains(*materialEditorContent, x, y)) {
        const MaterialEditorPanelLayout layout = MaterialEditorPanelRenderer::ResolveLayout(*materialEditorContent);
        if (Contains(layout.graphCanvas, x, y)) {
            sceneContext_.SetMaterialGraphCanvasViewport(
                MaterialEditorPanelRectWidth(layout.graphCanvas),
                MaterialEditorPanelRectHeight(layout.graphCanvas));
            return sceneContext_.ZoomMaterialGraph(wheelDelta, x - layout.graphCanvas.left, y - layout.graphCanvas.top);
        }
    }

    EditorSceneViewportCameraController sceneCamera(mainWindow_, dockModel_, floatingWindows_, metrics_, sceneContext_, sceneViewport_);
    if (sceneCamera.HandleMouseWheel(messageWindow_, x, y, wheelDelta)) {
        return true;
    }

    const std::optional<RECT> inspectorContent = EditorPanelContentResolver::Resolve(DockPanelKind::Inspector, messageWindow_, mainWindow_, dockModel_, floatingWindows_, metrics_);
    EditorInspectorPointerController inspectorPointer(sceneContext_);
    if (inspectorContent.has_value() && inspectorPointer.HandleMouseWheel(*inspectorContent, x, y, wheelDelta)) {
        InvalidateRect(messageWindow_, nullptr, FALSE);
        if (messageWindow_ != mainWindow_) {
            InvalidateRect(mainWindow_, nullptr, FALSE);
        }
        return true;
    }

    const std::optional<RECT> pluginsContent = EditorPanelContentResolver::Resolve(DockPanelKind::Plugins, messageWindow_, mainWindow_, dockModel_, floatingWindows_, metrics_);
    EditorPluginsPointerController pluginsPointer(sceneContext_);
    if (pluginsContent.has_value() && pluginsPointer.HandleMouseWheel(*pluginsContent, x, y, wheelDelta)) {
        InvalidateRect(messageWindow_, nullptr, FALSE);
        if (messageWindow_ != mainWindow_) {
            InvalidateRect(mainWindow_, nullptr, FALSE);
        }
        return true;
    }

    const std::optional<RECT> assetContent = EditorPanelContentResolver::Resolve(DockPanelKind::Assets, messageWindow_, mainWindow_, dockModel_, floatingWindows_, metrics_);
    return assetContent.has_value() && projectFilesWheel.HandleMouseWheel(*assetContent, x, y, wheelDelta);
}

} // namespace kb::editor

#endif
