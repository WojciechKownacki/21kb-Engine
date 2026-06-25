#pragma once

#include "kb/editor/theme/EditorTheme.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialGraphDocument.hpp"
#include "kb/render/resources/RenderMaterialTypeSchema.hpp"
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
#include <sstream>
#include <string>
#include <optional>
#include <vector>

namespace kb::editor {

enum class MaterialEditorPanelCommand {
    None,
    Info,
    ApplyToSelection,
    Save,
    Revert,
    Validate,
};

struct MaterialEditorPanelDiagnosticRows {
    std::vector<std::string> rows;
    bool hasError = false;
};

struct MaterialEditorPanelDetailsRows {
    std::string title;
    std::vector<std::string> parameterRows;
    std::vector<std::string> textureSlotRows;
};

struct MaterialEditorPanelLayout {
#if defined(_WIN32)
    RECT applyButton{};
    RECT infoButton{};
    RECT saveButton{};
    RECT revertButton{};
    RECT validateButton{};
    RECT previewFrame{};
    RECT assetBadge{};
    RECT diagnosticsPanel{};
    RECT detailsPanel{};
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
inline constexpr int GraphNodeHeaderHeight = 46;
inline constexpr int GraphNodeBodyTopPadding = 18;
inline constexpr int GraphNodePinRowHeight = 38;
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
    [[nodiscard]] static MaterialEditorPanelDiagnosticRows DiagnosticRows(const kb::render::RenderMaterialAssetParseResult& result);
    [[nodiscard]] static MaterialEditorPanelDetailsRows DetailsRows(const kb::render::RenderMaterialTypeSchema& schema, std::uint32_t selectedNodeId);
    [[nodiscard]] static std::optional<EditorMaterialTextureSlot> TextureSlotAt(const RECT& content, int x, int y) noexcept;
    [[nodiscard]] static std::optional<EditorMaterialTextureSlot> TextureSlotAt(
        const RECT& content,
        const kb::render::RenderMaterialGraphDocument& graph,
        const EditorSceneContext& sceneContext,
        kb::assets::AssetId assetId,
        int x,
        int y) noexcept;
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
    layout.applyButton = RECT{ layout.saveButton.left - buttonGap - 118, buttonTop, layout.saveButton.left - buttonGap, buttonBottom };
    layout.infoButton = RECT{ layout.applyButton.left - buttonGap - 54, buttonTop, layout.applyButton.left - buttonGap, buttonBottom };

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
    layout.diagnosticsPanel = RECT{
        std::max(layout.assetBadge.right + MaterialEditorPanelMetrics::Padding, layout.graphCanvas.right - 372),
        overlayTop,
        layout.graphCanvas.right - MaterialEditorPanelMetrics::Padding,
        std::min(static_cast<int>(layout.graphCanvas.bottom - MaterialEditorPanelMetrics::Padding), overlayTop + 132),
    };
    layout.detailsPanel = RECT{
        layout.diagnosticsPanel.left,
        layout.diagnosticsPanel.bottom + MaterialEditorPanelMetrics::Padding,
        layout.diagnosticsPanel.right,
        layout.graphCanvas.bottom - MaterialEditorPanelMetrics::Padding,
    };
    return layout;
}

inline MaterialEditorPanelCommand MaterialEditorPanelRenderer::CommandAt(const RECT& content, int x, int y) noexcept {
    const MaterialEditorPanelLayout layout = MaterialEditorPanelRenderer::ResolveLayout(content);
    if (MaterialEditorPanelPointInRect(layout.infoButton, x, y)) {
        return MaterialEditorPanelCommand::Info;
    }
    if (MaterialEditorPanelPointInRect(layout.applyButton, x, y)) {
        return MaterialEditorPanelCommand::ApplyToSelection;
    }
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

inline MaterialEditorPanelDiagnosticRows MaterialEditorPanelRenderer::DiagnosticRows(const kb::render::RenderMaterialAssetParseResult& result) {
    MaterialEditorPanelDiagnosticRows rows;
    for (const kb::render::RenderMaterialAssetParseDiagnostic& diagnostic : result.diagnostics) {
        rows.hasError = rows.hasError || diagnostic.severity == kb::render::RenderMaterialAssetParseDiagnosticSeverity::Error;
        std::ostringstream line;
        line << (diagnostic.severity == kb::render::RenderMaterialAssetParseDiagnosticSeverity::Error ? "Error" : "Warning")
             << " " << kb::render::RenderMaterialAssetParseDiagnosticCodeName(diagnostic.code);
        if (diagnostic.line > 0U) {
            line << " line " << diagnostic.line;
        }
        if (!diagnostic.field.empty()) {
            line << " " << diagnostic.field;
        }
        line << ": " << diagnostic.message;
        if (!diagnostic.text.empty()) {
            line << " [" << diagnostic.text << "]";
        }
        rows.rows.push_back(line.str());
    }
    return rows;
}

inline std::string MaterialEditorPanelParameterTypeName(kb::render::RenderMaterialParameterType type) {
    switch (type) {
    case kb::render::RenderMaterialParameterType::Scalar:
        return "Scalar";
    case kb::render::RenderMaterialParameterType::Vec3:
        return "Vec3";
    case kb::render::RenderMaterialParameterType::Vec4:
        return "Vec4";
    case kb::render::RenderMaterialParameterType::Color:
        return "Color";
    case kb::render::RenderMaterialParameterType::Enum:
        return "Enum";
    case kb::render::RenderMaterialParameterType::Bool:
        return "Bool";
    case kb::render::RenderMaterialParameterType::Texture:
        return "Texture";
    }
    return "Value";
}

inline std::string MaterialEditorPanelColorSpaceName(kb::render::RenderMaterialTextureColorSpace colorSpace) {
    switch (colorSpace) {
    case kb::render::RenderMaterialTextureColorSpace::Srgb:
        return "sRGB";
    case kb::render::RenderMaterialTextureColorSpace::Linear:
        return "Linear";
    case kb::render::RenderMaterialTextureColorSpace::Unknown:
        return "Any";
    }
    return "Any";
}

inline MaterialEditorPanelDetailsRows MaterialEditorPanelRenderer::DetailsRows(const kb::render::RenderMaterialTypeSchema& schema, std::uint32_t selectedNodeId) {
    MaterialEditorPanelDetailsRows rows;
    rows.title = selectedNodeId == 0U ? "PBR Schema" : "Selected Node #" + std::to_string(selectedNodeId);
    for (const kb::render::RenderMaterialParameterSchema& parameter : schema.parameters) {
        if (parameter.runtimeSupport != kb::render::RenderMaterialFeatureSupport::Supported || parameter.group == kb::render::RenderMaterialParameterGroup::Advanced) {
            continue;
        }
        std::string row{ parameter.name };
        row += "  ";
        row += MaterialEditorPanelParameterTypeName(parameter.type);
        if (parameter.range.has_value()) {
            row += " ";
            row += std::to_string(parameter.range->min).substr(0, 4);
            row += "..";
            row += std::to_string(parameter.range->max).substr(0, 4);
        } else if (!parameter.defaultValueHint.empty()) {
            row += " default ";
            row += parameter.defaultValueHint;
        }
        rows.parameterRows.push_back(std::move(row));
    }
    for (const kb::render::RenderMaterialTextureSlotSchema& slot : schema.textureSlots) {
        if (slot.runtimeSupport != kb::render::RenderMaterialFeatureSupport::Supported) {
            continue;
        }
        std::string row{ slot.name };
        row += "  ";
        row += MaterialEditorPanelColorSpaceName(slot.expectedColorSpace);
        row += "  ";
        row += slot.assetIdFieldName;
        rows.textureSlotRows.push_back(std::move(row));
    }
    return rows;
}

inline std::optional<EditorMaterialTextureSlot> MaterialEditorPanelTextureSlotForOutputRow(std::size_t row) noexcept {
    switch (row) {
    case 0U:
        return EditorMaterialTextureSlot::Albedo;
    case 1U:
        return EditorMaterialTextureSlot::Normal;
    case 2U:
    case 3U:
        return EditorMaterialTextureSlot::MetallicRoughness;
    case 4U:
        return EditorMaterialTextureSlot::Emissive;
    case 5U:
        return EditorMaterialTextureSlot::Occlusion;
    default:
        return std::nullopt;
    }
}

inline std::optional<EditorMaterialTextureSlot> MaterialEditorPanelTextureSlotAtOutputNode(const RECT& node, int x, int y) noexcept {
    if (!MaterialEditorPanelPointInRect(node, x, y)) {
        return std::nullopt;
    }

    const float scale = static_cast<float>(MaterialEditorPanelRectWidth(node)) / static_cast<float>(std::max(1, MaterialEditorPanelMetrics::GraphNodeWidth));
    const int rowTop =
        node.top
        + MaterialEditorPanelScaled(MaterialEditorPanelMetrics::GraphNodeHeaderHeight, scale)
        + MaterialEditorPanelScaled(MaterialEditorPanelMetrics::GraphNodeBodyTopPadding, scale);
    const int rowHeight = MaterialEditorPanelScaled(MaterialEditorPanelMetrics::GraphNodePinRowHeight, scale);
    const int relativeY = y - rowTop;
    if (relativeY < 0) {
        return std::nullopt;
    }

    const std::size_t row = static_cast<std::size_t>(relativeY / std::max(1, rowHeight));
    const RECT rowRect{
        node.left,
        rowTop + static_cast<int>(row) * rowHeight,
        node.right,
        rowTop + static_cast<int>(row + 1U) * rowHeight,
    };
    if (!MaterialEditorPanelPointInRect(rowRect, x, y)) {
        return std::nullopt;
    }
    return MaterialEditorPanelTextureSlotForOutputRow(row);
}

inline std::optional<EditorMaterialTextureSlot> MaterialEditorPanelRenderer::TextureSlotAt(const RECT& content, int x, int y) noexcept {
    const kb::render::RenderMaterialGraphDocument graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    for (std::size_t index = graph.nodes.size(); index-- > 0U;) {
        const kb::render::RenderMaterialGraphNode& node = graph.nodes[index];
        if (node.kind != kb::render::RenderMaterialGraphNodeKind::MaterialOutput) {
            continue;
        }
        const std::optional<RECT> rect = GraphNodeRect(content, graph, node.id);
        if (!rect.has_value()) {
            continue;
        }
        if (const std::optional<EditorMaterialTextureSlot> slot = MaterialEditorPanelTextureSlotAtOutputNode(*rect, x, y)) {
            return slot;
        }
    }
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

inline std::optional<EditorMaterialTextureSlot> MaterialEditorPanelRenderer::TextureSlotAt(
    const RECT& content,
    const kb::render::RenderMaterialGraphDocument& graph,
    const EditorSceneContext& sceneContext,
    kb::assets::AssetId assetId,
    int x,
    int y) noexcept {
    const kb::render::RenderMaterialGraphDocument defaultGraph = graph.nodes.empty()
        ? kb::render::MakeDefaultRenderMaterialGraphDocument()
        : kb::render::RenderMaterialGraphDocument{};
    const kb::render::RenderMaterialGraphDocument& graphView = graph.nodes.empty() ? defaultGraph : graph;
    for (std::size_t index = graphView.nodes.size(); index-- > 0U;) {
        const kb::render::RenderMaterialGraphNode& node = graphView.nodes[index];
        if (node.kind != kb::render::RenderMaterialGraphNodeKind::MaterialOutput) {
            continue;
        }
        const std::optional<RECT> rect = GraphNodeRect(content, graphView, node.id, sceneContext, assetId);
        if (!rect.has_value()) {
            continue;
        }
        if (const std::optional<EditorMaterialTextureSlot> slot = MaterialEditorPanelTextureSlotAtOutputNode(*rect, x, y)) {
            return slot;
        }
    }
    return std::nullopt;
}
#endif

} // namespace kb::editor
