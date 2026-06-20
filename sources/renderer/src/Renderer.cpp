#include "kb/render/Renderer.hpp"

#include "kb/render/BgfxContext.hpp"
#include "kb/render/RendererCapabilityReport.hpp"
#include "kb/render/RenderSurface.hpp"
#include "kb/render/ViewIdPolicy.hpp"
#include "engine/ecs/WorkerPool.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "kb/render/scene/EcsRenderSceneSynchronizer.hpp"
#include "kb/render/scene/RenderScene.hpp"
#include "kb/render/scene/SceneRenderer.hpp"
#include "kb/render/post/SceneExposureMeter.hpp"
#include "renderer/RendererEditorOverlaySubmitter.hpp"
#include "renderer/RendererExposureSubmitter.hpp"
#include "renderer/RendererFinalCompositeSubmitter.hpp"
#include "renderer/RendererMeshPassSubmitter.hpp"
#include "renderer/RendererMatrixMath.hpp"
#include "renderer/RendererPostProcessSubmitter.hpp"
#include "renderer/RendererRuntimeResourceStatsBuilder.hpp"
#include "renderer/RendererSceneLightingConfigResolver.hpp"
#include "renderer/RendererShadowSubmitter.hpp"
#include "renderer/RendererTemporalJitter.hpp"
#include "renderer/RendererViewConfigurator.hpp"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace kb::render {

Renderer::Renderer() = default;

Renderer::~Renderer() {
    Shutdown();
}

bool Renderer::Initialize(RenderSurface& surface, const DisplayConfig* config) {
    displayConfig_ = config == nullptr ? DisplayConfig{} : *config;
    context_ = std::make_unique<BgfxContext>();

    bgfx::RendererType::Enum preferred = bgfx::RendererType::Count;
    if (displayConfig_.preferredBgfxRendererType >= 0 && displayConfig_.preferredBgfxRendererType < static_cast<std::int32_t>(bgfx::RendererType::Count)) {
        preferred = static_cast<bgfx::RendererType::Enum>(displayConfig_.preferredBgfxRendererType);
    } else {
        bgfx::RendererType::Enum supportedBackends[bgfx::RendererType::Count]{};
        const std::uint8_t supportedBackendCount = bgfx::getSupportedRenderers(static_cast<std::uint8_t>(bgfx::RendererType::Count), supportedBackends);
        preferred = ResolvePreferredRendererBackend(supportedBackends, supportedBackendCount);
    }

    if (!context_->Initialize(surface, displayConfig_, preferred)) {
        context_.reset();
        return false;
    }

    sceneRenderer_ = std::make_unique<SceneRenderer>();
    if (!sceneRenderer_->Initialize()) {
        Shutdown();
        return false;
    }
    sceneRenderer_->SetDefaultDrawBudget(defaultSceneDrawBudget_);
    sceneRenderer_->SetDefaultLightingConfig(defaultSceneLightingConfig_);
    SetGpuDrivenRuntimeDispatchEnabled(gpuDrivenRuntimeDispatchEnabled_);
    renderSceneSynchronizer_ = std::make_unique<EcsRenderSceneSynchronizer>();
    ApplyRuntimeSceneResourceReserve();

    scenePostProcessRenderer_ = std::make_unique<ScenePostProcessRenderer>();
    if (!scenePostProcessRenderer_->Initialize()) {
        Shutdown();
        return false;
    }
    static_cast<void>(sceneExposureMeter_.InitializeGpuResources());

    finalCompositePass_ = std::make_unique<FinalCompositePass>();
    if (!finalCompositePass_->Initialize()) {
        Shutdown();
        return false;
    }
    if (!editorPassSubmitter_.Initialize()) {
        Shutdown();
        return false;
    }

    if (!postProcessChain_.Configure(PostProcessChain::DefaultSceneChainDesc())) {
        Shutdown();
        return false;
    }
    SetDefaultPostProcessSettings(defaultPostProcessSettings_);

    return true;
}

void Renderer::Shutdown() {
    if (context_ == nullptr && sceneRenderer_ == nullptr && scenePostProcessRenderer_ == nullptr &&
        finalCompositePass_ == nullptr && renderSceneSynchronizer_ == nullptr) {
        frameActive_ = false;
        frameState_.Reset();
        return;
    }

    frameActive_ = false;
    lastSceneSubmitStats_ = SceneRenderSubmitStats{};
    lastScenePassSubmitStats_.clear();
    lastSceneExposureStats_.clear();
    lastSceneDiagnostics_.Clear();
    temporalViewportStates_.clear();
    frameState_.Reset();
    renderSceneStore_.ReleaseAll();
    runtimeResourceCache_.DestroyAll(sceneRenderer_.get());
    frameReferences_.Clear();
    runtimeAssetDiscovery_.Clear();
    sceneExposureMeter_.ShutdownGpuResources();
    defaultShadowMap_.Shutdown();
    defaultPostProcessTargets_.Shutdown();
    defaultSceneTarget_.Shutdown();
    renderSceneSynchronizer_.reset();
    if (finalCompositePass_ != nullptr) {
        finalCompositePass_->Shutdown();
        finalCompositePass_.reset();
    }
    editorPassSubmitter_.Shutdown();
    if (sceneRenderer_ != nullptr) {
        sceneRenderer_->Shutdown();
        sceneRenderer_.reset();
    }
    if (scenePostProcessRenderer_ != nullptr) {
        scenePostProcessRenderer_->Shutdown();
        scenePostProcessRenderer_.reset();
    }
    sceneExposureMeter_.Reset();
    if (context_ != nullptr) {
        context_->Shutdown();
        context_.reset();
    }
}

