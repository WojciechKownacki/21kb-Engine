#include "kb/render/Renderer.hpp"

#include "kb/render/BgfxContext.hpp"
#include "kb/render/RendererCapabilityReport.hpp"
#include "kb/render/RenderSurface.hpp"
#include "kb/render/SceneDeferredLightingPass.hpp"
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
#include "renderer/RendererDebugLog.hpp"
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
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kb::render {

namespace {

void WriteRendererBreadcrumb(std::string_view category, std::string_view message) {
    WriteRendererDebugLog(category, message);
}

[[nodiscard]] const char* BoolText(bool value) noexcept {
    return value ? "true" : "false";
}

[[nodiscard]] const char* LightingPathName(SceneRenderLightingPath path) noexcept {
    switch (path) {
    case SceneRenderLightingPath::Forward:
        return "Forward";
    case SceneRenderLightingPath::ClusteredForwardPlus:
        return "ClusteredForwardPlus";
    case SceneRenderLightingPath::Deferred:
        return "Deferred";
    case SceneRenderLightingPath::VisibilityBuffer:
        return "VisibilityBuffer";
    }
    return "Unknown";
}

[[nodiscard]] const char* MeshPassModeName(SceneRenderMeshPassMode mode) noexcept {
    switch (mode) {
    case SceneRenderMeshPassMode::OpaqueOnly:
        return "OpaqueOnly";
    case SceneRenderMeshPassMode::OpaqueAndTransparent:
        return "OpaqueAndTransparent";
    }
    return "Unknown";
}

[[nodiscard]] std::uint16_t HandleValue(bgfx::FrameBufferHandle handle) noexcept {
    return handle.idx;
}

[[nodiscard]] std::uint16_t HandleValue(bgfx::TextureHandle handle) noexcept {
    return handle.idx;
}

void ApplyPostProcessSettingsOverride(PostProcessOutput& output, const std::optional<ScenePostProcessSettings>& settingsOverride) noexcept {
    if (!settingsOverride.has_value()) {
        return;
    }

    const ScenePostProcessSettings& settings = *settingsOverride;
    output.postProcessSettings = settings;
    output.outputTransform = settings.outputTransform;
    output.bloomEnabled = settings.bloomEnabled && settings.bloomStrength > 0.0F;
    output.fxaaEnabled = settings.fxaaEnabled;
    output.temporalAntiAliasingEnabled = settings.temporalAntiAliasingEnabled;
    output.tonemapEnabled = settings.tonemapEnabled;
}

[[nodiscard]] bool UsesDeferredLighting(SceneRenderLightingPath path) noexcept {
    return path == SceneRenderLightingPath::Deferred;
}

} // namespace

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
    if (!graphShaderCacheRoot_.empty()) {
        sceneRenderer_->SetGraphShaderCacheRoot(graphShaderCacheRoot_);
    }
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
    deferredLightingPass_ = std::make_unique<SceneDeferredLightingPass>();
    if (!deferredLightingPass_->Initialize()) {
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
    {
        std::ostringstream message;
        message << "Initialize end ok backend=" << CapabilityReport().selectedBackendName
                << " rendererType=" << static_cast<int>(bgfx::getRendererType())
                << " backbuffer=" << BackbufferWidth() << 'x' << BackbufferHeight()
                << " homogeneousDepth=" << (SceneDepthPolicy::HomogeneousDepth() ? "true" : "false");
        WriteRendererBreadcrumb("renderer", message.str());
    }

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
    lastRuntimeMaterialLightingPath_.reset();
    sceneExposureMeter_.ShutdownGpuResources();
    defaultShadowMap_.Shutdown();
    defaultPostProcessTargets_.Shutdown();
    defaultSceneGBuffer_.Shutdown();
    defaultSceneTarget_.Shutdown();
    renderSceneSynchronizer_.reset();
    if (finalCompositePass_ != nullptr) {
        finalCompositePass_->Shutdown();
        finalCompositePass_.reset();
    }
    if (deferredLightingPass_ != nullptr) {
        deferredLightingPass_->Shutdown();
        deferredLightingPass_.reset();
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
        sceneRenderer_->AdvanceFrameTime(frameDeltaSeconds_);
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
    {
        std::ostringstream message;
        message << "SubmitScenes begin submissions=" << submissions.size()
                << " frameActive=" << BoolText(frameActive_)
                << " context=" << BoolText(context_ != nullptr)
                << " sceneRenderer=" << BoolText(sceneRenderer_ != nullptr);
        WriteRendererBreadcrumb("renderer", message.str());
    }
    lastSceneSubmitStats_ = SceneRenderSubmitStats{};
    lastScenePassSubmitStats_.clear();
    lastScenePassSubmitStats_.reserve(submissions.size() * 4U);
    lastSceneExposureStats_.clear();
    lastSceneExposureStats_.reserve(submissions.size());
    lastSceneDiagnostics_.Clear();
    lastUnresolvedMaterialTexturePathCount_ = 0U;
    lastDefaultMaterialFallbackCount_ = 0U;
    lastErrorMaterialFallbackCount_ = 0U;
    lastMaterialLoadedCount_ = 0U;
    lastMaterialFallbackCount_ = 0U;
    lastMaterialErrorCount_ = 0U;
    lastMaterialReloadCount_ = 0U;
    lastMaterialResolverDiagnosticCount_ = 0U;
    lastGraphMaterialCpuFallbackCount_ = 0U;
    lastGraphMaterialGpuCount_ = 0U;
    frameReferences_.Clear();
    if (context_ == nullptr || !context_->IsInitialized() || !frameActive_ || sceneRenderer_ == nullptr || !sceneRenderer_->IsInitialized() || submissions.empty()) {
        std::ostringstream message;
        message << "SubmitScenes early_exit invalid_state context=" << BoolText(context_ != nullptr)
                << " contextInitialized=" << BoolText(context_ != nullptr && context_->IsInitialized())
                << " frameActive=" << BoolText(frameActive_)
                << " sceneRenderer=" << BoolText(sceneRenderer_ != nullptr)
                << " sceneRendererInitialized=" << BoolText(sceneRenderer_ != nullptr && sceneRenderer_->IsInitialized())
                << " empty=" << BoolText(submissions.empty());
        WriteRendererBreadcrumb("renderer", message.str());
        return false;
    }

    RenderFrameDesc frameDesc{};
    frameDesc.frameIndex = static_cast<std::uint64_t>(lastCompletedFrame_) + 1ULL;
    frameDesc.viewports.reserve(submissions.size());
    for (std::size_t index = 0; index < submissions.size(); ++index) {
        const SceneFrameSubmission& submission = submissions[index];
        if (!submission.IsValid()) {
            std::ostringstream message;
            message << "SubmitScenes invalid_submission index=" << index
                    << " scene=" << BoolText(submission.scene != nullptr)
                    << " descValid=" << BoolText(submission.desc.IsValid());
            WriteRendererBreadcrumb("renderer", message.str());
            return false;
        }
        std::ostringstream message;
        message << "SubmitScenes viewport_desc index=" << index
                << " viewportId=" << submission.desc.target.viewport.id.value
                << " viewportIndex=" << submission.desc.target.viewport.viewportIndex
                << " extent=" << submission.desc.target.viewport.extent.width << 'x' << submission.desc.target.viewport.extent.height
                << " postProcess=" << BoolText(submission.desc.postProcessEnabled)
                << " postTargets=" << BoolText(submission.desc.postProcess.enabled)
                << " finalComposite=" << BoolText(submission.desc.finalComposite.enabled)
                << " meshPassMode=" << MeshPassModeName(submission.desc.meshPassMode)
                << " lightingPath=" << LightingPathName(RendererSceneLightingConfigResolver::Resolve(submission.desc.lightingConfig, defaultSceneLightingConfig_).lightingPath);
        WriteRendererBreadcrumb("renderer", message.str());
        frameDesc.viewports.push_back(submission.desc.target.viewport);
    }

    WriteRendererBreadcrumb("renderer", "SubmitScenes BuildFramePlan begin");
    const RenderFramePlan plan = framePipeline_.Build(frameDesc);
    if (!plan.Succeeded() || plan.viewports.size() != submissions.size()) {
        std::ostringstream message;
        message << "SubmitScenes BuildFramePlan failed succeeded=" << BoolText(plan.Succeeded())
                << " planViewports=" << plan.viewports.size()
                << " submissions=" << submissions.size();
        WriteRendererBreadcrumb("renderer", message.str());
        return false;
    }
    {
        std::ostringstream message;
        message << "SubmitScenes BuildFramePlan end viewports=" << plan.viewports.size();
        WriteRendererBreadcrumb("renderer", message.str());
    }

    RenderFrameState stagedFrameState;
    stagedFrameState.Begin(frameDesc.frameIndex);
    for (const RenderViewportPlan& viewportPlan : plan.viewports) {
        if (!stagedFrameState.RegisterViewportPlan(viewportPlan)) {
            std::ostringstream message;
            message << "SubmitScenes RegisterViewportPlan failed viewportId=" << viewportPlan.viewport.id.value
                    << " viewportIndex=" << viewportPlan.viewport.viewportIndex;
            WriteRendererBreadcrumb("renderer", message.str());
            return false;
        }
    }
    frameState_ = stagedFrameState;
    RendererViewConfigurator::ApplyViewOrder(frameState_.ViewOrder());

    for (std::size_t index = 0; index < submissions.size(); ++index) {
        {
            std::ostringstream message;
            message << "SubmitScenes SubmitSceneToViewport begin index=" << index
                    << " viewportId=" << submissions[index].desc.target.viewport.id.value
                    << " viewportIndex=" << submissions[index].desc.target.viewport.viewportIndex;
            WriteRendererBreadcrumb("renderer", message.str());
        }
        if (!SubmitSceneToViewport(*submissions[index].scene, submissions[index].desc, plan.viewports[index])) {
            std::ostringstream message;
            message << "SubmitScenes SubmitSceneToViewport failed index=" << index
                    << " viewportId=" << submissions[index].desc.target.viewport.id.value
                    << " viewportIndex=" << submissions[index].desc.target.viewport.viewportIndex;
            WriteRendererBreadcrumb("renderer", message.str());
            return false;
        }
        {
            std::ostringstream message;
            message << "SubmitScenes SubmitSceneToViewport end index=" << index
                    << " viewportId=" << submissions[index].desc.target.viewport.id.value
                    << " viewportIndex=" << submissions[index].desc.target.viewport.viewportIndex;
            WriteRendererBreadcrumb("renderer", message.str());
        }
    }
    std::vector<const kb::scene::Scene*> submittedScenes;
    submittedScenes.reserve(submissions.size());
    for (const SceneFrameSubmission& submission : submissions) {
        submittedScenes.push_back(submission.scene);
    }
    WriteRendererBreadcrumb("renderer", "SubmitScenes PruneUnused begin");
    runtimeResourceCache_.PruneUnused(
        submittedScenes,
        frameReferences_,
        *sceneRenderer_,
        frameDesc.frameIndex,
        kRuntimeAssetRetentionFrames);
    WriteRendererBreadcrumb("renderer", "SubmitScenes PruneUnused end");

    WriteRendererBreadcrumb("renderer", "SubmitScenes end ok");
    return true;
}

bool Renderer::SubmitSceneToViewport(const kb::scene::Scene& scene, const RenderSceneSubmitDesc& desc, const RenderViewportPlan& viewportPlan) {
    {
        std::ostringstream message;
        message << "SubmitSceneToViewport begin viewportId=" << desc.target.viewport.id.value
                << " viewportIndex=" << desc.target.viewport.viewportIndex
                << " extent=" << desc.target.viewport.extent.width << 'x' << desc.target.viewport.extent.height
                << " meshPassMode=" << MeshPassModeName(desc.meshPassMode)
                << " synchronizeScene=" << BoolText(desc.synchronizeScene)
                << " transformAffineSync=" << BoolText(desc.transformAffineSync)
                << " postProcess=" << BoolText(desc.postProcessEnabled)
                << " postTargets=" << BoolText(desc.postProcess.enabled)
                << " finalComposite=" << BoolText(desc.finalComposite.enabled)
                << " selectionMask=" << BoolText(desc.selectionMaskEnabled)
                << " overlays=" << BoolText(desc.editorSceneOverlaysEnabled)
                << " targetFb=" << HandleValue(desc.target.frameBuffer)
                << " colorTex=" << HandleValue(desc.target.colorTexture)
                << " depthTex=" << HandleValue(desc.target.depthTexture);
        WriteRendererBreadcrumb("renderer", message.str());
    }
    if (desc.meshPassMode != SceneRenderMeshPassMode::OpaqueOnly &&
        desc.meshPassMode != SceneRenderMeshPassMode::OpaqueAndTransparent) {
        WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport invalid meshPassMode");
        return false;
    }

    const std::uint32_t width = desc.target.viewport.extent.width;
    const std::uint32_t height = desc.target.viewport.extent.height;
    if (renderSceneSynchronizer_ == nullptr) {
        WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport missing renderSceneSynchronizer");
        return false;
    }
    WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport RenderSceneFor begin");
    RenderScene& renderScene = RenderSceneFor(scene);
    WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport RenderSceneFor end");
    if (desc.synchronizeScene) {
        WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport Sync full begin");
        renderSceneSynchronizer_->Sync(scene, renderScene);
        WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport Sync full end");
    } else {
        if (desc.transformAffineSync) {
            const std::span<const kb::scene::SceneEntity> affineEntities = scene.Runtime().TransformRenderProxyUpdateEntities();
            const std::span<const kb::scene::WorldTransformAffine3x4> affines = scene.Runtime().TransformRenderProxyWorldAffine3x4();
            // Above a threshold the columnar affine sync is worth dispatching across
            // the shared render-sync worker pool (H6); below it the serial path wins.
            constexpr std::size_t kParallelAffineSyncThreshold = 8U * 1024U;
            if (affineEntities.size() >= kParallelAffineSyncThreshold) {
                std::ostringstream message;
                message << "SubmitSceneToViewport SyncMeshWorldAffinesParallel begin count=" << affineEntities.size();
                WriteRendererBreadcrumb("renderer", message.str());
                if (renderSyncWorkerPool_ == nullptr) {
                    renderSyncWorkerPool_ = std::make_unique<kb::ecs::WorkerPool>(kb::ecs::WorkerPoolConfig{});
                }
                if (!renderSyncWorkerPool_->Running()) {
                    renderSyncWorkerPool_->Start(kb::ecs::WorkerPoolConfig{});
                }
                renderSceneSynchronizer_->SyncMeshWorldAffinesParallel(renderScene, affineEntities, affines, *renderSyncWorkerPool_);
                WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport SyncMeshWorldAffinesParallel end");
            } else {
                std::ostringstream message;
                message << "SubmitSceneToViewport SyncMeshWorldAffines begin count=" << affineEntities.size();
                WriteRendererBreadcrumb("renderer", message.str());
                renderSceneSynchronizer_->SyncMeshWorldAffines(renderScene, affineEntities, affines);
                WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport SyncMeshWorldAffines end");
            }
        }
        if (!desc.dirtySceneEntityIds.empty()) {
            std::ostringstream message;
            message << "SubmitSceneToViewport SyncEntities begin count=" << desc.dirtySceneEntityIds.size();
            WriteRendererBreadcrumb("renderer", message.str());
            renderSceneSynchronizer_->SyncEntities(scene, renderScene, desc.dirtySceneEntityIds);
            WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport SyncEntities end");
        }
        if (!scene.Runtime().MeshRendererRenderProxyUpdateEntities().empty()) {
            std::ostringstream message;
            message << "SubmitSceneToViewport SyncMeshRendererUpdates begin count=" << scene.Runtime().MeshRendererRenderProxyUpdateEntities().size();
            WriteRendererBreadcrumb("renderer", message.str());
            renderSceneSynchronizer_->SyncMeshRendererUpdates(scene, renderScene);
            WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport SyncMeshRendererUpdates end");
        }
    }
    SceneRenderLightingConfig effectiveLightingConfig = RendererSceneLightingConfigResolver::Resolve(desc.lightingConfig, defaultSceneLightingConfig_);
    if (!desc.shadowPassEnabled) {
        effectiveLightingConfig.shadowsEnabled = false;
    }
    const bool deferredLighting = UsesDeferredLighting(effectiveLightingConfig.lightingPath);
    RenderMaterialGraphBuildContext runtimeGraphContext{};
    runtimeGraphContext.shadingPath = deferredLighting
        ? RenderMaterialGraphShadingPath::Deferred
        : RenderMaterialGraphShadingPath::Forward;
    runtimeMaterialResolver_.SetGraphBuildContext(std::move(runtimeGraphContext));
    if (!lastRuntimeMaterialLightingPath_.has_value() || *lastRuntimeMaterialLightingPath_ != effectiveLightingConfig.lightingPath) {
        runtimeResourceCache_.InvalidateMaterials(sceneRenderer_.get());
        lastRuntimeMaterialLightingPath_ = effectiveLightingConfig.lightingPath;
        WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport runtime material cache invalidated for lighting path change");
    }
    WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport EnsureSceneResources begin");
    runtimeResourceCache_.EnsureSceneResources(RuntimeRenderResourceEnsureContext{
        .scene = const_cast<kb::scene::Scene&>(scene),
        .renderScene = renderScene,
        .sceneRenderer = *sceneRenderer_,
        .assetDiscovery = runtimeAssetDiscovery_,
        .frameReferences = frameReferences_,
        .materialResolver = runtimeMaterialResolver_,
        .diagnostics = lastSceneDiagnostics_,
        .unresolvedMaterialTexturePathCount = lastUnresolvedMaterialTexturePathCount_,
        .defaultMaterialFallbackCount = lastDefaultMaterialFallbackCount_,
        .errorMaterialFallbackCount = lastErrorMaterialFallbackCount_,
        .materialLoadedCount = lastMaterialLoadedCount_,
        .materialFallbackCount = lastMaterialFallbackCount_,
        .materialErrorCount = lastMaterialErrorCount_,
        .materialReloadCount = lastMaterialReloadCount_,
        .materialResolverDiagnosticCount = lastMaterialResolverDiagnosticCount_,
        .graphMaterialCpuFallbackCount = lastGraphMaterialCpuFallbackCount_,
        .graphMaterialGpuCount = lastGraphMaterialGpuCount_,
        .currentFrame = static_cast<std::uint64_t>(lastCompletedFrame_) + 1ULL,
    });
    {
        std::ostringstream message;
        message << "SubmitSceneToViewport EnsureSceneResources end materialLoaded=" << lastMaterialLoadedCount_
                << " materialFallback=" << lastMaterialFallbackCount_
                << " graphCpuFallback=" << lastGraphMaterialCpuFallbackCount_
                << " graphGpu=" << lastGraphMaterialGpuCount_
                << " diagnostics=" << lastSceneDiagnostics_.events.size();
        WriteRendererBreadcrumb("renderer", message.str());
    }
    {
        std::ostringstream message;
        message << "SubmitSceneToViewport lighting resolved path=" << LightingPathName(effectiveLightingConfig.lightingPath)
                << " deferred=" << BoolText(deferredLighting)
                << " shadows=" << BoolText(effectiveLightingConfig.shadowsEnabled);
        WriteRendererBreadcrumb("renderer", message.str());
    }
    if (deferredLighting && !defaultSceneGBuffer_.Ensure(SceneGBufferDesc{ .extent = desc.target.viewport.extent })) {
        lastSceneDiagnostics_.events.push_back(SceneRenderDiagnosticEvent{
            .severity = SceneRenderDiagnosticSeverity::Error,
            .kind = SceneRenderDiagnosticKind::DeferredRendererUnavailable,
            .instanceCount = 1U,
        });
        WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport GBuffer Ensure failed");
        return false;
    }
    if (deferredLighting) {
        const SceneGBufferFormatSelection selection = defaultSceneGBuffer_.FormatSelection();
        std::ostringstream message;
        message << "SubmitSceneToViewport GBuffer Ensure end ok"
                << " fb=" << HandleValue(defaultSceneGBuffer_.FrameBuffer())
                << " albedoTex=" << HandleValue(defaultSceneGBuffer_.AlbedoTexture())
                << " normalTex=" << HandleValue(defaultSceneGBuffer_.NormalTexture())
                << " materialTex=" << HandleValue(defaultSceneGBuffer_.MaterialTexture())
                << " depthTex=" << HandleValue(defaultSceneGBuffer_.DepthTexture())
                << " extent=" << defaultSceneGBuffer_.Width() << 'x' << defaultSceneGBuffer_.Height()
                << " formats=(" << SceneTextureFormatName(selection.albedoFormat)
                << ',' << SceneTextureFormatName(selection.normalFormat)
                << ',' << SceneTextureFormatName(selection.materialFormat)
                << ',' << SceneTextureFormatName(selection.depth.format) << ')'
                << " targetFb=" << HandleValue(desc.target.frameBuffer)
                << " finalFb=" << HandleValue(desc.finalComposite.frameBuffer);
        WriteRendererBreadcrumb("renderer", message.str());
    }
    if (deferredLighting) {
        WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport Configure GBuffer clear begin");
        RendererViewConfigurator::ConfigureFramebufferClear(
            viewportPlan.viewIds.gbufferGeometry,
            defaultSceneGBuffer_.FrameBuffer(),
            desc.target.viewport.extent,
            "KB GBuffer Geometry",
            BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH | BGFX_CLEAR_STENCIL,
            desc.clearRgba,
            desc.clearDepth,
            desc.clearStencil);
        WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport Configure GBuffer clear end");
    } else {
        WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport Configure opaque clear begin");
        RendererViewConfigurator::ConfigureSceneClear(viewportPlan.viewIds.opaqueScene, desc);
        WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport Configure opaque clear end");
    }
    WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport Configure transparent no-clear begin");
    RendererViewConfigurator::ConfigureSceneNoClear(viewportPlan.viewIds.transparentScene, desc, "KB Scene Transparent");
    WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport Configure transparent no-clear end");

    // MAT-80/#18b: expose the opaque scene depth to the transparent pass so depth-sampling graph materials
    // (SceneDepth / DepthFade) read real geometry depth. Deferred uses the GBuffer depth attachment.
    WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport scene texture bindings begin");
    sceneRenderer_->SetSceneDepthTexture(deferredLighting ? defaultSceneGBuffer_.DepthTexture() : desc.target.depthTexture);
    sceneRenderer_->SetSceneColorTexture(BGFX_INVALID_HANDLE);
    WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport scene texture bindings end");

    SceneGpuDrivenFeatureSupport effectiveGpuDrivenSupport = sceneRenderer_->GpuDrivenRuntimeSupport();
    if (!desc.gpuDrivenRuntimeDispatchEnabled) {
        effectiveGpuDrivenSupport = SceneGpuDrivenFeatureSupport{};
    }
    WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport ShadowSubmit begin");
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
    {
        std::ostringstream message;
        message << "SubmitSceneToViewport ShadowSubmit end valid=" << BoolText(shadowBinding.IsValid())
                << " depthTex=" << HandleValue(shadowBinding.depthTexture);
        WriteRendererBreadcrumb("renderer", message.str());
    }

    WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport camera resolve begin");
    const std::optional<SceneRenderCamera> primaryCamera = desc.cameraOverride.has_value() ? std::optional<SceneRenderCamera>{} : renderScene.BuildPrimaryCamera(width, height);
    const SceneRenderCamera* overlayCamera = desc.cameraOverride.has_value()
        ? &(*desc.cameraOverride)
        : (primaryCamera.has_value() ? &(*primaryCamera) : nullptr);
    std::optional<SceneRenderCamera> jitteredCamera{};
    const std::uint64_t frameIndex = static_cast<std::uint64_t>(lastCompletedFrame_) + 1ULL;
    const bool temporalJitterEnabled = desc.postProcessEnabled &&
        (desc.postProcessSettings.has_value()
                ? desc.postProcessSettings->temporalJitterEnabled
                : defaultPostProcessSettings_.temporalJitterEnabled) &&
        !desc.editorSceneOverlaysEnabled;
    const std::array<float, 2> jitter = RendererTemporalJitter::Compute(frameIndex, desc.target.viewport.extent, temporalJitterEnabled);
    if (overlayCamera != nullptr) {
        jitteredCamera = *overlayCamera;
        RendererTemporalJitter::Apply(*jitteredCamera, jitter);
    }
    const SceneRenderCamera* sceneCamera = jitteredCamera.has_value() ? &(*jitteredCamera) : overlayCamera;
    {
        std::ostringstream message;
        message << "SubmitSceneToViewport camera resolve end overlayCamera=" << BoolText(overlayCamera != nullptr)
                << " sceneCamera=" << BoolText(sceneCamera != nullptr)
                << " temporalJitter=" << BoolText(temporalJitterEnabled);
        WriteRendererBreadcrumb("renderer", message.str());
    }

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

    if (deferredLighting) {
        WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport GBuffer pass begin");
        RendererMeshPassSubmitter::SubmitViewportPass(
            meshPassSubmitDesc,
            viewportPlan.viewIds.gbufferGeometry,
            RenderPassKind::GBufferGeometry,
            MeshPassType::GBuffer,
            shadowBinding.IsValid() ? &shadowBinding : nullptr);
        const SceneRenderPassSubmitStats* gbufferPassStats = lastScenePassSubmitStats_.empty()
            ? nullptr
            : &lastScenePassSubmitStats_.back();
        if (gbufferPassStats != nullptr) {
            const SceneRenderSubmitStats& stats = gbufferPassStats->stats;
            std::ostringstream message;
            message << "SubmitSceneToViewport GBuffer pass end"
                    << " visibleMeshes=" << stats.visibleMeshCount
                    << " visibleGroups=" << stats.visibleDrawGroupCount
                    << " submittedMeshes=" << stats.submittedMeshCount
                    << " submittedDrawCalls=" << stats.submittedDrawCallCount
                    << " missingMeshBinding=" << stats.missingMeshBindingCount
                    << " missingMeshResource=" << stats.missingMeshResourceCount
                    << " unsupportedVertexFormat=" << stats.unsupportedMeshVertexFormatCount
                    << " missingMaterialBinding=" << stats.missingMaterialBindingCount
                    << " missingMaterialResource=" << stats.missingMaterialResourceCount
                    << " missingTextureBinding=" << stats.missingTextureBindingCount
                    << " missingTextureResource=" << stats.missingTextureResourceCount
                    << " diagnostics=" << lastSceneDiagnostics_.events.size();
            WriteRendererBreadcrumb("renderer", message.str());

        } else {
            WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport GBuffer pass end stats missing");
        }
        if (deferredLightingPass_ == nullptr) {
            lastSceneDiagnostics_.events.push_back(SceneRenderDiagnosticEvent{
                .severity = SceneRenderDiagnosticSeverity::Error,
                .kind = SceneRenderDiagnosticKind::DeferredRendererUnavailable,
                .instanceCount = 1U,
            });
            WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport deferredLightingPass missing");
            return false;
        }
        SceneRenderSubmitStats deferredStats{};
        WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport deferred lighting pass begin");
        if (!deferredLightingPass_->Submit(SceneDeferredLightingPassDesc{
                .viewId = viewportPlan.viewIds.deferredLighting,
                .frameBuffer = desc.target.frameBuffer,
                .gbuffer = &defaultSceneGBuffer_,
                .renderScene = &renderScene,
                .camera = sceneCamera,
                .lightingConfig = effectiveLightingConfig,
                .extent = desc.target.viewport.extent,
                .clearRgba = desc.clearRgba,
            }, deferredStats)) {
            lastSceneDiagnostics_.events.push_back(SceneRenderDiagnosticEvent{
                .severity = SceneRenderDiagnosticSeverity::Error,
                .kind = SceneRenderDiagnosticKind::DeferredRendererUnavailable,
                .instanceCount = 1U,
            });
            WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport deferred lighting pass failed");
            return false;
        }
        {
            std::ostringstream message;
            message << "SubmitSceneToViewport deferred lighting pass end drawCalls=" << deferredStats.submittedDrawCallCount
                    << " submittedMeshes=" << deferredStats.submittedMeshCount;
            WriteRendererBreadcrumb("renderer", message.str());
        }
        lastSceneSubmitStats_ += deferredStats;
        lastScenePassSubmitStats_.push_back(SceneRenderPassSubmitStats{
            .viewportId = desc.target.viewport.id.value,
            .viewportIndex = desc.target.viewport.viewportIndex,
            .renderPass = RenderPassKind::DeferredLighting,
            .pass = MeshPassType::GBuffer,
            .stats = deferredStats,
        });
    } else {
        WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport opaque pass begin");
        RendererMeshPassSubmitter::SubmitViewportPass(
            meshPassSubmitDesc,
            viewportPlan.viewIds.opaqueScene,
            RenderPassKind::OpaqueScene,
            MeshPassType::BaseOpaque,
            shadowBinding.IsValid() ? &shadowBinding : nullptr);
        WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport opaque pass end");
    }
    if (desc.meshPassMode == SceneRenderMeshPassMode::OpaqueAndTransparent) {
        if (bgfx::isValid(desc.target.colorTexture) && bgfx::isValid(desc.postProcess.pingTexture)) {
            WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport transparent sceneColor blit begin");
            bgfx::blit(viewportPlan.viewIds.transparentScene, desc.postProcess.pingTexture, 0U, 0U, desc.target.colorTexture);
            sceneRenderer_->SetSceneColorTexture(desc.postProcess.pingTexture);
            WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport transparent sceneColor blit end");
        }
        WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport transparent pass begin");
        RendererMeshPassSubmitter::SubmitViewportPass(
            meshPassSubmitDesc,
            viewportPlan.viewIds.transparentScene,
            RenderPassKind::TransparentScene,
            MeshPassType::BaseTransparent,
            shadowBinding.IsValid() ? &shadowBinding : nullptr);
        sceneRenderer_->SetSceneColorTexture(BGFX_INVALID_HANDLE);
        WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport transparent pass end");
    }
    if (desc.selectionMaskEnabled) {
        WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport selection mask setup begin");
        editorPassSubmitter_.SubmitSelectionMask(viewportPlan, desc);
        WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport selection mask setup end");
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
        WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport selection mask mesh pass begin");
        RendererMeshPassSubmitter::SubmitSelectionMask(selectionMaskSubmitDesc);
        WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport selection mask mesh pass end");
    }

    if (desc.finalComposite.enabled && finalCompositePass_ != nullptr) {
        WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport final composite branch begin");
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
                WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport missing scenePostProcessRenderer");
                return false;
            }
            WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport postProcess Evaluate begin");
            postProcessOutput = postProcessChain_.Evaluate(PostProcessInput{
                .sceneColor = desc.target.colorTexture,
                .selectionMask = desc.postProcess.selectionMaskTexture,
                .outputFrameBuffer = desc.postProcess.finalFrameBuffer,
                .outputColor = desc.postProcess.finalTexture,
                .extent = desc.target.viewport.extent,
            });
            ApplyPostProcessSettingsOverride(postProcessOutput, desc.postProcessSettings);
            {
                std::ostringstream message;
                message << "SubmitSceneToViewport postProcess Evaluate end valid=" << BoolText(postProcessOutput.IsValid())
                        << " gpuSubmitted=" << BoolText(postProcessOutput.gpuSubmitted)
                        << " enabledPassCount=" << postProcessOutput.enabledPassCount;
                WriteRendererBreadcrumb("renderer", message.str());
            }
        }
        if (!postProcessOutput.IsValid()) {
            WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport postProcessOutput invalid");
            return false;
        }
        if (desc.postProcessEnabled) {
            WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport exposure submit begin");
            lastSceneExposureStats_.push_back(RendererExposureSubmitter::Submit(
                sceneExposureMeter_,
                postProcessOutput,
                desc,
                viewportPlan,
                renderScene,
                effectiveLightingConfig,
                lastCompletedFrame_));
            WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport exposure submit end");

            TemporalViewportState& temporalState = TemporalStateFor(desc.target.viewport.id, desc.target.viewport.viewportIndex);
            WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport postProcess Submit begin");
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
                WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport postProcess Submit invalid output");
                return false;
            }
            {
                std::ostringstream message;
                message << "SubmitSceneToViewport postProcess Submit end outputTex=" << HandleValue(scenePostProcessOutput);
                WriteRendererBreadcrumb("renderer", message.str());
            }
        }
        WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport final composite submit begin");
        if (!RendererFinalCompositeSubmitter::Submit(*finalCompositePass_, viewportPlan, desc, postProcessOutput, scenePostProcessOutput)) {
            WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport final composite submit failed");
            return false;
        }
        WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport final composite submit end");

        WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport editor overlay submit begin");
        RendererEditorOverlaySubmitter::Submit(
            editorPassSubmitter_,
            viewportPlan,
            desc,
            overlayCamera,
            desc.selectionOutlineEnabled && postProcessOutput.selectionOutlineEnabled);
        WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport editor overlay submit end");
        WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport end ok finalComposite");
        return true;
    }

    WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport editor overlay submit begin noFinalComposite");
    RendererEditorOverlaySubmitter::Submit(editorPassSubmitter_, viewportPlan, desc, overlayCamera, true);
    WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport editor overlay submit end noFinalComposite");
    WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport end ok noFinalComposite");
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
    defaultSceneGBuffer_.Shutdown();
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

