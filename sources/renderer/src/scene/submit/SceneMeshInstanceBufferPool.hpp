#pragma once

#include "kb/render/scene/MeshPipeline.hpp"
#include <unordered_map>

namespace kb::render {

struct SceneMeshInstanceBufferOwner {
    std::uint64_t batchId = 0U;
    std::uint32_t commandIndex = 0U;
    MeshPassType pass = MeshPassType::BaseOpaque;
    bgfx::ViewId viewId = 0U;
    [[nodiscard]] bool operator==(const SceneMeshInstanceBufferOwner&) const noexcept = default;
};

// Owns GPU instance allocations independently of draw ordering and CPU command validity.
class SceneMeshInstanceBufferPool {
public:
    SceneMeshInstanceBufferPool() = default;
    ~SceneMeshInstanceBufferPool();
    SceneMeshInstanceBufferPool(const SceneMeshInstanceBufferPool&) = delete;
    SceneMeshInstanceBufferPool& operator=(const SceneMeshInstanceBufferPool&) = delete;

    [[nodiscard]] bgfx::DynamicVertexBufferHandle Upload(
        std::span<const SceneRenderMeshInstance> instances,
        const RenderMaterialResource* material,
        bool encodeShadowReceiver,
        std::uint64_t instanceRevision = 0U,
        SceneMeshInstanceBufferOwner owner = {});
    [[nodiscard]] std::uint64_t LastUploadBytes() const noexcept { return lastUploadBytes_; }
    void EndFrame() noexcept;
    [[nodiscard]] std::size_t OwnedBufferCount() const noexcept { return ownedSlots_.size(); }
    void Shutdown() noexcept;

private:
    struct Slot {
        bgfx::DynamicVertexBufferHandle buffer = BGFX_INVALID_HANDLE;
        std::uint32_t capacity = 0U;
        std::uint64_t instanceRevision = 0U;
        std::uint32_t count = 0U;
        bool encodeShadowReceiver = false;
        std::uint64_t lastUsedFrame = UINT64_MAX;
    };
    struct OwnerKey {
        SceneMeshInstanceBufferOwner owner;
        std::uint32_t occurrence = 0U;
        [[nodiscard]] bool operator==(const OwnerKey&) const noexcept = default;
    };
    struct OwnerKeyHash {
        [[nodiscard]] std::size_t operator()(const OwnerKey& key) const noexcept;
    };
    [[nodiscard]] Slot& Acquire(SceneMeshInstanceBufferOwner owner);
    std::unordered_map<OwnerKey, Slot, OwnerKeyHash> ownedSlots_;
    std::vector<Slot> slots_;
    std::size_t usedSlots_ = 0U;
    std::uint64_t lastUploadBytes_ = 0U;
    std::uint64_t frameIndex_ = 0U;
};

} // namespace kb::render