bool Renderer::BeginFrame() {
    if (context_ == nullptr) {
        return false;
    }

    frameActive_ = context_->BeginFrame();
    if (frameActive_) {
        frameState_.Begin(static_cast<std::uint64_t>(lastCompletedFrame_) + 1ULL);
    } else {
        frameState_.Reset();
    }
    return frameActive_;
}

void Renderer::EndFrame() {
    if (context_ == nullptr || !frameActive_) {
        frameState_.Reset();
        return;
    }

    lastCompletedFrame_ = context_->EndFrame();
    if (sceneRenderer_ != nullptr) {
        sceneRenderer_->TickFrame();
    }
    frameActive_ = false;
    frameState_.End();
}

void Renderer::SubmitClear(std::uint32_t rgba, float depth, std::uint8_t stencil) {
    if (context_ == nullptr || !context_->IsInitialized() || !frameActive_) {
        return;
    }

    const std::array<float, 16> identity = RendererMatrixMath::Identity();
    bgfx::setViewName(ViewId::Scene3D, "KB Scene3D");
    bgfx::setViewFrameBuffer(ViewId::Scene3D, BGFX_INVALID_HANDLE);
    bgfx::setViewTransform(ViewId::Scene3D, identity.data(), identity.data());
    bgfx::setViewClear(ViewId::Scene3D, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH | BGFX_CLEAR_STENCIL, rgba, depth, stencil);
    const std::uint32_t width = context_->Width();
    const std::uint32_t height = context_->Height();
    bgfx::setViewRect(ViewId::Scene3D, 0, 0, static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height));
    bgfx::touch(ViewId::Scene3D);
}

void Renderer::SubmitScene(const kb::scene::Scene& scene) {
    const RenderExtent extent{ BackbufferWidth(), BackbufferHeight() };
    if (!extent.IsValid()) {
        return;
    }
    if (!defaultSceneTarget_.Ensure(SceneRenderTargetDesc{
            .extent = extent,
            .colorPolicy = SceneColorFormatPolicy::Auto,
        })) {
        return;
    }
    if (!defaultPostProcessTargets_.Ensure(ScenePostProcessTargetsDesc{
            .extent = extent,
            .colorPolicy = SceneColorFormatPolicy::Auto,
        })) {
        return;
    }

    const RenderSceneSubmitDesc desc{
        .target = RenderSceneTargetBinding{
            .frameBuffer = defaultSceneTarget_.FrameBuffer(),
            .colorTexture = defaultSceneTarget_.ColorTexture(),
            .depthTexture = defaultSceneTarget_.DepthTexture(),
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ 1U },
                .extent = extent,
                .viewportIndex = 0U,
            },
        },
        .postProcess = defaultPostProcessTargets_.Binding(),
        .finalComposite = RenderFinalCompositeTargetBinding{
            .frameBuffer = BGFX_INVALID_HANDLE,
            .extent = extent,
            .enabled = true,
        },
        .drawBudget = defaultSceneDrawBudget_,
        .lightingConfig = defaultSceneLightingConfig_,
    };
    (void)SubmitScene(scene, desc);
}

bool Renderer::SubmitScene(const kb::scene::Scene& scene, const RenderSceneSubmitDesc& desc) {
    const std::array<SceneFrameSubmission, 1U> submissions{
        SceneFrameSubmission{
            .scene = &scene,
            .desc = desc,
        },
    };
    return SubmitScenes(submissions);
}

