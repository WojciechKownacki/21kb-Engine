#pragma once

#include "kb/render/MaterialProgramRegistry.hpp"
#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/scene/MeshPipeline.hpp"
#include "kb/render/scene/SceneRenderResourceMap.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace kb::render {

class SceneMeshSubmitter;
class RenderScene;

class SceneRenderer {
public:
    SceneRenderer();
    ~SceneRenderer();

    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    [[nodiscard]] bool Initialize();
    void Shutdown();
    void Submit(bgfx::ViewId viewId, const RenderScene& renderScene, std::uint32_t viewportWidth, std::uint32_t viewportHeight, const SceneRenderCamera* cameraOverride = nullptr, SceneRenderDrawBudget drawBudget = {}, SceneRenderLightingConfig lightingConfig = {}) const;
    void SubmitMeshPass(
        bgfx::ViewId viewId,
        MeshPassType pass,
        const RenderScene& renderScene,
        std::uint32_t viewportWidth,
        std::uint32_t viewportHeight,
        const SceneRenderCamera* cameraOverride = nullptr,
        SceneRenderDrawBudget drawBudget = {},
        SceneRenderLightingConfig lightingConfig = {},
        const SceneRenderShadowMapBinding* shadowMap = nullptr,
        std::span<const std::uint64_t> selectedEntityIds = {},
        const SceneGpuDrivenFeatureSupport* gpuDrivenSupportOverride = nullptr) const;
    void SetDefaultDrawBudget(SceneRenderDrawBudget drawBudget) noexcept;
    [[nodiscard]] SceneRenderDrawBudget DefaultDrawBudget() const noexcept;
    void SetDefaultLightingConfig(SceneRenderLightingConfig lightingConfig) noexcept;
    [[nodiscard]] SceneRenderLightingConfig DefaultLightingConfig() const noexcept;
    void SetGpuDrivenRuntimeSupport(SceneGpuDrivenFeatureSupport support) noexcept;
    [[nodiscard]] SceneGpuDrivenFeatureSupport GpuDrivenRuntimeSupport() const noexcept;
    void SetGraphShaderCacheRoot(std::string root);
    [[nodiscard]] const std::string& GraphShaderCacheRoot() const noexcept;
    // MAT-72: advance per-frame material time (seconds accumulate, frame index increments) and
    // expose it as the u_time vec4 (time, deltaTime, frameIndex, 0) bound during mesh submit.
    void AdvanceFrameTime(float deltaSeconds) noexcept;
    [[nodiscard]] std::array<float, 4> FrameTimeConstants() const noexcept;
    // MAT-30: graph DynamicParameter node source, bound as u_dynamicParameter for graph shaders.
    void SetDynamicParameter(std::array<float, 4> parameter) noexcept;
    [[nodiscard]] std::array<float, 4> DynamicParameterConstants() const noexcept;
    // MAT-80/#18b: the opaque scene depth texture the transparent pass binds to depth-sampling graph
    // materials (SceneDepth / DepthFade). Set by the renderer before submitting the scene.
    void SetSceneDepthTexture(bgfx::TextureHandle texture) noexcept;
    // MAT-31: opaque scene color snapshot bound to color-sampling graph materials (SceneColor / SceneTexture).
    void SetSceneColorTexture(bgfx::TextureHandle texture) noexcept;
    void TickFrame() noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] RenderResourceRegistry& Resources() noexcept;
    [[nodiscard]] const RenderResourceRegistry& Resources() const noexcept;
    [[nodiscard]] SceneRenderResourceMap& ResourceMap() noexcept;
    [[nodiscard]] const SceneRenderResourceMap& ResourceMap() const noexcept;
    [[nodiscard]] SceneRenderSubmitStats ValidateSceneResources(const RenderScene& renderScene) const noexcept;
    [[nodiscard]] SceneRenderSubmitStats ValidateSceneResources(const RenderScene& renderScene, MeshPassType pass) const noexcept;
    [[nodiscard]] SceneRenderDiagnostics ValidateSceneDiagnostics(const RenderScene& renderScene) const;
    [[nodiscard]] SceneRenderDiagnostics ValidateSceneDiagnostics(const RenderScene& renderScene, MeshPassType pass) const;
    [[nodiscard]] SceneRenderSubmitStats LastSubmitStats() const noexcept;
    [[nodiscard]] const SceneRenderDiagnostics& LastDiagnostics() const noexcept;
    [[nodiscard]] MaterialProgramRegistryStats MaterialProgramStats() const noexcept;

private:
    RenderResourceRegistry resources_;
    SceneRenderResourceMap resourceMap_;
    std::unique_ptr<SceneMeshSubmitter> meshSubmitter_;
    mutable MeshPipelineBuildResult validationPipelineScratch_;
    mutable SceneRenderSubmitStats lastSubmitStats_{};
    mutable SceneRenderDiagnostics lastDiagnostics_{};
    SceneRenderDrawBudget defaultDrawBudget_{};
    SceneRenderLightingConfig defaultLightingConfig_{};
    SceneGpuDrivenFeatureSupport gpuDrivenRuntimeSupport_{};
    std::string graphShaderCacheRoot_;
    bgfx::TextureHandle sceneDepthTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle sceneColorTexture_ = BGFX_INVALID_HANDLE;
    float frameTimeSeconds_ = 0.0F;
    float frameDeltaSeconds_ = 0.0F;
    std::uint32_t frameTimeIndex_ = 0U;
    std::array<float, 4> dynamicParameter_{};
    bool initialized_ = false;
};

} // namespace kb::render
