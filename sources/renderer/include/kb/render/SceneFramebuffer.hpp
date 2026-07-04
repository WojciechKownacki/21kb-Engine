#pragma once

#include "kb/render/SceneRenderTargetFormat.hpp"

#include <bgfx/bgfx.h>

#include <cstdint>

namespace kb::render {

class SceneFramebuffer {
public:
    SceneFramebuffer() = default;
    ~SceneFramebuffer();

    SceneFramebuffer(const SceneFramebuffer&) = delete;
    SceneFramebuffer& operator=(const SceneFramebuffer&) = delete;

    [[nodiscard]] bool Ensure(
        std::uint32_t width,
        std::uint32_t height,
        SceneColorFormatPolicy colorPolicy = SceneColorFormatPolicy::Auto,
        std::uint8_t msaaSamples = 0U);
    void Shutdown();

    [[nodiscard]] bgfx::FrameBufferHandle FrameBuffer() const noexcept;
    [[nodiscard]] bgfx::TextureHandle ColorTexture() const noexcept;
    [[nodiscard]] bgfx::TextureHandle ResolvedColorTexture() const noexcept;
    [[nodiscard]] bgfx::TextureHandle DepthTexture() const noexcept;
    [[nodiscard]] std::uint32_t Width() const noexcept;
    [[nodiscard]] std::uint32_t Height() const noexcept;
    [[nodiscard]] std::uint8_t MsaaSamples() const noexcept;
    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] bool DepthTextureSampled() const noexcept;
    [[nodiscard]] SceneColorFormatSelection ColorSelection() const noexcept;
    [[nodiscard]] SceneDepthFormatSelection DepthSelection() const noexcept;

private:
    bgfx::TextureHandle colorTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle resolvedColorTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle depthTexture_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle frameBuffer_ = BGFX_INVALID_HANDLE;
    SceneColorFormatSelection colorSelection_{};
    SceneDepthFormatSelection depthSelection_{};
    SceneColorFormatPolicy colorPolicy_ = SceneColorFormatPolicy::Auto;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::uint8_t msaaSamples_ = 0U;
};

} // namespace kb::render