bool Renderer::SubmitScenes(std::span<const SceneFrameSubmission> submissions) {
    lastSceneSubmitStats_ = SceneRenderSubmitStats{};
    lastScenePassSubmitStats_.clear();
    lastScenePassSubmitStats_.reserve(submissions.size() * 4U);
    lastSceneExposureStats_.clear();
    lastSceneExposureStats_.reserve(submissions.size());
    lastSceneDiagnostics_.Clear();
    lastUnresolvedMaterialTexturePathCount_ = 0U;
    frameReferences_.Clear();
    if (context_ == nullptr || !context_->IsInitialized() || !frameActive_ || sceneRenderer_ == nullptr || !sceneRenderer_->IsInitialized() || submissions.empty()) {
        return false;
    }

    RenderFrameDesc frameDesc{};
    frameDesc.frameIndex = static_cast<std::uint64_t>(lastCompletedFrame_) + 1ULL;
    frameDesc.viewports.reserve(submissions.size());
    for (const SceneFrameSubmission& submission : submissions) {
        if (!submission.IsValid()) {
            return false;
        }
        frameDesc.viewports.push_back(submission.desc.target.viewport);
    }

    const RenderFramePlan plan = framePipeline_.Build(frameDesc);
    if (!plan.Succeeded() || plan.viewports.size() != submissions.size()) {
        return false;
    }

    RenderFrameState stagedFrameState;
    stagedFrameState.Begin(frameDesc.frameIndex);
    for (const RenderViewportPlan& viewportPlan : plan.viewports) {
        if (!stagedFrameState.RegisterViewportPlan(viewportPlan)) {
            return false;
        }
    }
    frameState_ = stagedFrameState;
    RendererViewConfigurator::ApplyViewOrder(frameState_.ViewOrder());

    for (std::size_t index = 0; index < submissions.size(); ++index) {
        if (!SubmitSceneToViewport(*submissions[index].scene, submissions[index].desc, plan.viewports[index])) {
            return false;
        }
    }
    std::vector<const kb::scene::Scene*> submittedScenes;
    submittedScenes.reserve(submissions.size());
    for (const SceneFrameSubmission& submission : submissions) {
        submittedScenes.push_back(submission.scene);
    }
    runtimeResourceCache_.PruneUnused(
        submittedScenes,
        frameReferences_,
        *sceneRenderer_,
        frameDesc.frameIndex,
        kRuntimeAssetRetentionFrames);

    return true;
}

