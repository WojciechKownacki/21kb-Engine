#pragma once

#include "kb/render/frame/RenderSceneSubmitDesc.hpp"
#include "kb/render/frame/RenderViewportViewIds.hpp"
#include "kb/render/post/SceneDisplayCompositeRenderer.hpp"
#include "kb/render/post/ScenePostProcessSettings.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>

namespace kb::render {

struct SceneTemporalReprojectionDesc {
    bgfx::TextureHandle depthTexture = BGFX_INVALID_HANDLE;
    std::array<float, 16> currentViewProjection{};
    std::array<float, 16> inverseCurrentViewProjection{};
    std::array<float, 16> previousViewProjection{};
    std::array<float, 4> jitterAndParams{};
    std::array<float, 4> historyJitterParams{};
    // x: reproject depthless background against the editor grid plane, y: plane world height.
    std::array<float, 4> backgroundPlaneParams{};
    bool historyValid = false;

    [[nodiscard]] bool IsValid() const noexcept;
};

struct ScenePostProcessSubmitDesc {
    bgfx::TextureHandle sceneColor = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle sceneDepth = BGFX_INVALID_HANDLE;
    RenderPostProcessTargetBinding target{};
    RenderViewportViewIds viewIds{};
    ScenePostProcessSettings settings{};
    SceneTemporalReprojectionDesc temporal{};

    [[nodiscard]] bool IsValid() const noexcept;
};

class ScenePostProcessRenderer {
public:
    ScenePostProcessRenderer() = default;
    ~ScenePostProcessRenderer();

    ScenePostProcessRenderer(const ScenePostProcessRenderer&) = delete;
    ScenePostProcessRenderer& operator=(const ScenePostProcessRenderer&) = delete;

    [[nodiscard]] bool Initialize();
    void Shutdown() noexcept;
    [[nodiscard]] bgfx::TextureHandle Submit(const ScenePostProcessSubmitDesc& desc) const;
    [[nodiscard]] bool IsInitialized() const noexcept;

private:
    class Submitter;

    void DestroyPrograms() noexcept;

    bgfx::ProgramHandle prefilterProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle blurProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle combineProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle motionVectorsProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle taaResolveProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle fxaaProgram_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sourceSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle bloomSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle depthSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle historySampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle velocitySampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle postParams_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle inverseViewProjectionUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle previousViewProjectionUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle temporalParamsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout fullscreenLayout_{};
    bgfx::VertexBufferHandle fullscreenVertexBuffer_ = BGFX_INVALID_HANDLE;
};

} // namespace kb::render
