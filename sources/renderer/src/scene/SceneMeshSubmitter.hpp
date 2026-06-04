#pragma once

#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/scene/MeshPipeline.hpp"
#include "kb/render/scene/RenderScene.hpp"
#include "kb/render/scene/SceneGpuDrivenCullingPass.hpp"
#include "kb/render/scene/SceneRenderResourceMap.hpp"
#include "scene/submit/SceneMeshPassResources.hpp"

#include <bgfx/bgfx.h>

#include <span>
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
        SceneGpuDrivenFeatureSupport gpuDrivenSupport = {}) const;
    [[nodiscard]] static SceneRenderSubmitStats ValidateResourcesInto(
        const RenderScene& renderScene,
        const RenderResourceRegistry& resources,
        const SceneRenderResourceMap& resourceMap,
        std::vector<SceneRenderDrawGroup>& drawGroupsScratch,
        MeshPipelineBuildResult& pipelineScratch,
        MeshPassType pass = MeshPassType::BaseOpaque,
        const SceneRenderCamera* camera = nullptr,
        SceneRenderDiagnostics* diagnostics = nullptr,
        SceneRenderDrawBudget drawBudget = {},
        SceneRenderLightingConfig lightingConfig = {},
        std::span<const std::uint64_t> selectedEntityIds = {},
        SceneGpuDrivenFeatureSupport gpuDrivenSupport = {}) noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;

private:
    SceneMeshPassResources passResources_;
    SceneGpuDrivenCullingPass gpuDrivenCullingPass_;
    mutable SceneGpuDrivenFrameResources gpuDrivenFrameResources_;
    mutable std::vector<SceneRenderDrawGroup> drawGroupsScratch_;
    mutable MeshPipelineBuildResult pipelineScratch_;
};

} // namespace kb::render
