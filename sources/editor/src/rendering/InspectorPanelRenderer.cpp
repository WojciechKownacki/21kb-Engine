#include "rendering/InspectorPanelRenderer.hpp"

#if defined(_WIN32)
#include "inspection/InspectorPanelTextBuilder.hpp"
#include "rendering/GdiDrawing.hpp"

namespace kb::editor {

void InspectorPanelRenderer::Paint(HDC dc, const RECT& content, const EditorTheme& theme, const EditorSceneContext& sceneContext) const {
    const auto text = InspectorPanelTextBuilder{}.Build(sceneContext);
    if (!text.has_value()) {
        GdiDrawing::DrawTextBlock(dc, content, "No entity selected", GdiDrawing::ToColorRef(theme.textDisabled));
        return;
    }

    GdiDrawing::DrawTextBlock(dc, content, text->c_str(), GdiDrawing::ToColorRef(theme.textSecondary));
}

} // namespace kb::editor

#endif
