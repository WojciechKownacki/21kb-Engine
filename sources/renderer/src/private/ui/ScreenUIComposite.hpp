#pragma once

#include "private/ui/ScreenUIDrawData.hpp"

#include "kb/render/frame/FullscreenTexturePass.hpp"
#include "kb/render/frame/RenderTargetDesc.hpp"

#include <bgfx/bgfx.h>

#include <cstdint>
#include <span>

namespace kb::render {

struct ScreenUIResolvedTexture {
    ScreenUITextureKey key{};
    bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
    std::uint16_t width = 0U;
    std::uint16_t height = 0U;
};

struct ScreenUICompositeDesc {
    std::uint16_t viewId = 0U;
    bgfx::FrameBufferHandle frameBuffer = BGFX_INVALID_HANDLE;
    RenderExtent presentationExtent{};
    RenderExtent outputExtent{};
    RenderViewportRect outputRect{};
    FullscreenTextureOutputTransform outputTransform{};
    const ScreenUIDrawList* drawList = nullptr;
    std::span<const ScreenUIResolvedTexture> textures;

    [[nodiscard]] bool IsValid() const noexcept;
};

class ScreenUIComposite {
  public:
    ScreenUIComposite() = default;
    ~ScreenUIComposite();

    ScreenUIComposite(const ScreenUIComposite&) = delete;
    ScreenUIComposite& operator=(const ScreenUIComposite&) = delete;

    [[nodiscard]] bool Initialize();
    void Shutdown() noexcept;
    [[nodiscard]] bool Submit(const ScreenUICompositeDesc& desc) const;
    [[nodiscard]] bool IsInitialized() const noexcept;

  private:
    [[nodiscard]] static const ScreenUIResolvedTexture* Resolve(std::span<const ScreenUIResolvedTexture> textures,
                                                                 ScreenUITextureKey key) noexcept;

    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle whiteTexture_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle viewportUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle fillColorUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle borderColorUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle borderWidthsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle cornerRadiiUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle clipRectUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle styleParamsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle effectParamsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle tonemapParamsUniform_ = BGFX_INVALID_HANDLE;
};

} // namespace kb::render
