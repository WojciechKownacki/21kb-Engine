#include "scene/EditorHierarchyRowPicker.hpp"

#if defined(_WIN32)
#include "rendering/HierarchyRowLayout.hpp"
#include "rendering/HierarchyToolbarLayout.hpp"
#include "scene/EditorHierarchyMetrics.hpp"

#include <algorithm>
#include <optional>

namespace kb::editor {
namespace {

[[nodiscard]] bool Contains(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

[[nodiscard]] int RectHeight(const RECT& rect) noexcept {
    return std::max(0L, rect.bottom - rect.top);
}

[[nodiscard]] int ResolvedScrollOffset(const RECT& listContent, const EditorSceneContext& sceneContext) {
    const int contentHeight = static_cast<int>(sceneContext.HierarchyRowCount()) * kHierarchyRowHeight;
    const int maxOffset = std::max(0, contentHeight - RectHeight(listContent));
    return std::clamp(sceneContext.HierarchyScrollOffset(), 0, maxOffset);
}

[[nodiscard]] std::optional<std::size_t> RowIndexAt(const RECT& listContent, int y, const EditorSceneContext& sceneContext) {
    if (y < listContent.top || y >= listContent.bottom) {
        return std::nullopt;
    }

    const int relativeY = y - listContent.top + ResolvedScrollOffset(listContent, sceneContext);
    if (relativeY < 0) {
        return std::nullopt;
    }

    const std::size_t rowIndex = static_cast<std::size_t>(relativeY / kHierarchyRowHeight);
    return rowIndex < sceneContext.HierarchyRowCount() ? std::optional<std::size_t>{ rowIndex } : std::nullopt;
}

} // namespace

bool EditorHierarchyRowPicker::SelectAtContentPoint(const RECT& content, int x, int y, EditorSceneContext& sceneContext) {
    return SelectAtContentPoint(content, x, y, false, false, sceneContext);
}

bool EditorHierarchyRowPicker::SelectAtContentPoint(const RECT& content, int x, int y, bool additive, bool range, EditorSceneContext& sceneContext) {
    if (!Contains(content, x, y)) {
        return false;
    }

    const HierarchyToolbarLayoutRects toolbar = HierarchyToolbarLayout::Resolve(content);
    if (Contains(toolbar.addButton, x, y)) {
        sceneContext.FocusHierarchySearch(false);
        sceneContext.ClearHierarchySearch();
        static_cast<void>(sceneContext.CreateHierarchyObject());
        return true;
    }
    if (Contains(toolbar.searchBox, x, y)) {
        sceneContext.ClearHierarchySelection();
        sceneContext.FocusHierarchySearch(true);
        return true;
    }
    if (y < toolbar.listContent.top) {
        sceneContext.ClearHierarchySelection();
        sceneContext.FocusHierarchySearch(false);
        return true;
    }

    sceneContext.FocusHierarchySearch(false);

    const std::optional<std::size_t> pickedRow = RowIndexAt(toolbar.listContent, y, sceneContext);
    if (!pickedRow.has_value()) {
        sceneContext.ClearHierarchySelection();
        return true;
    }

    const std::size_t rowIndex = *pickedRow;
    const EditorHierarchyRow* row = sceneContext.HierarchyRowAt(rowIndex);
    if (row == nullptr) {
        sceneContext.ClearHierarchySelection();
        return true;
    }

    const int rowTop = toolbar.listContent.top + static_cast<int>(rowIndex) * kHierarchyRowHeight - ResolvedScrollOffset(toolbar.listContent, sceneContext);
    const RECT rowRect{
        toolbar.listContent.left,
        rowTop,
        toolbar.listContent.right,
        rowTop + kHierarchyRowHeight,
    };
    const HierarchyRowLayoutRects rowLayout = HierarchyRowLayout::Resolve(rowRect, *row);

    if (Contains(rowLayout.visibilityCell, x, y)) {
        static_cast<void>(sceneContext.ToggleEntityVisibility(row->entity));
        return true;
    }
    if (row->hasChildren && Contains(rowLayout.expanderHit, x, y)) {
        static_cast<void>(sceneContext.ToggleHierarchyRowExpanded(rowIndex));
        return true;
    }

    [[maybe_unused]] const bool selected = sceneContext.SelectHierarchyRow(rowIndex, additive, range);
    return true;
}

kb::scene::SceneEntity EditorHierarchyRowPicker::EntityAtContentPoint(const RECT& content, int x, int y, const EditorSceneContext& sceneContext) {
    if (!Contains(content, x, y)) {
        return {};
    }

    const HierarchyToolbarLayoutRects toolbar = HierarchyToolbarLayout::Resolve(content);
    if (y < toolbar.listContent.top || Contains(toolbar.addButton, x, y) || Contains(toolbar.searchBox, x, y)) {
        return {};
    }

    const std::optional<std::size_t> rowIndex = RowIndexAt(toolbar.listContent, y, sceneContext);
    const EditorHierarchyRow* row = rowIndex.has_value() ? sceneContext.HierarchyRowAt(*rowIndex) : nullptr;
    return row != nullptr ? row->entity : kb::scene::SceneEntity{};
}

} // namespace kb::editor

#endif
