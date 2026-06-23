#pragma once

#include "kb/editor/theme/EditorTheme.hpp"
#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <array>
#include <cstddef>
#include <optional>

namespace kb::editor {

enum class MaterialEditorPanelCommand {
    None,
    Save,
    Revert,
    Validate,
};

struct MaterialEditorPanelLayout {
#if defined(_WIN32)
    RECT saveButton{};
    RECT revertButton{};
    RECT validateButton{};
    RECT previewFrame{};
    int parameterSectionTop = 0;
    int mvpParameterBottom = 0;
    int textureSectionTop = 0;
    int textureSlotBottom = 0;
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
inline constexpr int MvpParameterRowCount = 6;
inline constexpr int TextureSlotRowCount = 5;
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
    [[nodiscard]] static MaterialEditorPanelCommand CommandAt(const RECT& content, int x, int y) noexcept;
    [[nodiscard]] static std::optional<EditorMaterialTextureSlot> TextureSlotAt(const RECT& content, int x, int y) noexcept;
#endif
};

#if defined(_WIN32)
inline bool MaterialEditorPanelPointInRect(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

inline MaterialEditorPanelLayout MaterialEditorPanelRenderer::ResolveLayout(const RECT& content) noexcept {
    MaterialEditorPanelLayout layout{};
    const int buttonTop = content.top + 8;
    const int buttonBottom = buttonTop + 26;
    const int buttonGap = 6;
    layout.validateButton = RECT{ content.right - MaterialEditorPanelMetrics::Padding - 72, buttonTop, content.right - MaterialEditorPanelMetrics::Padding, buttonBottom };
    layout.revertButton = RECT{ layout.validateButton.left - buttonGap - 62, buttonTop, layout.validateButton.left - buttonGap, buttonBottom };
    layout.saveButton = RECT{ layout.revertButton.left - buttonGap - 54, buttonTop, layout.revertButton.left - buttonGap, buttonBottom };

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
    y = layout.mvpParameterBottom + 6;
    layout.textureSectionTop = y;
    y += MaterialEditorPanelMetrics::SectionHeight + 4;
    layout.textureSlotBottom = y + (MaterialEditorPanelMetrics::TextureSlotRowCount * MaterialEditorPanelMetrics::RowHeight);
    return layout;
}

inline MaterialEditorPanelCommand MaterialEditorPanelRenderer::CommandAt(const RECT& content, int x, int y) noexcept {
    const MaterialEditorPanelLayout layout = ResolveLayout(content);
    if (MaterialEditorPanelPointInRect(layout.saveButton, x, y)) {
        return MaterialEditorPanelCommand::Save;
    }
    if (MaterialEditorPanelPointInRect(layout.revertButton, x, y)) {
        return MaterialEditorPanelCommand::Revert;
    }
    if (MaterialEditorPanelPointInRect(layout.validateButton, x, y)) {
        return MaterialEditorPanelCommand::Validate;
    }
    return MaterialEditorPanelCommand::None;
}

inline std::optional<EditorMaterialTextureSlot> MaterialEditorPanelRenderer::TextureSlotAt(const RECT& content, int x, int y) noexcept {
    const MaterialEditorPanelLayout layout = ResolveLayout(content);
    const int firstRowTop = layout.textureSectionTop + MaterialEditorPanelMetrics::SectionHeight + 4;
    const std::array<EditorMaterialTextureSlot, MaterialEditorPanelMetrics::TextureSlotRowCount> slots{
        EditorMaterialTextureSlot::Albedo,
        EditorMaterialTextureSlot::Normal,
        EditorMaterialTextureSlot::MetallicRoughness,
        EditorMaterialTextureSlot::Emissive,
        EditorMaterialTextureSlot::Occlusion,
    };
    for (std::size_t index = 0; index < slots.size(); ++index) {
        const int rowTop = firstRowTop + (static_cast<int>(index) * MaterialEditorPanelMetrics::RowHeight);
        const RECT row{
            content.left + MaterialEditorPanelMetrics::Padding,
            rowTop,
            content.right - MaterialEditorPanelMetrics::Padding,
            rowTop + MaterialEditorPanelMetrics::RowHeight,
        };
        if (MaterialEditorPanelPointInRect(row, x, y)) {
            return slots[index];
        }
    }
    return std::nullopt;
}
#endif

} // namespace kb::editor
