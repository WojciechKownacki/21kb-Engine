#pragma once

#include "kb/render/SceneRenderTargetFormat.hpp"
#include "kb/render/SceneGBufferContract.hpp"
#include "kb/render/frame/RenderTargetDesc.hpp"

#include <bgfx/bgfx.h>

#include <cstdint>

namespace kb::render {

struct SceneGBufferDesc {
    RenderExtent extent{};

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return extent.IsValid();
    }
};

struct SceneGBufferFormatSelection {
    bgfx::TextureFormat::Enum albedoFormat = bgfx::TextureFormat::Count;
    bgfx::TextureFormat::Enum normalFormat = bgfx::TextureFormat::Count;
    bgfx::TextureFormat::Enum materialFormat = bgfx::TextureFormat::Count;
    bgfx::TextureFormat::Enum surfaceFormat = bgfx::TextureFormat::Count;
    SceneDepthFormatSelection depth{};
    SceneTargetFormatSelectionStatus status = SceneTargetFormatSelectionStatus::Unsupported;

    [[nodiscard]] bool IsSupported() const noexcept;
};

class SceneGBuffer {
public:
    SceneGBuffer() = default;
    ~SceneGBuffer();

    SceneGBuffer(const SceneGBuffer&) = delete;
    SceneGBuffer& operator=(const SceneGBuffer&) = delete;

    [[nodiscard]] bool Ensure(const SceneGBufferDesc& desc);
    [[nodiscard]] bool Ensure(std::uint32_t width, std::uint32_t height);
    void Shutdown();

    [[nodiscard]] bgfx::FrameBufferHandle FrameBuffer() const noexcept;
    [[nodiscard]] bgfx::TextureHandle AlbedoTexture() const noexcept;
    [[nodiscard]] bgfx::TextureHandle NormalTexture() const noexcept;
    [[nodiscard]] bgfx::TextureHandle MaterialTexture() const noexcept;
    [[nodiscard]] bgfx::TextureHandle SurfaceTexture() const noexcept;
    [[nodiscard]] bgfx::TextureHandle DepthTexture() const noexcept;
    [[nodiscard]] RenderExtent Extent() const noexcept;
    [[nodiscard]] std::uint32_t Width() const noexcept;
    [[nodiscard]] std::uint32_t Height() const noexcept;
    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] SceneGBufferFormatSelection FormatSelection() const noexcept;
    [[nodiscard]] RenderTargetDesc AlbedoTargetDesc() const noexcept;
    [[nodiscard]] RenderTargetDesc NormalTargetDesc() const noexcept;
    [[nodiscard]] RenderTargetDesc MaterialTargetDesc() const noexcept;
    [[nodiscard]] RenderTargetDesc SurfaceTargetDesc() const noexcept;
    [[nodiscard]] RenderTargetDesc DepthTargetDesc() const noexcept;

private:
    bgfx::TextureHandle albedoTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle normalTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle materialTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle surfaceTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle depthTexture_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle frameBuffer_ = BGFX_INVALID_HANDLE;
    SceneGBufferFormatSelection selection_{};
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
};

[[nodiscard]] SceneGBufferFormatSelection SelectSceneGBufferFormats(std::uint64_t colorTextureFlags, std::uint64_t depthTextureFlags) noexcept;

} // namespace kb::render
