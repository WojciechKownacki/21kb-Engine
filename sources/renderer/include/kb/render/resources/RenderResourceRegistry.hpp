#pragma once

#include "kb/render/resources/RenderHandles.hpp"
#include "kb/render/resources/RenderResources.hpp"
#include "kb/render/resources/detail/RenderResourceSlotPool.hpp"

#include <cstdint>
#include <vector>

namespace kb::render {

struct RenderResourceRegistryStats {
    std::uint64_t frameNumber = 0;
    std::uint32_t meshCount = 0;
    std::uint32_t materialCount = 0;
    std::uint32_t textureCount = 0;
    std::uint32_t pendingDestroyCount = 0;
    std::uint32_t pendingMeshDestroyCount = 0;
    std::uint32_t pendingMaterialDestroyCount = 0;
    std::uint32_t pendingTextureDestroyCount = 0;
    std::uint32_t meshSlotCapacity = 0;
    std::uint32_t materialSlotCapacity = 0;
    std::uint32_t textureSlotCapacity = 0;
    std::uint32_t freeMeshSlotCount = 0;
    std::uint32_t freeMaterialSlotCount = 0;
    std::uint32_t freeTextureSlotCount = 0;
};

struct RenderResourceRegistryReserveDesc {
    std::uint32_t meshSlots = 0;
    std::uint32_t materialSlots = 0;
    std::uint32_t textureSlots = 0;
};

class RenderResourceRegistry {
public:
    RenderResourceRegistry();
    ~RenderResourceRegistry();

    RenderResourceRegistry(const RenderResourceRegistry&) = delete;
    RenderResourceRegistry& operator=(const RenderResourceRegistry&) = delete;

    [[nodiscard]] RenderMeshHandle RegisterMesh(const RenderMeshDesc& desc);
    [[nodiscard]] const RenderMeshResource* FindMesh(RenderMeshHandle handle) const noexcept;
    [[nodiscard]] bool ContainsMesh(RenderMeshHandle handle) const noexcept;
    void DestroyMesh(RenderMeshHandle handle) noexcept;

    [[nodiscard]] RenderMaterialHandle RegisterMaterial(const RenderMaterialDesc& desc);
    [[nodiscard]] const RenderMaterialResource* FindMaterial(RenderMaterialHandle handle) const noexcept;
    [[nodiscard]] bool ContainsMaterial(RenderMaterialHandle handle) const noexcept;
    void DestroyMaterial(RenderMaterialHandle handle) noexcept;

    [[nodiscard]] RenderTextureHandle RegisterTexture2D(const RenderTextureDesc& desc);
    [[nodiscard]] const RenderTextureResource* FindTexture(RenderTextureHandle handle) const noexcept;
    [[nodiscard]] bool ContainsTexture(RenderTextureHandle handle) const noexcept;
    void DestroyTexture(RenderTextureHandle handle) noexcept;

    void Reserve(const RenderResourceRegistryReserveDesc& desc);
    void TickFrame() noexcept;
    void Shutdown() noexcept;

    [[nodiscard]] RenderResourceRegistryStats Stats() const noexcept;

private:
    enum class DeferredDestroyKind : std::uint8_t {
        Mesh,
        Material,
        Texture,
    };

    struct DeferredDestroyEntry {
        DeferredDestroyKind kind = DeferredDestroyKind::Mesh;
        std::uint32_t slot = 0;
        std::uint64_t releaseFrame = 0;
    };

    void QueueDestroy(DeferredDestroyKind kind, std::uint32_t slot) noexcept;
    void ReleaseDeferred(const DeferredDestroyEntry& entry) noexcept;

    detail::RenderResourceSlotPool<RenderMeshResource> meshes_;
    detail::RenderResourceSlotPool<RenderMaterialResource> materials_;
    detail::RenderResourceSlotPool<RenderTextureResource> textures_;
    std::vector<DeferredDestroyEntry> deferredDestroy_;
    std::uint64_t frameNumber_ = 0;
};

} // namespace kb::render
