#include "render/postfx/PostProcessGraphBuilder.hpp"

#include <algorithm>
#include <queue>
#include <stdexcept>
#include <unordered_map>

namespace kb::render::postfx {

PostProcessDependencyGraph PostProcessGraphBuilder::Build(const std::vector<PostProcessCompiledPass>& passes) const {
    PostProcessDependencyGraph graph{};
    graph.edges.assign(passes.size(), {});

    AddResourceDependencies(passes, graph);
    SortAndDeduplicateEdges(graph);
    ResolveExecutionOrder(graph);

    return graph;
}

void PostProcessGraphBuilder::AddResourceDependencies(const std::vector<PostProcessCompiledPass>& passes, PostProcessDependencyGraph& graph) {
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> readers;
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> writers;

    for (std::uint32_t passIndex = 0; passIndex < passes.size(); ++passIndex) {
        for (const auto read : passes[passIndex].reads) {
            readers[read.id].push_back(passIndex);
        }
        for (const auto write : passes[passIndex].writes) {
            writers[write.id].push_back(passIndex);
        }
    }

    for (const auto& [resourceId, writePasses] : writers) {
        auto& readPasses = readers[resourceId];

        for (const auto writer : writePasses) {
            for (const auto reader : readPasses) {
                if (writer != reader) {
                    graph.edges[writer].push_back(reader);
                }
            }
        }

        for (std::size_t i = 0; i < writePasses.size(); ++i) {
            for (std::size_t j = i + 1; j < writePasses.size(); ++j) {
                graph.edges[writePasses[i]].push_back(writePasses[j]);
            }
        }
    }
}

void PostProcessGraphBuilder::SortAndDeduplicateEdges(PostProcessDependencyGraph& graph) {
    for (auto& list : graph.edges) {
        std::sort(list.begin(), list.end());
        list.erase(std::unique(list.begin(), list.end()), list.end());
    }
}

void PostProcessGraphBuilder::ResolveExecutionOrder(PostProcessDependencyGraph& graph) {
    std::vector<std::uint32_t> indegree(graph.edges.size(), 0);

    for (const auto& adjacency : graph.edges) {
        for (const auto target : adjacency) {
            ++indegree[target];
        }
    }

    std::queue<std::uint32_t> ready;
    for (std::uint32_t i = 0; i < indegree.size(); ++i) {
        if (indegree[i] == 0) {
            ready.push(i);
        }
    }

    graph.executionOrder.clear();
    graph.executionOrder.reserve(graph.edges.size());

    while (!ready.empty()) {
        const auto current = ready.front();
        ready.pop();
        graph.executionOrder.push_back(current);

        for (const auto next : graph.edges[current]) {
            if (--indegree[next] == 0) {
                ready.push(next);
            }
        }
    }

    if (graph.executionOrder.size() != graph.edges.size()) {
        throw std::runtime_error("PostProcess dependency graph contains a cycle");
    }
}

} // namespace kb::render::postfx