bool Renderer::SubmitSceneToViewport(const kb::scene::Scene& scene, const RenderSceneSubmitDesc& desc, const RenderViewportPlan& viewportPlan) {
    if (desc.meshPassMode != SceneRenderMeshPassMode::OpaqueOnly &&
        desc.meshPassMode != SceneRenderMeshPassMode::OpaqueAndTransparent) {
        return false;
    }

    RendererViewConfigurator::ConfigureSceneClear(viewportPlan.viewIds.opaqueScene, desc);
    RendererViewConfigurator::ConfigureSceneNoClear(viewportPlan.viewIds.transparentScene, desc, "KB Scene Transparent");

    const std::uint32_t width = desc.target.viewport.extent.width;
    const std::uint32_t height = desc.target.viewport.extent.height;
    if (renderSceneSynchronizer_ == nullptr) {
        return false;
    }
    RenderScene& renderScene = RenderSceneFor(scene);
    if (desc.synchronizeScene) {
        renderSceneSynchronizer_->Sync(scene, renderScene);
    } else if (desc.transformAffineSync) {
        const std::span<const kb::scene::SceneEntity> affineEntities = scene.Runtime().TransformRenderProxyUpdateEntities();
        const std::span<const kb::scene::WorldTransformAffine3x4> affines = scene.Runtime().TransformRenderProxyWorldAffine3x4();
        // Above a threshold the columnar affine sync is worth dispatching across
        // the shared render-sync worker pool (H6); below it the serial path wins.
        constexpr std::size_t kParallelAffineSyncThreshold = 8U * 1024U;
        if (affineEntities.size() >= kParallelAffineSyncThreshold) {
            if (renderSyncWorkerPool_ == nullptr) {
                renderSyncWorkerPool_ = std::make_unique<kb::ecs::WorkerPool>(kb::ecs::WorkerPoolConfig{});
            }
            if (!renderSyncWorkerPool_->Running()) {
                renderSyncWorkerPool_->Start(kb::ecs::WorkerPoolConfig{});
            }
            renderSceneSynchronizer_->SyncMeshWorldAffinesParallel(renderScene, affineEntities, affines, *renderSyncWorkerPool_);
        } else {
            renderSceneSynchronizer_->SyncMeshWorldAffines(renderScene, affineEntities, affines);
        }
    } else if (!desc.dirtySceneEntityIds.empty()) {
        renderSceneSynchronizer_->SyncEntities(scene, renderScene, desc.dirtySceneEntityIds);
    }
    runtimeResourceCache_.EnsureSceneResources(RuntimeRenderResourceEnsureContext{
        .scene = const_cast<kb::scene::Scene&>(scene),
        .renderScene = renderScene,
        .sceneRenderer = *sceneRenderer_,
        .assetDiscovery = runtimeAssetDiscovery_,
        .frameReferences = frameReferences_,
        .materialResolver = runtimeMaterialResolver_,
        .diagnostics = lastSceneDiagnostics_,
        .unresolvedMaterialTexturePathCount = lastUnresolvedMaterialTexturePathCount_,
        .currentFrame = static_cast<std::uint64_t>(lastCompletedFrame_) + 1ULL,
    });
    SceneRenderLightingConfig effectiveLightingConfig = RendererSceneLightingConfigResolver::Resolve(desc.lightingConfig, defaultSceneLightingConfig_);
    if (!desc.shadowPassEnabled) {
        effectiveLightingConfig.shadowsEnabled = false;
    }
    SceneGpuDrivenFeatureSupport effectiveGpuDrivenSupport = sceneRenderer_->GpuDrivenRuntimeSupport();
    if (!desc.gpuDrivenRuntimeDispatchEnabled) {
        effectiveGpuDrivenSupport = SceneGpuDrivenFeatureSupport{};
    }
    const SceneRenderShadowMapBinding shadowBinding = RendererShadowSubmitter::Submit(RendererShadowSubmitDesc{
        .renderScene = renderScene,
        .sceneRenderer = *sceneRenderer_,
        .shadowMap = defaultShadowMap_,
        .sceneDesc = desc,
        .viewportPlan = viewportPlan,
        .lightingConfig = effectiveLightingConfig,
        .gpuDrivenSupport = effectiveGpuDrivenSupport,
        .aggregateSubmitStats = lastSceneSubmitStats_,
        .diagnostics = lastSceneDiagnostics_,
        .passSubmitStats = lastScenePassSubmitStats_,
    });

    const std::optional<SceneRenderCamera> primaryCamera = desc.cameraOverride.has_value() ? std::optional<SceneRenderCamera>{} : renderScene.BuildPrimaryCamera(width, height);
    const SceneRenderCamera* overlayCamera = desc.cameraOverride.has_value()
        ? &(*desc.cameraOverride)
        : (primaryCamera.has_value() ? &(*primaryCamera) : nullptr);
    std::optional<SceneRenderCamera> jitteredCamera{};
    const std::uint64_t frameIndex = static_cast<std::uint64_t>(lastCompletedFrame_) + 1ULL;
    const bool temporalJitterEnabled = defaultPostProcessSettings_.temporalJitterEnabled && !desc.editorSceneOverlaysEnabled;
    const std::array<float, 2> jitter = RendererTemporalJitter::Compute(frameIndex, desc.target.viewport.extent, temporalJitterEnabled);
    if (overlayCamera != nullptr) {
        jitteredCamera = *overlayCamera;
        RendererTemporalJitter::Apply(*jitteredCamera, jitter);
    }
    const SceneRenderCamera* sceneCamera = jitteredCamera.has_value() ? &(*jitteredCamera) : overlayCamera;

    const RendererMeshPassSubmitDesc meshPassSubmitDesc{
        .sceneRenderer = *sceneRenderer_,
        .renderScene = renderScene,
        .sceneDesc = desc,
        .viewportPlan = viewportPlan,
        .sceneCamera = sceneCamera,
        .lightingConfig = effectiveLightingConfig,
        .width = width,
        .height = height,
        .gpuDrivenSupport = effectiveGpuDrivenSupport,
        .aggregateSubmitStats = lastSceneSubmitStats_,
        .diagnostics = lastSceneDiagnostics_,
        .passSubmitStats = lastScenePassSubmitStats_,
    };

    RendererMeshPassSubmitter::SubmitViewportPass(
        meshPassSubmitDesc,
        viewportPlan.viewIds.opaqueScene,
        RenderPassKind::OpaqueScene,
        MeshPassType::BaseOpaque,
        shadowBinding.IsValid() ? &shadowBinding : nullptr);
    if (desc.meshPassMode == SceneRenderMeshPassMode::OpaqueAndTransparent) {
        RendererMeshPassSubmitter::SubmitViewportPass(
            meshPassSubmitDesc,
            viewportPlan.viewIds.transparentScene,
            RenderPassKind::TransparentScene,
            MeshPassType::BaseTransparent,
            shadowBinding.IsValid() ? &shadowBinding : nullptr);
    }
    if (desc.selectionMaskEnabled) {
        editorPassSubmitter_.SubmitSelectionMask(viewportPlan, desc);
        const RendererMeshPassSubmitDesc selectionMaskSubmitDesc{
            .sceneRenderer = *sceneRenderer_,
            .renderScene = renderScene,
            .sceneDesc = desc,
            .viewportPlan = viewportPlan,
            .sceneCamera = overlayCamera,
            .lightingConfig = effectiveLightingConfig,
            .width = width,
            .height = height,
            .gpuDrivenSupport = effectiveGpuDrivenSupport,
            .aggregateSubmitStats = lastSceneSubmitStats_,
            .diagnostics = lastSceneDiagnostics_,
            .passSubmitStats = lastScenePassSubmitStats_,
        };
        RendererMeshPassSubmitter::SubmitSelectionMask(selectionMaskSubmitDesc);
    }

    if (desc.finalComposite.enabled && finalCompositePass_ != nullptr) {
        PostProcessOutput postProcessOutput{
            .color = desc.target.colorTexture,
            .extent = desc.target.viewport.extent,
            .producer = PostProcessPassKind::IdentityCopy,
            .colorSpace = PostProcessColorSpace::SceneHdr,
            .enabledPassCount = 0U,
            .passthrough = true,
            .gpuSubmitted = false,
            .sceneHdrPreserved = true,
            .tonemapEnabled = true,
        };
        bgfx::TextureHandle scenePostProcessOutput = desc.target.colorTexture;
        if (desc.postProcessEnabled) {
            if (scenePostProcessRenderer_ == nullptr) {
                return false;
            }
            postProcessOutput = postProcessChain_.Evaluate(PostProcessInput{
                .sceneColor = desc.target.colorTexture,
                .selectionMask = desc.postProcess.selectionMaskTexture,
                .outputFrameBuffer = desc.postProcess.finalFrameBuffer,
                .outputColor = desc.postProcess.finalTexture,
                .extent = desc.target.viewport.extent,
            });
        }
        if (!postProcessOutput.IsValid()) {
            return false;
        }
        if (desc.postProcessEnabled) {
            lastSceneExposureStats_.push_back(RendererExposureSubmitter::Submit(
                sceneExposureMeter_,
                postProcessOutput,
                desc,
                viewportPlan,
                renderScene,
                effectiveLightingConfig,
                lastCompletedFrame_));

            TemporalViewportState& temporalState = TemporalStateFor(desc.target.viewport.id, desc.target.viewport.viewportIndex);
            scenePostProcessOutput = RendererPostProcessSubmitter::Submit(RendererPostProcessSubmitDesc{
                .postProcessRenderer = *scenePostProcessRenderer_,
                .sceneDesc = desc,
                .viewportPlan = viewportPlan,
                .postProcessOutput = postProcessOutput,
                .sceneCamera = sceneCamera,
                .jitter = jitter,
                .frameIndex = frameIndex,
                .temporalExtent = temporalState.extent,
                .previousViewProjection = temporalState.previousViewProjection,
                .hasTemporalHistory = temporalState.hasHistory,
            });
            if (!bgfx::isValid(scenePostProcessOutput)) {
                return false;
            }
        }
        if (!RendererFinalCompositeSubmitter::Submit(*finalCompositePass_, viewportPlan, desc, postProcessOutput, scenePostProcessOutput)) {
            return false;
        }

        RendererEditorOverlaySubmitter::Submit(
            editorPassSubmitter_,
            viewportPlan,
            desc,
            overlayCamera,
            desc.selectionOutlineEnabled && postProcessOutput.selectionOutlineEnabled);
        return true;
    }

    RendererEditorOverlaySubmitter::Submit(editorPassSubmitter_, viewportPlan, desc, overlayCamera, true);
    return true;
}

