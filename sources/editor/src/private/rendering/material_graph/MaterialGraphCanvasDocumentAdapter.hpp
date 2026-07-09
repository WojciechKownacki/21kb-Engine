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

[[nodiscard]] MaterialGraphCanvasDocumentBuildResult BuildMaterialGraphCanvasFromDocument(
    const kb::render::RenderMaterialGraphDocument& document);

} // namespace kb::editor
