#pragma once

#include "kb/render/frame/RenderSceneSubmitDesc.hpp"
#include "kb/render/frame/RenderViewportViewIds.hpp"
#include "kb/render/post/SceneDisplayCompositeRenderer.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>

namespace kb::render {

struct ScenePostProcessSettings {
    enum class AutoExposureMeteringMode : std::uint8_t {
        HdrColor,
        SceneLighting,
        Manual,
    };

    bool bloomEnabled = true;
    float bloomStrength = 0.05F;
    float bloomThreshold = 1.0F;
    float bloomSoftKnee = 0.5F;
    float bloomRadiusPixels = 1.5F;
    bool temporalAntiAliasingEnabled = true;
    bool temporalJitterEnabled = true;
    float temporalHistoryBlend = 0.08F;
    bool tonemapEnabled = true;
    AutoExposureMeteringMode autoExposureMetering = AutoExposureMeteringMode::HdrColor;
    SceneDisplayOutputTransform outputTransform{
        .autoExposure = FullscreenTextureAutoExposureSettings{
            .enabled = true,
        },
    };
};

struct SceneTemporalReprojectionDesc {
    bgfx::TextureHandle depthTexture = BGFX_INVALID_HANDLE;
    std::array<float, 16> currentViewProjection{};
    std::array<float, 16> inverseCurrentViewProjection{};
    std::array<float, 16> previousViewProjection{};
    std::array<float, 4> jitterAndParams{};
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
