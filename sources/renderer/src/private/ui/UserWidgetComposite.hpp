#pragma once

#include "private/ui/UserWidgetDrawData.hpp"

#include "kb/render/frame/FullscreenTexturePass.hpp"
#include "kb/render/frame/RenderTargetDesc.hpp"

#include <bgfx/bgfx.h>

#include <cstdint>
#include <span>

namespace kb::render {

struct UserWidgetResolvedTexture {
    UserWidgetTextureKey key{};
    bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
    std::uint16_t width = 0U;
    std::uint16_t height = 0U;
};

struct UserWidgetCompositeDesc {
    std::uint16_t viewId = 0U;
    bgfx::FrameBufferHandle frameBuffer = BGFX_INVALID_HANDLE;
    RenderExtent presentationExtent{};
    RenderExtent outputExtent{};
    RenderViewportRect outputRect{};
    FullscreenTextureOutputTransform outputTransform{};
    const UserWidgetDrawList* drawList = nullptr;
    std::span<const UserWidgetResolvedTexture> textures;

    [[nodiscard]] bool IsValid() const noexcept;
};

class UserWidgetComposite {
  public:
    UserWidgetComposite() = default;
    ~UserWidgetComposite();

    UserWidgetComposite(const UserWidgetComposite&) = delete;
    UserWidgetComposite& operator=(const UserWidgetComposite&) = delete;

    [[nodiscard]] bool Initialize();
    void Shutdown() noexcept;
    [[nodiscard]] bool Submit(const UserWidgetCompositeDesc& desc) const;
    [[nodiscard]] bool IsInitialized() const noexcept;

  private:
    [[nodiscard]] const UserWidgetResolvedTexture* Resolve(std::span<const UserWidgetResolvedTexture> textures,
                                                           UserWidgetTextureKey key) const noexcept;

    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle whiteTexture_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle viewportUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle fillColorUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle borderColorUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle borderWidthsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle cornerRadiiUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle styleParamsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle effectParamsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle tonemapParamsUniform_ = BGFX_INVALID_HANDLE;
};

} // namespace kb::render
