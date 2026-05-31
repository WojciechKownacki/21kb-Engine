#include "rendering/EditorDragOverlayRenderer.hpp"

#if defined(_WIN32)
#include "engine/scene/SceneEntities.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/HeroIconPainter.hpp"
#include "rendering/HierarchyPanelStyle.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"

#include <string>

namespace kb::editor {
namespace {

[[nodiscard]] std::string Label(const EditorPointerDragState& drag, const EditorSceneContext& sceneContext) {
    switch (drag.kind) {
    case EditorPointerDragKind::HierarchyEntity:
        return sceneContext.Scene().Entities().IsAlive(drag.entity) ? sceneContext.Scene().Entities().Name(drag.entity) : std::string{};
    case EditorPointerDragKind::PrefabAsset:
        return drag.assetPath.filename().string();
    case EditorPointerDragKind::None:
    default:
        return {};
    }
}

} // namespace

void EditorDragOverlayRenderer::Paint(HDC dc, const EditorPointerDragState& drag, const EditorTheme& theme, const EditorSceneContext& sceneContext) const {
    if (!drag.Active()) {
        return;
    }

    const std::string label = Label(drag, sceneContext);
    if (label.empty()) {
        return;
    }

    RECT row{
        drag.x + 8,
        drag.y - 10,
        drag.x + 260,
        drag.y + 14,
    };
    GdiDrawing::FillRectAlpha(dc, row, GdiDrawing::ToColorRef(theme.tabActive), 188);

    RECT icon{
        row.left + 5,
        row.top + 4,
        row.left + 21,
        row.top + 20,
    };
    HeroIconPainter::Draw(dc, icon, HeroIconKind::Cube, HierarchyPanelStyle::CubeStroke(), 2);

    ScopedFont font{ 12, FW_NORMAL };
    const ScopedGdiObject selectedFont(dc, font.handle);
    RECT text{
        row.left + 27,
        row.top,
        row.right,
        row.bottom,
    };
    GdiDrawing::DrawTabText(dc, text, label.c_str(), GdiDrawing::ToColorRef(theme.textPrimary));
}

} // namespace kb::editor

#endif
