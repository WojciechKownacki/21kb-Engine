#include "app/inspector/EditorInspectorPointerController.hpp"

#if defined(_WIN32)
#include "inspection/InspectorPanelInteraction.hpp"
#include "rendering/InspectorPanelRenderer.hpp"
#include "scene/EditorSceneContext.hpp"

namespace kb::editor {
namespace {

[[nodiscard]] bool PointInRect(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

} // namespace

EditorInspectorPointerController::EditorInspectorPointerController(EditorSceneContext& sceneContext) noexcept
    : sceneContext_(sceneContext) {}

bool EditorInspectorPointerController::HandlePointerDown(const RECT& content, int x, int y) {
    const InspectorPanelRenderer::Hit hit = InspectorPanelRenderer::HitTest(content, sceneContext_, x, y);
    static_cast<void>(InspectorPanelInteraction::HandlePointerDown(sceneContext_, hit, x, y));
    shouldCaptureMouse_ = hit.kind == InspectorHitKind::FloatField;
    return true;
}

bool EditorInspectorPointerController::HandlePointerDrag(int x, int y) {
    return InspectorPanelInteraction::HandlePointerDrag(sceneContext_, x, y);
}

bool EditorInspectorPointerController::HandlePointerUp() {
    return InspectorPanelInteraction::HandlePointerUp(sceneContext_);
}

bool EditorInspectorPointerController::UpdateHover(const RECT& content, int x, int y) {
    const InspectorPanelRenderer::Hit hit = InspectorPanelRenderer::HitTest(content, sceneContext_, x, y);
    return InspectorPanelInteraction::UpdateHover(sceneContext_, hit);
}

bool EditorInspectorPointerController::UpdateHoverOrClear(const std::optional<RECT>& content, int x, int y) {
    if (Contains(content, x, y)) {
        return UpdateHover(*content, x, y);
    }
    return ClearHoverIfNeeded();
}

bool EditorInspectorPointerController::Contains(const std::optional<RECT>& content, int x, int y) const noexcept {
    return content.has_value() && PointInRect(*content, x, y);
}

bool EditorInspectorPointerController::ClearHoverIfNeeded() {
    if (!sceneContext_.Inspector().IsAnyHovered()) {
        return false;
    }
    sceneContext_.Inspector().ClearHover();
    return true;
}

bool EditorInspectorPointerController::ShouldCaptureMouse() const noexcept {
    return shouldCaptureMouse_;
}

} // namespace kb::editor

#endif