RenderScene& Renderer::RenderSceneFor(const kb::scene::Scene& scene) {
    return renderSceneStore_.ForScene(scene.Id(), RenderSceneReserveDesc{
        .meshProxies = runtimeSceneResourceReserveDesc_.renderSceneMeshProxies,
        .cameraProxies = runtimeSceneResourceReserveDesc_.renderSceneCameraProxies,
        .lightProxies = runtimeSceneResourceReserveDesc_.renderSceneLightProxies,
        .drawGroupKeys = runtimeSceneResourceReserveDesc_.renderSceneDrawGroupKeys,
    });
}

void Renderer::OnResize(std::uint32_t width, std::uint32_t height) {
    if (context_ == nullptr || !context_->IsInitialized() || width == 0 || height == 0) {
        return;
    }

    defaultPostProcessTargets_.Shutdown();
    defaultSceneTarget_.Shutdown();
    temporalViewportStates_.clear();
    context_->Reset(width, height, displayConfig_.ComputeResetFlags());
}

bool Renderer::IsInitialized() const noexcept {
    return context_ != nullptr && context_->IsInitialized();
}

bool Renderer::IsFrameActive() const noexcept {
    return frameActive_;
}

std::uint32_t Renderer::BackbufferWidth() const noexcept {
    return context_ == nullptr ? 0 : context_->Width();
}

std::uint32_t Renderer::BackbufferHeight() const noexcept {
    return context_ == nullptr ? 0 : context_->Height();
}

void* Renderer::NativeWindowHandle() const noexcept {
    return context_ == nullptr ? nullptr : context_->NativeWindowHandle();
}

const RendererCapabilityReport& Renderer::CapabilityReport() const noexcept {
    static const RendererCapabilityReport emptyReport{};
    return context_ == nullptr ? emptyReport : context_->CapabilityReport();
}

std::uint32_t Renderer::LastCompletedFrame() const noexcept {
    return lastCompletedFrame_;
}

