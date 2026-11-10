#pragma once

#include "docking/EditorDockController.hpp"
#include "docking/EditorDockModel.hpp"
#include "docking/EditorFloatingWindowManager.hpp"
#include "app/EditorPlayModeState.hpp"
#include "app/EditorPointerDragState.hpp"
#include "app/EditorShellInteractionState.hpp"
#include "rendering/EditorRenderBackendSettings.hpp"
#include "rendering/EditorSceneBgfxViewport.hpp"
#include "scene/EditorHierarchySelectionController.hpp"
#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

struct EditorPanelPointerHitContext;

#if defined(_WIN32)
class EditorLeftButtonDownRouter {
public:
    EditorLeftButtonDownRouter(
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
        const EditorMetrics& metrics) noexcept;

    void Handle(HWND messageWindow, int x, int y);

private:
    // The World 2D Map rows that open a window or a menu: the Map Editor and the client box sizes.
    void HandleWorld2DMapClick(InspectorPropertyId property);
    // One panel's share of a left press each. True when the panel took the press, so Handle stops there.
    [[nodiscard]] bool HandleParticleEditorDown(HWND messageWindow, int x, int y, const RECT& particleEditorContent);
    [[nodiscard]] bool HandleMaterialEditorDown(HWND messageWindow, int x, int y, const RECT& materialEditorContent);
    [[nodiscard]] bool HandleSkeletalMeshEditorDown(HWND messageWindow, int x, int y, const RECT& skeletalMeshEditorContent);
    [[nodiscard]] bool HandleAnimationClipEditorDown(HWND messageWindow, int x, int y, const RECT& animationClipEditorContent);
    [[nodiscard]] bool HandleInspectorDown(HWND messageWindow, int x, int y, const EditorPanelPointerHitContext& panelHit);
    [[nodiscard]] bool HandleBuildGameDown(HWND messageWindow, int x, int y, const RECT& buildGameContent);

    HWND mainWindow_ = nullptr;
    EditorDockModel& dockModel_;
    EditorFloatingWindowManager& floatingWindows_;
    EditorDockController& dockController_;
    EditorHierarchySelectionController& hierarchySelection_;
    EditorSceneContext& sceneContext_;
    EditorRenderBackendSettings& renderBackendSettings_;
    EditorSceneBgfxViewport& sceneViewport_;
    EditorPlayModeState& playMode_;
    EditorShellInteractionState& shellInteraction_;
    EditorPointerDragState& pointerDrag_;
    const EditorMetrics& metrics_;
};
#endif

} // namespace kb::editor
