#pragma once

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>

namespace kb::render {

class ShadowMapResource {
public:
    ~ShadowMapResource();

    ShadowMapResource() = default;
    ShadowMapResource(const ShadowMapResource&) = delete;
    ShadowMapResource& operator=(const ShadowMapResource&) = delete;

    [[nodiscard]] bool Ensure(std::uint32_t size);
    // Rectangular map, e.g. the point-light face atlas.
    [[nodiscard]] bool Ensure(std::uint32_t width, std::uint32_t height);
    void Shutdown() noexcept;

    [[nodiscard]] bgfx::TextureHandle DepthTexture() const noexcept;
    [[nodiscard]] bgfx::FrameBufferHandle FrameBuffer() const noexcept;
    [[nodiscard]] std::uint32_t Size() const noexcept;
    [[nodiscard]] std::uint32_t Height() const noexcept;
    [[nodiscard]] bool IsAllocated() const noexcept;
    [[nodiscard]] std::uint64_t AllocationBytes() const noexcept;

    [[nodiscard]] static std::uint64_t AllocationBytesFor(std::uint32_t shadowMapSize) noexcept;
    [[nodiscard]] static std::uint64_t AllocationBytesFor(std::uint32_t width, std::uint32_t height) noexcept;

    // What the tiles of light slot `slot` were last rendered for, when its light is static (Mobility
    // Static): unchanged, the tiles are kept instead of rendered again. 0 = nothing cached.
    [[nodiscard]] std::uint64_t CachedTiles(std::uint32_t slot) const noexcept {
        return slot < cachedTiles_.size() ? cachedTiles_[slot] : 0U;
    }
    void SetCachedTiles(std::uint32_t slot, std::uint64_t key) noexcept {
        if (slot < cachedTiles_.size()) cachedTiles_[slot] = key;
    }

private:
    std::array<std::uint64_t, 32> cachedTiles_{};
    bgfx::TextureHandle depthTexture_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle frameBuffer_ = BGFX_INVALID_HANDLE;
    std::uint32_t size_ = 0;
    std::uint32_t height_ = 0;
};

} // namespace kb::render
