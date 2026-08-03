#pragma once

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace kb::render {

using RenderSkinningMatrix = std::array<float, 16U>;

struct RenderSkinningPaletteHandle {
    std::uint64_t frame = 0U;
    std::uint32_t firstMatrix = 0U;
    std::uint32_t matrixCount = 0U;
    std::uint8_t bufferIndex = 0U;

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return frame != 0U && matrixCount != 0U;
    }

    [[nodiscard]] friend constexpr bool operator==(RenderSkinningPaletteHandle,
        RenderSkinningPaletteHandle) noexcept = default;
};

struct RenderSkinningPaletteAllocatorDesc {
    std::uint32_t matrixCapacityPerFrame = 0U;
};

struct RenderSkinningPaletteAllocatorStats {
    std::uint64_t activeFrame = 0U;
    std::uint64_t completedFrame = 0U;
    std::uint32_t matrixCapacityPerFrame = 0U;
    std::uint32_t allocatedMatrices = 0U;
    std::uint32_t allocationFailures = 0U;
    std::uint64_t uploadedBytes = 0U;
};

// Double-buffered frame allocator. The caller must pass the GPU-completed
// frame to BeginFrame; the allocator never overwrites a palette still covered
// by that fence. Exhaustion is explicit through an invalid handle.
class RenderSkinningPaletteAllocator final {
public:
    RenderSkinningPaletteAllocator() = default;
    explicit RenderSkinningPaletteAllocator(RenderSkinningPaletteAllocatorDesc desc);
    ~RenderSkinningPaletteAllocator();

    RenderSkinningPaletteAllocator(const RenderSkinningPaletteAllocator&) = delete;
    RenderSkinningPaletteAllocator& operator=(const RenderSkinningPaletteAllocator&) = delete;

    [[nodiscard]] bool BeginFrame(std::uint64_t frame, std::uint64_t completedFrame) noexcept;
    [[nodiscard]] RenderSkinningPaletteHandle Allocate(std::uint32_t matrixCount) noexcept;
    [[nodiscard]] bool Upload(RenderSkinningPaletteHandle handle,
        std::span<const RenderSkinningMatrix> matrices) noexcept;
    [[nodiscard]] bgfx::TextureHandle Texture(
        RenderSkinningPaletteHandle handle) const noexcept;
    [[nodiscard]] RenderSkinningPaletteAllocatorStats Stats() const noexcept;
    void Shutdown() noexcept;

private:
    [[nodiscard]] bool EnsureBuffers() noexcept;
    [[nodiscard]] bool Owns(RenderSkinningPaletteHandle handle) const noexcept;
    [[nodiscard]] bool IsResident(RenderSkinningPaletteHandle handle) const noexcept;

    RenderSkinningPaletteAllocatorDesc desc_{};
    std::array<bgfx::TextureHandle, 2U> textures_{
        bgfx::TextureHandle{ UINT16_MAX },
        bgfx::TextureHandle{ UINT16_MAX } };
    std::array<std::vector<RenderSkinningMatrix>, 2U> staging_{};
    std::array<std::uint64_t, 2U> fenceFrames_{};
    std::array<std::uint32_t, 2U> allocatedMatricesByBuffer_{};
    std::uint64_t activeFrame_ = 0U;
    std::uint64_t completedFrame_ = 0U;
    std::uint32_t allocatedMatrices_ = 0U;
    std::uint32_t allocationFailures_ = 0U;
    std::uint64_t uploadedBytes_ = 0U;
};

} // namespace kb::render
