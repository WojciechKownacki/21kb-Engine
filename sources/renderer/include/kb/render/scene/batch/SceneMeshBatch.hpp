#pragma once

#include "kb/render/scene/SceneRenderTypes.hpp"

#include <cstdint>
#include <span>

namespace kb::render {

struct SceneMeshBatch {
    std::uint64_t meshAssetId = 0;
    std::uint64_t materialAssetId = 0;
    std::uint32_t sourceDrawGroupIndex = 0;
    std::span<const SceneRenderMeshInstance> instances{};

    [[nodiscard]] bool Empty() const noexcept {
        return instances.empty();
    }
};

} // namespace kb::render
