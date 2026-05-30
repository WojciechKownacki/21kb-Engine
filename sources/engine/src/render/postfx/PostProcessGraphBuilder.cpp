#include "render/postfx/PostProcessGraphBuilder.hpp"

#include "render/postfx/PostProcessEdgeNormalizer.hpp"
#include "render/postfx/PostProcessResourceDependencyIndexer.hpp"
#include "render/postfx/PostProcessResourceEdgeBuilder.hpp"
#include "render/postfx/PostProcessTopologicalSorter.hpp"

namespace kb::render::postfx {

PostProcessDependencyGraph PostProcessGraphBuilder::Build(const std::vector<PostProcessCompiledPass>& passes) const {
    PostProcessDependencyGraph graph{};
    graph.edges.assign(passes.size(), {});

    const PostProcessResourceDependencyIndex index = PostProcessResourceDependencyIndexer{}.Build(passes);
    PostProcessResourceEdgeBuilder{}.AddEdges(index, graph);
    PostProcessEdgeNormalizer{}.Normalize(graph);
    graph.executionOrder = PostProcessTopologicalSorter{}.Sort(graph);

    return graph;
}

} // namespace kb::render::postfx
