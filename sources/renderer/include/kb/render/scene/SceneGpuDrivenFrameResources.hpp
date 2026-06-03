#pragma once

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace kb::render {

struct SceneGpuDrivenInputRecord {
    std::uint64_t entityId = 0;
    std::array<float, 4> worldBounds{};
    std::uint32_t drawCommandIndex = 0;
    std::uint32_t lodLevel = 0;
    std::uint32_t firstMeshlet = 0;
    std::uint32_t meshletCount = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return worldBounds[3] > 0.0F;
    }
};

struct SceneGpuDrivenFrameBatch {
    bgfx::DynamicVertexBufferHandle boundsBuffer = BGFX_INVALID_HANDLE;
    bgfx::DynamicVertexBufferHandle metadataBuffer = BGFX_INVALID_HANDLE;
    bgfx::DynamicIndexBufferHandle predicateBuffer = BGFX_INVALID_HANDLE;
    bgfx::DynamicIndexBufferHandle visibleListBuffer = BGFX_INVALID_HANDLE;
    bgfx::DynamicIndexBufferHandle counterBuffer = BGFX_INVALID_HANDLE;
    std::uint32_t instanceCount = 0;
    std::uint32_t capacity = 0;
    std::uint64_t uploadBytes = 0;

    [[nodiscard]] bool IsValid() const noexcept {
        return instanceCount > 0U &&
            bgfx::isValid(boundsBuffer) &&
            bgfx::isValid(metadataBuffer) &&
            bgfx::isValid(predicateBuffer) &&
            bgfx::isValid(visibleListBuffer) &&
            bgfx::isValid(counterBuffer);
    }
};

class SceneGpuDrivenFrameResources {
public:
    SceneGpuDrivenFrameResources() = default;
    SceneGpuDrivenFrameResources(const SceneGpuDrivenFrameResources&) = delete;
    SceneGpuDrivenFrameResources& operator=(const SceneGpuDrivenFrameResources&) = delete;
    SceneGpuDrivenFrameResources(SceneGpuDrivenFrameResources&&) = delete;
    SceneGpuDrivenFrameResources& operator=(SceneGpuDrivenFrameResources&&) = delete;
    ~SceneGpuDrivenFrameResources();

    void Shutdown() noexcept;
    [[nodiscard]] SceneGpuDrivenFrameBatch Upload(std::span<const SceneGpuDrivenInputRecord> records);
    [[nodiscard]] std::uint32_t Capacity() const noexcept { return capacity_; }

private:
    [[nodiscard]] bool EnsureCapacity(std::uint32_t requestedCapacity);
    void DestroyBuffers() noexcept;

    bgfx::DynamicVertexBufferHandle boundsBuffer_ = BGFX_INVALID_HANDLE;
    bgfx::DynamicVertexBufferHandle metadataBuffer_ = BGFX_INVALID_HANDLE;
    bgfx::DynamicIndexBufferHandle predicateBuffer_ = BGFX_INVALID_HANDLE;
    bgfx::DynamicIndexBufferHandle visibleListBuffer_ = BGFX_INVALID_HANDLE;
    bgfx::DynamicIndexBufferHandle counterBuffer_ = BGFX_INVALID_HANDLE;
    std::uint32_t capacity_ = 0;
};

} // namespace kb::render
