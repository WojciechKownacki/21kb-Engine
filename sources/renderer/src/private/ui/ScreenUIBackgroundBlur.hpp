#pragma once

#include "kb/render/frame/RenderTargetDesc.hpp"
#include "kb/render/frame/RenderViewportViewIds.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>

namespace kb::render {

class ScreenUIBackgroundBlur {
  public:
    ScreenUIBackgroundBlur() = default;
    ~ScreenUIBackgroundBlur();

    ScreenUIBackgroundBlur(const ScreenUIBackgroundBlur&) = delete;
    ScreenUIBackgroundBlur& operator=(const ScreenUIBackgroundBlur&) = delete;

    [[nodiscard]] bool Initialize();
    void Shutdown() noexcept;
    void InvalidateTargets() noexcept;
    [[nodiscard]] bool Submit(std::uint32_t viewportIndex, RenderExtent extent, bgfx::TextureFormat::Enum format,
                              bgfx::TextureHandle source, std::uint16_t horizontalView, std::uint16_t verticalView,
                              float radius);
    [[nodiscard]] bgfx::TextureHandle Output(std::uint32_t viewportIndex) const noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;

  private:
    struct TargetSet {
        bgfx::TextureHandle pingTexture = BGFX_INVALID_HANDLE;
        bgfx::FrameBufferHandle pingFrameBuffer = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle outputTexture = BGFX_INVALID_HANDLE;
        bgfx::FrameBufferHandle outputFrameBuffer = BGFX_INVALID_HANDLE;
        RenderExtent extent{};
        bgfx::TextureFormat::Enum format = bgfx::TextureFormat::Count;
    };

    [[nodiscard]] bool Ensure(std::uint32_t viewportIndex, RenderExtent extent, bgfx::TextureFormat::Enum format);
    static void Release(TargetSet& targets) noexcept;
    [[nodiscard]] bool SubmitPass(std::uint16_t viewId, RenderExtent extent, bgfx::FrameBufferHandle output,
                                  bgfx::TextureHandle source, float stepX, float stepY, const char* viewName) const;

    std::array<TargetSet, RenderViewportViewIdAllocator::kMaxViewportCount> targets_{};
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sourceSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle paramsUniform_ = BGFX_INVALID_HANDLE;
};

} // namespace kb::render
