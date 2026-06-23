#pragma once

#include "kb/editor/theme/EditorTheme.hpp"
#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <optional>

namespace kb::editor {

struct MaterialEditorPanelLayout {
#if defined(_WIN32)
    RECT previewFrame{};
    int parameterSectionTop = 0;
    int mvpParameterBottom = 0;
#endif
};

#if defined(_WIN32)
namespace MaterialEditorPanelMetrics {
inline constexpr int HeaderHeight = 42;
inline constexpr int Padding = 10;
inline constexpr int RowHeight = 18;
inline constexpr int SectionHeight = 26;
inline constexpr int TitleHeight = 24;
inline constexpr int PreviewHeight = 112;
inline constexpr int PreviewPadding = 10;
inline constexpr int PreviewGap = 6;
inline constexpr int MvpParameterRowCount = 10;
} // namespace MaterialEditorPanelMetrics
#endif

/// Dedicated Material Editor panel. Renders the live state of the Material Instance
/// asset currently selected for inspection (`EditorSceneContext::AssetBrowser().InspectorAsset()`).
///
/// This is a presentational (read-only) surface through KBMAT-0203. The top frame
/// is a live bgfx material sphere preview; interactive editing, drag/drop and undo arrive later. It is intentionally a
/// standalone component (not an extraction of the Inspector's material section, which is
/// coupled to Inspector-only interaction types).
class MaterialEditorPanelRenderer {
public:
#if defined(_WIN32)
    void Paint(HDC dc, const RECT& content, const EditorTheme& theme, const EditorSceneContext& sceneContext) const;

    [[nodiscard]] static MaterialEditorPanelLayout ResolveLayout(const RECT& content) noexcept;

    /// Live material preview target rect (sphere) inside this panel's content area, or
    /// std::nullopt when no Material Instance is selected. Consumed by the editor frame
    /// loop to present the bgfx preview into this panel (mirrors the Inspector preview path).
    [[nodiscard]] static std::optional<RECT> MaterialPreviewRect(const RECT& content, const EditorSceneContext& sceneContext) noexcept;
#endif
};

#if defined(_WIN32)
inline MaterialEditorPanelLayout MaterialEditorPanelRenderer::ResolveLayout(const RECT& content) noexcept {
    MaterialEditorPanelLayout layout{};
    const int previewY = content.top + MaterialEditorPanelMetrics::HeaderHeight + MaterialEditorPanelMetrics::Padding;
    layout.previewFrame = RECT{
        content.left + MaterialEditorPanelMetrics::PreviewPadding,
        previewY,
        content.right - MaterialEditorPanelMetrics::PreviewPadding,
        previewY + MaterialEditorPanelMetrics::PreviewHeight,
    };
    int y = layout.previewFrame.bottom
        + MaterialEditorPanelMetrics::PreviewGap
        + MaterialEditorPanelMetrics::TitleHeight
        + MaterialEditorPanelMetrics::RowHeight
        + 6;
    layout.parameterSectionTop = y;
    y += MaterialEditorPanelMetrics::SectionHeight + 4;
    layout.mvpParameterBottom = y + (MaterialEditorPanelMetrics::MvpParameterRowCount * MaterialEditorPanelMetrics::RowHeight);
    return layout;
}
#endif

} // namespace kb::editor
