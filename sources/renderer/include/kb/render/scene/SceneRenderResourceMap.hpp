#pragma once

#include "kb/render/resources/RenderHandles.hpp"
#include "kb/render/resources/RenderResources.hpp"

#include <cstdint>
#include <unordered_map>

namespace kb::render {

class RenderResourceRegistry;

struct SceneRenderResourceMapStats {
    std::uint32_t meshBindingCount = 0;
    std::uint32_t materialBindingCount = 0;
    std::uint32_t textureBindingCount = 0;
    std::uint32_t meshBindingCapacity = 0;
    std::uint32_t materialBindingCapacity = 0;
    std::uint32_t textureBindingCapacity = 0;
};

struct SceneRenderResourceMapReserveDesc {
    std::uint32_t meshBindings = 0;
    std::uint32_t materialBindings = 0;
    std::uint32_t textureBindings = 0;
};

class SceneRenderResourceMap {
public:
    struct TextureBindingKey {
        std::uint64_t assetId = 0;
        RenderTextureColorSpace colorSpace = RenderTextureColorSpace::Linear;

        [[nodiscard]] friend constexpr bool operator==(TextureBindingKey lhs, TextureBindingKey rhs) noexcept = default;
    };

    struct TextureBindingKeyHash {
        [[nodiscard]] std::size_t operator()(TextureBindingKey key) const noexcept;
    };

    void Reserve(const SceneRenderResourceMapReserveDesc& desc);

    void BindMesh(std::uint64_t meshAssetId, RenderMeshHandle handle);
    void UnbindMesh(std::uint64_t meshAssetId) noexcept;
    void UnbindMeshHandle(RenderMeshHandle handle) noexcept;
    [[nodiscard]] RenderMeshHandle ResolveMesh(std::uint64_t meshAssetId) const noexcept;

    void BindMaterial(std::uint64_t materialAssetId, RenderMaterialHandle handle);
    void UnbindMaterial(std::uint64_t materialAssetId) noexcept;
    void UnbindMaterialHandle(RenderMaterialHandle handle) noexcept;
    [[nodiscard]] RenderMaterialHandle ResolveMaterial(std::uint64_t materialAssetId) const noexcept;

    void BindTexture(std::uint64_t textureAssetId, RenderTextureHandle handle);
    void BindTexture(std::uint64_t textureAssetId, RenderTextureColorSpace colorSpace, RenderTextureHandle handle);
    void UnbindTexture(std::uint64_t textureAssetId) noexcept;
    void UnbindTexture(std::uint64_t textureAssetId, RenderTextureColorSpace colorSpace) noexcept;
    void UnbindTextureHandle(RenderTextureHandle handle) noexcept;
    [[nodiscard]] RenderTextureHandle ResolveTexture(std::uint64_t textureAssetId) const noexcept;
    [[nodiscard]] RenderTextureHandle ResolveTexture(std::uint64_t textureAssetId, RenderTextureColorSpace colorSpace) const noexcept;

    void PruneInvalidBindings(const RenderResourceRegistry& registry) noexcept;
    void Clear() noexcept;
    [[nodiscard]] SceneRenderResourceMapStats Stats() const noexcept;

private:
    std::unordered_map<std::uint64_t, RenderMeshHandle> meshes_;
    std::unordered_map<std::uint64_t, RenderMaterialHandle> materials_;
    std::unordered_map<TextureBindingKey, RenderTextureHandle, TextureBindingKeyHash> textures_;
};

} // namespace kb::render
