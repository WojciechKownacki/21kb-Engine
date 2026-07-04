#pragma once

#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialGraphDocument.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace kb::editor {

class EditorMaterialNodePreviewBuilder {
public:
    EditorMaterialNodePreviewBuilder() = delete;

    [[nodiscard]] static std::optional<kb::render::RenderMaterialAssetData> Build(
        const kb::render::RenderMaterialAssetData& source,
        std::uint32_t nodeId) {
        if (nodeId == 0U) {
            return std::nullopt;
        }
        const kb::render::RenderMaterialGraphNode* node = kb::render::FindRenderMaterialGraphNode(source.graph, nodeId);
        const kb::render::RenderMaterialGraphNode* outputNode = FirstMaterialOutputNode(source.graph);
        if (node == nullptr || outputNode == nullptr || node->kind == kb::render::RenderMaterialGraphNodeKind::MaterialOutput) {
            return std::nullopt;
        }
        const std::optional<std::string> outputPin = PreviewableOutputPin(*node, *outputNode);
        if (!outputPin.has_value()) {
            return std::nullopt;
        }

        kb::render::RenderMaterialAssetData preview = source;
        std::erase_if(preview.graph.links, [outputNode](const kb::render::RenderMaterialGraphLink& link) {
            return link.toNodeId == outputNode->id && link.toPin == "baseColor";
        });

        kb::render::RenderMaterialGraphLink link{
            .fromNodeId = node->id,
            .fromPinId = kb::render::RenderMaterialGraphStablePinId(*node, *outputPin, true),
            .fromPin = *outputPin,
            .toNodeId = outputNode->id,
            .toPinId = kb::render::RenderMaterialGraphStablePinId(*outputNode, "baseColor", false),
            .toPin = "baseColor",
        };
        link.id = kb::render::MakeRenderMaterialGraphLinkId(link);
        preview.graph.links.push_back(std::move(link));
        return preview;
    }

private:
    [[nodiscard]] static const kb::render::RenderMaterialGraphNode* FirstMaterialOutputNode(
        const kb::render::RenderMaterialGraphDocument& graph) noexcept {
        for (const kb::render::RenderMaterialGraphNode& node : graph.nodes) {
            if (node.kind == kb::render::RenderMaterialGraphNodeKind::MaterialOutput) {
                return &node;
            }
        }
        return nullptr;
    }

    [[nodiscard]] static std::optional<std::string> PreviewableOutputPin(
        const kb::render::RenderMaterialGraphNode& node,
        const kb::render::RenderMaterialGraphNode& outputNode) {
        for (const std::string& pin : kb::render::RenderMaterialGraphNodeOutputPinNames(node)) {
            if (kb::render::AreRenderMaterialGraphPinsCompatible(node, pin, outputNode, "baseColor")) {
                return pin;
            }
        }
        return std::nullopt;
    }
};

} // namespace kb::editor
