#pragma once

#include "render/postfx/PostProcessCompiledPass.hpp"
#include "render/postfx/PostProcessDependencyGraph.hpp"

#include <vector>

namespace kb::render::postfx {

class PostProcessGraphBuilder {
public:
    [[nodiscard]] PostProcessDependencyGraph Build(const std::vector<PostProcessCompiledPass>& passes) const;
};

} // namespace kb::render::postfx
