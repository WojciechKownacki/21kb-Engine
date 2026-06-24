#include "app/pointer/EditorLeftButtonDownRouter.hpp"

#if defined(_WIN32)
#include "app/EditorAssetBrowserPointerHandler.hpp"
#include "app/EditorPendingTextEditCommitter.hpp"
#include "app/EditorPointerDragInteraction.hpp"
#include "app/EditorPointerDragSourceResolver.hpp"
#include "app/EditorWindowInvalidator.hpp"
#include "app/EditorWindowToolbarPointerHandler.hpp"
#include "app/console/EditorConsolePointerController.hpp"
#include "app/docking/EditorMainDockSplitterPointerController.hpp"
#include "app/inspector/EditorInspectorPointerController.hpp"
#include "app/panels/EditorPanelPointerHitContext.hpp"
#include "app/plugins/EditorPluginsPointerController.hpp"
#include "app/project_files/EditorProjectFilesDeleteConfirmOverlayController.hpp"
#include "app/project_files/EditorProjectFilesTransientUiController.hpp"
#include "app/project_settings/EditorProjectSettingsPointerController.hpp"
#include "app/scene_viewport/EditorSceneViewportCameraController.hpp"
#include "app/scene_viewport/EditorSceneViewportObjectInteraction.hpp"
#include "app/scene_viewport/EditorSceneViewportToolbarPointerController.hpp"
#include "docking/DockMainLayoutResolver.hpp"
#include "rendering/HierarchyToolbarLayout.hpp"
#include "rendering/MaterialEditorPanelRenderer.hpp"
#include "rendering/DockTabControlGeometry.hpp"
#include "scene/EditorHierarchyMetrics.hpp"

#include <algorithm>

namespace kb::editor {
namespace {

constexpr int kHierarchyScrollbarWidth = 12;
constexpr int kHierarchyScrollbarInset = 3;
constexpr int kHierarchyScrollbarMinThumb = 24;

[[nodiscard]] bool PointInRect(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

[[nodiscard]] const DockPanelLayout* TabCloseHit(const DockLayout& layout, int x, int y) noexcept {
    for (const DockPanelLayout& panel : layout.panels) {
        if (DockTabControlGeometry::ContainsClose(panel.tab, x, y)) {
            return &panel;
        }
    }
    return nullptr;
}

[[nodiscard]] int RectHeight(const RECT& rect) noexcept {
    return std::max(0L, rect.bottom - rect.top);
}

[[nodiscard]] RECT HierarchyScrollbarTrack(const RECT& hierarchyContent) noexcept {
    const RECT list = HierarchyToolbarLayout::Resolve(hierarchyContent).listContent;
    return RECT{
        .left = list.right - kHierarchyScrollbarWidth,
        .top = list.top + kHierarchyScrollbarInset,
        .right = list.right - kHierarchyScrollbarInset,
        .bottom = list.bottom - kHierarchyScrollbarInset,
    };
}

[[nodiscard]] int HierarchyContentHeight(const EditorSceneContext& sceneContext) {
    return static_cast<int>(sceneContext.HierarchyRowCount()) * kHierarchyRowHeight;
}

[[nodiscard]] int HierarchyMaxScroll(const RECT& hierarchyContent, const EditorSceneContext& sceneContext) {
    const RECT list = HierarchyToolbarLayout::Resolve(hierarchyContent).listContent;
    return std::max(0, HierarchyContentHeight(sceneContext) - RectHeight(list));
}

[[nodiscard]] RECT HierarchyScrollbarThumb(const RECT& hierarchyContent, const EditorSceneContext& sceneContext) {
    const RECT list = HierarchyToolbarLayout::Resolve(hierarchyContent).listContent;
    const RECT track = HierarchyScrollbarTrack(hierarchyContent);
    const int viewportHeight = RectHeight(list);
    const int contentHeight = HierarchyContentHeight(sceneContext);
    const int trackHeight = RectHeight(track);
    if (trackHeight <= 0 || contentHeight <= viewportHeight) {
        return {};
    }

    const int thumbHeight = std::clamp((trackHeight * viewportHeight) / std::max(1, contentHeight), kHierarchyScrollbarMinThumb, trackHeight);
    const int maxOffset = std::max(1, contentHeight - viewportHeight);
    const int travel = std::max(0, trackHeight - thumbHeight);
    const int thumbTop = track.top + (travel * std::clamp(sceneContext.HierarchyScrollOffset(), 0, maxOffset)) / maxOffset;
    return RECT{ track.left + 2, thumbTop, track.right - 2, thumbTop + thumbHeight };
}

} // namespace

EditorLeftButtonDownRouter::EditorLeftButtonDownRouter(
    HWND mainWindow,
    EditorDockModel& dockModel,
    EditorFloatingWindowManager& floatingWindows,
    EditorDockController& dockController,
    EditorHierarchySelectionController& hierarchySelection,
    EditorSceneContext& sceneContext,
    EditorRenderBackendSettings& renderBackendSettings,
    EditorSceneBgfxViewport& sceneViewport,
    EditorPlayModeState& playMode,
    EditorShellInteractionState& shellInteraction,
    EditorPointerDragState& pointerDrag,
    const EditorMetrics& metrics) noexcept
    : mainWindow_(mainWindow)
    , dockModel_(dockModel)
    , floatingWindows_(floatingWindows)
    , dockController_(dockController)
    , hierarchySelection_(hierarchySelection)
    , sceneContext_(sceneContext)
    , renderBackendSettings_(renderBackendSettings)
    , sceneViewport_(sceneViewport)
    , playMode_(playMode)
    , shellInteraction_(shellInteraction)
    , pointerDrag_(pointerDrag)
    , metrics_(metrics) {}

void EditorLeftButtonDownRouter::Handle(HWND messageWindow, int x, int y) {
    const EditorProjectFilesDeleteConfirmOverlayController deleteConfirm(messageWindow, sceneContext_);
    if (deleteConfirm.HandlePointerDown(x, y)) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    EditorPendingTextEditCommitter pendingTextEdits(sceneContext_);
    if (pendingTextEdits.CommitPendingEdits()) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
    }
    if (EditorWindowToolbarPointerHandler::HandleLeftButtonDown(mainWindow_, messageWindow, x, y, dockModel_, sceneContext_, sceneViewport_, playMode_, shellInteraction_, metrics_)) {
        return;
    }
    if (messageWindow == mainWindow_) {
        const DockLayout layout = DockMainLayoutResolver::Resolve(mainWindow_, dockModel_, metrics_);
        if (const DockPanelLayout* closeTab = TabCloseHit(layout, x, y); closeTab != nullptr) {
            const DockPanel* panel = dockModel_.Queries().FindPanel(closeTab->panelId);
            if (panel != nullptr && panel->kind == DockPanelKind::MaterialEditor && !sceneContext_.PrepareMaterialEditorClose("closing the Material Editor tab")) {
                return;
            }
            if (dockModel_.Commands().ClosePanel(closeTab->panelId)) {
                sceneViewport_.RequestPresent();
                EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            }
            return;
        }
    }

