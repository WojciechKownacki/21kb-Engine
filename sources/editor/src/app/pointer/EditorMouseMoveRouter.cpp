#include "app/pointer/EditorMouseMoveRouter.hpp"

#if defined(_WIN32)
#include "app/EditorAssetBrowserPointerHandler.hpp"
#include "app/EditorPointerDragInteraction.hpp"
#include "app/EditorWindowInvalidator.hpp"
#include "app/EditorWindowToolbarPointerHandler.hpp"
#include "app/console/EditorConsolePointerController.hpp"
#include "app/cursor/EditorInternalSplitterCursorController.hpp"
#include "app/inspector/EditorInspectorPointerController.hpp"
#include "app/plugins/EditorPluginsPointerController.hpp"
#include "app/project_settings/EditorProjectSettingsPointerController.hpp"
#include "app/scene_viewport/EditorSceneViewportCameraController.hpp"
#include "app/scene_viewport/EditorSceneViewportObjectInteraction.hpp"
#include "rendering/EditorPanelContentResolver.hpp"
#include "rendering/EditorHostSurfaceLayoutResolver.hpp"
#include "rendering/HierarchyToolbarLayout.hpp"
#include "rendering/MaterialEditorPanelRenderer.hpp"
#include "rendering/SceneViewportToolbarRenderer.hpp"
#include "scene/EditorHierarchyMetrics.hpp"

#include <algorithm>
#include <optional>

