#pragma once

#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/scene/MeshPipeline.hpp"
#include "kb/render/scene/RenderScene.hpp"
#include "kb/render/scene/SceneGpuDrivenCullingPass.hpp"
#include "kb/render/scene/SceneRenderResourceMap.hpp"
#include "scene/submit/SceneMeshPassResources.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace kb::render {

class SceneMeshSubmitter {
public:
    [[nodiscard]] bool Initialize();
    void Shutdown();
    [[nodiscard]] SceneRenderSubmitStats Submit(
        bgfx::ViewId viewId,
        const RenderScene& renderScene,
        const RenderResourceRegistry& resources,
        const SceneRenderResourceMap& resourceMap,
        MeshPassType pass = MeshPassType::BaseOpaque,
        const SceneRenderCamera* camera = nullptr,
        SceneRenderDiagnostics* diagnostics = nullptr,
        SceneRenderDrawBudget drawBudget = {},
        SceneRenderLightingConfig lightingConfig = {},
        const SceneRenderShadowMapBinding* shadowMap = nullptr,
        std::span<const std::uint64_t> selectedEntityIds = {},
        SceneGpuDrivenFeatureSupport gpuDrivenSupport = {},
        std::array<float, 4> frameTime = {},
        std::array<float, 4> dynamicParameter = {},
        bgfx::TextureHandle sceneDepthTexture = BGFX_INVALID_HANDLE,
        bgfx::TextureHandle sceneColorTexture = BGFX_INVALID_HANDLE) const;
    [[nodiscard]] static SceneRenderSubmitStats ValidateResourcesInto(
        const RenderScene& renderScene,
        const RenderResourceRegistry& resources,
        const SceneRenderResourceMap& resourceMap,
        MeshPipelineBuildResult& pipelineScratch,
        MeshPassType pass = MeshPassType::BaseOpaque,
        const SceneRenderCamera* camera = nullptr,
        SceneRenderDiagnostics* diagnostics = nullptr,
        SceneRenderDrawBudget drawBudget = {},
        SceneRenderLightingConfig lightingConfig = {},
        std::span<const std::uint64_t> selectedEntityIds = {},
        SceneGpuDrivenFeatureSupport gpuDrivenSupport = {}) noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] MaterialProgramRegistryStats ProgramRegistryStats() const noexcept { return passResources_.ProgramRegistryStats(); }
    // Reset once per scene submit (before its passes) so the per-material GPU/fallback dedup accumulates
    // across all of a submit's passes; read after the submit for the true GPU-vs-fallback material counts.
    void ResetGraphMaterialDrawStats() const noexcept { passResources_.ResetGraphMaterialDrawStats(); }
    [[nodiscard]] SceneMeshGraphMaterialDrawStats GraphMaterialDrawStats() const noexcept { return passResources_.GraphMaterialDrawStats(); }
    void EndFrame(std::uint64_t frameIndex) const { passResources_.EndFrame(frameIndex); }
    void SetGraphShaderCacheRoot(std::string root) { passResources_.SetGraphShaderCacheRoot(std::move(root)); }

private:
    SceneMeshPassResources passResources_;
    SceneGpuDrivenCullingPass gpuDrivenCullingPass_;
    mutable SceneGpuDrivenFrameResources gpuDrivenFrameResources_;
    mutable MeshPipelineBuildResult pipelineScratch_;
    mutable std::vector<SceneRenderVisibilityBlocker> visibilityBlockerScratch_;
    // Derived renderer state only. The authored detail-switch policy remains in ECS.
    mutable const RenderScene* detailSwitchScene_ = nullptr;
};

} // namespace kb::render
