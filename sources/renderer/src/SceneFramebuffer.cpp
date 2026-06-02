#include "kb/render/SceneFramebuffer.hpp"

#include <algorithm>

namespace kb::render {
namespace {

constexpr std::uint64_t kSceneColorTextureFlags =
    BGFX_TEXTURE_RT |
    BGFX_SAMPLER_MIP_POINT |
    BGFX_SAMPLER_U_CLAMP |
    BGFX_SAMPLER_V_CLAMP;

constexpr std::uint64_t kSceneDepthTextureFlags =
    BGFX_TEXTURE_RT |
    BGFX_SAMPLER_MIN_POINT |
    BGFX_SAMPLER_MAG_POINT |
    BGFX_SAMPLER_MIP_POINT |
    BGFX_SAMPLER_U_CLAMP |
    BGFX_SAMPLER_V_CLAMP;

[[nodiscard]] bool IsSupportedExtent(std::uint32_t width, std::uint32_t height) noexcept {
    return width > 0U && height > 0U && width <= UINT16_MAX && height <= UINT16_MAX;
}

} // namespace

SceneFramebuffer::~SceneFramebuffer() {
    Shutdown();
}

bool SceneFramebuffer::Ensure(std::uint32_t width, std::uint32_t height, SceneColorFormatPolicy colorPolicy) {
    width = std::max(1U, width);
    height = std::max(1U, height);

    if (!IsSupportedExtent(width, height)) {
        return false;
    }

    if (IsValid() && width_ == width && height_ == height && colorPolicy_ == colorPolicy) {
        return true;
    }

    Shutdown();

    colorSelection_ = SelectSceneColorFormat(colorPolicy, kSceneColorTextureFlags);
    depthSelection_ = SelectSceneDepthFormat(kSceneDepthTextureFlags);
    if (!colorSelection_.IsSupported() || !depthSelection_.IsSupported()) {
        return false;
    }

    const auto textureWidth = static_cast<std::uint16_t>(width);
    const auto textureHeight = static_cast<std::uint16_t>(height);
    colorTexture_ = bgfx::createTexture2D(textureWidth, textureHeight, false, 1, colorSelection_.format, kSceneColorTextureFlags);
    if (!bgfx::isValid(colorTexture_)) {
        Shutdown();
        return false;
    }
    bgfx::setName(colorTexture_, "KB Scene Color");

    depthTexture_ = bgfx::createTexture2D(textureWidth, textureHeight, false, 1, depthSelection_.format, kSceneDepthTextureFlags);
    if (!bgfx::isValid(depthTexture_)) {
        Shutdown();
        return false;
    }
    bgfx::setName(depthTexture_, "KB Scene Depth");

    bgfx::Attachment attachments[2]{};
    attachments[0].init(colorTexture_);
    attachments[1].init(depthTexture_);
    frameBuffer_ = bgfx::createFrameBuffer(2, attachments, true);
    if (!bgfx::isValid(frameBuffer_)) {
        Shutdown();
        return false;
    }

    width_ = width;
    height_ = height;
    colorPolicy_ = colorPolicy;
    return true;
}

void SceneFramebuffer::Shutdown() {
    if (bgfx::isValid(frameBuffer_)) {
        bgfx::destroy(frameBuffer_);
    } else {
        if (bgfx::isValid(depthTexture_)) {
            bgfx::destroy(depthTexture_);
        }
        if (bgfx::isValid(colorTexture_)) {
            bgfx::destroy(colorTexture_);
        }
    }

    frameBuffer_ = BGFX_INVALID_HANDLE;
    colorTexture_ = BGFX_INVALID_HANDLE;
    depthTexture_ = BGFX_INVALID_HANDLE;
    colorSelection_ = {};
    depthSelection_ = {};
    colorPolicy_ = SceneColorFormatPolicy::Auto;
    width_ = 0;
    height_ = 0;
}

bgfx::FrameBufferHandle SceneFramebuffer::FrameBuffer() const noexcept {
    return frameBuffer_;
}

bgfx::TextureHandle SceneFramebuffer::ColorTexture() const noexcept {
    return colorTexture_;
}

bgfx::TextureHandle SceneFramebuffer::DepthTexture() const noexcept {
    return depthTexture_;
}

std::uint32_t SceneFramebuffer::Width() const noexcept {
    return width_;
}

std::uint32_t SceneFramebuffer::Height() const noexcept {
    return height_;
}

bool SceneFramebuffer::IsValid() const noexcept {
    return bgfx::isValid(frameBuffer_) && bgfx::isValid(colorTexture_) && bgfx::isValid(depthTexture_);
}

SceneColorFormatSelection SceneFramebuffer::ColorSelection() const noexcept {
    return colorSelection_;
}

SceneDepthFormatSelection SceneFramebuffer::DepthSelection() const noexcept {
    return depthSelection_;
}

} // namespace kb::render