    EditorMainDockSplitterPointerController mainSplitter(mainWindow_, dockModel_, dockController_, sceneContext_, sceneViewport_, metrics_);
    if (mainSplitter.HandlePointerDown(messageWindow, x, y)) {
        return;
    }

    const EditorPanelPointerHitContext panelHit =
        EditorPanelPointerHitContextResolver::Resolve(messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_, x, y);

    if (const std::optional<RECT> materialEditorContent = EditorPanelContentResolver::Resolve(DockPanelKind::MaterialEditor, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
        materialEditorContent.has_value() && PointInRect(*materialEditorContent, x, y)) {
        const kb::assets::AssetId materialId = sceneContext_.AssetBrowser().InspectorAsset();
        const MaterialEditorPanelCommand command = MaterialEditorPanelRenderer::CommandAt(*materialEditorContent, x, y);
        if (command != MaterialEditorPanelCommand::None) {
            switch (command) {
            case MaterialEditorPanelCommand::Save:
                static_cast<void>(sceneContext_.SaveMaterialEditorAsset(materialId));
                break;
            case MaterialEditorPanelCommand::Revert:
                static_cast<void>(sceneContext_.RevertMaterialEditorAsset(materialId));
                break;
            case MaterialEditorPanelCommand::Validate:
                static_cast<void>(sceneContext_.ValidateMaterialEditorAsset(materialId));
                break;
            case MaterialEditorPanelCommand::None:
                break;
            }
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            return;
        }

        const MaterialEditorPanelLayout materialLayout = MaterialEditorPanelRenderer::ResolveLayout(*materialEditorContent);
        if (MaterialEditorPanelPointInRect(materialLayout.graphCanvas, x, y)) {
            const std::optional<kb::render::RenderMaterialAssetData> material = sceneContext_.ReadMaterialDocumentAsset(materialId);
            if (material.has_value()) {
                if (const std::optional<std::uint32_t> nodeId = MaterialEditorPanelRenderer::GraphNodeAt(*materialEditorContent, material->graph, sceneContext_, materialId, x, y)) {
                    static_cast<void>(sceneContext_.SelectMaterialGraphNode(*nodeId));
                    if (sceneContext_.BeginMaterialGraphNodeDrag(materialId, *nodeId, x, y)) {
                        SetCapture(messageWindow);
                    }
                } else {
                    static_cast<void>(sceneContext_.ClearMaterialGraphNodeSelection());
                }
            } else {
                static_cast<void>(sceneContext_.ClearMaterialGraphNodeSelection());
            }
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            return;
        }
    }

    // A click outside the Project Settings panel dismisses its open dropdown,
    // then proceeds with normal handling of wherever the click landed.
    if (!panelHit.inProjectSettingsPanel && sceneContext_.CloseProjectSettingsDropdowns()) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
    }