RenderResourceRegistry* Renderer::SceneResources() noexcept {
    return sceneRenderer_ == nullptr ? nullptr : &sceneRenderer_->Resources();
}

const RenderResourceRegistry* Renderer::SceneResources() const noexcept {
    return sceneRenderer_ == nullptr ? nullptr : &sceneRenderer_->Resources();
}

SceneRenderResourceMap* Renderer::SceneResourceMap() noexcept {
    return sceneRenderer_ == nullptr ? nullptr : &sceneRenderer_->ResourceMap();
}

const SceneRenderResourceMap* Renderer::SceneResourceMap() const noexcept {
    return sceneRenderer_ == nullptr ? nullptr : &sceneRenderer_->ResourceMap();
}

SceneRenderSubmitStats Renderer::LastSceneSubmitStats() const noexcept {
    return lastSceneSubmitStats_;
}

std::span<const SceneRenderPassSubmitStats> Renderer::LastScenePassSubmitStats() const noexcept {
    return lastScenePassSubmitStats_;
}

std::span<const SceneRenderExposureSubmitStats> Renderer::LastSceneExposureStats() const noexcept {
    return lastSceneExposureStats_;
}

const SceneRenderDiagnostics& Renderer::LastSceneDiagnostics() const noexcept {
    return lastSceneDiagnostics_;
}

Renderer::RuntimeSceneResourceStats Renderer::RuntimeResourceStats() const noexcept {
    RenderResourceRegistryStats resourceStats{};
    SceneRenderResourceMapStats resourceMapStats{};
    EcsRenderSceneSynchronizerStats syncStats{};
    if (sceneRenderer_ != nullptr) {
        resourceStats = sceneRenderer_->Resources().Stats();
        resourceMapStats = sceneRenderer_->ResourceMap().Stats();
    }
    if (renderSceneSynchronizer_ != nullptr) {
        syncStats = renderSceneSynchronizer_->Stats();
    }
    return RendererRuntimeResourceStatsBuilder::Build(RendererRuntimeResourceStatsBuildDesc{
        .cacheStats = runtimeResourceCache_.Stats(),
        .referenceStats = frameReferences_.Stats(),
        .discoveryStats = runtimeAssetDiscovery_.Stats(),
        .storeStats = renderSceneStore_.Stats(),
        .resourceStats = resourceStats,
        .resourceMapStats = resourceMapStats,
        .syncStats = syncStats,
        .defaultLightingConfig = defaultSceneLightingConfig_,
        .unresolvedMaterialTexturePathCount = lastUnresolvedMaterialTexturePathCount_,
        .scenePassSubmitStatsCapacity = static_cast<std::uint32_t>(lastScenePassSubmitStats_.capacity()),
        .shadowMapSize = defaultShadowMap_.Size(),
        .shadowMapAllocationBytes = defaultShadowMap_.AllocationBytes(),
        .shadowMapAllocated = defaultShadowMap_.IsAllocated(),
        .retentionFrames = kRuntimeAssetRetentionFrames,
        .assetDiscoveryIntervalFrames = runtimeAssetDiscovery_.DiscoveryIntervalFrames(),
    });
}

void Renderer::ReserveRuntimeSceneResources(const RuntimeSceneResourceReserveDesc& desc) {
    runtimeSceneResourceReserveDesc_ = desc;
    ApplyRuntimeSceneResourceReserve();
}

void Renderer::SetDefaultSceneDrawBudget(SceneRenderDrawBudget drawBudget) noexcept {
    defaultSceneDrawBudget_ = drawBudget;
    if (sceneRenderer_ != nullptr) {
        sceneRenderer_->SetDefaultDrawBudget(drawBudget);
    }
}

SceneRenderDrawBudget Renderer::DefaultSceneDrawBudget() const noexcept {
    return defaultSceneDrawBudget_;
}

void Renderer::SetDefaultSceneLightingConfig(SceneRenderLightingConfig lightingConfig) noexcept {
    defaultSceneLightingConfig_ = lightingConfig;
    if (sceneRenderer_ != nullptr) {
        sceneRenderer_->SetDefaultLightingConfig(lightingConfig);
    }
}

SceneRenderLightingConfig Renderer::DefaultSceneLightingConfig() const noexcept {
    return defaultSceneLightingConfig_;
}

void Renderer::SetGpuDrivenRuntimeDispatchEnabled(bool enabled) noexcept {
    gpuDrivenRuntimeDispatchEnabled_ = enabled;
    if (sceneRenderer_ == nullptr || context_ == nullptr) {
        return;
    }

    const RendererCapabilityReport& capabilityReport = context_->CapabilityReport();
    sceneRenderer_->SetGpuDrivenRuntimeSupport(SceneGpuDrivenFeatureSupport{
        .computeCullingSupported = capabilityReport.gpuDrivenComputeCullingSupported,
        .indirectDrawSupported = enabled ? false : capabilityReport.gpuDrivenIndirectSubmitSupported,
        .meshletSubmitSupported = capabilityReport.gpuDrivenMeshletSubmitSupported,
        .runtimeGpuDispatchSupported = enabled && capabilityReport.gpuDrivenComputeCullingSupported,
    });
}

