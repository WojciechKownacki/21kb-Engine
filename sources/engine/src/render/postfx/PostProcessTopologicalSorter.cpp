#include "render/postfx/PostProcessTopologicalSorter.hpp"

#include <queue>
#include <stdexcept>

namespace kb::render::postfx {

std::vector<std::uint32_t> PostProcessTopologicalSorter::Sort(const PostProcessDependencyGraph& graph) const {
    std::vector<std::uint32_t> indegree = CalculateIndegree(graph);

    std::queue<std::uint32_t> ready;
    for (std::uint32_t i = 0; i < indegree.size(); ++i) {
        if (indegree[i] == 0) {
            ready.push(i);
        }
    }

    std::vector<std::uint32_t> executionOrder;
    executionOrder.reserve(graph.edges.size());

    while (!ready.empty()) {
        const auto current = ready.front();
        ready.pop();
        executionOrder.push_back(current);

        for (const auto next : graph.edges[current]) {
            if (--indegree[next] == 0) {
                ready.push(next);
            }
        }
    }

    if (executionOrder.size() != graph.edges.size()) {
        throw std::runtime_error("PostProcess dependency graph contains a cycle");
    }

    return executionOrder;
}

std::vector<std::uint32_t> PostProcessTopologicalSorter::CalculateIndegree(const PostProcessDependencyGraph& graph) {
    std::vector<std::uint32_t> indegree(graph.edges.size(), 0);

    for (const auto& adjacency : graph.edges) {
        for (const auto target : adjacency) {
            ++indegree[target];
        }
    }

    return indegree;
}

} // namespace kb::render::postfx
