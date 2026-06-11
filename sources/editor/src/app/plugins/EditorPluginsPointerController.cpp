#include "app/plugins/EditorPluginsPointerController.hpp"

#if defined(_WIN32)
#include "rendering/PluginsPanelRenderer.hpp"
#include "scene/EditorSceneContext.hpp"

#include <algorithm>
#include <limits>

namespace kb::editor {
namespace {

[[nodiscard]] bool PointInRect(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

[[nodiscard]] int RectHeight(const RECT& rect) noexcept {
    return std::max(0L, rect.bottom - rect.top);
}

} // namespace

EditorPluginsPointerController::EditorPluginsPointerController(EditorSceneContext& sceneContext) noexcept
    : sceneContext_(sceneContext) {}

bool EditorPluginsPointerController::HandlePointerDown(const RECT& content, int x, int y) {
    const PluginsPanelRenderer::Hit hit = PluginsPanelRenderer::HitTest(content, sceneContext_, x, y);
    switch (hit.kind) {
    case PluginsPanelHitKind::Toggle:
    case PluginsPanelHitKind::Row:
        return sceneContext_.ToggleProjectPlugin(static_cast<std::size_t>(hit.index));
    case PluginsPanelHitKind::ScrollbarThumb:
        sceneContext_.Plugins().BeginScrollbarDrag(y);
        return true;
    case PluginsPanelHitKind::ScrollbarTrack: {
        const int page = std::max(34, RectHeight(hit.rect) - 34);
        const int midpoint = hit.rect.top + RectHeight(hit.rect) / 2;
        return sceneContext_.Plugins().SetScrollOffset(
            sceneContext_.Plugins().ScrollOffset() + (y < midpoint ? -page : page),
            PluginsPanelRenderer::MaxScrollOffset(content));
    }
    case PluginsPanelHitKind::None:
    default:
        return sceneContext_.Plugins().SetHoveredPluginIndex(std::numeric_limits<std::size_t>::max());
    }
}

bool EditorPluginsPointerController::HandlePointerMove(const std::optional<RECT>& content, int x, int y, bool leftButtonDown) {
    if (sceneContext_.Plugins().IsScrollbarDragging()) {
        if (!leftButtonDown) {
            sceneContext_.Plugins().EndScrollbarDrag();
            return true;
        }
        if (content.has_value()) {
            return sceneContext_.Plugins().DragScrollbar(
                y,
                PluginsPanelRenderer::ScrollbarTrackTravel(*content),
                PluginsPanelRenderer::MaxScrollOffset(*content));
        }
        return false;
    }
    return UpdateHoverOrClear(content, x, y);
}

bool EditorPluginsPointerController::HandleMouseWheel(const RECT& content, int x, int y, int wheelDelta) {
    if (!PointInRect(content, x, y)) {
        return false;
    }
    const int direction = wheelDelta > 0 ? 1 : -1;
    return sceneContext_.Plugins().SetScrollOffset(
        sceneContext_.Plugins().ScrollOffset() - direction * 34 * 3,
        PluginsPanelRenderer::MaxScrollOffset(content));
}

bool EditorPluginsPointerController::UpdateHoverOrClear(const std::optional<RECT>& content, int x, int y) {
    if (!content.has_value() || !PointInRect(*content, x, y)) {
        return sceneContext_.Plugins().SetHoveredPluginIndex(std::numeric_limits<std::size_t>::max());
    }
    const PluginsPanelRenderer::Hit hit = PluginsPanelRenderer::HitTest(*content, sceneContext_, x, y);
    const std::size_t hovered = (hit.kind == PluginsPanelHitKind::Toggle || hit.kind == PluginsPanelHitKind::Row) ? hit.index : std::numeric_limits<std::size_t>::max();
    return sceneContext_.Plugins().SetHoveredPluginIndex(hovered);
}

bool EditorPluginsPointerController::Contains(const std::optional<RECT>& content, int x, int y) const noexcept {
    return content.has_value() && PointInRect(*content, x, y);
}

} // namespace kb::editor

#endif
