#pragma once

#include "kb/editor/theme/EditorTheme.hpp"
#include "kb/render/resources/RenderMaterialGraphDocument.hpp"
#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
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
    RECT assetBadge{};
    RECT graphCanvas{};
#endif
};

#if defined(_WIN32)
namespace MaterialEditorPanelMetrics {
inline constexpr int HeaderHeight = 42;
inline constexpr int Padding = 10;
inline constexpr int RowHeight = 18;
inline constexpr int PreviewWidth = 154;
inline constexpr int PreviewHeight = 104;
inline constexpr int GraphNodeWidth = 324;
inline constexpr int GraphNodeHeight = 348;
inline constexpr int TextureSlotRowCount = 5;
} // namespace MaterialEditorPanelMetrics
#endif

/// Dedicated Material Editor panel. The selected material document is presented as
/// a full-tab graph workspace; compact overlays provide preview and identity context
/// without turning the tab back into an inspector-style property form.
class MaterialEditorPanelRenderer {
public:
#if defined(_WIN32)
    void Paint(HDC dc, const RECT& content, const EditorTheme& theme, const EditorSceneContext& sceneContext) const;

    [[nodiscard]] static MaterialEditorPanelLayout ResolveLayout(const RECT& content) noexcept;

    /// Live material preview target rect (sphere) inside this panel's content area, or
    /// std::nullopt when no material document is selected. Consumed by the editor frame
    /// loop to present the bgfx preview into this panel (mirrors the Inspector preview path).
    [[nodiscard]] static std::optional<RECT> MaterialPreviewRect(const RECT& content, const EditorSceneContext& sceneContext) noexcept;
    [[nodiscard]] static MaterialEditorPanelCommand CommandAt(const RECT& content, int x, int y) noexcept;
    [[nodiscard]] static std::optional<EditorMaterialTextureSlot> TextureSlotAt(const RECT& content, int x, int y) noexcept;
    [[nodiscard]] static std::optional<std::uint32_t> GraphNodeAt(const RECT& content, const kb::render::RenderMaterialGraphDocument& graph, int x, int y) noexcept;
    [[nodiscard]] static std::optional<std::uint32_t> GraphNodeAt(const RECT& content, const kb::render::RenderMaterialGraphDocument& graph, const EditorSceneContext& sceneContext, kb::assets::AssetId assetId, int x, int y) noexcept;
    [[nodiscard]] static std::optional<RECT> GraphNodeRect(const RECT& content, const kb::render::RenderMaterialGraphDocument& graph, std::uint32_t nodeId) noexcept;
    [[nodiscard]] static std::optional<RECT> GraphNodeRect(const RECT& content, const kb::render::RenderMaterialGraphDocument& graph, std::uint32_t nodeId, const EditorSceneContext& sceneContext, kb::assets::AssetId assetId) noexcept;
#endif
};

