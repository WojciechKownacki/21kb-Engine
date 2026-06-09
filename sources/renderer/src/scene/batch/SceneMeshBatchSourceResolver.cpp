#include "scene/batch/SceneMeshBatchSourceResolver.hpp"

#include "kb/render/scene/batch/SceneMeshBatchBuilder.hpp"

namespace kb::render {

std::span<const SceneMeshBatch> SceneMeshBatchSourceResolver::Resolve(
    const SceneMeshBatchSourceDesc& desc,
    std::vector<SceneMeshBatch>& scratch) {
    if (desc.meshBatches != nullptr) {
        scratch.clear();
        return *desc.meshBatches;
    }

    if (desc.drawGroups != nullptr) {
        SceneMeshBatchBuilder::BuildInto(*desc.drawGroups, scratch);
        return scratch;
    }

    scratch.clear();
    return {};
}

} // namespace kb::render