bool Renderer::GpuDrivenRuntimeDispatchEnabled() const noexcept {
    return gpuDrivenRuntimeDispatchEnabled_;
}

void Renderer::SetDefaultPostProcessSettings(ScenePostProcessSettings settings) noexcept {
    if (settings.temporalAntiAliasingEnabled) {
        settings.fxaaEnabled = false;
    }
    if (!settings.temporalAntiAliasingEnabled) {
        settings.temporalJitterEnabled = false;
    }
    settings.bloomStrength = std::max(settings.bloomStrength, 0.0F);
    settings.bloomThreshold = std::max(settings.bloomThreshold, 0.0F);
    settings.bloomSoftKnee = std::clamp(settings.bloomSoftKnee, 0.0F, 1.0F);
    settings.bloomRadiusPixels = std::max(settings.bloomRadiusPixels, 0.0F);
    settings.temporalHistoryBlend = std::clamp(settings.temporalHistoryBlend, 0.0F, 1.0F);
    settings.outputTransform.gamma = std::max(settings.outputTransform.gamma, 0.001F);
    settings.outputTransform.colorGradingLutStrength = std::clamp(settings.outputTransform.colorGradingLutStrength, 0.0F, 1.0F);
    settings.outputTransform.autoExposure.meteredAverageLuminance = std::max(settings.outputTransform.autoExposure.meteredAverageLuminance, 0.0001F);
    settings.outputTransform.autoExposure.middleGray = std::max(settings.outputTransform.autoExposure.middleGray, 0.0001F);
    settings.outputTransform.autoExposure.brightAdaptationRate = std::max(settings.outputTransform.autoExposure.brightAdaptationRate, 0.0F);
    settings.outputTransform.autoExposure.darkAdaptationRate = std::max(settings.outputTransform.autoExposure.darkAdaptationRate, 0.0F);
    defaultPostProcessSettings_ = settings;
    if (std::optional<PostProcessPass> antiAliasing = postProcessChain_.FindPass(PostProcessPassKind::AntiAliasing); antiAliasing.has_value()) {
        antiAliasing->enabled = settings.temporalAntiAliasingEnabled || settings.fxaaEnabled;
        antiAliasing->postProcessSettings = settings;
        static_cast<void>(postProcessChain_.SetPass(*antiAliasing));
    }
    if (std::optional<PostProcessPass> bloom = postProcessChain_.FindPass(PostProcessPassKind::Bloom); bloom.has_value()) {
        bloom->enabled = settings.bloomEnabled;
        bloom->postProcessSettings = settings;
        static_cast<void>(postProcessChain_.SetPass(*bloom));
    }
    if (std::optional<PostProcessPass> tonemap = postProcessChain_.FindPass(PostProcessPassKind::Tonemap); tonemap.has_value()) {
        tonemap->enabled = settings.tonemapEnabled;
        tonemap->postProcessSettings = settings;
        tonemap->outputTransform = settings.outputTransform;
        static_cast<void>(postProcessChain_.SetPass(*tonemap));
    }
}

ScenePostProcessSettings Renderer::DefaultPostProcessSettings() const noexcept {
    return defaultPostProcessSettings_;
}

bool Renderer::ConfigurePostProcessChain(const PostProcessChainDesc& desc) {
    return postProcessChain_.Configure(desc);
}

bool Renderer::AddPostProcessPass(PostProcessPass pass) {
    return postProcessChain_.AddPass(pass);
}

bool Renderer::InsertPostProcessPass(std::uint32_t index, PostProcessPass pass) {
    return postProcessChain_.InsertPass(index, pass);
}

bool Renderer::RemovePostProcessPass(PostProcessPassKind kind) noexcept {
    return postProcessChain_.RemovePass(kind);
}

bool Renderer::SetPostProcessPass(PostProcessPass pass) {
    return postProcessChain_.SetPass(pass);
}

bool Renderer::SetPostProcessPassEnabled(PostProcessPassKind kind, bool enabled) noexcept {
    return postProcessChain_.SetPassEnabled(kind, enabled);
}

std::optional<PostProcessPass> Renderer::FindPostProcessPass(PostProcessPassKind kind) const noexcept {
    return postProcessChain_.FindPass(kind);
}

std::span<const PostProcessPass> Renderer::PostProcessPasses() const noexcept {
    return postProcessChain_.Passes();
}

void Renderer::SetRuntimeAssetDiscoveryIntervalFrames(std::uint64_t frameInterval) noexcept {
    runtimeAssetDiscovery_.SetDiscoveryIntervalFrames(frameInterval);
}

