#pragma once

#include "kb/render/DisplayConfig.hpp"
#include "kb/render/MaterialProgramRegistry.hpp"
#include "kb/render/RendererCapabilityReport.hpp"
#include "kb/render/SceneRenderTarget.hpp"
#include "kb/render/SceneDeferredLightingPass.hpp"
#include "kb/render/SceneDepthPolicy.hpp"
#include "kb/render/SceneGBuffer.hpp"
#include "kb/render/frame/EditorRenderPassSubmitter.hpp"
#include "kb/render/frame/FinalCompositePass.hpp"
#include "kb/render/frame/RenderFramePipeline.hpp"
#include "kb/render/frame/RenderFrameState.hpp"
#include "kb/render/frame/RenderSceneSubmitDesc.hpp"
#include "kb/render/post/PostProcessChain.hpp"
#include "kb/render/post/SceneExposureMeter.hpp"
#include "kb/render/post/ScenePostProcessRenderer.hpp"
#include "kb/render/post/ScenePostProcessTargets.hpp"
#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/runtime/RuntimeFrameResourceReferences.hpp"
#include "kb/render/runtime/RuntimeMaterialResolver.hpp"
#include "kb/render/runtime/RuntimeRenderAssetDiscovery.hpp"
#include "kb/render/runtime/RuntimeRenderResourceCache.hpp"
#include "kb/render/scene/SceneRenderResourceMap.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"
#include "kb/render/scene/RenderSceneStore.hpp"
#include "kb/render/shadow/ShadowMapResource.hpp"

#include "engine/scene/SceneRenderFeedback.hpp"

#include <cstdint>
#include <array>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace kb::render {

class BgfxContext;
class EcsRenderSceneSynchronizer;
class RenderScene;
class RenderSurface;
class RendererScreenCapture;
class SceneParticleRenderSynchronizer;
class SceneRenderer;

} // namespace kb::render

namespace kb::scene {
class Scene;
}

namespace kb::ecs {
class WorkerPool;
}

namespace kb::render {

class Renderer {
public:
    static constexpr std::uint64_t kRuntimeAssetRetentionFrames = 120ULL;
    static constexpr std::uint64_t kRuntimeAssetDiscoveryIntervalFrames = 30ULL;

    struct RuntimeSceneResourceStats {
        std::uint32_t cachedMeshCount = 0;
        std::uint32_t cachedMaterialCount = 0;
        std::uint32_t cachedTextureCount = 0;
        std::uint32_t cachedMeshCapacity = 0;
        std::uint32_t cachedMaterialCapacity = 0;
        std::uint32_t cachedTextureCapacity = 0;
        std::uint32_t referencedMeshAssetCount = 0;
        std::uint32_t referencedMaterialAssetCount = 0;
        std::uint32_t referencedTextureAssetCount = 0;
        std::uint32_t unresolvedMaterialTexturePathCount = 0;
        std::uint32_t defaultMaterialFallbackCount = 0;
        std::uint32_t errorMaterialFallbackCount = 0;
        std::uint32_t materialLoadedCount = 0;
        std::uint32_t materialFallbackCount = 0;
        std::uint32_t materialErrorCount = 0;
        std::uint32_t materialReloadCount = 0;
        std::uint32_t materialResolverDiagnosticCount = 0;
        std::uint32_t graphMaterialCpuFallbackCount = 0;
        std::uint32_t graphMaterialGpuCount = 0;
        std::uint32_t referencedMeshAssetCapacity = 0;
        std::uint32_t referencedMaterialAssetCapacity = 0;
        std::uint32_t referencedTextureAssetCapacity = 0;
        std::uint32_t scenePassSubmitStatsCapacity = 0;
        std::uint32_t registeredRuntimeAssetLoaderSceneCount = 0;
        std::uint32_t runtimeAssetDiscoverySceneCount = 0;
        std::uint32_t runtimeAssetDiscoverySceneCapacity = 0;
        std::uint32_t renderSceneCount = 0;
        std::uint32_t renderSceneCapacity = 0;
        std::uint32_t renderSceneMeshProxyCount = 0;
        std::uint32_t renderSceneCameraProxyCount = 0;
        std::uint32_t renderSceneLightProxyCount = 0;
        std::uint32_t renderSceneMeshProxyCapacity = 0;
        std::uint32_t renderSceneCameraProxyCapacity = 0;
        std::uint32_t renderSceneLightProxyCapacity = 0;
        std::uint32_t renderSceneDrawGroupLookupCapacity = 0;
        std::uint32_t meshResourceSlotCapacity = 0;
        std::uint32_t materialResourceSlotCapacity = 0;
        std::uint32_t textureResourceSlotCapacity = 0;
        std::uint32_t meshBindingCapacity = 0;
        std::uint32_t materialBindingCapacity = 0;
        std::uint32_t textureBindingCapacity = 0;
        std::uint32_t shadowMapSize = 0;
        std::uint64_t shadowMapAllocationBytes = 0;
        bool shadowMapAllocated = false;
        std::uint32_t syncMeshSeenCount = 0;
        std::uint32_t syncCameraSeenCount = 0;
        std::uint32_t syncLightSeenCount = 0;
        std::uint32_t syncMeshSeenCapacity = 0;
        std::uint32_t syncCameraSeenCapacity = 0;
        std::uint32_t syncLightSeenCapacity = 0;
        std::uint32_t syncTransformCacheCount = 0;
        std::uint32_t syncTransformResolvingCount = 0;
        std::uint32_t syncTransformCacheCapacity = 0;
        std::uint32_t syncTransformResolvingCapacity = 0;
        std::uint32_t defaultForwardLightCapacity = kMaxSceneForwardLights;
        std::uint32_t defaultEnvironmentLightingMode = static_cast<std::uint32_t>(SceneRenderEnvironmentMode::Constant) + 1U;
        std::uint32_t defaultEnvironmentLightingSampleCount = 1U;
        std::uint32_t defaultShadowFilterSampleCount = 9U;
        std::uint64_t retentionFrames = kRuntimeAssetRetentionFrames;
        std::uint64_t assetDiscoveryIntervalFrames = kRuntimeAssetDiscoveryIntervalFrames;
    };

