#pragma once

#include "kb/render/frame/RenderTargetDesc.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"
#include "kb/render/ViewIdPolicy.hpp"

#include <bgfx/bgfx.h>

#include <array>

namespace kb::render {

struct SceneGridPassDesc {
    bgfx::ViewId viewId = 0;
    bgfx::FrameBufferHandle frameBuffer = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle colorTexture = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle depthTexture = BGFX_INVALID_HANDLE;
    RenderExtent extent{};
    RenderViewportRect outputRect{};
    const SceneRenderCamera* camera = nullptr;
    float minorSpacingMeters = 1.0F;
    std::uint32_t majorEvery = 10U;
    bool buildDepthFrameBuffer = false;

    [[nodiscard]] bool IsValid() const noexcept;
};

class SceneGridPass {
public:
    // World-space height of the grid plane. The grid never writes depth, so TAA motion
    // vectors reproject depthless background pixels against this plane to give the grid
    // correct parallax velocity.
    static constexpr float kGridPlaneY = 0.0F;

    SceneGridPass() = default;
    ~SceneGridPass();

    SceneGridPass(const SceneGridPass&) = delete;
    SceneGridPass& operator=(const SceneGridPass&) = delete;

    [[nodiscard]] bool Initialize();
    void Shutdown() noexcept;
    void InvalidateFrameBuffers() noexcept;
    [[nodiscard]] bool Submit(const SceneGridPassDesc& desc) const;
    [[nodiscard]] bool IsInitialized() const noexcept;

private:
    struct DepthFrameBufferEntry {
        bgfx::FrameBufferHandle frameBuffer = BGFX_INVALID_HANDLE;
        bgfx::FrameBufferHandle sourceFrameBuffer = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle color = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle depth = BGFX_INVALID_HANDLE;
        RenderExtent extent{};
    };

    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle cameraPosUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle basisRightUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle basisUpUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle basisForwardUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle gridParamsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle gridOriginUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle gridWidthsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle gridStyleUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle depthParamsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle viewProjectionUniform_ = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout fullscreenLayout_{};
    // A frame can submit several editor viewports. Each bgfx view must retain its own attachment
    // wrapper until the frame is consumed; replacing one global framebuffer while assembling the
    // next viewport destroyed resources that the earlier grid submission still referenced.
    mutable std::array<DepthFrameBufferEntry, ViewId::Max> depthFrameBuffers_{};
    bool initialized_ = false;
};

} // namespace kb::render