#if defined(_WIN32)
inline bool MaterialEditorPanelPointInRect(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

inline int MaterialEditorPanelRectWidth(const RECT& rect) noexcept {
    return std::max(0, static_cast<int>(rect.right - rect.left));
}

inline int MaterialEditorPanelRectHeight(const RECT& rect) noexcept {
    return std::max(0, static_cast<int>(rect.bottom - rect.top));
}

inline int MaterialEditorPanelScaled(int value, float zoom) noexcept {
    return std::max(1, static_cast<int>(std::lround(static_cast<float>(value) * zoom)));
}

inline MaterialEditorPanelLayout MaterialEditorPanelRenderer::ResolveLayout(const RECT& content) noexcept {
    MaterialEditorPanelLayout layout{};
    const int buttonTop = content.top + 8;
    const int buttonBottom = buttonTop + 26;
    const int buttonGap = 6;
    layout.validateButton = RECT{ content.right - MaterialEditorPanelMetrics::Padding - 72, buttonTop, content.right - MaterialEditorPanelMetrics::Padding, buttonBottom };
    layout.revertButton = RECT{ layout.validateButton.left - buttonGap - 62, buttonTop, layout.validateButton.left - buttonGap, buttonBottom };
    layout.saveButton = RECT{ layout.revertButton.left - buttonGap - 54, buttonTop, layout.revertButton.left - buttonGap, buttonBottom };

    layout.graphCanvas = RECT{
        content.left,
        content.top + MaterialEditorPanelMetrics::HeaderHeight,
        content.right,
        content.bottom,
    };
    const int overlayLeft = layout.graphCanvas.left + MaterialEditorPanelMetrics::Padding;
    const int overlayTop = layout.graphCanvas.top + MaterialEditorPanelMetrics::Padding;
    layout.previewFrame = RECT{
        overlayLeft,
        overlayTop,
        overlayLeft + MaterialEditorPanelMetrics::PreviewWidth,
        overlayTop + MaterialEditorPanelMetrics::PreviewHeight,
    };
    layout.assetBadge = RECT{
        overlayLeft,
        layout.previewFrame.bottom + 8,
        std::min(static_cast<int>(layout.graphCanvas.right - MaterialEditorPanelMetrics::Padding), overlayLeft + 280),
        layout.previewFrame.bottom + 58,
    };
    return layout;
}

inline MaterialEditorPanelCommand MaterialEditorPanelRenderer::CommandAt(const RECT& content, int x, int y) noexcept {
    const MaterialEditorPanelLayout layout = MaterialEditorPanelRenderer::ResolveLayout(content);
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
    static_cast<void>(content);
    static_cast<void>(x);
    static_cast<void>(y);
    return std::nullopt;
}

inline std::optional<RECT> MaterialEditorPanelGraphNodeRectWithView(
    const RECT& content,
    const kb::render::RenderMaterialGraphDocument& graph,
    std::uint32_t nodeId,
    float zoom,
    int panX,
    int panY,
    int nodeOffsetX,
    int nodeOffsetY) noexcept {
    const kb::render::RenderMaterialGraphNode* target = kb::render::FindRenderMaterialGraphNode(graph, nodeId);
    if (target == nullptr) {
        return std::nullopt;
    }
    const MaterialEditorPanelLayout layout = MaterialEditorPanelRenderer::ResolveLayout(content);
    const RECT canvas = layout.graphCanvas;
    const float clampedZoom = std::clamp(zoom, 0.25F, 2.0F);
    const int nodeWidth = MaterialEditorPanelScaled(MaterialEditorPanelMetrics::GraphNodeWidth, clampedZoom);
    const int nodeHeight = MaterialEditorPanelScaled(MaterialEditorPanelMetrics::GraphNodeHeight, clampedZoom);

    const int x = canvas.left + panX + MaterialEditorPanelScaled(target->positionX + nodeOffsetX, clampedZoom);
    const int y = canvas.top + panY + MaterialEditorPanelScaled(target->positionY + nodeOffsetY, clampedZoom);
    return RECT{ x, y, x + nodeWidth, y + nodeHeight };
}

inline std::optional<RECT> MaterialEditorPanelRenderer::GraphNodeRect(const RECT& content, const kb::render::RenderMaterialGraphDocument& graph, std::uint32_t nodeId) noexcept {
    return MaterialEditorPanelGraphNodeRectWithView(content, graph, nodeId, 1.0F, 0, 0, 0, 0);
}

inline std::optional<RECT> MaterialEditorPanelRenderer::GraphNodeRect(
    const RECT& content,
    const kb::render::RenderMaterialGraphDocument& graph,
    std::uint32_t nodeId,
    const EditorSceneContext& sceneContext,
    kb::assets::AssetId assetId) noexcept {
    return MaterialEditorPanelGraphNodeRectWithView(
        content,
        graph,
        nodeId,
        sceneContext.MaterialGraphZoom(),
        sceneContext.MaterialGraphPanX(),
        sceneContext.MaterialGraphPanY(),
        sceneContext.MaterialGraphNodeOffsetX(assetId, nodeId),
        sceneContext.MaterialGraphNodeOffsetY(assetId, nodeId));
}

inline std::optional<std::uint32_t> MaterialEditorPanelRenderer::GraphNodeAt(const RECT& content, const kb::render::RenderMaterialGraphDocument& graph, int x, int y) noexcept {
    for (std::size_t index = graph.nodes.size(); index-- > 0U;) {
        const kb::render::RenderMaterialGraphNode& node = graph.nodes[index];
        const std::optional<RECT> rect = GraphNodeRect(content, graph, node.id);
        if (rect.has_value() && MaterialEditorPanelPointInRect(*rect, x, y)) {
            return node.id;
        }
    }
    return std::nullopt;
}

inline std::optional<std::uint32_t> MaterialEditorPanelRenderer::GraphNodeAt(
    const RECT& content,
    const kb::render::RenderMaterialGraphDocument& graph,
    const EditorSceneContext& sceneContext,
    kb::assets::AssetId assetId,
    int x,
    int y) noexcept {
    for (std::size_t index = graph.nodes.size(); index-- > 0U;) {
        const kb::render::RenderMaterialGraphNode& node = graph.nodes[index];
        const std::optional<RECT> rect = GraphNodeRect(content, graph, node.id, sceneContext, assetId);
        if (rect.has_value() && MaterialEditorPanelPointInRect(*rect, x, y)) {
            return node.id;
        }
    }
    return std::nullopt;
}
#endif

} // namespace kb::editor
