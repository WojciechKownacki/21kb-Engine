#include "scene/EditorHierarchyRowPicker.hpp"

#if defined(_WIN32)
#include "scene/EditorHierarchyMetrics.hpp"

namespace kb::editor {

bool EditorHierarchyRowPicker::SelectAtContentPoint(const RECT& content, int x, int y, EditorSceneContext& sceneContext) {
    if (x < content.left || x >= content.right || y < content.top || y >= content.bottom) {
        return false;
    }

    const int relativeY = y - content.top;
    const std::size_t rowIndex = static_cast<std::size_t>(relativeY / kHierarchyRowHeight);
    [[maybe_unused]] const bool selected = sceneContext.SelectHierarchyRow(rowIndex);
    return true;
}

} // namespace kb::editor

#endif
