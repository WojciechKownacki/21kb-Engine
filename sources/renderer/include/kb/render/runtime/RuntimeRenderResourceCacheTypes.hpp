#pragma once

#include "kb/render/resources/RenderHandles.hpp"
#include "kb/render/runtime/RuntimeMaterialResolver.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kb::render {

struct RuntimeAssetKey {
    std::uint64_t sceneId = 0;
    std::uint64_t assetId = 0;

    [[nodiscard]] friend constexpr bool operator==(RuntimeAssetKey lhs, RuntimeAssetKey rhs) noexcept = default;
};

struct RuntimeAssetKeyHash {
    [[nodiscard]] std::size_t operator()(RuntimeAssetKey key) const noexcept {
        const std::uint64_t mixed = key.sceneId ^ (key.assetId + 0x9e3779b97f4a7c15ULL + (key.sceneId << 6U) + (key.sceneId >> 2U));
        return static_cast<std::size_t>(mixed);
    }
};

struct RuntimeMeshResource {
    RenderMeshHandle handle{};
    std::uint64_t contentHash = 0;
    std::uint64_t lastReferencedFrame = 0;
};

struct RuntimeMaterialResource {
    RenderMaterialHandle handle{};
    std::uint64_t contentHash = 0;
    std::uint64_t lastReferencedFrame = 0;
    RuntimeMaterialResolveStatus status = RuntimeMaterialResolveStatus::Resolved;
    std::vector<RuntimeMaterialResolveDiagnostic> diagnostics{};
};

struct RuntimeTextureResource {
    RenderTextureHandle handle{};
    std::uint64_t contentHash = 0;
    std::uint64_t lastReferencedFrame = 0;
};

} // namespace kb::render
