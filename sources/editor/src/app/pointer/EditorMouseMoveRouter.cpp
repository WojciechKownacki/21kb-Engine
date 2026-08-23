#include "app/pointer/EditorMouseMoveRouter.hpp"

#if defined(_WIN32)
#include "app/EditorAssetBrowserPointerHandler.hpp"
#include "app/EditorPointerDragInteraction.hpp"
#include "app/ParticleEditorPanelInteraction.hpp"
#include "app/EditorWindowInvalidator.hpp"
#include "app/EditorWindowToolbarPointerHandler.hpp"
#include "app/console/EditorConsolePointerController.hpp"
#include "app/cursor/EditorInternalSplitterCursorController.hpp"
#include "app/inspector/EditorInspectorPointerController.hpp"
#include "app/plugins/EditorPluginsPointerController.hpp"
#include "app/project_settings/EditorProjectSettingsPointerController.hpp"
#include "app/scene_viewport/EditorSceneViewportCameraController.hpp"
#include "app/scene_viewport/EditorSceneViewportObjectInteraction.hpp"
#include "app/scene_viewport/EditorTerrainViewportInteraction.hpp"
#include "rendering/EditorPanelContentResolver.hpp"
#include "rendering/EditorHostSurfaceLayoutResolver.hpp"
#include "rendering/HierarchyToolbarLayout.hpp"
#include "rendering/MaterialEditorPanelRenderer.hpp"
#include "rendering/ParticleEditorPanelLayout.hpp"
#include "rendering/SceneViewportToolbarRenderer.hpp"
#include "rendering/SkeletalMeshEditorPanelLayout.hpp"
#include "rendering/SkeletalMeshEditorPanelRenderer.hpp"
#include "scene/EditorHierarchyMetrics.hpp"
#include "scene/EditorTerrainService.hpp"

#include <algorithm>
#include <optional>
#include <vector>

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

namespace {

// Graph interactions run on every mouse move: repaint the Material Editor panel only, so the scene
// viewport, hierarchy, inspector and project files are not redrawn for a node being dragged.
void InvalidateMaterialGraphPanel(
    HWND messageWindow,
    HWND mainWindow,
    EditorDockModel& dockModel,
    EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics) {
    const std::optional<RECT> panel = EditorPanelContentResolver::Resolve(
        DockPanelKind::MaterialEditor,
        messageWindow,
        mainWindow,
        dockModel,
        floatingWindows,
        metrics);
    if (panel.has_value()) {
        EditorWindowInvalidator::InvalidatePanel(messageWindow, *panel);
        return;
    }
    EditorWindowInvalidator::InvalidateMainAndSource(mainWindow, messageWindow);
}

} // namespace

