#pragma once

#include "kb/render/scene/SceneRenderTypes.hpp"
#include "kb/render/scene/batch/SceneMeshBatch.hpp"

#include <span>
#include <vector>

namespace kb::render {

struct SceneMeshBatchSourceDesc {
    const std::vector<SceneMeshBatch>* meshBatches = nullptr;
    const std::vector<SceneRenderDrawGroup>* drawGroups = nullptr;
};

class SceneMeshBatchSourceResolver {
public:
    SceneMeshBatchSourceResolver() = delete;

    [[nodiscard]] static std::span<const SceneMeshBatch> Resolve(
        const SceneMeshBatchSourceDesc& desc,
        std::vector<SceneMeshBatch>& scratch);
};

} // namespace kb::render
