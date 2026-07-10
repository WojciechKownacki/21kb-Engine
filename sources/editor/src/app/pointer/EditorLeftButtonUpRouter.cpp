#include "app/pointer/EditorLeftButtonUpRouter.hpp"

#if defined(_WIN32)
#include "app/EditorAssetBrowserPointerHandler.hpp"
#include "app/EditorPointerDragInteraction.hpp"
#include "app/EditorWindowInvalidator.hpp"
#include "app/console/EditorConsolePointerController.hpp"
#include "app/inspector/EditorInspectorPointerController.hpp"
#include "app/scene_viewport/EditorSceneViewportObjectInteraction.hpp"
#include "assets/EditorAssetBrowserState.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "rendering/EditorPanelContentResolver.hpp"
#include "rendering/EditorHostSurfaceLayoutResolver.hpp"
#include "rendering/MaterialEditorPanelRenderer.hpp"

#include <algorithm>
#include <vector>

namespace kb::editor {

namespace {

[[nodiscard]] POINT MaterialGraphDocumentPointFromWindow(const MaterialEditorPanelLayout& layout, const EditorSceneContext& sceneContext, int x, int y) noexcept {
    const float zoom = std::max(0.1F, sceneContext.MaterialGraphZoom());
    return POINT{
        static_cast<LONG>(static_cast<float>(x - layout.graphCanvas.left - sceneContext.MaterialGraphPanX()) / zoom),
        static_cast<LONG>(static_cast<float>(y - layout.graphCanvas.top - sceneContext.MaterialGraphPanY()) / zoom),
    };
}

} // namespace

EditorLeftButtonUpRouter::EditorLeftButtonUpRouter(
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

void EditorLeftButtonUpRouter::Handle(HWND messageWindow, int x, int y) {
    static_cast<void>(x);
    static_cast<void>(y);
    shellInteraction_.ClearPressedSave();
    shellInteraction_.ClearPressedTransport();
    if (sceneContext_.IsHierarchyScrollbarDragging()) {
        sceneContext_.EndHierarchyScrollbarDrag();
        ReleaseCapture();
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    if (sceneContext_.IsMaterialGraphNodeDragging()) {
        static_cast<void>(sceneContext_.EndMaterialGraphNodeDrag());
        ReleaseCapture();
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    if (sceneContext_.IsMaterialGraphCommentDragging()) {
        static_cast<void>(sceneContext_.EndMaterialGraphCommentDrag());
        ReleaseCapture();
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    if (sceneContext_.IsMaterialGraphBoxSelecting()) {
        std::vector<std::uint32_t> nodeIds;
        const kb::assets::AssetId materialId = sceneContext_.MaterialEditor().OpenAssetId();
        const std::optional<RECT> materialEditorContent = EditorPanelContentResolver::Resolve(DockPanelKind::MaterialEditor, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
        if (materialEditorContent.has_value()) {
            const MaterialEditorPanelLayout materialLayout = MaterialEditorPanelRenderer::ResolveLayout(*materialEditorContent);
            if (sceneContext_.MaterialEditor().InfoPanelVisible() &&
                MaterialEditorPanelRectWidth(materialLayout.detailsPanel) >= 220 &&
                MaterialEditorPanelRectHeight(materialLayout.detailsPanel) >= 140 &&
                MaterialEditorPanelPointInRect(materialLayout.detailsPanel, x, y)) {
                static_cast<void>(sceneContext_.CancelMaterialGraphPinConnection());
                ReleaseCapture();
                EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
                return;
            }
            const std::optional<kb::render::RenderMaterialAssetData> material = sceneContext_.MaterialEditor().WorkingCopy().has_value()
                ? sceneContext_.MaterialEditor().WorkingCopy()
                : sceneContext_.ReadMaterialDocumentAsset(materialId);
            if (material.has_value()) {
                const RECT selectionRect{
                    sceneContext_.MaterialGraphBoxSelectionStartX(),
                    sceneContext_.MaterialGraphBoxSelectionStartY(),
                    sceneContext_.MaterialGraphBoxSelectionCurrentX(),
                    sceneContext_.MaterialGraphBoxSelectionCurrentY(),
                };
                nodeIds = MaterialEditorPanelRenderer::GraphNodeIdsInRect(*materialEditorContent, material->graph, sceneContext_, materialId, selectionRect);
            }
        }
        const std::uint32_t primaryNodeId = nodeIds.empty() ? 0U : nodeIds.back();
        static_cast<void>(sceneContext_.EndMaterialGraphBoxSelection(std::move(nodeIds), primaryNodeId));
        ReleaseCapture();
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    if (sceneContext_.HasMaterialGraphPinConnection()) {
        const kb::assets::AssetId materialId = sceneContext_.MaterialGraphPinConnectionAssetId();
        const std::optional<RECT> materialEditorContent = EditorPanelContentResolver::Resolve(DockPanelKind::MaterialEditor, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
        if (materialEditorContent.has_value()) {
            const MaterialEditorPanelLayout materialLayout = MaterialEditorPanelRenderer::ResolveLayout(*materialEditorContent);
            if (sceneContext_.MaterialEditor().InfoPanelVisible() &&
                MaterialEditorPanelRectWidth(materialLayout.detailsPanel) >= 220 &&
                MaterialEditorPanelRectHeight(materialLayout.detailsPanel) >= 140 &&
                MaterialEditorPanelPointInRect(materialLayout.detailsPanel, x, y)) {
                static_cast<void>(sceneContext_.CancelMaterialGraphPinConnection());
                ReleaseCapture();
                EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
                return;
            }
            const std::optional<kb::render::RenderMaterialAssetData> material = sceneContext_.MaterialEditor().WorkingCopy().has_value()
                ? sceneContext_.MaterialEditor().WorkingCopy()
                : sceneContext_.ReadMaterialDocumentAsset(materialId);
            if (material.has_value()) {
                if (const std::optional<MaterialEditorGraphPinHit> pin = MaterialEditorPanelRenderer::GraphPinAt(*materialEditorContent, material->graph, sceneContext_, materialId, x, y)) {
                    if (!sceneContext_.CompleteMaterialGraphPinConnection(
                        materialId,
                        pin->nodeId,
                        pin->pin,
                        pin->direction == MaterialEditorGraphPinDirection::Input)) {
                        static_cast<void>(sceneContext_.CancelMaterialGraphPinConnection());
                    }
                } else {
                    if (MaterialEditorPanelPointInRect(materialLayout.graphCanvas, x, y)) {
                        const POINT graphPoint = MaterialGraphDocumentPointFromWindow(materialLayout, sceneContext_, x, y);
                        if (!sceneContext_.OpenMaterialGraphContextMenuForPinConnection(materialId, x, y, graphPoint.x, graphPoint.y)) {
                            static_cast<void>(sceneContext_.CancelMaterialGraphPinConnection());
                        }
                    } else {
                        static_cast<void>(sceneContext_.CancelMaterialGraphPinConnection());
                    }
                }
            } else {
                static_cast<void>(sceneContext_.CancelMaterialGraphPinConnection());
            }
        } else {
            static_cast<void>(sceneContext_.CancelMaterialGraphPinConnection());
        }
        ReleaseCapture();
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    EditorInspectorPointerController inspectorPointer(sceneContext_);
    const bool wasDraggingMeshPreview = sceneContext_.Inspector().IsDraggingMeshPreview();
    if (inspectorPointer.HandlePointerUp()) {
        ReleaseCapture();
        if (!wasDraggingMeshPreview) {
            sceneViewport_.RequestPresent();
        }
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    if (EditorSceneViewportObjectInteraction::EndGizmoDrag(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_)) {
        ReleaseCapture();
        sceneViewport_.RequestPresent();
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    if (EditorSceneViewportObjectInteraction::CommitBoxSelection(sceneContext_)) {
        ReleaseCapture();
        sceneViewport_.RequestPresent();
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    if (pointerDrag_.Potential()) {
        if (pointerDrag_.kind == EditorPointerDragKind::MaterialGraphPaletteCommand && !pointerDrag_.Active()) {
            const MaterialEditorGraphMenuCommand command = pointerDrag_.materialGraphCommand;
            pointerDrag_.Clear();
            static_cast<void>(sceneContext_.ExecuteMaterialGraphContextMenuCommand(command));
            ReleaseCapture();
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            return;
        }
        const bool wasDragging = pointerDrag_.Active();
        const bool handledDrop = EditorPointerDragInteraction::Complete(messageWindow, mainWindow_, x, y, dockModel_, floatingWindows_, metrics_, sceneContext_, pointerDrag_);
        // A press without a drag is a plain click: commit the deferred asset
        // selection now (preview in the Inspector). A real drag changes nothing.
        if (!wasDragging) {
            const kb::assets::AssetId pending = sceneContext_.AssetBrowser().TakePendingPreviewAsset();
            if (pending.IsValid() &&
                sceneContext_.AssetBrowser().SelectAsset(pending, sceneContext_.Scene().Assets().Manager())) {
                EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
            }
        } else {
            sceneContext_.AssetBrowser().ClearPendingPreviewAsset();
        }
        if (handledDrop) {
            sceneViewport_.RequestPresent();
        }
        return;
    }

    if (EditorAssetBrowserPointerHandler::HandlePointerUp(sceneContext_)) {
        ReleaseCapture();
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }
    EditorConsolePointerController consolePointer(messageWindow, sceneContext_);
    if (consolePointer.HandlePointerUp()) {
        ReleaseCapture();
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        return;
    }

    const DockPointerDrag* activeDockDrag = dockController_.ActiveDrag();
    const bool endingSplitterDrag = activeDockDrag != nullptr && activeDockDrag->kind == DockHitKind::Splitter;
    if (dockController_.HandlePointerUp(messageWindow)) {
        if (endingSplitterDrag && messageWindow == mainWindow_) {
            EditorHostSurfaceLayoutResolver::SyncMainWindow(mainWindow_, dockModel_, metrics_, sceneContext_, sceneViewport_);
            EditorWindowInvalidator::InvalidateMainAndSource(mainWindow_, messageWindow);
        }
        sceneViewport_.RequestPresent();
    }
}

} // namespace kb::editor

#endif
