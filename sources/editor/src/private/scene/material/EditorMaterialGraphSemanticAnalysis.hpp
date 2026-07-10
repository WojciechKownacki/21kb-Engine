#pragma once

#include "kb/render/resources/RenderMaterialGraphDocument.hpp"

#include <cstdint>
#include <unordered_set>
#include <vector>

namespace kb::editor {

[[nodiscard]] inline bool EditorMaterialGraphNodeFeedsSurfaceNormal(
    const kb::render::RenderMaterialGraphDocument& graph,
    std::uint32_t sourceNodeId) noexcept {
    std::vector<std::uint32_t> pending{ sourceNodeId };
    std::unordered_set<std::uint32_t> visited;
    while (!pending.empty()) {
        const std::uint32_t nodeId = pending.back();
        pending.pop_back();
        if (!visited.insert(nodeId).second) {
            continue;
        }
        for (const kb::render::RenderMaterialGraphLink& link : graph.links) {
            if (link.fromNodeId != nodeId) {
                continue;
            }
            const kb::render::RenderMaterialGraphNode* destination =
                kb::render::FindRenderMaterialGraphNode(graph, link.toNodeId);
            if (destination == nullptr) {
                continue;
            }
            if (destination->kind == kb::render::RenderMaterialGraphNodeKind::MaterialOutput) {
                if (link.toPin == "normal") {
                    return true;
                }
                continue;
            }
            pending.push_back(destination->id);
        }
    }
    return false;
}

} // namespace kb::editor