void EditorMouseMoveRouter::Handle(HWND messageWindow, int x, int y, bool leftButtonDown, bool rightButtonDown) {
    if (sceneContext_.ParticleEditorWorkspace().EmitterDragActive() ||
        sceneContext_.ParticleEditorWorkspace().ModuleDragActive()) {
        const std::optional<RECT> content = EditorPanelContentResolver::Resolve(
            DockPanelKind::ParticleEditor,
            messageWindow,
            mainWindow_,
            dockModel_,
            floatingWindows_,
            metrics_);
        if (!leftButtonDown || !content.has_value()) {
            sceneContext_.CancelParticleEditorEmitterDrag();
            sceneContext_.CancelParticleEditorModuleDrag();
            ReleaseCapture();
        } else {
            const std::vector<kb::particle_editor::ParticleEmitterListRow> rows =
                sceneContext_.ParticleEditorEmitterRows();
            const auto inspector = sceneContext_.ParticleEditorInspector();
            const auto recipes = sceneContext_.ParticleEditorRecipes();
            const ParticleEditorPanelLayout layout = ParticleEditorPanelLayoutResolver::Resolve(
                *content,
                rows,
                sceneContext_.ParticleEditorWorkspace().ComposerScrollOffset(),
                GetDpiForWindow(messageWindow), &inspector, recipes.size(), &sceneContext_.ParticleEditorWorkspace());
            ParticleEditorPanelInteraction::UpdateDrag(sceneContext_, layout, y);
            EditorWindowInvalidator::InvalidatePanel(messageWindow, *content);
        }
        return;
    }

    const std::optional<RECT> sceneContent = EditorPanelContentResolver::Resolve(DockPanelKind::Scene, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
    if (SceneViewportToolbarRenderer::UpdateInfoHover(sceneContent.value_or(RECT{}), x, y)) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
    }

    EditorSceneViewportCameraController sceneCamera(mainWindow_, dockModel_, floatingWindows_, metrics_, sceneContext_, sceneViewport_);
    if (sceneCamera.HandlePointerMove(messageWindow, x, y)) {
        return;
    }

    if (!EditorTerrainService::ToolState().strokeActive &&
        EditorTerrainViewportInteraction::UpdateHover(
            messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_)) {
        sceneViewport_.RequestPresent();
    }

    if (EditorTerrainService::ToolState().strokeActive) {
        if (!leftButtonDown) {
            EditorTerrainService::ToolState().strokeActive = false;
            EditorTerrainService::ToolState().heldSculptElapsedSeconds = 0.0F;
            std::string error;
            if (!sceneContext_.CommitTerrainBrushStroke(&error)) {
                sceneContext_.Console().Warning(
                    "Terrain",
                    error.empty() ? "Terrain stroke could not be committed." : error);
            }
            ReleaseCapture();
        } else {
            static_cast<void>(EditorTerrainViewportInteraction::Stamp(
                messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_, false));
        }
        sceneViewport_.RequestPresent();
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

    if (sceneContext_.IsSkeletalMeshEditorToolboxWidthDragging() ||
        sceneContext_.IsSkeletalMeshEditorSkeletonTreeWidthDragging() ||
        sceneContext_.IsSkeletalMeshEditorTreeDetailsHeightDragging()) {
        if (!leftButtonDown) {
            sceneContext_.EndSkeletalMeshEditorPanelResizeDrag();
            ReleaseCapture();
        } else if (const std::optional<RECT> skeletalMeshEditorContent =
                       EditorPanelContentResolver::Resolve(
                           DockPanelKind::SkeletalMeshEditor,
                           messageWindow,
                           mainWindow_,
                           dockModel_,
                           floatingWindows_,
                           metrics_);
                   skeletalMeshEditorContent.has_value()) {
            if (sceneContext_.IsSkeletalMeshEditorTreeDetailsHeightDragging()) {
                sceneContext_.SetSkeletalMeshEditorSkeletonTreeHeight(
                    SkeletalMeshEditorPanelLayoutResolver::SkeletonTreeHeightFromPointer(
                        *skeletalMeshEditorContent, y));
            } else if (sceneContext_.IsSkeletalMeshEditorToolboxWidthDragging()) {
                sceneContext_.SetSkeletalMeshEditorToolboxWidth(
                    x - skeletalMeshEditorContent->left);
            } else {
                sceneContext_.SetSkeletalMeshEditorSkeletonTreeWidth(
                    skeletalMeshEditorContent->right - x);
            }
            const SkeletalMeshEditorPanelLayout layout =
                SkeletalMeshEditorPanelLayoutResolver::Resolve(
                    *skeletalMeshEditorContent,
                    sceneContext_.SkeletalMeshEditorToolboxWidth(),
                    sceneContext_.SkeletalMeshEditorSkeletonTreeWidth(),
                    sceneContext_.SkeletalMeshEditorSkeletonTreeHeight());
            sceneContext_.SetSkeletalMeshEditorToolboxWidth(
                static_cast<int>(layout.toolbox.right - layout.toolbox.left));
            sceneContext_.SetSkeletalMeshEditorSkeletonTreeWidth(
                static_cast<int>(layout.skeletonTree.right - layout.skeletonTree.left));
            sceneContext_.SetSkeletalMeshEditorSkeletonTreeHeight(
                static_cast<int>(layout.skeletonTree.bottom - layout.skeletonTree.top));
            if (messageWindow == mainWindow_) {
                EditorHostSurfaceLayoutResolver::SyncMainWindow(
                    mainWindow_, dockModel_, metrics_, sceneContext_, sceneViewport_);
            }
        }
        sceneViewport_.RequestPresent();
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    if (sceneContext_.IsSkeletalMeshEditorTreeScrollbarDragging()) {
        if (!leftButtonDown) {
            sceneContext_.EndSkeletalMeshEditorTreeScrollbarDrag();
            ReleaseCapture();
        } else if (const std::optional<RECT> skeletalMeshEditorContent = EditorPanelContentResolver::Resolve(
                       DockPanelKind::SkeletalMeshEditor, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
                   skeletalMeshEditorContent.has_value()) {
            const RECT track = SkeletalMeshEditorPanelRenderer::TreeScrollbarTrack(
                *skeletalMeshEditorContent, sceneContext_);
            const RECT thumb = SkeletalMeshEditorPanelRenderer::TreeScrollbarThumb(
                *skeletalMeshEditorContent, sceneContext_);
            sceneContext_.DragSkeletalMeshEditorTreeScrollbar(
                y,
                std::max(1, RectHeight(track) - RectHeight(thumb)),
                SkeletalMeshEditorPanelRenderer::TreeMaxScroll(*skeletalMeshEditorContent, sceneContext_));
        }
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    if (sceneContext_.IsSkeletalMeshEditorDetailsScrollbarDragging()) {
        if (!leftButtonDown) {
            sceneContext_.EndSkeletalMeshEditorDetailsScrollbarDrag();
            ReleaseCapture();
        } else if (const std::optional<RECT> skeletalMeshEditorContent = EditorPanelContentResolver::Resolve(
                       DockPanelKind::SkeletalMeshEditor, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
                   skeletalMeshEditorContent.has_value()) {
            const RECT track = SkeletalMeshEditorPanelRenderer::DetailsScrollbarTrack(
                *skeletalMeshEditorContent, sceneContext_);
            const RECT thumb = SkeletalMeshEditorPanelRenderer::DetailsScrollbarThumb(
                *skeletalMeshEditorContent, sceneContext_);
            sceneContext_.DragSkeletalMeshEditorDetailsScrollbar(
                y,
                std::max(1, RectHeight(track) - RectHeight(thumb)),
                SkeletalMeshEditorPanelRenderer::DetailsMaxScroll(*skeletalMeshEditorContent, sceneContext_));
        }
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

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

    if (sceneContext_.IsMaterialPreviewOrbiting()) {
        if (!leftButtonDown) {
            // Gesture ended without a WM_LBUTTONUP reaching us (rare). Repaint the panel once so any
            // active-state chrome resets; this is a one-shot transition, not the hot path.
            static_cast<void>(sceneContext_.EndMaterialPreviewOrbit());
            ReleaseCapture();
            InvalidateMaterialGraphPanel(messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
            return;
        }
        static_cast<void>(sceneContext_.DragMaterialPreviewOrbit(x, y));
        // The preview is a REAL-TIME bgfx child surface (presented every frame by TickEditorFrame),
        // NOT a GDI-drawn widget like the graph nodes. Orbiting therefore needs only a viewport present
        // - never a Material Editor panel repaint. Repainting the whole panel (graph nodes + chrome) on
        // every mouse pixel ran at the raw mouse-move rate and starved the preview's per-frame present:
        // THAT was the orbit lag, not GPU re-sync. This mirrors UE exactly - FEditorViewportClient orbit
        // ends in Invalidate(false, false) -> Viewport->InvalidateDisplay(), i.e. the viewport's display
        // pixels only, leaving the surrounding editor UI (graph, details, toolbars) untouched.
        sceneViewport_.RequestPresent();
        return;
    }

    if (sceneContext_.IsMaterialGraphNodeDragging()) {
        bool changed = false;
        if (!leftButtonDown) {
            changed = sceneContext_.EndMaterialGraphNodeDrag();
            ReleaseCapture();
        } else {
            changed = sceneContext_.DragMaterialGraphNode(x, y);
        }
        if (changed) {
            InvalidateMaterialGraphPanel(messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
        }
        return;
    }

    if (sceneContext_.IsMaterialGraphCommentDragging()) {
        if (!leftButtonDown) {
            static_cast<void>(sceneContext_.EndMaterialGraphCommentDrag());
            ReleaseCapture();
        } else {
            static_cast<void>(sceneContext_.DragMaterialGraphComment(x, y));
        }
        InvalidateMaterialGraphPanel(messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
        return;
    }

    if (sceneContext_.IsMaterialGraphBoxSelecting()) {
        if (!leftButtonDown) {
            static_cast<void>(sceneContext_.EndMaterialGraphBoxSelection({}, 0U));
            ReleaseCapture();
        } else {
            static_cast<void>(sceneContext_.DragMaterialGraphBoxSelection(x, y));
        }
        InvalidateMaterialGraphPanel(messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
        return;
    }

    // While the "what do you want to connect?" menu is parked open (a wire was dropped on empty canvas),
    // the pending connection is deliberately kept so picking a node connects it - exactly like UE, where a
    // pin drag-drop opens a filtered action menu that remembers the from-pin. It is NOT an active drag, so
    // this branch must be skipped: otherwise the first mouse move toward the menu (button up) would hit the
    // !leftButtonDown path and cancel the pending connection, and the pick would then create nothing
    // (AddMaterialGraphNodeForPendingConnection finds no pending pin) - the "selecting just cancels" bug.
    if (sceneContext_.HasMaterialGraphPinConnection() && !sceneContext_.IsMaterialGraphContextMenuOpen()) {
        if (!leftButtonDown) {
            static_cast<void>(sceneContext_.CancelMaterialGraphPinConnection());
            ReleaseCapture();
        } else {
            static_cast<void>(sceneContext_.DragMaterialGraphPinConnection(x, y));
        }
        InvalidateMaterialGraphPanel(messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
        return;
    }

    if (sceneContext_.IsMaterialGraphPanning()) {
        if (!rightButtonDown) {
            static_cast<void>(sceneContext_.EndMaterialGraphPan());
            ReleaseCapture();
        } else {
            static_cast<void>(sceneContext_.DragMaterialGraphPan(x, y));
        }
        InvalidateMaterialGraphPanel(messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
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
