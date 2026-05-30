#include "engine/render/postfx/PostProcessPipeline.hpp"

#include <algorithm>
#include <queue>

namespace kb::render::postfx {

ResourceHandle PostProcessPipeline::RegisterResource(ResourceDesc desc) {
    if (desc.name.empty()) {
        throw std::invalid_argument("PostProcess resource name cannot be empty");
    }

    if (desc.texture.width == 0 || desc.texture.height == 0) {
        throw std::invalid_argument("PostProcess resource dimensions must be greater than zero");
    }

    const auto id = nextResourceId_++;
    resources_.emplace(id, std::move(desc));
    compiled_ = false;
    return ResourceHandle{ id };
}

void PostProcessPipeline::AddPass(PassDesc desc) {
    ValidatePass(desc);
    passes_.push_back(std::move(desc));
    compiled_ = false;
}

void PostProcessPipeline::Compile() {
    if (passes_.empty()) {
        throw std::runtime_error("PostProcess pipeline has no passes");
    }

    compiledPasses_.clear();
    compiledPasses_.reserve(passes_.size());

    for (const auto& pass : passes_) {
        compiledPasses_.push_back(CompiledPass{
            .name = pass.name,
            .reads = pass.reads,
            .writes = pass.writes,
            .execute = pass.execute,
        });
    }

    BuildGraph();
    compiled_ = true;
}

void PostProcessPipeline::Execute() {
    if (!compiled_) {
        Compile();
    }

    PassContext ctx{ .resources = &resources_ };

    for (const auto passIndex : executionOrder_) {
        compiledPasses_[passIndex].execute(ctx);
    }
}

const std::vector<std::uint32_t>& PostProcessPipeline::GetExecutionOrder() const noexcept {
    return executionOrder_;
}

void PostProcessPipeline::ValidatePass(const PassDesc& desc) const {
    if (desc.name.empty()) {
        throw std::invalid_argument("Pass name cannot be empty");
    }

    if (!desc.execute) {
        throw std::invalid_argument("Pass execute callback must be provided");
    }

    std::unordered_set<std::uint32_t> writeSet;
    writeSet.reserve(desc.writes.size());

    for (const auto write : desc.writes) {
        if (!write.IsValid() || !resources_.contains(write.id)) {
            throw std::invalid_argument("Pass writes unknown resource");
        }
        if (!writeSet.insert(write.id).second) {
            throw std::invalid_argument("Pass writes same resource more than once");
        }
    }

    for (const auto read : desc.reads) {
        if (!read.IsValid() || !resources_.contains(read.id)) {
            throw std::invalid_argument("Pass reads unknown resource");
        }
        if (writeSet.contains(read.id)) {
            throw std::invalid_argument("Read/write hazard in single pass is not allowed");
        }
    }
}

void PostProcessPipeline::BuildGraph() {
    edges_.assign(compiledPasses_.size(), {});

    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> readers;
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> writers;

    for (std::uint32_t passIndex = 0; passIndex < compiledPasses_.size(); ++passIndex) {
        for (const auto r : compiledPasses_[passIndex].reads) {
            readers[r.id].push_back(passIndex);
        }
        for (const auto w : compiledPasses_[passIndex].writes) {
            writers[w.id].push_back(passIndex);
        }
    }

    for (const auto& [resourceId, writePasses] : writers) {
        auto& readPasses = readers[resourceId];

        for (const auto writer : writePasses) {
            for (const auto reader : readPasses) {
                if (writer != reader) {
                    edges_[writer].push_back(reader);
                }
            }
        }

        for (std::size_t i = 0; i < writePasses.size(); ++i) {
            for (std::size_t j = i + 1; j < writePasses.size(); ++j) {
                edges_[writePasses[i]].push_back(writePasses[j]);
            }
        }
    }

    for (auto& list : edges_) {
        std::sort(list.begin(), list.end());
        list.erase(std::unique(list.begin(), list.end()), list.end());
    }

    std::vector<std::uint32_t> indegree(compiledPasses_.size(), 0);

    for (const auto& adjacency : edges_) {
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

    executionOrder_.clear();
    executionOrder_.reserve(compiledPasses_.size());

    while (!ready.empty()) {
        const auto current = ready.front();
        ready.pop();
        executionOrder_.push_back(current);

        for (const auto next : edges_[current]) {
            if (--indegree[next] == 0) {
                ready.push(next);
            }
        }
    }

    if (executionOrder_.size() != compiledPasses_.size()) {
        throw std::runtime_error("PostProcess dependency graph contains a cycle");
    }
}

} // namespace kb::render::postfx
