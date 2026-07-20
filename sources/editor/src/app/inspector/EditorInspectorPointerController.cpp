#include "app/inspector/EditorInspectorPointerController.hpp"

#if defined(_WIN32)
#include "inspection/InspectorPanelInteraction.hpp"
#include "rendering/InspectorPanelRenderer.hpp"
#include "scene/EditorSceneContext.hpp"

#include <algorithm>

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
    // The Add Component menu's own scrollbar (section == AddComponent) is distinct
    // from the whole-panel scrollbar (section == None).
    if (hit.kind == InspectorHitKind::ScrollbarThumb && hit.section == InspectorSectionId::AddComponent) {
        sceneContext_.Inspector().BeginAddComponentScrollbarDrag(y - static_cast<int>(hit.rect.top));
        shouldCaptureMouse_ = true;
        return true;
    }
    if (hit.kind == InspectorHitKind::ScrollbarTrack && hit.section == InspectorSectionId::AddComponent) {
        const InspectorPanelRenderer::AddComponentScrollInfo info = InspectorPanelRenderer::AddComponentScrollGeometry(content, sceneContext_);
        static_cast<void>(sceneContext_.Inspector().SetAddComponentScroll(
            sceneContext_.Inspector().AddComponentScroll() + (y < info.thumb.top ? -104 : 104), info.maxScroll));
        shouldCaptureMouse_ = false;
        return true;
    }
    if (hit.kind == InspectorHitKind::ScrollbarThumb) {
        sceneContext_.Inspector().BeginScrollbarDrag(y);
        shouldCaptureMouse_ = true;
        return true;
    }
    if (hit.kind == InspectorHitKind::ScrollbarTrack) {
        const RECT thumb = InspectorPanelRenderer::ScrollbarThumbRect(content, sceneContext_);
        const int page = std::max(24, static_cast<int>(content.bottom - content.top) - 24);
        static_cast<void>(sceneContext_.Inspector().SetScrollOffset(
            sceneContext_.Inspector().ScrollOffset() + (y < thumb.top ? -page : page),
            InspectorPanelRenderer::MaxScrollOffset(content, sceneContext_)));
        shouldCaptureMouse_ = false;
        return true;
    }
    static_cast<void>(InspectorPanelInteraction::HandlePointerDown(sceneContext_, hit, x, y));
    shouldCaptureMouse_ = hit.kind == InspectorHitKind::FloatField || hit.kind == InspectorHitKind::MeshPreview;
    return true;
}

bool EditorInspectorPointerController::HandlePointerDrag(const std::optional<RECT>& content, int x, int y) {
    if (sceneContext_.Inspector().IsAddComponentScrollbarDragging()) {
        if (!content.has_value()) {
            sceneContext_.Inspector().EndAddComponentScrollbarDrag();
            return true;
        }
        const InspectorPanelRenderer::AddComponentScrollInfo info = InspectorPanelRenderer::AddComponentScrollGeometry(*content, sceneContext_);
        if (!info.active) {
            sceneContext_.Inspector().EndAddComponentScrollbarDrag();
            return true;
        }
        const int thumbHeight = std::max(1, static_cast<int>(info.thumb.bottom - info.thumb.top));
        const int travel = std::max(1, static_cast<int>(info.track.bottom - info.track.top) - thumbHeight);
        const int newThumbTop = y - sceneContext_.Inspector().AddComponentScrollbarGrabOffset();
        static_cast<void>(sceneContext_.Inspector().SetAddComponentScroll((newThumbTop - static_cast<int>(info.track.top)) * info.maxScroll / travel, info.maxScroll));
        return true;
    }
    if (sceneContext_.Inspector().IsScrollbarDragging()) {
        if (!content.has_value()) {
            sceneContext_.Inspector().EndScrollbarDrag();
            return true;
        }
        const RECT track = InspectorPanelRenderer::ScrollbarTrackRect(*content);
        const RECT thumb = InspectorPanelRenderer::ScrollbarThumbRect(*content, sceneContext_);
        const int thumbHeight = std::max(1, static_cast<int>(thumb.bottom - thumb.top));
        const int trackPixels = std::max(1, static_cast<int>(track.bottom - track.top) - thumbHeight);
        sceneContext_.Inspector().DragScrollbar(y, trackPixels, InspectorPanelRenderer::MaxScrollOffset(*content, sceneContext_));
        return true;
    }
    return InspectorPanelInteraction::HandlePointerDrag(sceneContext_, x, y);
}

bool EditorInspectorPointerController::HandlePointerUp() {
    if (sceneContext_.Inspector().IsAddComponentScrollbarDragging()) {
        sceneContext_.Inspector().EndAddComponentScrollbarDrag();
        return true;
    }
    if (sceneContext_.Inspector().IsScrollbarDragging()) {
        sceneContext_.Inspector().EndScrollbarDrag();
        return true;
    }
    return InspectorPanelInteraction::HandlePointerUp(sceneContext_);
}

bool EditorInspectorPointerController::UpdateHover(const RECT& content, int x, int y) {
    const InspectorPanelRenderer::Hit hit = InspectorPanelRenderer::HitTest(content, sceneContext_, x, y);
    return InspectorPanelInteraction::UpdateHover(sceneContext_, hit);
}

bool EditorInspectorPointerController::HandleMouseWheel(const RECT& content, int x, int y, int wheelDelta) {
    if (!PointInRect(content, x, y)) {
        return false;
    }
    // Wheeling over the open Add Component list scrolls that list, not the panel.
    if (InspectorPanelRenderer::AddComponentListContains(content, sceneContext_, x, y)) {
        const InspectorPanelRenderer::AddComponentScrollInfo info = InspectorPanelRenderer::AddComponentScrollGeometry(content, sceneContext_);
        if (info.active) {
            const int direction = wheelDelta > 0 ? 1 : -1;
            static_cast<void>(sceneContext_.Inspector().SetAddComponentScroll(sceneContext_.Inspector().AddComponentScroll() - direction * 52, info.maxScroll));
            return true;
        }
    }
    const InspectorPanelRenderer::Hit hit = InspectorPanelRenderer::HitTest(content, sceneContext_, x, y);
    const int maxScroll = InspectorPanelRenderer::MaxScrollOffset(content, sceneContext_);
    if (maxScroll > 0) {
        const int direction = wheelDelta > 0 ? 1 : -1;
        static_cast<void>(sceneContext_.Inspector().SetScrollOffset(sceneContext_.Inspector().ScrollOffset() - direction * 72, maxScroll));
        return true;
    }
    return InspectorPanelInteraction::HandleMouseWheel(sceneContext_, hit, wheelDelta);
}

bool EditorInspectorPointerController::HandleDoubleClick(const RECT& content, int x, int y) {
    const InspectorPanelRenderer::Hit hit = InspectorPanelRenderer::HitTest(content, sceneContext_, x, y);
    return InspectorPanelInteraction::HandleDoubleClick(sceneContext_, hit);
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
