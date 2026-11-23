#pragma once
#include "scene/cache/SceneMeshCommandReuseGate.hpp"
#include "scene/cache/SceneMeshBatchCommandCache.hpp"

#include "engine/particles/ParticleRenderSnapshot.hpp"

#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/particles/ParticleGpuEmitterSimulation.hpp"
#include "kb/render/particles/ParticleMeshBatchBuilder.hpp"
#include "kb/render/particles/ParticleRenderBatcher.hpp"
#include "kb/render/resources/RenderSkinningPaletteAllocator.hpp"
#include "kb/render/scene/MeshPipeline.hpp"
#include "kb/render/scene/RenderScene.hpp"
#include "kb/render/scene/batch/SceneMeshBatchBuilder.hpp"
#include "kb/render/scene/SceneGpuDrivenCullingPass.hpp"
#include "kb/render/scene/SceneRenderResourceMap.hpp"
#include "scene/submit/SceneMeshPassResources.hpp"
#include "scene/submit/SceneMeshDrawCommandSubmitter.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <deque>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace kb::ecs {
class WorkerPool;
}

namespace kb::render {

class ParticleGpuRenderer;

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
        bgfx::TextureHandle sceneColorTexture = BGFX_INVALID_HANDLE,
        bool terrainLayersOnly = false,
        const SceneMotionVectorFrame* motionVectors = nullptr,
        ParticleGpuRenderer* particleRenderer = nullptr,
        const kb::particles::ParticleRenderSnapshot* particleSnapshot = nullptr,
        std::span<const ParticleGpuMeshDraw> gpuMeshDraws = {}) const;
    // Builds the commands of `pass` for each of `cameras` together, on the workers, ahead of the Submit calls that follow
    // for the same scene with the same budget and GPU-driven support: each takes the commands built for its camera
    // instead of building them. Does nothing for fewer than two cameras, while mesh particles join the pass (their
    // batches depend on the camera), or when the pass holds the same commands for every camera because the GPU culls
    // them; Submit then builds as usual. Submit finds the commands by the value of its camera.
    void PrebuildPasses(
        const RenderScene& renderScene,
        const RenderResourceRegistry& resources,
        const SceneRenderResourceMap& resourceMap,
        MeshPassType pass,
        std::span<const SceneRenderCamera* const> cameras,
        SceneRenderDrawBudget drawBudget,
        SceneGpuDrivenFeatureSupport gpuDrivenSupport,
        bool terrainLayersOnly = false) const;
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
        SceneGpuDrivenFeatureSupport gpuDrivenSupport = {},
        bool terrainLayersOnly = false) noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] bool GpuDrivenDispatchReady() const noexcept { return gpuDrivenCullingPass_.IsInitialized(); }
    [[nodiscard]] MaterialProgramRegistryStats ProgramRegistryStats() const noexcept { return passResources_.ProgramRegistryStats(); }
    // Reset once per scene submit (before its passes) so the per-material GPU/fallback dedup accumulates
    // across all of a submit's passes; read after the submit for the true GPU-vs-fallback material counts.
    void ResetGraphMaterialDrawStats() const noexcept { passResources_.ResetGraphMaterialDrawStats(); }
    [[nodiscard]] SceneMeshGraphMaterialDrawStats GraphMaterialDrawStats() const noexcept { return passResources_.GraphMaterialDrawStats(); }
    void EndFrame(std::uint64_t frameIndex) const {
        for (PrebuiltPass& prebuilt : prebuiltPasses_) prebuilt.ready = false;
        gpuDrivenCullingPass_.EndFrame();
        instanceBuffers_.EndFrame();
        passResources_.EndFrame(frameIndex);
    }
    void SetGraphShaderCacheRoot(std::string root) { passResources_.SetGraphShaderCacheRoot(std::move(root)); }
    [[nodiscard]] bool SubmitScreenMaterial(const SceneMeshScreenMaterialDesc& desc) const { return passResources_.SubmitScreenMaterial(desc); }
    void SetSkinningPaletteAllocator(const RenderSkinningPaletteAllocator* allocator) noexcept { skinningPaletteAllocator_ = allocator; }
    void SetCoverageProbe(SceneMeshCoverageProbe* probe) noexcept { coverageProbe_ = probe; }
    void SetWorkerPool(kb::ecs::WorkerPool* workerPool) noexcept { workerPool_ = workerPool; }

private:
    // The commands of one pass that PrebuildPasses built, until the Submit call for its camera takes them.
    struct PrebuiltPass {
        const RenderScene* scene = nullptr;
        MeshPassType pass = MeshPassType::BaseOpaque;
        SceneMeshCommandReuseKey key;
        MeshPipelineBuildResult result;
        SceneRenderDiagnostics diagnostics;
        bool ready = false;
    };

    [[nodiscard]] SceneGpuDrivenFeatureSupport EffectiveGpuSupport(SceneGpuDrivenFeatureSupport support) const noexcept;
    // The detail-switch levels shared by the passes of a scene start over with each scene.
    void BindDetailSwitchScene(const RenderScene& renderScene) const noexcept;
    [[nodiscard]] PrebuiltPass* FindPrebuiltPass(const RenderScene& renderScene, MeshPassType pass, const SceneMeshCommandReuseKey& key) const noexcept;

    SceneMeshPassResources passResources_;
    SceneGpuDrivenCullingPass gpuDrivenCullingPass_;
    mutable SceneMeshInstanceBufferPool instanceBuffers_;
    mutable MeshPipelineBuildResult pipelineScratch_;
    mutable MeshPipelineBuildResult gpuMeshScratch_;
    mutable std::vector<MeshDrawCommand> gpuMeshCommandScratch_;
    mutable std::vector<MeshDrawCommand> mergedCommandScratch_;
    mutable std::deque<PrebuiltPass> prebuiltPasses_;
    mutable std::vector<SceneMeshBatch> gpuMeshBatchScratch_;
    mutable std::vector<SceneRenderMeshInstance> gpuMeshInstanceScratch_;
    mutable std::array<std::vector<MeshDrawCommand>, kMeshPassTypeCount> passCommandScratch_;
    mutable std::array<SceneMeshCommandReuseGate, kMeshPassTypeCount> passCommandReuse_;
    mutable std::array<SceneMeshBatchCommandCache, kMeshPassTypeCount> passBatchCommandReuse_;
    mutable std::vector<SceneRenderVisibilityBlocker> visibilityBlockerScratch_;
    mutable std::vector<TransparentDrawOrderEntry> transparentSubmissionScratch_;
    mutable std::vector<SceneMeshBatch> meshBatchSubmissionScratch_;
    mutable ParticleMeshBatchBuilder particleMeshBatchBuilder_;
    // Derived renderer state only. The authored detail-switch policy remains in ECS.
    mutable const RenderScene* detailSwitchScene_ = nullptr;
    const RenderSkinningPaletteAllocator* skinningPaletteAllocator_ = nullptr;
    SceneMeshCoverageProbe* coverageProbe_ = nullptr;
    kb::ecs::WorkerPool* workerPool_ = nullptr;
};

} // namespace kb::render
