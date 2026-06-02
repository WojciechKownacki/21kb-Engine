#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>

namespace kb::render {

class NativeWindowFramebuffer {
public:
    NativeWindowFramebuffer() = default;
    ~NativeWindowFramebuffer();

    NativeWindowFramebuffer(const NativeWindowFramebuffer&) = delete;
    NativeWindowFramebuffer& operator=(const NativeWindowFramebuffer&) = delete;

    [[nodiscard]] bool Ensure(
        void* nativeWindow,
        std::uint32_t width,
        std::uint32_t height,
        bgfx::TextureFormat::Enum colorFormat = bgfx::TextureFormat::BGRA8,
        bgfx::TextureFormat::Enum depthFormat = bgfx::TextureFormat::D24S8);
    void Shutdown() noexcept;

    [[nodiscard]] bgfx::FrameBufferHandle FrameBuffer() const noexcept;
    [[nodiscard]] std::uint32_t Width() const noexcept;
    [[nodiscard]] std::uint32_t Height() const noexcept;
    [[nodiscard]] void* NativeWindow() const noexcept;
    [[nodiscard]] bool IsValid() const noexcept;

private:
    bgfx::FrameBufferHandle frameBuffer_ = BGFX_INVALID_HANDLE;
    void* nativeWindow_ = nullptr;
    bgfx::TextureFormat::Enum colorFormat_ = bgfx::TextureFormat::Count;
    bgfx::TextureFormat::Enum depthFormat_ = bgfx::TextureFormat::Count;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
};

} // namespace kb::render
