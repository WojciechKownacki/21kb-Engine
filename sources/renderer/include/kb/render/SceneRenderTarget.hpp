#pragma once

#include "kb/render/SceneFramebuffer.hpp"
#include "kb/render/frame/RenderTargetDesc.hpp"

#include <bgfx/bgfx.h>

#include <cstdint>

namespace kb::render {

struct SceneRenderTargetDesc {
    RenderExtent extent{};
    SceneColorFormatPolicy colorPolicy = SceneColorFormatPolicy::Auto;

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return extent.IsValid();
    }
};

class SceneRenderTarget {
public:
    SceneRenderTarget() = default;
    ~SceneRenderTarget() = default;

    SceneRenderTarget(const SceneRenderTarget&) = delete;
    SceneRenderTarget& operator=(const SceneRenderTarget&) = delete;

    [[nodiscard]] bool Ensure(const SceneRenderTargetDesc& desc);
    [[nodiscard]] bool Ensure(std::uint32_t width, std::uint32_t height, SceneColorFormatPolicy colorPolicy = SceneColorFormatPolicy::Auto);
    void Shutdown();

    [[nodiscard]] bgfx::FrameBufferHandle FrameBuffer() const noexcept;
    [[nodiscard]] bgfx::TextureHandle ColorTexture() const noexcept;
    [[nodiscard]] bgfx::TextureHandle DepthTexture() const noexcept;
    [[nodiscard]] RenderExtent Extent() const noexcept;
    [[nodiscard]] std::uint32_t Width() const noexcept;
    [[nodiscard]] std::uint32_t Height() const noexcept;
    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] bool UsesHdrColor() const noexcept;
    [[nodiscard]] bool UsesPreferredDepth() const noexcept;
    [[nodiscard]] SceneColorFormatSelection ColorSelection() const noexcept;
    [[nodiscard]] SceneDepthFormatSelection DepthSelection() const noexcept;
    [[nodiscard]] RenderTargetDesc ColorTargetDesc() const noexcept;
    [[nodiscard]] RenderTargetDesc DepthTargetDesc() const noexcept;

private:
    SceneFramebuffer framebuffer_;
};

} // namespace kb::render
