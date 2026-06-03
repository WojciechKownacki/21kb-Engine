#include "kb/render/resources/NativeWindowFramebuffer.hpp"

#include <limits>

namespace kb::render {
namespace {

[[nodiscard]] bool FitsBgfxSwapChainExtent(std::uint32_t value) noexcept {
    return value > 0U && value <= static_cast<std::uint32_t>(std::numeric_limits<std::uint16_t>::max());
}

} // namespace

NativeWindowFramebuffer::~NativeWindowFramebuffer() {
    Shutdown();
}

bool NativeWindowFramebufferDesc::IsValid() const noexcept {
    return nativeWindow != nullptr && FitsBgfxSwapChainExtent(width) && FitsBgfxSwapChainExtent(height) &&
           colorFormat != bgfx::TextureFormat::Count;
}

bool NativeWindowFramebuffer::Ensure(
    void* nativeWindow,
    std::uint32_t width,
    std::uint32_t height,
    bgfx::TextureFormat::Enum colorFormat,
    bgfx::TextureFormat::Enum depthFormat,
    bool flushBeforeRecreate) {
    if (nativeWindow == nullptr || !FitsBgfxSwapChainExtent(width) || !FitsBgfxSwapChainExtent(height)) {
        return false;
    }

    if (IsValid() && nativeWindow_ == nativeWindow && width_ == width && height_ == height &&
        colorFormat_ == colorFormat && depthFormat_ == depthFormat) {
        return true;
    }

    const bool replacingActiveSwapchain = IsValid();
    Shutdown();
    if (flushBeforeRecreate && replacingActiveSwapchain) {
        bgfx::frame();
    }

    frameBuffer_ = bgfx::createFrameBuffer(
        nativeWindow,
        static_cast<std::uint16_t>(width),
        static_cast<std::uint16_t>(height),
        colorFormat,
        depthFormat);

    if (!bgfx::isValid(frameBuffer_)) {
        return false;
    }

    nativeWindow_ = nativeWindow;
    colorFormat_ = colorFormat;
    depthFormat_ = depthFormat;
    width_ = width;
    height_ = height;
    return true;
}

bool NativeWindowFramebuffer::Ensure(const NativeWindowFramebufferDesc& desc) {
    if (!desc.IsValid()) {
        return false;
    }

    return Ensure(desc.nativeWindow, desc.width, desc.height, desc.colorFormat, desc.depthFormat, desc.flushBeforeRecreate);
}

void NativeWindowFramebuffer::Shutdown() noexcept {
    if (bgfx::isValid(frameBuffer_)) {
        bgfx::destroy(frameBuffer_);
        frameBuffer_ = BGFX_INVALID_HANDLE;
    }

    nativeWindow_ = nullptr;
    colorFormat_ = bgfx::TextureFormat::Count;
    depthFormat_ = bgfx::TextureFormat::Count;
    width_ = 0;
    height_ = 0;
}

bgfx::FrameBufferHandle NativeWindowFramebuffer::FrameBuffer() const noexcept {
    return frameBuffer_;
}

std::uint32_t NativeWindowFramebuffer::Width() const noexcept {
    return width_;
}

std::uint32_t NativeWindowFramebuffer::Height() const noexcept {
    return height_;
}

void* NativeWindowFramebuffer::NativeWindow() const noexcept {
    return nativeWindow_;
}

bgfx::TextureFormat::Enum NativeWindowFramebuffer::ColorFormat() const noexcept {
    return colorFormat_;
}

bgfx::TextureFormat::Enum NativeWindowFramebuffer::DepthFormat() const noexcept {
    return depthFormat_;
}

bool NativeWindowFramebuffer::IsValid() const noexcept {
    return bgfx::isValid(frameBuffer_);
}

} // namespace kb::render
