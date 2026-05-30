#pragma once

#include "render/postfx/PostProcessDependencyGraph.hpp"
#include "render/postfx/PostProcessResourceDependencyIndexer.hpp"

namespace kb::render::postfx {

class PostProcessResourceEdgeBuilder {
public:
    void AddEdges(const PostProcessResourceDependencyIndex& index, PostProcessDependencyGraph& graph) const;

private:
    static void AddWriterReaderEdges(const std::vector<std::uint32_t>& writers, const std::vector<std::uint32_t>& readers, PostProcessDependencyGraph& graph);
    static void AddWriterOrderingEdges(const std::vector<std::uint32_t>& writers, PostProcessDependencyGraph& graph);
};

} // namespace kb::render::postfx
