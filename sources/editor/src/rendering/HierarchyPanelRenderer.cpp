#include "rendering/HierarchyPanelRenderer.hpp"

#if defined(_WIN32)
#include "rendering/EditorSurfacePainter.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/HierarchyPanelStyle.hpp"
#include "rendering/HierarchyPanelToolbarRenderer.hpp"
#include "rendering/HierarchyRowRenderer.hpp"
#include "scene/EditorHierarchyMetrics.hpp"

namespace kb::editor {

void HierarchyPanelRenderer::Paint(HDC dc, const RECT& content, const EditorTheme& theme, const EditorSceneContext& sceneContext) const {
    const std::vector<EditorHierarchyRow> rows = sceneContext.HierarchyRows();
    const kb::scene::SceneEntity selected = sceneContext.SelectedEntity();

    GdiDrawing::FillRectColor(dc, content, HierarchyPanelStyle::PanelBackground());
    const RECT listContent = HierarchyPanelToolbarRenderer{}.Paint(dc, content, theme, sceneContext);

    int y = listContent.top;
    for (const EditorHierarchyRow& row : rows) {
        RECT rowRect{ listContent.left, y, listContent.right, y + kHierarchyRowHeight };
        if (rowRect.top >= listContent.bottom) {
            break;
        }

        HierarchyRowRenderer{}.Paint(dc, rowRect, theme, row, row.entity == selected);
        y += kHierarchyRowHeight;
    }
}

} // namespace kb::editor

#endif
