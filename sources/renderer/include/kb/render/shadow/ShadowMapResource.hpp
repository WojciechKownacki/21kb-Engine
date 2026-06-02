#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>

namespace kb::render {

class ShadowMapResource {
public:
    ~ShadowMapResource();

    ShadowMapResource() = default;
    ShadowMapResource(const ShadowMapResource&) = delete;
    ShadowMapResource& operator=(const ShadowMapResource&) = delete;

    [[nodiscard]] bool Ensure(std::uint32_t size);
    void Shutdown() noexcept;

    [[nodiscard]] bgfx::TextureHandle DepthTexture() const noexcept;
    [[nodiscard]] bgfx::FrameBufferHandle FrameBuffer() const noexcept;
    [[nodiscard]] std::uint32_t Size() const noexcept;
    [[nodiscard]] bool IsAllocated() const noexcept;
    [[nodiscard]] std::uint64_t AllocationBytes() const noexcept;

    [[nodiscard]] static std::uint64_t AllocationBytesFor(std::uint32_t shadowMapSize) noexcept;

private:
    bgfx::TextureHandle depthTexture_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle frameBuffer_ = BGFX_INVALID_HANDLE;
    std::uint32_t size_ = 0;
};

} // namespace kb::render
