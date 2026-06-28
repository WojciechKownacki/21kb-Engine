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

namespace kb::editor {

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

    if (sceneContext_.HasMaterialGraphPinConnection()) {
        const kb::assets::AssetId materialId = sceneContext_.MaterialGraphPinConnectionAssetId();
        const std::optional<RECT> materialEditorContent = EditorPanelContentResolver::Resolve(DockPanelKind::MaterialEditor, messageWindow, mainWindow_, dockModel_, floatingWindows_, metrics_);
        if (materialEditorContent.has_value()) {
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
                        sceneContext_.CancelMaterialGraphPinConnection();
                    }
                } else {
                    sceneContext_.CancelMaterialGraphPinConnection();
                }
            } else {
                sceneContext_.CancelMaterialGraphPinConnection();
            }
        } else {
            sceneContext_.CancelMaterialGraphPinConnection();
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
