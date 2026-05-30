#pragma once

#include "render/postfx/PostProcessCompiledPass.hpp"
#include "render/postfx/PostProcessDependencyGraph.hpp"

#include <vector>

namespace kb::render::postfx {

class PostProcessGraphBuilder {
public:
    [[nodiscard]] PostProcessDependencyGraph Build(const std::vector<PostProcessCompiledPass>& passes) const;

private:
    static void AddResourceDependencies(const std::vector<PostProcessCompiledPass>& passes, PostProcessDependencyGraph& graph);
    static void SortAndDeduplicateEdges(PostProcessDependencyGraph& graph);
    static void ResolveExecutionOrder(PostProcessDependencyGraph& graph);
};

} // namespace kb::render::postfx
