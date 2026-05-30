#pragma once

#include "render/postfx/PostProcessDependencyGraph.hpp"

namespace kb::render::postfx {

class PostProcessEdgeNormalizer {
public:
    void Normalize(PostProcessDependencyGraph& graph) const;
};

} // namespace kb::render::postfx
