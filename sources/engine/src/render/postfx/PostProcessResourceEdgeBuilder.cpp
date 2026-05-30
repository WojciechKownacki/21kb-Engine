#include "render/postfx/PostProcessResourceEdgeBuilder.hpp"

namespace kb::render::postfx {

void PostProcessResourceEdgeBuilder::AddEdges(const PostProcessResourceDependencyIndex& index, PostProcessDependencyGraph& graph) const {
    for (const auto& [resourceId, writers] : index.writers) {
        static_cast<void>(resourceId);
        const auto readersIt = index.readers.find(resourceId);
        const std::vector<std::uint32_t> noReaders;
        const std::vector<std::uint32_t>& readers = readersIt == index.readers.end() ? noReaders : readersIt->second;

        AddWriterReaderEdges(writers, readers, graph);
        AddWriterOrderingEdges(writers, graph);
    }
}

void PostProcessResourceEdgeBuilder::AddWriterReaderEdges(const std::vector<std::uint32_t>& writers, const std::vector<std::uint32_t>& readers, PostProcessDependencyGraph& graph) {
    for (const auto writer : writers) {
        for (const auto reader : readers) {
            if (writer != reader) {
                graph.edges[writer].push_back(reader);
            }
        }
    }
}

void PostProcessResourceEdgeBuilder::AddWriterOrderingEdges(const std::vector<std::uint32_t>& writers, PostProcessDependencyGraph& graph) {
    for (std::size_t i = 0; i < writers.size(); ++i) {
        for (std::size_t j = i + 1; j < writers.size(); ++j) {
            graph.edges[writers[i]].push_back(writers[j]);
        }
    }
}

} // namespace kb::render::postfx