std::uint64_t Renderer::RuntimeAssetDiscoveryIntervalFrames() const noexcept {
    return runtimeAssetDiscovery_.DiscoveryIntervalFrames();
}

void Renderer::ReleaseScene(const kb::scene::Scene& scene) noexcept {
    runtimeResourceCache_.ReleaseScene(const_cast<kb::scene::Scene&>(scene), sceneRenderer_.get());
    renderSceneStore_.Release(scene.Id());
    runtimeAssetDiscovery_.ReleaseScene(scene.Id());
}

void Renderer::ReleaseAllScenes() noexcept {
    renderSceneStore_.ReleaseAll();
    runtimeResourceCache_.DestroyAll(sceneRenderer_.get());
    runtimeAssetDiscovery_.Clear();
}

Renderer::TemporalViewportState& Renderer::TemporalStateFor(RenderViewportId viewportId, std::uint32_t viewportIndex) {
    const auto iter = std::ranges::find_if(temporalViewportStates_, [viewportId, viewportIndex](const TemporalViewportState& state) {
        return state.viewportId == viewportId && state.viewportIndex == viewportIndex;
    });
    if (iter != temporalViewportStates_.end()) {
        return *iter;
    }

    temporalViewportStates_.push_back(TemporalViewportState{
        .viewportId = viewportId,
        .viewportIndex = viewportIndex,
        .previousViewProjection = RendererMatrixMath::Identity(),
    });
    return temporalViewportStates_.back();
}

void Renderer::ApplyRuntimeSceneResourceReserve() {
    if (runtimeSceneResourceReserveDesc_.sceneCount > 0U) {
        renderSceneStore_.ReserveSceneCount(runtimeSceneResourceReserveDesc_.sceneCount);
        runtimeAssetDiscovery_.ReserveSceneCount(runtimeSceneResourceReserveDesc_.sceneCount);
    }
    runtimeResourceCache_.Reserve(RuntimeRenderResourceCacheReserveDesc{
        .meshes = runtimeSceneResourceReserveDesc_.cachedMeshes,
        .materials = runtimeSceneResourceReserveDesc_.cachedMaterials,
        .textures = runtimeSceneResourceReserveDesc_.cachedTextures,
    });
    frameReferences_.Reserve(RuntimeFrameResourceReferenceReserveDesc{
        .meshes = runtimeSceneResourceReserveDesc_.frameReferencedMeshes,
        .materials = runtimeSceneResourceReserveDesc_.frameReferencedMaterials,
        .textures = runtimeSceneResourceReserveDesc_.frameReferencedTextures,
    });
    if (runtimeSceneResourceReserveDesc_.scenePassSubmitStats > 0U) {
        lastScenePassSubmitStats_.reserve(runtimeSceneResourceReserveDesc_.scenePassSubmitStats);
    }
    const RenderSceneReserveDesc renderSceneReserve{
        .meshProxies = runtimeSceneResourceReserveDesc_.renderSceneMeshProxies,
        .cameraProxies = runtimeSceneResourceReserveDesc_.renderSceneCameraProxies,
        .lightProxies = runtimeSceneResourceReserveDesc_.renderSceneLightProxies,
        .drawGroupKeys = runtimeSceneResourceReserveDesc_.renderSceneDrawGroupKeys,
    };
    renderSceneStore_.ApplyRenderSceneReserve(renderSceneReserve);
    if (sceneRenderer_ != nullptr) {
        sceneRenderer_->Resources().Reserve(RenderResourceRegistryReserveDesc{
            .meshSlots = runtimeSceneResourceReserveDesc_.meshResourceSlots,
            .materialSlots = runtimeSceneResourceReserveDesc_.materialResourceSlots,
            .textureSlots = runtimeSceneResourceReserveDesc_.textureResourceSlots,
        });
        sceneRenderer_->ResourceMap().Reserve(SceneRenderResourceMapReserveDesc{
            .meshBindings = runtimeSceneResourceReserveDesc_.meshBindings,
            .materialBindings = runtimeSceneResourceReserveDesc_.materialBindings,
            .textureBindings = runtimeSceneResourceReserveDesc_.textureBindings,
        });
    }
    if (renderSceneSynchronizer_ != nullptr) {
        renderSceneSynchronizer_->Reserve(EcsRenderSceneSynchronizerReserveDesc{
            .meshProxies = runtimeSceneResourceReserveDesc_.syncMeshProxies,
            .cameraProxies = runtimeSceneResourceReserveDesc_.syncCameraProxies,
            .lightProxies = runtimeSceneResourceReserveDesc_.syncLightProxies,
            .transformCacheEntries = runtimeSceneResourceReserveDesc_.syncTransformCacheEntries,
            .transformResolvingEntries = runtimeSceneResourceReserveDesc_.syncTransformResolvingEntries,
        });
    }
}

} // namespace kb::render