MaterialProgramRegistryStats Renderer::MaterialProgramStats() const noexcept {
    return sceneRenderer_ != nullptr ? sceneRenderer_->MaterialProgramStats() : MaterialProgramRegistryStats{};
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
        .defaultMaterialFallbackCount = lastDefaultMaterialFallbackCount_,
        .errorMaterialFallbackCount = lastErrorMaterialFallbackCount_,
        .materialLoadedCount = lastMaterialLoadedCount_,
        .materialFallbackCount = lastMaterialFallbackCount_,
        .materialErrorCount = lastMaterialErrorCount_,
        .materialReloadCount = lastMaterialReloadCount_,
        .materialResolverDiagnosticCount = lastMaterialResolverDiagnosticCount_,
        .graphMaterialCpuFallbackCount = lastGraphMaterialCpuFallbackCount_,
        .graphMaterialGpuCount = lastGraphMaterialGpuCount_,
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

void Renderer::SetGraphShaderCacheRoot(std::string root) {
    graphShaderCacheRoot_ = std::move(root);
    if (sceneRenderer_ != nullptr) {
        sceneRenderer_->SetGraphShaderCacheRoot(graphShaderCacheRoot_);
    }
}

const std::string& Renderer::GraphShaderCacheRoot() const noexcept {
    return graphShaderCacheRoot_;
}

void Renderer::SetFrameDeltaSeconds(float seconds) noexcept {
    frameDeltaSeconds_ = seconds;
}

float Renderer::FrameDeltaSeconds() const noexcept {
    return frameDeltaSeconds_;
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

void Renderer::SetRuntimeAssetDiscoveryEnabled(bool enabled) noexcept {
    runtimeAssetDiscovery_.SetDiscoveryEnabled(enabled);
}

bool Renderer::RuntimeAssetDiscoveryEnabled() const noexcept {
    return runtimeAssetDiscovery_.DiscoveryEnabled();
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
