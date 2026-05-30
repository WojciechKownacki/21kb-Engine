#include "render/postfx/PostProcessEdgeNormalizer.hpp"

#include <algorithm>

namespace kb::render::postfx {

void PostProcessEdgeNormalizer::Normalize(PostProcessDependencyGraph& graph) const {
    for (auto& edges : graph.edges) {
        std::sort(edges.begin(), edges.end());
        edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
    }
}

} // namespace kb::render::postfx
