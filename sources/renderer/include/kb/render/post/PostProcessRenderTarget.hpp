#pragma once

#include "kb/render/SceneFramebuffer.hpp"
#include "kb/render/frame/RenderTargetDesc.hpp"

#include <bgfx/bgfx.h>

#include <cstdint>

namespace kb::render {

struct PostProcessRenderTargetDesc {
    RenderExtent extent{};
    SceneColorFormatPolicy colorPolicy = SceneColorFormatPolicy::Auto;

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return extent.IsValid();
    }
};

class PostProcessRenderTarget {
public:
    PostProcessRenderTarget() = default;
    ~PostProcessRenderTarget() = default;

    PostProcessRenderTarget(const PostProcessRenderTarget&) = delete;
    PostProcessRenderTarget& operator=(const PostProcessRenderTarget&) = delete;

    [[nodiscard]] bool Ensure(const PostProcessRenderTargetDesc& desc);
    [[nodiscard]] bool Ensure(std::uint32_t width, std::uint32_t height, SceneColorFormatPolicy colorPolicy = SceneColorFormatPolicy::Auto);
    void Shutdown();

    [[nodiscard]] bgfx::FrameBufferHandle FrameBuffer() const noexcept;
    [[nodiscard]] bgfx::TextureHandle ColorTexture() const noexcept;
    [[nodiscard]] RenderExtent Extent() const noexcept;
    [[nodiscard]] std::uint32_t Width() const noexcept;
    [[nodiscard]] std::uint32_t Height() const noexcept;
    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] bool UsesHdrColor() const noexcept;
    [[nodiscard]] SceneColorFormatSelection ColorSelection() const noexcept;
    [[nodiscard]] RenderTargetDesc ColorTargetDesc() const noexcept;

private:
    bgfx::TextureHandle colorTexture_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle frameBuffer_ = BGFX_INVALID_HANDLE;
    SceneColorFormatSelection colorSelection_{};
    SceneColorFormatPolicy colorPolicy_ = SceneColorFormatPolicy::Auto;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
};

} // namespace kb::render
