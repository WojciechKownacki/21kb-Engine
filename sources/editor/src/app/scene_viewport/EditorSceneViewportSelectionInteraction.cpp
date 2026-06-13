#include "app/scene_viewport/EditorSceneViewportSelectionInteraction.hpp"

#if defined(_WIN32)
#include "app/scene_viewport/EditorSceneViewportHitResolver.hpp"
#include "app/scene_viewport/EditorSceneViewportMeshPicker.hpp"
#include "app/scene_viewport/EditorSceneViewportSelectionController.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace kb::editor {
namespace {

constexpr int kBoxSelectionStartThresholdPixels = 4;

[[nodiscard]] bool CtrlDown() noexcept {
    return (GetKeyState(VK_CONTROL) & 0x8000) != 0;
}

[[nodiscard]] EditorSceneViewportSelectionMode CurrentSelectionMode() noexcept {
    return CtrlDown() ? EditorSceneViewportSelectionMode::Toggle : EditorSceneViewportSelectionMode::Replace;
}

[[nodiscard]] RECT RectFromPoints(POINT a, POINT b) noexcept {
    RECT rect{a.x, a.y, b.x, b.y};
    if (rect.left > rect.right) {
        std::swap(rect.left, rect.right);
    }
    if (rect.top > rect.bottom) {
        std::swap(rect.top, rect.bottom);
    }
    return rect;
}

[[nodiscard]] bool ExceedsBoxThreshold(POINT start, POINT current) noexcept {
    return std::abs(current.x - start.x) >= kBoxSelectionStartThresholdPixels ||
        std::abs(current.y - start.y) >= kBoxSelectionStartThresholdPixels;
}

[[nodiscard]] bool SameViewport(const EditorSceneViewportBoxSelectionState& selection, const EditorSceneViewportHit& hit) noexcept {
    return selection.pending && selection.panelId == hit.panelId;
}

} // namespace

bool EditorSceneViewportSelectionInteraction::SelectAt(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext) {
    const std::optional<EditorSceneViewportHit> hit =
        EditorSceneViewportHitResolver::ResolveRay(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext);
    if (!hit.has_value()) {
        return false;
    }

    const EditorSceneViewportPickResult pick = EditorSceneViewportMeshPicker::PickNearest(sceneContext.Scene(), hit->ray);
    EditorSceneViewportSelectionController::ApplyClick(sceneContext, pick.entity, CurrentSelectionMode());
    return true;
}

bool EditorSceneViewportSelectionInteraction::BeginBoxSelection(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext) {
    const std::optional<EditorSceneViewportHit> hit =
        EditorSceneViewportHitResolver::ResolveRay(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext);
    if (!hit.has_value()) {
        return false;
    }

    const POINT point{static_cast<LONG>(hit->localX), static_cast<LONG>(hit->localY)};
    sceneContext.BeginViewportBoxSelection(EditorSceneViewportBoxSelectionState{
        .pending = true,
        .active = false,
        .additive = CtrlDown(),
        .panelId = hit->panelId,
        .start = point,
        .current = point,
        .renderArea = hit->renderArea,
    });
    return true;
}

bool EditorSceneViewportSelectionInteraction::UpdateBoxSelection(
    HWND sourceWindow,
    HWND mainWindow,
    int x,
    int y,
    const EditorDockModel& dockModel,
    const EditorFloatingWindowManager& floatingWindows,
    const EditorMetrics& metrics,
    EditorSceneContext& sceneContext,
    bool leftButtonDown) {
    const EditorSceneViewportBoxSelectionState& selection = sceneContext.ViewportBoxSelection();
    if (!selection.pending) {
        return false;
    }
    if (!leftButtonDown) {
        sceneContext.ClearViewportBoxSelection();
        return true;
    }

    const std::optional<EditorSceneViewportHit> hit =
        EditorSceneViewportHitResolver::ResolveRay(sourceWindow, mainWindow, x, y, dockModel, floatingWindows, metrics, sceneContext);
    if (!hit.has_value() || !SameViewport(selection, *hit)) {
        return false;
    }

    const POINT current{static_cast<LONG>(hit->localX), static_cast<LONG>(hit->localY)};
    sceneContext.UpdateViewportBoxSelection(current, selection.active || ExceedsBoxThreshold(selection.start, current));
    return sceneContext.ViewportBoxSelection().active;
}

bool EditorSceneViewportSelectionInteraction::CommitBoxSelection(EditorSceneContext& sceneContext) {
    const EditorSceneViewportBoxSelectionState selection = sceneContext.ViewportBoxSelection();
    if (!selection.pending) {
        return false;
    }
    sceneContext.ClearViewportBoxSelection();
    if (!selection.active) {
        return true;
    }

    RECT selectionRect = RectFromPoints(selection.start, selection.current);
    const EditorViewportCameraState& camera = sceneContext.ViewportCamera(selection.panelId);
    const std::vector<kb::scene::SceneEntity> entities = EditorSceneViewportMeshPicker::PickInsideRect(
        sceneContext.Scene(),
        camera,
        selection.renderArea,
        selectionRect);
    EditorSceneViewportSelectionController::ApplyBox(
        sceneContext,
        entities,
        selection.additive ? EditorSceneViewportSelectionMode::Toggle : EditorSceneViewportSelectionMode::Replace);
    return true;
}

} // namespace kb::editor

#endif
