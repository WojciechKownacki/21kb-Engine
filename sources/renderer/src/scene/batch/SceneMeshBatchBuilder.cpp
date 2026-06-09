#include "kb/render/scene/batch/SceneMeshBatchBuilder.hpp"

#include <cstddef>

namespace kb::render {

std::vector<SceneMeshBatch> SceneMeshBatchBuilder::Build(std::span<const SceneRenderDrawGroup> drawGroups) {
    std::vector<SceneMeshBatch> batches;
    BuildInto(drawGroups, batches);
    return batches;
}

void SceneMeshBatchBuilder::BuildInto(std::span<const SceneRenderDrawGroup> drawGroups, std::vector<SceneMeshBatch>& outBatches) {
    outBatches.clear();
    outBatches.reserve(drawGroups.size());

    for (std::size_t index = 0U; index < drawGroups.size(); ++index) {
        const SceneRenderDrawGroup& group = drawGroups[index];
        if (group.instances.empty()) {
            continue;
        }

        outBatches.push_back(SceneMeshBatch{
            .meshAssetId = group.meshAssetId,
            .materialAssetId = group.materialAssetId,
            .sourceDrawGroupIndex = static_cast<std::uint32_t>(index),
            .instances = std::span<const SceneRenderMeshInstance>(group.instances.data(), group.instances.size()),
        });
    }
}

} // namespace kb::render
