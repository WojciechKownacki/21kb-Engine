#pragma once

#include "kb/render/frame/RenderTargetDesc.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"

#include <bgfx/bgfx.h>

namespace kb::render {

struct SceneGridPassDesc {
    bgfx::ViewId viewId = 0;
    bgfx::FrameBufferHandle frameBuffer = BGFX_INVALID_HANDLE;
    RenderExtent extent{};
    RenderViewportRect outputRect{};
    const SceneRenderCamera* camera = nullptr;
    bgfx::TextureHandle sceneDepthTexture = BGFX_INVALID_HANDLE;
    float minorSpacingMeters = 1.0F;
    std::uint32_t majorEvery = 10U;

    [[nodiscard]] bool IsValid() const noexcept;
};

class SceneGridPass {
public:
    SceneGridPass() = default;
    ~SceneGridPass();

    SceneGridPass(const SceneGridPass&) = delete;
    SceneGridPass& operator=(const SceneGridPass&) = delete;

    [[nodiscard]] bool Initialize();
    void Shutdown() noexcept;
    [[nodiscard]] bool Submit(const SceneGridPassDesc& desc) const;
    [[nodiscard]] bool IsInitialized() const noexcept;

private:
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle cameraPosUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle basisRightUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle basisUpUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle basisForwardUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle gridParamsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle gridOriginUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle gridWidthsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle gridStyleUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sceneDepthSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle depthParamsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle viewProjectionUniform_ = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout fullscreenLayout_{};
    bool initialized_ = false;
};

} // namespace kb::render