    struct RuntimeSceneResourceReserveDesc {
        std::uint32_t sceneCount = 0;
        std::uint32_t cachedMeshes = 0;
        std::uint32_t cachedMaterials = 0;
        std::uint32_t cachedTextures = 0;
        std::uint32_t frameReferencedMeshes = 0;
        std::uint32_t frameReferencedMaterials = 0;
        std::uint32_t frameReferencedTextures = 0;
        std::uint32_t scenePassSubmitStats = 0;
        std::uint32_t renderSceneMeshProxies = 0;
        std::uint32_t renderSceneCameraProxies = 0;
        std::uint32_t renderSceneLightProxies = 0;
        std::uint32_t renderSceneDrawGroupKeys = 0;
        std::uint32_t meshResourceSlots = 0;
        std::uint32_t materialResourceSlots = 0;
        std::uint32_t textureResourceSlots = 0;
        std::uint32_t meshBindings = 0;
        std::uint32_t materialBindings = 0;
        std::uint32_t textureBindings = 0;
        std::uint32_t syncMeshProxies = 0;
        std::uint32_t syncCameraProxies = 0;
        std::uint32_t syncLightProxies = 0;
        std::uint32_t syncTransformCacheEntries = 0;
        std::uint32_t syncTransformResolvingEntries = 0;
    };

    Renderer();
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    [[nodiscard]] bool Initialize(RenderSurface& surface, const DisplayConfig* config = nullptr);
    void Shutdown();

    [[nodiscard]] bool BeginFrame();
    void EndFrame();
    void SubmitClear(std::uint32_t rgba, float depth = SceneDepthPolicy::ClearDepth(), std::uint8_t stencil = 0);
    void SubmitScene(const kb::scene::Scene& scene);
    [[nodiscard]] bool SubmitScene(const kb::scene::Scene& scene, const RenderSceneSubmitDesc& desc);
    struct SceneFrameSubmission {
        const kb::scene::Scene* scene = nullptr;
        RenderSceneSubmitDesc desc{};

