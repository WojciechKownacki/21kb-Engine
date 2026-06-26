#pragma once

#include "kb/render/runtime/RuntimeRenderResourceCacheTypes.hpp"

#include <cstdint>
#include <unordered_set>

namespace kb::render {

struct RuntimeFrameResourceReferenceReserveDesc {
    std::uint32_t meshes = 0;
    std::uint32_t materials = 0;
    std::uint32_t textures = 0;
};

struct RuntimeFrameResourceReferenceStats {
    std::uint32_t meshCount = 0;
    std::uint32_t materialCount = 0;
    std::uint32_t textureCount = 0;
    std::uint32_t meshCapacity = 0;
    std::uint32_t materialCapacity = 0;
    std::uint32_t textureCapacity = 0;
};

class RuntimeFrameResourceReferences {
public:
    void Clear() noexcept;
    void Reserve(const RuntimeFrameResourceReferenceReserveDesc& desc);

    void MarkMesh(RuntimeAssetKey key);
    void MarkMaterial(RuntimeAssetKey key);
    void MarkTexture(RuntimeTextureAssetKey key);

    [[nodiscard]] bool ContainsMesh(RuntimeAssetKey key) const;
    [[nodiscard]] bool ContainsMaterial(RuntimeAssetKey key) const;
    [[nodiscard]] bool ContainsTexture(RuntimeTextureAssetKey key) const;

    [[nodiscard]] const std::unordered_set<RuntimeAssetKey, RuntimeAssetKeyHash>& Materials() const noexcept;
    [[nodiscard]] RuntimeFrameResourceReferenceStats Stats() const noexcept;

private:
    std::unordered_set<RuntimeAssetKey, RuntimeAssetKeyHash> meshes_;
    std::unordered_set<RuntimeAssetKey, RuntimeAssetKeyHash> materials_;
    std::unordered_set<RuntimeTextureAssetKey, RuntimeTextureAssetKeyHash> textures_;
};

} // namespace kb::render
