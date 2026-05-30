#include "rendering/HierarchyPanelRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"
#include "scene/EditorHierarchyMetrics.hpp"

#include <string>

namespace kb::editor {

void HierarchyPanelRenderer::Paint(HDC dc, const RECT& content, const EditorTheme& theme, const EditorSceneContext& sceneContext) const {
    const std::vector<EditorSceneContext::HierarchyRow> rows = sceneContext.HierarchyRows();
    const kb::scene::SceneEntity selected = sceneContext.SelectedEntity();

    int y = content.top;
    for (const EditorSceneContext::HierarchyRow& row : rows) {
        RECT rowRect{ content.left, y, content.right, y + kHierarchyRowHeight };
        if (rowRect.top >= content.bottom) {
            break;
        }

        if (row.entity == selected) {
            GdiDrawing::FillRectColor(dc, rowRect, GdiDrawing::ToColorRef(theme.tabActive));
        }

        std::string label(static_cast<std::size_t>(row.depth) * 2U, ' ');
        label += sceneContext.Scene().Name(row.entity);
        RECT textRect{ rowRect.left + 8, rowRect.top + 3, rowRect.right - 8, rowRect.bottom };
        GdiDrawing::DrawTextBlock(dc, textRect, label.c_str(), GdiDrawing::ToColorRef(row.entity == selected ? theme.textPrimary : theme.textSecondary));
        y += kHierarchyRowHeight;
    }
}

} // namespace kb::editor

#endif
