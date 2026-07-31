#pragma once

#include "kb/render/SceneGBuffer.hpp"
#include "kb/render/frame/FullscreenTexturePass.hpp"
#include "kb/render/frame/RenderSceneSubmitDesc.hpp"
#include "kb/render/scene/RenderScene.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"

#include <bgfx/bgfx.h>

#include <cstdint>

namespace kb::render {

struct SceneDeferredLightingPassDesc {
    bgfx::ViewId viewId = 0;
    bgfx::FrameBufferHandle frameBuffer = BGFX_INVALID_HANDLE;
    const SceneGBuffer* gbuffer = nullptr;
    const RenderScene* renderScene = nullptr;
    const SceneRenderCamera* camera = nullptr;
    SceneRenderLightingConfig lightingConfig{};
    RenderExtent extent{};
    std::uint32_t clearRgba = 0x000000FFU;
    const SceneRenderShadowMapBinding* shadowMap = nullptr;
    const SceneRenderWorldBackdrop* worldBackdrop = nullptr;
    bgfx::TextureHandle worldBackdropEnvironment = BGFX_INVALID_HANDLE;

    [[nodiscard]] bool IsValid() const noexcept;
};

class SceneDeferredLightingPass {
public:
    SceneDeferredLightingPass() = default;
    ~SceneDeferredLightingPass();

    SceneDeferredLightingPass(const SceneDeferredLightingPass&) = delete;
    SceneDeferredLightingPass& operator=(const SceneDeferredLightingPass&) = delete;

    [[nodiscard]] bool Initialize();
    void Shutdown() noexcept;
    [[nodiscard]] bool Submit(const SceneDeferredLightingPassDesc& desc, SceneRenderSubmitStats& stats) const;
    [[nodiscard]] bool IsInitialized() const noexcept;

private:
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    FullscreenTexturePass debugNormalPresentPass_{};
    bgfx::UniformHandle albedoSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle normalSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle materialSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle surfaceSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle depthSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle lightDirKindUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle lightPositionRangeUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle lightColorIntensityUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle lightSpotUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle lightParamsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle ambientColorUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle environmentZenithUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle environmentGroundUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle environmentParamsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle cameraPositionUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle inverseViewProjectionUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle depthParamsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle shadowMapSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle shadowViewProjUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle shadowParamsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle backdropHorizonUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle backdropZenithUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle backdropParamsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle backdropEnvironmentSampler_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle fallbackShadowTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle fallbackBackdropEnvironmentTexture_ = BGFX_INVALID_HANDLE;
};

} // namespace kb::render
