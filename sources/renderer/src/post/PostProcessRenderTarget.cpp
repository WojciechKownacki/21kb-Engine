#include "kb/render/post/PostProcessRenderTarget.hpp"

#include <algorithm>

namespace kb::render {
namespace {

constexpr std::uint64_t kPostProcessColorTextureFlags =
    BGFX_TEXTURE_RT |
    BGFX_TEXTURE_BLIT_DST |
    BGFX_SAMPLER_MIP_POINT |
    BGFX_SAMPLER_U_CLAMP |
    BGFX_SAMPLER_V_CLAMP;

[[nodiscard]] bool IsSupportedExtent(std::uint32_t width, std::uint32_t height) noexcept {
    return width > 0U && height > 0U && width <= UINT16_MAX && height <= UINT16_MAX;
}

[[nodiscard]] RenderTargetFormat ToRenderTargetFormat(bgfx::TextureFormat::Enum format) noexcept {
    switch (format) {
    case bgfx::TextureFormat::RGBA8:
        return RenderTargetFormat::Rgba8;
    case bgfx::TextureFormat::RGBA16:
        return RenderTargetFormat::Rgba16;
    case bgfx::TextureFormat::RGBA16F:
        return RenderTargetFormat::Rgba16F;
    default:
        return RenderTargetFormat::Unknown;
    }
}

} // namespace

bool PostProcessRenderTarget::Ensure(const PostProcessRenderTargetDesc& desc) {
    if (!desc.IsValid()) {
        return false;
    }
    return Ensure(desc.extent.width, desc.extent.height, desc.colorPolicy);
}

bool PostProcessRenderTarget::Ensure(std::uint32_t width, std::uint32_t height, SceneColorFormatPolicy colorPolicy) {
    width = std::max(1U, width);
    height = std::max(1U, height);

    if (!IsSupportedExtent(width, height)) {
        return false;
    }

    if (IsValid() && width_ == width && height_ == height && colorPolicy_ == colorPolicy) {
        return true;
    }

    Shutdown();

    colorSelection_ = SelectSceneColorFormat(colorPolicy, kPostProcessColorTextureFlags);
    if (!colorSelection_.IsSupported()) {
        return false;
    }

    colorTexture_ = bgfx::createTexture2D(
        static_cast<std::uint16_t>(width),
        static_cast<std::uint16_t>(height),
        false,
        1,
        colorSelection_.format,
        kPostProcessColorTextureFlags);
    if (!bgfx::isValid(colorTexture_)) {
        Shutdown();
        return false;
    }
    bgfx::setName(colorTexture_, "KB Post Process Color");

    frameBuffer_ = bgfx::createFrameBuffer(1, &colorTexture_, false);
    if (!bgfx::isValid(frameBuffer_)) {
        Shutdown();
        return false;
    }

    width_ = width;
    height_ = height;
    colorPolicy_ = colorPolicy;
    return true;
}

void PostProcessRenderTarget::Shutdown() {
    if (bgfx::isValid(frameBuffer_)) {
        bgfx::destroy(frameBuffer_);
    }
    if (bgfx::isValid(colorTexture_)) {
        bgfx::destroy(colorTexture_);
    }

    frameBuffer_ = BGFX_INVALID_HANDLE;
    colorTexture_ = BGFX_INVALID_HANDLE;
    colorSelection_ = {};
    colorPolicy_ = SceneColorFormatPolicy::Auto;
    width_ = 0;
    height_ = 0;
}

bgfx::FrameBufferHandle PostProcessRenderTarget::FrameBuffer() const noexcept {
    return frameBuffer_;
}

bgfx::TextureHandle PostProcessRenderTarget::ColorTexture() const noexcept {
    return colorTexture_;
}

RenderExtent PostProcessRenderTarget::Extent() const noexcept {
    return RenderExtent{
        .width = width_,
        .height = height_,
    };
}

std::uint32_t PostProcessRenderTarget::Width() const noexcept {
    return width_;
}

std::uint32_t PostProcessRenderTarget::Height() const noexcept {
    return height_;
}

bool PostProcessRenderTarget::IsValid() const noexcept {
    return bgfx::isValid(frameBuffer_) && bgfx::isValid(colorTexture_);
}

bool PostProcessRenderTarget::UsesHdrColor() const noexcept {
    return colorSelection_.IsHdr();
}

SceneColorFormatSelection PostProcessRenderTarget::ColorSelection() const noexcept {
    return colorSelection_;
}

RenderTargetDesc PostProcessRenderTarget::ColorTargetDesc() const noexcept {
    return RenderTargetDesc{
        .role = RenderTargetRole::PostProcessColor,
        .format = ToRenderTargetFormat(colorSelection_.format),
        .extent = Extent(),
        .renderable = true,
        .sampled = true,
    };
}

} // namespace kb::render