        [[nodiscard]] bool IsValid() const noexcept {
            return scene != nullptr && desc.IsValid();
        }
    };
    [[nodiscard]] bool SubmitScenes(std::span<const SceneFrameSubmission> submissions);
    void OnResize(std::uint32_t width, std::uint32_t height);

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] bool IsFrameActive() const noexcept;
    [[nodiscard]] std::uint32_t BackbufferWidth() const noexcept;
    [[nodiscard]] std::uint32_t BackbufferHeight() const noexcept;
    [[nodiscard]] void* NativeWindowHandle() const noexcept;
    [[nodiscard]] const RendererCapabilityReport& CapabilityReport() const noexcept;
    [[nodiscard]] std::uint32_t LastCompletedFrame() const noexcept;
    [[nodiscard]] RenderResourceRegistry* SceneResources() noexcept;
    [[nodiscard]] const RenderResourceRegistry* SceneResources() const noexcept;
    [[nodiscard]] SceneRenderResourceMap* SceneResourceMap() noexcept;
    [[nodiscard]] const SceneRenderResourceMap* SceneResourceMap() const noexcept;
    // Lets a caller carry the auto-exposure meter's adapted luminance across a full Shutdown()+
    // Initialize() cycle (e.g. an editor-side MSAA/backend switch) that has nothing to do with the
    // scene's actual brightness, so the image doesn't visibly re-expose from scratch on every such
    // switch. HasExposureHistory() reports whether there's an adapted value worth carrying over.
    [[nodiscard]] float CurrentExposureLuminance() const noexcept;
    [[nodiscard]] bool HasExposureHistory() const noexcept;
    void PrimeExposureAdaptation(float luminance) noexcept;
    [[nodiscard]] SceneRenderSubmitStats LastSceneSubmitStats() const noexcept;
    [[nodiscard]] std::span<const SceneRenderPassSubmitStats> LastScenePassSubmitStats() const noexcept;
    [[nodiscard]] std::span<const SceneRenderExposureSubmitStats> LastSceneExposureStats() const noexcept;
    [[nodiscard]] std::span<const std::string> LastAaPipelineTraceLines() const noexcept;
    [[nodiscard]] const SceneRenderDiagnostics& LastSceneDiagnostics() const noexcept;
    // LIB-142: the fully-resolved post-process settings (the caller's own per-submit desc
    // override if it supplied one, otherwise the scene's own asset-based active
    // PostProcessProfile if resolvable, otherwise nullopt) from the most recent
    // SubmitScene(s) call - a diagnostic/test accessor mirroring LastSceneDiagnostics' own
    // shape. Computed unconditionally (unlike the GPU-facing PostProcessChain evaluation,
    // which only runs when RenderSceneSubmitDesc::finalComposite.enabled is set), so it stays
    // observable even for a minimal offscreen-only submission.
    [[nodiscard]] const std::optional<ScenePostProcessSettings>& LastResolvedPostProcessSettings() const noexcept;
    [[nodiscard]] RuntimeSceneResourceStats RuntimeResourceStats() const noexcept;
    [[nodiscard]] MaterialProgramRegistryStats MaterialProgramStats() const noexcept;
    void ReserveRuntimeSceneResources(const RuntimeSceneResourceReserveDesc& desc);
    void SetDefaultSceneDrawBudget(SceneRenderDrawBudget drawBudget) noexcept;
    [[nodiscard]] SceneRenderDrawBudget DefaultSceneDrawBudget() const noexcept;
    void SetDefaultSceneLightingConfig(SceneRenderLightingConfig lightingConfig) noexcept;
    [[nodiscard]] SceneRenderLightingConfig DefaultSceneLightingConfig() const noexcept;
    void SetGpuDrivenRuntimeDispatchEnabled(bool enabled) noexcept;
    [[nodiscard]] bool GpuDrivenRuntimeDispatchEnabled() const noexcept;
    void SetGraphShaderCacheRoot(std::string root);
    [[nodiscard]] const std::string& GraphShaderCacheRoot() const noexcept;
    // MAT-72: seconds advanced per completed frame; drives material u_time (Time/animation nodes).
    void SetFrameDeltaSeconds(float seconds) noexcept;
    [[nodiscard]] float FrameDeltaSeconds() const noexcept;
    void SetDefaultPostProcessSettings(ScenePostProcessSettings settings) noexcept;
    [[nodiscard]] ScenePostProcessSettings DefaultPostProcessSettings() const noexcept;
    [[nodiscard]] bool ConfigurePostProcessChain(const PostProcessChainDesc& desc);
    [[nodiscard]] bool AddPostProcessPass(PostProcessPass pass);
    [[nodiscard]] bool InsertPostProcessPass(std::uint32_t index, PostProcessPass pass);
    [[nodiscard]] bool RemovePostProcessPass(PostProcessPassKind kind) noexcept;
    [[nodiscard]] bool SetPostProcessPass(PostProcessPass pass);
    [[nodiscard]] bool SetPostProcessPassEnabled(PostProcessPassKind kind, bool enabled) noexcept;
    [[nodiscard]] std::optional<PostProcessPass> FindPostProcessPass(PostProcessPassKind kind) const noexcept;
    [[nodiscard]] std::span<const PostProcessPass> PostProcessPasses() const noexcept;
    void SetRuntimeAssetDiscoveryIntervalFrames(std::uint64_t frameInterval) noexcept;
    [[nodiscard]] std::uint64_t RuntimeAssetDiscoveryIntervalFrames() const noexcept;
    void SetRuntimeAssetDiscoveryEnabled(bool enabled) noexcept;
    [[nodiscard]] bool RuntimeAssetDiscoveryEnabled() const noexcept;
    void ReleaseScene(const kb::scene::Scene& scene) noexcept;
    void ReleaseAllScenes() noexcept;

private:
    [[nodiscard]] bool SubmitSceneToViewport(const kb::scene::Scene& scene, const RenderSceneSubmitDesc& desc, const RenderViewportPlan& viewportPlan);
    [[nodiscard]] RenderScene& RenderSceneFor(const kb::scene::Scene& scene);
    void ApplyRuntimeSceneResourceReserve();
    struct TemporalViewportState {
        RenderViewportId viewportId{};
        std::uint32_t viewportIndex = 0;
        RenderExtent extent{};
        std::array<float, 16> previousViewProjection{};
        std::array<float, 2> previousJitter{};
        bool hasHistory = false;
    };
    [[nodiscard]] TemporalViewportState& TemporalStateFor(RenderViewportId viewportId, std::uint32_t viewportIndex);
    std::unique_ptr<BgfxContext> context_;
    std::unique_ptr<EcsRenderSceneSynchronizer> renderSceneSynchronizer_;
    std::unique_ptr<SceneParticleRenderSynchronizer> particleRenderSynchronizer_;
    // Lazily created worker pool that parallelizes the columnar render-sync (H6).
    std::unique_ptr<kb::ecs::WorkerPool> renderSyncWorkerPool_;
    RenderSceneStore renderSceneStore_;
    std::unique_ptr<SceneRenderer> sceneRenderer_;
    std::unique_ptr<ScenePostProcessRenderer> scenePostProcessRenderer_;
    std::unique_ptr<FinalCompositePass> finalCompositePass_;
    std::unique_ptr<SceneDeferredLightingPass> deferredLightingPass_;
    SceneRenderTarget defaultSceneTarget_;
    SceneGBuffer defaultSceneGBuffer_;
    ScenePostProcessTargets defaultPostProcessTargets_;
    ShadowMapResource defaultShadowMap_;
    RenderFramePipeline framePipeline_;
    RenderFrameState frameState_;
    EditorRenderPassSubmitter editorPassSubmitter_;
    PostProcessChain postProcessChain_;
    SceneRenderSubmitStats lastSceneSubmitStats_{};
    std::vector<SceneRenderPassSubmitStats> lastScenePassSubmitStats_;
    std::vector<SceneRenderExposureSubmitStats> lastSceneExposureStats_;
    std::vector<std::string> lastAaPipelineTraceLines_;
    SceneRenderDiagnostics lastSceneDiagnostics_{};
    std::optional<ScenePostProcessSettings> lastResolvedPostProcessSettings_{};
    // LIB-144: scratch frame reused across every SubmitSceneToViewport - Publish swaps its
    // entries vector with the scene's stored frame, so both sides keep their capacity and
    // the steady state allocates nothing per frame.
    kb::scene::SceneRenderVisibilityFrame sceneRenderVisibilityScratch_{};
    // LIB-145: the async screen-capture controller (frame-gated blit+readTexture+PNG, see
    // RendererScreenCapture.hpp).
    std::unique_ptr<RendererScreenCapture> screenCapture_;
    RuntimeRenderResourceCache runtimeResourceCache_;
    RuntimeFrameResourceReferences frameReferences_;
    RuntimeRenderAssetDiscovery runtimeAssetDiscovery_;
    RuntimeMaterialResolver runtimeMaterialResolver_;
    SceneExposureMeter sceneExposureMeter_;
    RuntimeSceneResourceReserveDesc runtimeSceneResourceReserveDesc_{};
    SceneRenderDrawBudget defaultSceneDrawBudget_{};
    SceneRenderLightingConfig defaultSceneLightingConfig_{};
    ScenePostProcessSettings defaultPostProcessSettings_{};
    std::optional<SceneRenderLightingPath> lastRuntimeMaterialLightingPath_;
    std::optional<SceneRenderDebugView> lastRuntimeMaterialDebugView_;
    std::optional<RenderMaterialGraphQualityLevel> lastRuntimeMaterialQualityLevel_;
    std::optional<RenderMaterialGraphFeatureLevel> lastRuntimeMaterialFeatureLevel_;
    std::optional<RenderMaterialGraphShaderStage> lastRuntimeMaterialShaderStage_;
    std::optional<RenderMaterialGraphVariantUsage> lastRuntimeMaterialVariantUsage_;
    std::string graphShaderCacheRoot_;
    float frameDeltaSeconds_ = 1.0F / 60.0F;
    bool gpuDrivenRuntimeDispatchEnabled_ = true;
    std::uint32_t lastUnresolvedMaterialTexturePathCount_ = 0;
    std::uint32_t lastDefaultMaterialFallbackCount_ = 0;
    std::uint32_t lastErrorMaterialFallbackCount_ = 0;
    std::uint32_t lastMaterialLoadedCount_ = 0;
    std::uint32_t lastMaterialFallbackCount_ = 0;
    std::uint32_t lastMaterialErrorCount_ = 0;
    std::uint32_t lastMaterialReloadCount_ = 0;
    std::uint32_t lastMaterialResolverDiagnosticCount_ = 0;
    std::uint32_t lastCompletedFrame_ = 0;
    std::vector<TemporalViewportState> temporalViewportStates_;
    DisplayConfig displayConfig_{};
    bool frameActive_ = false;
};

} // namespace kb::render