    if (!panelHit.sceneContent.has_value() && sceneContext_.CloseViewportToolbarDropdowns()) {
        sceneViewport_.RequestPresent();
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    if (panelHit.sceneContent.has_value()) {
        EditorSceneViewportToolbarPointerController sceneToolbar(sceneContext_, sceneViewport_);
        if (sceneToolbar.HandlePointerDown(*panelHit.sceneContent, x, y)) {
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            return;
        }

        if (EditorSceneViewportObjectInteraction::BeginGizmoDrag(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_)) {
            SetCapture(messageWindow);
            sceneContext_.AssetBrowser().FocusSelection(false);
            EditorProjectFilesTransientUiController(sceneContext_).CloseTransientUi();
            sceneViewport_.RequestPresent();
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            return;
        }

        const bool boxSelectionReady = EditorSceneViewportObjectInteraction::BeginBoxSelection(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_);
        if (EditorSceneViewportObjectInteraction::SelectAt(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_)) {
            if (boxSelectionReady) {
                SetCapture(messageWindow);
            }
            sceneContext_.AssetBrowser().FocusSelection(false);
            EditorProjectFilesTransientUiController(sceneContext_).CloseTransientUi();
            sceneViewport_.RequestPresent();
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            return;
        }
    }

    EditorPointerDragSourceResolver::Resolve(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_, pointerDrag_);
    EditorPointerDragInteraction::CaptureIfActive(messageWindow, pointerDrag_);

    if (panelHit.hierarchyContent.has_value()) {
        const int maxOffset = HierarchyMaxScroll(*panelHit.hierarchyContent, sceneContext_);
        if (maxOffset > 0) {
            const RECT track = HierarchyScrollbarTrack(*panelHit.hierarchyContent);
            const RECT thumb = HierarchyScrollbarThumb(*panelHit.hierarchyContent, sceneContext_);
            if (PointInRect(thumb, x, y)) {
                sceneContext_.BeginHierarchyScrollbarDrag(y);
                SetCapture(messageWindow);
                EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
                return;
            }
            if (PointInRect(track, x, y)) {
                const int page = std::max(kHierarchyRowHeight, RectHeight(HierarchyToolbarLayout::Resolve(*panelHit.hierarchyContent).listContent) - kHierarchyRowHeight);
                static_cast<void>(sceneContext_.SetHierarchyScrollOffset(
                    sceneContext_.HierarchyScrollOffset() + (y < thumb.top ? -page : page),
                    maxOffset));
                EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
                return;
            }
        }
    }

    if (hierarchySelection_.HandlePointerDown(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_)) {
        sceneContext_.AssetBrowser().FocusSelection(false);
        EditorProjectFilesTransientUiController(sceneContext_).CloseTransientUi();
        sceneViewport_.RequestPresent();
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    if (EditorAssetBrowserPointerHandler::HandlePointerDown(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_)) {
        // Project Files has its own selection/focus. Clicking it must not clear
        // the current scene entity selection or hide the scene gizmo.
        if (EditorAssetBrowserPointerHandler::RequiresMouseCapture(sceneContext_)) {
            SetCapture(messageWindow);
        }
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    EditorConsolePointerController consolePointer(messageWindow, sceneContext_);
    if (panelHit.inConsolePanel && consolePointer.HandlePointerDown(*panelHit.consoleContent, x, y)) {
        sceneContext_.ClearHierarchySelection();
        sceneContext_.AssetBrowser().FocusSelection(false);
        EditorProjectFilesTransientUiController(sceneContext_).CloseTransientUi();
        if (sceneContext_.Console().IsDetailResizeDragging() || sceneContext_.Console().IsDetailScrollbarDragging() || sceneContext_.Console().IsListScrollbarDragging()) {
            SetCapture(messageWindow);
        }
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    if (panelHit.inInspectorPanel) {
        EditorInspectorPointerController inspectorPointer(sceneContext_);
        static_cast<void>(inspectorPointer.HandlePointerDown(*panelHit.inspectorContent, x, y));
        if (inspectorPointer.ShouldCaptureMouse()) {
            SetCapture(messageWindow);
        }
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    if (panelHit.inProjectSettingsPanel) {
        EditorProjectSettingsPointerController projectSettingsPointer(sceneContext_);
        if (projectSettingsPointer.HandlePointerDown(*panelHit.projectSettingsContent, x, y, renderBackendSettings_)) {
            sceneViewport_.RequestPresent();
        }
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    if (panelHit.inPluginsPanel) {
        EditorPluginsPointerController pluginsPointer(sceneContext_);
        static_cast<void>(pluginsPointer.HandlePointerDown(*panelHit.pluginsContent, x, y));
        if (sceneContext_.Plugins().IsScrollbarDragging()) {
            SetCapture(messageWindow);
        }
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    if (!panelHit.inAssetPanel) {
        sceneContext_.AssetBrowser().FocusSelection(false);
        EditorProjectFilesTransientUiController(sceneContext_).CloseTransientUi();
    }
    if (!panelHit.inHierarchyPanel && !panelHit.inConsolePanel) {
        sceneContext_.ClearHierarchySelection();
    }
    if (dockController_.HandlePointerDown(messageWindow, x, y)) {
        sceneViewport_.RequestPresent();
    }
}

} // namespace kb::editor

#endif
