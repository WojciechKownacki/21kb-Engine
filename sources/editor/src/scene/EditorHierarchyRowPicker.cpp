#include "scene/EditorHierarchyRowPicker.hpp"

#if defined(_WIN32)
#include "rendering/HierarchyRowLayout.hpp"
#include "rendering/HierarchyToolbarLayout.hpp"
#include "scene/EditorHierarchyMetrics.hpp"

namespace kb::editor {
namespace {

[[nodiscard]] bool Contains(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

} // namespace

bool EditorHierarchyRowPicker::SelectAtContentPoint(const RECT& content, int x, int y, EditorSceneContext& sceneContext) {
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

    const int relativeY = y - toolbar.listContent.top;
    const std::size_t rowIndex = static_cast<std::size_t>(relativeY / kHierarchyRowHeight);
    const std::vector<EditorHierarchyRow> rows = sceneContext.HierarchyRows();
    if (rowIndex >= rows.size()) {
        sceneContext.ClearHierarchySelection();
        return true;
    }

    const RECT rowRect{
        toolbar.listContent.left,
        toolbar.listContent.top + static_cast<int>(rowIndex) * kHierarchyRowHeight,
        toolbar.listContent.right,
        toolbar.listContent.top + static_cast<int>(rowIndex + 1U) * kHierarchyRowHeight,
    };
    const HierarchyRowLayoutRects rowLayout = HierarchyRowLayout::Resolve(rowRect, rows[rowIndex]);

    if (Contains(rowLayout.visibilityCell, x, y)) {
        static_cast<void>(sceneContext.ToggleEntityVisibility(rows[rowIndex].entity));
        return true;
    }
    if (rows[rowIndex].hasChildren && Contains(rowLayout.expanderHit, x, y)) {
        static_cast<void>(sceneContext.ToggleHierarchyRowExpanded(rowIndex));
        return true;
    }

    [[maybe_unused]] const bool selected = sceneContext.SelectHierarchyRow(rowIndex);
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

    const int relativeY = y - toolbar.listContent.top;
    const std::size_t rowIndex = static_cast<std::size_t>(relativeY / kHierarchyRowHeight);
    const std::vector<EditorHierarchyRow> rows = sceneContext.HierarchyRows();
    return rowIndex < rows.size() ? rows[rowIndex].entity : kb::scene::SceneEntity{};
}

} // namespace kb::editor

#endif