namespace kb::editor {
namespace {

[[nodiscard]] int RectHeight(const RECT& rect) noexcept {
    return std::max(0L, rect.bottom - rect.top);
}

void RepaintNow(HWND window) noexcept {
    if (window != nullptr) {
        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
    }
}

[[nodiscard]] int HierarchyMaxScroll(const RECT& hierarchyContent, const EditorSceneContext& sceneContext) {
    const RECT list = HierarchyToolbarLayout::Resolve(hierarchyContent).listContent;
    const int viewportHeight = RectHeight(list);
    const int contentHeight = static_cast<int>(sceneContext.HierarchyRowCount()) * kHierarchyRowHeight;
    return std::max(0, contentHeight - viewportHeight);
}

} // namespace

EditorMouseMoveRouter::EditorMouseMoveRouter(
    HWND mainWindow,
    EditorDockModel& dockModel,
    EditorFloatingWindowManager& floatingWindows,
    EditorDockController& dockController,
    EditorSceneContext& sceneContext,
    EditorSceneBgfxViewport& sceneViewport,
    EditorShellInteractionState& shellInteraction,
    EditorPointerDragState& pointerDrag,
    const EditorMetrics& metrics) noexcept
    : mainWindow_(mainWindow)
    , dockModel_(dockModel)
    , floatingWindows_(floatingWindows)
    , dockController_(dockController)
    , sceneContext_(sceneContext)
    , sceneViewport_(sceneViewport)
    , shellInteraction_(shellInteraction)
    , pointerDrag_(pointerDrag)
    , metrics_(metrics) {}

void EditorMouseMoveRouter::Handle(HWND messageWindow, int x, int y, bool leftButtonDown, bool rightButtonDown) {
    const std::optional<RECT> sceneContent = EditorPanelContentResolver::Resolve(DockPanelKind::Scene, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
    if (SceneViewportToolbarRenderer::UpdateInfoHover(sceneContent.value_or(RECT{}), x, y)) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
    }

    EditorSceneViewportCameraController sceneCamera(mainWindow_, dockModel_, floatingWindows_, metrics_, sceneContext_, sceneViewport_);
    if (sceneCamera.HandlePointerMove(messageWindow, x, y)) {
        return;
    }

    if (EditorSceneViewportObjectInteraction::UpdateBoxSelection(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_, leftButtonDown)) {
        sceneViewport_.RequestPresent();
        return;
    }

    const bool wasGizmoDragging = sceneContext_.Gizmo().IsDragging();
    if (EditorSceneViewportObjectInteraction::UpdateGizmoDragOrHover(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_, leftButtonDown)) {
        sceneViewport_.RequestPresent();
        if (!wasGizmoDragging) {
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        }
        return;
    }

    const EditorInternalSplitterCursorController splitterCursor(messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_, sceneContext_);
    splitterCursor.UpdateCursor(x, y);
    static_cast<void>(EditorWindowToolbarPointerHandler::HandleMouseMove(mainWindow_, messageWindow, x, y, dockModel_, shellInteraction_, metrics_));
    EditorInspectorPointerController inspectorPointer(sceneContext_);
    const std::optional<RECT> inspectorContent = EditorPanelContentResolver::Resolve(DockPanelKind::Inspector, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);

    if (sceneContext_.IsHierarchyScrollbarDragging()) {
        if (!leftButtonDown) {
            sceneContext_.EndHierarchyScrollbarDrag();
        } else if (const std::optional<RECT> hierarchyContent = EditorPanelContentResolver::Resolve(DockPanelKind::Hierarchy, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
                   hierarchyContent.has_value()) {
            const RECT list = HierarchyToolbarLayout::Resolve(*hierarchyContent).listContent;
            sceneContext_.DragHierarchyScrollbar(y, std::max(1, RectHeight(list) - 24), HierarchyMaxScroll(*hierarchyContent, sceneContext_));
        }
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    if (sceneContext_.IsMaterialGraphConstantSliderDragging()) {
        bool changed = false;
        if (!leftButtonDown) {
            static_cast<void>(sceneContext_.EndMaterialGraphConstantSliderDrag());
            ReleaseCapture();
            changed = true;
        } else {
            changed = sceneContext_.DragMaterialGraphConstantSlider(x);
        }
        if (changed) {
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        }
        return;
    }

    if (sceneContext_.IsMaterialGraphNodeDragging()) {
        if (!leftButtonDown) {
            static_cast<void>(sceneContext_.EndMaterialGraphNodeDrag());
            ReleaseCapture();
        } else {
            static_cast<void>(sceneContext_.DragMaterialGraphNode(x, y));
        }
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    if (sceneContext_.IsMaterialGraphCommentDragging()) {
        if (!leftButtonDown) {
            static_cast<void>(sceneContext_.EndMaterialGraphCommentDrag());
            ReleaseCapture();
        } else {
            static_cast<void>(sceneContext_.DragMaterialGraphComment(x, y));
        }
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    if (sceneContext_.IsMaterialGraphBoxSelecting()) {
        if (!leftButtonDown) {
            static_cast<void>(sceneContext_.EndMaterialGraphBoxSelection({}, 0U));
            ReleaseCapture();
        } else {
            static_cast<void>(sceneContext_.DragMaterialGraphBoxSelection(x, y));
        }
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    if (sceneContext_.HasMaterialGraphPinConnection()) {
        if (!leftButtonDown) {
            static_cast<void>(sceneContext_.CancelMaterialGraphPinConnection());
            ReleaseCapture();
        } else {
            static_cast<void>(sceneContext_.DragMaterialGraphPinConnection(x, y));
        }
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    if (sceneContext_.IsMaterialGraphPanning()) {
        if (!rightButtonDown) {
            static_cast<void>(sceneContext_.EndMaterialGraphPan());
            ReleaseCapture();
        } else {
            static_cast<void>(sceneContext_.DragMaterialGraphPan(x, y));
        }
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    if (sceneContext_.IsMaterialGraphContextMenuOpen() &&
        pointerDrag_.kind != EditorPointerDragKind::MaterialGraphPaletteCommand) {
        const MaterialEditorGraphContextMenuHit hit = MaterialEditorPanelRenderer::GraphContextMenuHit(sceneContext_, x, y);
        bool changed = false;
        if (hit.kind == MaterialEditorGraphContextMenuHitKind::Category) {
            changed = sceneContext_.SetMaterialGraphContextMenuHover(hit.categoryIndex, MaterialEditorGraphMenuCommand::None);
        } else if (hit.kind == MaterialEditorGraphContextMenuHitKind::Command ||
            hit.kind == MaterialEditorGraphContextMenuHitKind::FavoriteToggle) {
            changed = sceneContext_.SetMaterialGraphContextMenuHover(hit.categoryIndex, hit.command);
        } else {
            changed = sceneContext_.ClearMaterialGraphContextMenuHover();
        }
        if (changed) {
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        }
        return;
    }

    const bool draggingMeshPreview = sceneContext_.Inspector().IsDraggingMeshPreview();
    if (leftButtonDown && inspectorPointer.HandlePointerDrag(inspectorContent, x, y)) {
        if (!draggingMeshPreview) {
            sceneViewport_.RequestPresent();
        }
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    if (leftButtonDown && pointerDrag_.Potential() && EditorPointerDragInteraction::Move(messageWindow, mainWindow_, x, y, pointerDrag_)) {
        sceneViewport_.RequestPresent();
        return;
    }

    if (leftButtonDown && dockController_.HandlePointerMove(messageWindow, x, y, true)) {
        if (messageWindow == mainWindow_) {
            EditorHostSurfaceLayoutResolver::SyncMainWindow(mainWindow_, dockModel_, metrics_, sceneContext_, sceneViewport_);
        }
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        sceneViewport_.RequestPresent();
        RepaintNow(mainWindow_);
        return;
    }

    const std::optional<RECT> consoleContent = EditorPanelContentResolver::Resolve(DockPanelKind::Console, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
    EditorConsolePointerController consolePointer(messageWindow, sceneContext_);
    if (consolePointer.UpdateHoverOrClear(consoleContent, x, y)) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
    }
    if (consolePointer.HandlePointerMove(consoleContent, y, leftButtonDown)) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    if (EditorAssetBrowserPointerHandler::HandlePointerMove(messageWindow, mainWindow_, x, y, leftButtonDown, dockModel_, floatingWindows_, metrics_, sceneContext_)) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }
    if (inspectorPointer.UpdateHoverOrClear(inspectorContent, x, y)) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
    }
    if (inspectorPointer.Contains(inspectorContent, x, y)) {
        return;
    }

    const std::optional<RECT> projectSettingsContent = EditorPanelContentResolver::Resolve(DockPanelKind::ProjectSettings, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
    EditorProjectSettingsPointerController projectSettingsPointer(sceneContext_);
    if (projectSettingsPointer.UpdateHoverOrClear(projectSettingsContent, x, y)) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
    }
    if (projectSettingsPointer.Contains(projectSettingsContent, x, y)) {
        return;
    }

    const std::optional<RECT> pluginsContent = EditorPanelContentResolver::Resolve(DockPanelKind::Plugins, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
    EditorPluginsPointerController pluginsPointer(sceneContext_);
    if (pluginsPointer.HandlePointerMove(pluginsContent, x, y, leftButtonDown)) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        if (sceneContext_.Plugins().IsScrollbarDragging()) {
            return;
        }
    }
    if (pluginsPointer.Contains(pluginsContent, x, y)) {
        return;
    }
    if (dockController_.HandlePointerMove(messageWindow, x, y, leftButtonDown)) {
        if (messageWindow == mainWindow_) {
            EditorHostSurfaceLayoutResolver::SyncMainWindow(mainWindow_, dockModel_, metrics_, sceneContext_, sceneViewport_);
        }
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        sceneViewport_.RequestPresent();
        RepaintNow(mainWindow_);
    }
    splitterCursor.UpdateCursor(x, y);
}

} // namespace kb::editor

#endif
