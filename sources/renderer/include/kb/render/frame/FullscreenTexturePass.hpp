#pragma once

#include "kb/render/frame/RenderTargetDesc.hpp"

#include <bgfx/bgfx.h>

#include <cstdint>

namespace kb::render {

enum class FullscreenTextureTonemapOperator : std::uint8_t {
    Aces,
    AgxApprox,
};

struct FullscreenTextureOutputTransform {
    float exposureStops = 0.0F;
    float gamma = 2.2F;
    FullscreenTextureTonemapOperator tonemap = FullscreenTextureTonemapOperator::Aces;
    float colorGradingLutStrength = 0.0F;
};

struct FullscreenTexturePassDesc {
    bgfx::ViewId viewId = 0;
    bgfx::TextureHandle sourceTexture = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle frameBuffer = BGFX_INVALID_HANDLE;
    RenderExtent extent{};
    FullscreenTextureOutputTransform outputTransform{};
    std::uint32_t clearRgba = 0x000000FFU;
    bool clearTarget = false;
    const char* viewName = "KB Fullscreen Texture";

    [[nodiscard]] bool IsValid() const noexcept;
};

class FullscreenTexturePass {
public:
    FullscreenTexturePass() = default;
    ~FullscreenTexturePass();

    FullscreenTexturePass(const FullscreenTexturePass&) = delete;
    FullscreenTexturePass& operator=(const FullscreenTexturePass&) = delete;

    [[nodiscard]] bool Initialize();
    void Shutdown() noexcept;
    [[nodiscard]] bool Submit(const FullscreenTexturePassDesc& desc) const;
    [[nodiscard]] bool IsInitialized() const noexcept;

private:
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle colorSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle colorGradeSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle tonemapParams_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle colorGradeParams_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle neutralColorGradeLut_ = BGFX_INVALID_HANDLE;
};

} // namespace kb::render
