#pragma once

#include "kb/render/scene/SceneRenderTypes.hpp"
#include "kb/render/scene/batch/SceneMeshBatch.hpp"

#include <span>
#include <vector>

namespace kb::render {

class SceneMeshBatchBuilder {
public:
    SceneMeshBatchBuilder() = delete;

    [[nodiscard]] static std::vector<SceneMeshBatch> Build(std::span<const SceneRenderDrawGroup> drawGroups);
    static void BuildInto(std::span<const SceneRenderDrawGroup> drawGroups, std::vector<SceneMeshBatch>& outBatches);
};

} // namespace kb::render
