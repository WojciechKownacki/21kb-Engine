#pragma once

#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialGraphDocument.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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
        const kb::render::RenderMaterialGraphNode* outputNode = SingleMaterialOutputNode(source.graph);
        if (node == nullptr || outputNode == nullptr || node->kind == kb::render::RenderMaterialGraphNodeKind::MaterialOutput) {
            return std::nullopt;
        }
        const std::optional<std::string> outputPin = PreviewableOutputPin(*node, *outputNode);
        if (!outputPin.has_value()) {
            return std::nullopt;
        }

        std::vector<std::uint32_t> requiredNodeIds{ nodeId };
        for (std::size_t index = 0U; index < requiredNodeIds.size(); ++index) {
            const std::uint32_t targetId = requiredNodeIds[index];
            for (const kb::render::RenderMaterialGraphLink& candidate : source.graph.links) {
                if (candidate.toNodeId == targetId &&
                    std::ranges::find(requiredNodeIds, candidate.fromNodeId) == requiredNodeIds.end()) {
                    requiredNodeIds.push_back(candidate.fromNodeId);
                }
            }
        }
        requiredNodeIds.push_back(outputNode->id);

        kb::render::RenderMaterialAssetData preview = source;
        preview.graph.shadingModel = "unlit";
        preview.graph.blendMode = "opaque";
        preview.graph.lastGoodArtifact = {};
        std::erase_if(preview.graph.nodes, [&requiredNodeIds](const kb::render::RenderMaterialGraphNode& candidate) {
            return std::ranges::find(requiredNodeIds, candidate.id) == requiredNodeIds.end();
        });
        std::erase_if(preview.graph.links, [&requiredNodeIds, outputNode](const kb::render::RenderMaterialGraphLink& candidate) {
            return candidate.toNodeId == outputNode->id ||
                std::ranges::find(requiredNodeIds, candidate.fromNodeId) == requiredNodeIds.end() ||
                std::ranges::find(requiredNodeIds, candidate.toNodeId) == requiredNodeIds.end();
        });
        preview.graph.comments.clear();
        preview.graph.composites.clear();

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
    [[nodiscard]] static const kb::render::RenderMaterialGraphNode* SingleMaterialOutputNode(
        const kb::render::RenderMaterialGraphDocument& graph) noexcept {
        const kb::render::RenderMaterialGraphNode* output = nullptr;
        for (const kb::render::RenderMaterialGraphNode& node : graph.nodes) {
            if (node.kind == kb::render::RenderMaterialGraphNodeKind::MaterialOutput) {
                if (output != nullptr) {
                    return nullptr;
                }
                output = &node;
            }
        }
        return output;
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
