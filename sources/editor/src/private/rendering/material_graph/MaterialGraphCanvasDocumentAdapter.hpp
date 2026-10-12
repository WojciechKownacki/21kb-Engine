#pragma once

#include "rendering/material_graph/MaterialGraphCanvas.hpp"

#include "kb/render/resources/RenderMaterialGraphDocument.hpp"

#include <cstddef>

namespace kb::editor {

struct MaterialGraphCanvasDocumentBuildResult final {
    MaterialGraphCanvas canvas;
    std::size_t skippedLinks{ 0U };
};

[[nodiscard]] MaterialGraphCanvasPinType MaterialGraphCanvasPinTypeFromRenderType(
    kb::render::RenderMaterialGraphPinType type) noexcept;

[[nodiscard]] MaterialGraphCanvasNode BuildMaterialGraphCanvasNode(
    const kb::render::RenderMaterialGraphNode& node);

// The height a node's pins need, as in Unreal where a node grows with its pin rows: the header, the body
// padding above and below, and one full pin row per pin on its taller side. Zero for the nodes whose pins sit
// beside value fields or a texture preview, which size themselves around those.
[[nodiscard]] float MaterialGraphCanvasPinRowsHeight(const MaterialGraphCanvasNode& node) noexcept;

[[nodiscard]] MaterialGraphCanvasDocumentBuildResult BuildMaterialGraphCanvasFromDocument(
    const kb::render::RenderMaterialGraphDocument& document);

} // namespace kb::editor
