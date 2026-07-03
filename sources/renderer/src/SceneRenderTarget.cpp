#include "kb/render/SceneRenderTarget.hpp"

namespace kb::render {
namespace {

[[nodiscard]] RenderTargetFormat ToRenderTargetFormat(bgfx::TextureFormat::Enum format) noexcept {
    switch (format) {
    case bgfx::TextureFormat::BGRA8:
        return RenderTargetFormat::Bgra8;
    case bgfx::TextureFormat::RGBA8:
        return RenderTargetFormat::Rgba8;
    case bgfx::TextureFormat::RGBA16:
        return RenderTargetFormat::Rgba16;
    case bgfx::TextureFormat::RGBA16F:
        return RenderTargetFormat::Rgba16F;
    case bgfx::TextureFormat::D24S8:
        return RenderTargetFormat::D24S8;
    case bgfx::TextureFormat::D32:
        return RenderTargetFormat::D32;
    case bgfx::TextureFormat::D32F:
        return RenderTargetFormat::D32F;
    default:
        return RenderTargetFormat::Unknown;
    }
}

} // namespace

bool SceneRenderTarget::Ensure(const SceneRenderTargetDesc& desc) {
    if (!desc.IsValid()) {
        return false;
    }
    return Ensure(desc.extent.width, desc.extent.height, desc.colorPolicy, desc.msaaSamples);
}

bool SceneRenderTarget::Ensure(std::uint32_t width, std::uint32_t height, SceneColorFormatPolicy colorPolicy, std::uint8_t msaaSamples) {
    return framebuffer_.Ensure(width, height, colorPolicy, msaaSamples);
}

void SceneRenderTarget::Shutdown() {
    framebuffer_.Shutdown();
}

bgfx::FrameBufferHandle SceneRenderTarget::FrameBuffer() const noexcept {
    return framebuffer_.FrameBuffer();
}

bgfx::TextureHandle SceneRenderTarget::ColorTexture() const noexcept {
    return framebuffer_.ColorTexture();
}

bgfx::TextureHandle SceneRenderTarget::DepthTexture() const noexcept {
    return framebuffer_.DepthTexture();
}

RenderExtent SceneRenderTarget::Extent() const noexcept {
    return RenderExtent{
        .width = framebuffer_.Width(),
        .height = framebuffer_.Height(),
    };
}

std::uint32_t SceneRenderTarget::Width() const noexcept {
    return framebuffer_.Width();
}

std::uint32_t SceneRenderTarget::Height() const noexcept {
    return framebuffer_.Height();
}

std::uint8_t SceneRenderTarget::MsaaSamples() const noexcept {
    return framebuffer_.MsaaSamples();
}

bool SceneRenderTarget::IsValid() const noexcept {
    return framebuffer_.IsValid();
}

bool SceneRenderTarget::DepthTextureSampled() const noexcept {
    return framebuffer_.DepthTextureSampled();
}

bool SceneRenderTarget::UsesHdrColor() const noexcept {
    return framebuffer_.ColorSelection().IsHdr();
}

bool SceneRenderTarget::UsesPreferredDepth() const noexcept {
    return framebuffer_.DepthSelection().IsPreferred();
}

SceneColorFormatSelection SceneRenderTarget::ColorSelection() const noexcept {
    return framebuffer_.ColorSelection();
}

SceneDepthFormatSelection SceneRenderTarget::DepthSelection() const noexcept {
    return framebuffer_.DepthSelection();
}

RenderTargetDesc SceneRenderTarget::ColorTargetDesc() const noexcept {
    return RenderTargetDesc{
        .role = RenderTargetRole::SceneColorHdr,
        .format = ToRenderTargetFormat(ColorSelection().format),
        .extent = Extent(),
        .renderable = true,
        .sampled = true,
    };
}

RenderTargetDesc SceneRenderTarget::DepthTargetDesc() const noexcept {
    return RenderTargetDesc{
        .role = RenderTargetRole::SceneDepth,
        .format = ToRenderTargetFormat(DepthSelection().format),
        .extent = Extent(),
        .renderable = true,
        .sampled = false,
    };
}

} // namespace kb::render
