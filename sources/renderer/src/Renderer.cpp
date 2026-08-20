#include "kb/render/Renderer.hpp"

#include "kb/render/BgfxContext.hpp"
#include "kb/render/RendererCapabilityReport.hpp"
#include "kb/render/RenderSurface.hpp"
#include "kb/render/SceneDeferredLightingPass.hpp"
#include "kb/render/ViewIdPolicy.hpp"
#include "engine/ecs/WorkerPool.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAuxFrameComponents.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/ScenePostProcessAccess.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "kb/render/resources/PostProcessProfileAssetLoader.hpp"
#include "kb/render/scene/EcsRenderSceneSynchronizer.hpp"
#include "kb/render/scene/SceneParticleRenderSynchronizer.hpp"
#include "scene/AuxFrameRenderer.hpp"
#include "kb/render/scene/RenderScene.hpp"
#include "kb/render/scene/SceneRenderer.hpp"
#include "kb/render/post/SceneExposureMeter.hpp"
#include "scene/SceneRenderVisibilityPublisher.hpp"
#include "renderer/RendererScreenCapture.hpp"
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

[[nodiscard]] SceneRenderLightingConfig ApplyAmbientRadiance(
    SceneRenderLightingConfig config,
    const std::optional<SceneRenderAmbientRadiance>& ambientRadiance) noexcept {
    if (!ambientRadiance.has_value()) return config;
    const SceneRenderAmbientRadiance& ambient = *ambientRadiance;
    config.ambientColor = ambient.color;
    config.ambientIntensity = ambient.intensity;
    config.environmentZenithColor = ambient.zenithColor;
    config.environmentGroundColor = ambient.horizonColor;
    config.environmentDiffuseIntensity = ambient.diffuseIntensity;
    config.environmentSpecularIntensity = ambient.specularIntensity;
    switch (ambient.mode) {
    case SceneRenderAmbientRadianceMode::Constant:
        config.environmentMode = SceneRenderEnvironmentMode::Constant;
        break;
    case SceneRenderAmbientRadianceMode::Gradient:
    case SceneRenderAmbientRadianceMode::ProceduralSky:
    case SceneRenderAmbientRadianceMode::CapturedEnvironment:
    case SceneRenderAmbientRadianceMode::EstimatedEnvironment:
        config.environmentMode = SceneRenderEnvironmentMode::Hemisphere;
        break;
    case SceneRenderAmbientRadianceMode::EnvironmentMap:
        config.environmentMode = SceneRenderEnvironmentMode::ImageBased;
        break;
    }
    return config;
}

[[nodiscard]] std::uint32_t PackOpaqueRgba(const std::array<float, 3>& color) noexcept {
    const auto channel = [](float value) noexcept -> std::uint32_t {
        return static_cast<std::uint32_t>(std::clamp(value, 0.0F, 1.0F) * 255.0F + 0.5F);
    };
    return (channel(color[0]) << 24U) | (channel(color[1]) << 16U) | (channel(color[2]) << 8U) | 0xFFU;
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

[[nodiscard]] const char* DebugViewName(SceneRenderDebugView view) noexcept {
    switch (view) {
    case SceneRenderDebugView::None:
        return "None";
    case SceneRenderDebugView::GBufferNormal:
        return "GBufferNormal";
    }
    return "Unknown";
}

[[nodiscard]] const char* MeshPassModeName(SceneRenderMeshPassMode mode) noexcept {
    switch (mode) {
    case SceneRenderMeshPassMode::OpaqueOnly:
        return "OpaqueOnly";
    case SceneRenderMeshPassMode::OpaqueAndTerrainLayers:
        return "OpaqueAndTerrainLayers";
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

// LIB-142: resolves the scene's own asset-based PostProcessProfile (kb::scene::
// ScenePostProcessAccess::ActiveProfile) into a live ScenePostProcessSettings value - the
// ONLY engine-side (script/scene-driven) producer of post-process settings; every other
// producer today is editor viewport code supplying RenderSceneSubmitDesc::postProcessSettings
// directly (see this ticket's own research). Called only when the caller's own desc did NOT
// already supply an explicit override, so an editor/player caller's explicit per-submit
// override still always wins - this is purely an ADDITIVE fallback, not a new precedence
// rule. An unset (0) or unresolvable profile asset id honestly resolves to std::nullopt (no
// override at all, falling through to defaultPostProcessSettings_ exactly as before this
// ticket), never a crash - the same "unresolvable reference silently falls back" shape every
// other renderer-consumed asset reference already follows.
[[nodiscard]] std::optional<ScenePostProcessSettings> ResolveScenePostProcessProfile(const kb::scene::Scene& scene) {
    const std::uint64_t profileAssetId = kb::scene::ScenePostProcessAccess::ActiveProfile(scene);
    if (profileAssetId == 0U) {
        return std::nullopt;
    }
    kb::assets::AssetManager& manager = const_cast<kb::scene::Scene&>(scene).Assets().Manager();
    const kb::assets::AssetHandle<ScenePostProcessSettings> handle =
        manager.Load<ScenePostProcessSettings>(kb::assets::AssetId{ profileAssetId });
    if (!handle.IsLoaded()) {
        return std::nullopt;
    }
    return *handle;
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

[[nodiscard]] bool ResolveSceneColorForSampling(
    const RenderViewportPlan& viewportPlan,
    const RenderSceneSubmitDesc& desc,
    bgfx::TextureHandle& sampledSceneColor) {
    sampledSceneColor = desc.target.colorTexture;
    if (!desc.target.RequiresColorResolve()) {
        return true;
    }

    {
        std::ostringstream message;
        message << "MSAA color resolve requested"
                << " viewId=" << viewportPlan.viewIds.sceneOverlays
                << " samples=" << static_cast<unsigned>(desc.target.msaaSamples)
                << " source=" << HandleValue(desc.target.colorTexture)
                << " resolved=" << HandleValue(desc.target.resolvedColorTexture)
                << " extent=" << desc.target.viewport.extent.width << 'x' << desc.target.viewport.extent.height;
        WriteRendererBreadcrumb("aa_trace", message.str());
    }
    const bgfx::Caps* caps = bgfx::getCaps();
    if (caps == nullptr || (caps->supported & BGFX_CAPS_TEXTURE_BLIT) == 0U) {
        WriteRendererBreadcrumb("aa_trace", "MSAA color resolve failed: BGFX_CAPS_TEXTURE_BLIT unavailable");
        return false;
    }
    if (!bgfx::isValid(desc.target.colorTexture) || !bgfx::isValid(desc.target.resolvedColorTexture)) {
        std::ostringstream message;
        message << "MSAA color resolve failed invalid handles"
                << " source=" << HandleValue(desc.target.colorTexture)
                << " resolved=" << HandleValue(desc.target.resolvedColorTexture);
        WriteRendererBreadcrumb("aa_trace", message.str());
        return false;
    }

    bgfx::blit(viewportPlan.viewIds.sceneOverlays, desc.target.resolvedColorTexture, 0U, 0U, desc.target.colorTexture);
    sampledSceneColor = desc.target.resolvedColorTexture;
    {
        std::ostringstream message;
        message << "MSAA color resolve submitted"
                << " viewId=" << viewportPlan.viewIds.sceneOverlays
                << " source=" << HandleValue(desc.target.colorTexture)
                << " sampled=" << HandleValue(sampledSceneColor);
        WriteRendererBreadcrumb("aa_trace", message.str());
    }
    return true;
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
    renderSceneSynchronizer_->SetSkinningPaletteAllocator(&sceneRenderer_->SkinningPalettes());
    particleRenderSynchronizer_ = std::make_unique<SceneParticleRenderSynchronizer>();
    auxFrameRenderer_ = std::make_unique<AuxFrameRenderer>();
    screenCapture_ = std::make_unique<RendererScreenCapture>();
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
    if (auxFrameRenderer_ != nullptr) {
        auxFrameRenderer_->Shutdown(sceneRenderer_.get());
        auxFrameRenderer_.reset();
    }
    runtimeResourceCache_.DestroyAll(sceneRenderer_.get());
    frameReferences_.Clear();
    runtimeAssetDiscovery_.Clear();
    lastRuntimeMaterialLightingPath_.reset();
    lastRuntimeMaterialDebugView_.reset();
    lastRuntimeMaterialQualityLevel_.reset();
    lastRuntimeMaterialFeatureLevel_.reset();
    lastRuntimeMaterialShaderStage_.reset();
    lastRuntimeMaterialVariantUsage_.reset();
    sceneExposureMeter_.ShutdownGpuResources();
    editorPassSubmitter_.Shutdown();
    defaultShadowMap_.Shutdown();
    defaultPostProcessTargets_.Shutdown();
    for (SceneGBuffer& gbuffer : sceneGBuffers_) {
        gbuffer.Shutdown();
    }
    defaultSceneTarget_.Shutdown();
    renderSceneSynchronizer_.reset();
    particleRenderSynchronizer_.reset();
    if (screenCapture_ != nullptr) {
        screenCapture_->Shutdown();
        screenCapture_.reset();
    }
    if (finalCompositePass_ != nullptr) {
        finalCompositePass_->Shutdown();
        finalCompositePass_.reset();
    }
    if (deferredLightingPass_ != nullptr) {
        deferredLightingPass_->Shutdown();
        deferredLightingPass_.reset();
    }
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
        skinningSynchronizedSceneIds_.clear();
        frameState_.Begin(static_cast<std::uint64_t>(lastCompletedFrame_) + 1ULL);
        if (sceneRenderer_ != nullptr) {
            static_cast<void>(sceneRenderer_->BeginSkinningFrame(
                static_cast<std::uint64_t>(lastCompletedFrame_) + 1ULL,
                static_cast<std::uint64_t>(lastCompletedFrame_)));
        }
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
            .resolvedColorTexture = defaultSceneTarget_.ResolvedColorTexture(),
            .depthTexture = defaultSceneTarget_.DepthTexture(),
            .viewport = RenderViewportDesc{
                .id = RenderViewportId{ 1U },
                .extent = extent,
                .viewportIndex = 0U,
            },
            .colorFormat = defaultSceneTarget_.ColorSelection().format,
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
    lastAaPipelineTraceLines_.clear();
    lastAaPipelineTraceLines_.reserve(submissions.size() * 3U);
    lastSceneDiagnostics_.Clear();
    lastResolvedPostProcessSettings_ = std::nullopt;
    lastUnresolvedMaterialTexturePathCount_ = 0U;
    lastDefaultMaterialFallbackCount_ = 0U;
    lastErrorMaterialFallbackCount_ = 0U;
    lastMaterialLoadedCount_ = 0U;
    lastMaterialFallbackCount_ = 0U;
    lastMaterialErrorCount_ = 0U;
    lastMaterialReloadCount_ = 0U;
    lastMaterialResolverDiagnosticCount_ = 0U;
    if (sceneRenderer_ != nullptr) {
        // graphMaterialGpuCount/CpuFallback are now the TRUE per-material draw outcome (a graph
        // material with no cooked binary renders the builtin flatten and counts as a fallback),
        // accumulated across this submit's viewports/passes. Clear it before rendering — this
        // replaces the resolve-time renderMode counting that used to run in the material ensurer.
        sceneRenderer_->ResetGraphMaterialDrawStats();
    }
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

    std::vector<SceneFrameSubmission> expandedSubmissions{submissions.begin(), submissions.end()};
    std::vector<AuxFramePanoramaConversion> panoramaConversions;
    std::array<bool, 7U> occupiedViewportIndices{};
    for (const SceneFrameSubmission& submission : expandedSubmissions) {
        if (submission.desc.target.viewport.viewportIndex < occupiedViewportIndices.size()) {
            occupiedViewportIndices[submission.desc.target.viewport.viewportIndex] = true;
        }
    }
    std::vector<const kb::scene::Scene*> auxScenes;
    auxScenes.reserve(expandedSubmissions.size());
    if (auxFrameRenderer_ != nullptr && renderSceneSynchronizer_ != nullptr) {
        for (const SceneFrameSubmission& submission : expandedSubmissions) {
            if (submission.scene == nullptr || std::find(auxScenes.begin(), auxScenes.end(), submission.scene) != auxScenes.end()) {
                continue;
            }
            bool hasAuxFrame = false;
            submission.scene->Components().AuxFrames().ForEach([](kb::scene::SceneEntity, const kb::scene::AuxFrameComponent& component, void* raw) {
                static_cast<void>(component);
                *static_cast<bool*>(raw) = true;
            }, &hasAuxFrame);
            if (!hasAuxFrame && !auxFrameRenderer_->HasSceneOutputs(submission.scene->Id())) {
                continue;
            }
            renderSceneSynchronizer_->Sync(*submission.scene, RenderSceneFor(*submission.scene));
            auxScenes.push_back(submission.scene);
            for (SceneFrameSubmission& expanded : expandedSubmissions) {
                if (expanded.scene == submission.scene) {
                    expanded.desc.synchronizeScene = false;
                    expanded.desc.transformAffineSync = false;
                    expanded.desc.dirtySceneEntityIds = {};
                }
            }
        }
        std::vector<std::uint32_t> availableViewportIndices;
        for (std::uint32_t index = 1U; index < occupiedViewportIndices.size(); ++index) {
            if (!occupiedViewportIndices[index]) {
                availableViewportIndices.push_back(index);
            }
        }
        auxFrameRenderer_->BeginFrame();
        std::vector<AuxFramePreparedSubmission> auxSubmissions;
        for (const kb::scene::Scene* scene : auxScenes) {
            if (availableViewportIndices.empty()) {
                break;
            }
            const auto source = std::find_if(expandedSubmissions.begin(), expandedSubmissions.end(), [scene](const SceneFrameSubmission& candidate) {
                return candidate.scene == scene;
            });
            if (source == expandedSubmissions.end()) {
                continue;
            }
            const std::size_t previousCount = auxSubmissions.size();
            auxFrameRenderer_->Collect(
                *scene,
                RenderSceneFor(*scene),
                source->desc,
                availableViewportIndices,
                static_cast<std::uint64_t>(lastCompletedFrame_) + 1ULL,
                *sceneRenderer_,
                auxSubmissions,
                panoramaConversions);
            const std::size_t consumed = auxSubmissions.size() - previousCount;
            if (consumed > 0U) {
                availableViewportIndices.erase(availableViewportIndices.begin(), availableViewportIndices.begin() + static_cast<std::ptrdiff_t>(consumed));
            }
        }
        expandedSubmissions.reserve(expandedSubmissions.size() + auxSubmissions.size());
        for (AuxFramePreparedSubmission& auxiliary : auxSubmissions) {
            expandedSubmissions.push_back(SceneFrameSubmission{.scene = auxiliary.scene, .desc = std::move(auxiliary.desc)});
        }
    }
    const std::span<const SceneFrameSubmission> submitList{expandedSubmissions};

    RenderFrameDesc frameDesc{};
    frameDesc.frameIndex = static_cast<std::uint64_t>(lastCompletedFrame_) + 1ULL;
    frameDesc.viewports.reserve(submitList.size());
    for (std::size_t index = 0; index < submitList.size(); ++index) {
        const SceneFrameSubmission& submission = submitList[index];
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
    if (!plan.Succeeded() || plan.viewports.size() != submitList.size()) {
        std::ostringstream message;
        message << "SubmitScenes BuildFramePlan failed succeeded=" << BoolText(plan.Succeeded())
                << " planViewports=" << plan.viewports.size()
                << " submissions=" << submitList.size();
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

    for (std::size_t index = 0; index < submitList.size(); ++index) {
        {
            std::ostringstream message;
            message << "SubmitScenes SubmitSceneToViewport begin index=" << index
                    << " viewportId=" << submitList[index].desc.target.viewport.id.value
                    << " viewportIndex=" << submitList[index].desc.target.viewport.viewportIndex;
            WriteRendererBreadcrumb("renderer", message.str());
        }
        if (!SubmitSceneToViewport(*submitList[index].scene, submitList[index].desc, plan.viewports[index])) {
            std::ostringstream message;
            message << "SubmitScenes SubmitSceneToViewport failed index=" << index
                    << " viewportId=" << submitList[index].desc.target.viewport.id.value
                    << " viewportIndex=" << submitList[index].desc.target.viewport.viewportIndex;
            WriteRendererBreadcrumb("renderer", message.str());
            return false;
        }
        {
            std::ostringstream message;
            message << "SubmitScenes SubmitSceneToViewport end index=" << index
                    << " viewportId=" << submitList[index].desc.target.viewport.id.value
                    << " viewportIndex=" << submitList[index].desc.target.viewport.viewportIndex;
            WriteRendererBreadcrumb("renderer", message.str());
        }
    }
    // The conversion view belongs to the final-composite slot of its last
    // cubemap face. Submit it only after frame-plan view ordering and all face
    // submissions have been configured.
    for (const AuxFramePanoramaConversion conversion : panoramaConversions) {
        if (auxFrameRenderer_ == nullptr || !auxFrameRenderer_->SubmitPanoramaConversion(conversion)) {
            return false;
        }
    }
    std::vector<const kb::scene::Scene*> submittedScenes;
    submittedScenes.reserve(submitList.size());
    for (const SceneFrameSubmission& submission : submitList) {
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
                << " depthTex=" << HandleValue(desc.target.depthTexture)
                << " targetMsaaSamples=" << static_cast<unsigned>(desc.target.msaaSamples);
        WriteRendererBreadcrumb("renderer", message.str());
    }
    if (desc.meshPassMode != SceneRenderMeshPassMode::OpaqueOnly &&
        desc.meshPassMode != SceneRenderMeshPassMode::OpaqueAndTerrainLayers &&
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
    if (particleRenderSynchronizer_ == nullptr || screenCapture_ == nullptr) {
        WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport missing particleRenderSynchronizer");
        return false;
    }
    WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport RenderSceneFor begin");
    RenderScene& renderScene = RenderSceneFor(scene);
    WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport RenderSceneFor end");
    const bool skinningAlreadySynchronized = std::ranges::find(
        skinningSynchronizedSceneIds_, scene.Id()) != skinningSynchronizedSceneIds_.end();
    if (desc.synchronizeScene) {
        WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport Sync full begin");
        renderSceneSynchronizer_->Sync(scene, renderScene);
        if (!skinningAlreadySynchronized) {
            skinningSynchronizedSceneIds_.push_back(scene.Id());
        }
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
        // Deformed proxies store frame-local palette handles. Even when the ECS scene and its
        // transforms are unchanged, a new renderer frame needs fresh palette uploads; retaining
        // the previous handle made a Skeletal Mesh visible for two frames and then disappear.
        if (!skinningAlreadySynchronized) {
            renderSceneSynchronizer_->SyncDeformedMeshPalettes(scene, renderScene);
            skinningSynchronizedSceneIds_.push_back(scene.Id());
        }
    }
    // History ribbons own transient samples in the render synchronizer. Full
    // sync already advances them; this idempotent call also covers the normal
    // transform/dirty-entity incremental path.
    renderSceneSynchronizer_->AdvanceHistoryRibbons(scene, renderScene);
    renderSceneSynchronizer_->SyncLensEchoes(scene, renderScene, desc.target.viewport.id.value);
    // Retain one view-independent, immutable simulation snapshot in renderer state.
    // GPU batching and alignment remain per-view work in the transparent pass.
    WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport particle sync begin");
    particleRenderSynchronizer_->Sync(scene, renderScene);
    if (const auto& particleSnapshot = renderScene.ParticleRenderSnapshot(); particleSnapshot != nullptr) {
        for (const kb::particles::ParticleRenderEmitterRecord& emitter : particleSnapshot->Emitters()) {
            if (emitter.textureAtlasAssetId != 0U) {
                frameReferences_.MarkTexture(RuntimeTextureAssetKey{
                    .sceneId = scene.Id(),
                    .assetId = emitter.textureAtlasAssetId,
                    .colorSpace = RenderTextureColorSpace::Srgb,
                });
            }
        }
    }
    WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport particle sync end");
    SceneRenderLightingConfig effectiveLightingConfig = ApplyAmbientRadiance(
        RendererSceneLightingConfigResolver::Resolve(desc.lightingConfig, defaultSceneLightingConfig_), renderScene.AmbientRadiance());
    if (!desc.shadowPassEnabled) {
        effectiveLightingConfig.shadowsEnabled = false;
    }
    const std::optional<SceneRenderWorldBackdrop>& worldBackdrop = renderScene.WorldBackdrop();
    if (worldBackdrop.has_value() &&
        worldBackdrop->mode == SceneRenderWorldBackdropMode::EnvironmentMap &&
        worldBackdrop->environmentAssetId != 0U) {
        frameReferences_.MarkTexture(RuntimeTextureAssetKey{
            .sceneId = scene.Id(),
            .assetId = worldBackdrop->environmentAssetId,
            .colorSpace = RenderTextureColorSpace::Linear,
        });
    }
    const std::optional<SceneRenderAmbientRadiance>& ambientRadiance = renderScene.AmbientRadiance();
    if (ambientRadiance.has_value() &&
        ambientRadiance->mode == SceneRenderAmbientRadianceMode::EnvironmentMap &&
        ambientRadiance->environmentAssetId != 0U) {
        frameReferences_.MarkTexture(RuntimeTextureAssetKey{
            .sceneId = scene.Id(),
            .assetId = ambientRadiance->environmentAssetId,
            .colorSpace = RenderTextureColorSpace::Linear,
        });
    }
    const bool backdropRequiresDeferredPass = worldBackdrop.has_value() &&
        (worldBackdrop->mode == SceneRenderWorldBackdropMode::VerticalGradient ||
         worldBackdrop->mode == SceneRenderWorldBackdropMode::ProceduralSky ||
         worldBackdrop->mode == SceneRenderWorldBackdropMode::EnvironmentMap);
    const bool deferredLighting = UsesDeferredLighting(effectiveLightingConfig.lightingPath) ||
        effectiveLightingConfig.debugView == SceneRenderDebugView::GBufferNormal || backdropRequiresDeferredPass;
    RenderMaterialGraphBuildContext runtimeGraphContext = desc.materialGraphContext;
    runtimeGraphContext.shadingPath = deferredLighting
        ? RenderMaterialGraphShadingPath::Deferred
        : effectiveLightingConfig.lightingPath == SceneRenderLightingPath::ClusteredForwardPlus
            ? RenderMaterialGraphShadingPath::ForwardPlus
            : RenderMaterialGraphShadingPath::Forward;
    runtimeMaterialResolver_.SetGraphBuildContext(std::move(runtimeGraphContext));
    if (!lastRuntimeMaterialLightingPath_.has_value() ||
        *lastRuntimeMaterialLightingPath_ != effectiveLightingConfig.lightingPath ||
        !lastRuntimeMaterialDebugView_.has_value() ||
        *lastRuntimeMaterialDebugView_ != effectiveLightingConfig.debugView ||
        !lastRuntimeMaterialQualityLevel_.has_value() ||
        *lastRuntimeMaterialQualityLevel_ != runtimeGraphContext.qualityLevel ||
        !lastRuntimeMaterialFeatureLevel_.has_value() ||
        *lastRuntimeMaterialFeatureLevel_ != runtimeGraphContext.featureLevel ||
        !lastRuntimeMaterialShaderStage_.has_value() ||
        *lastRuntimeMaterialShaderStage_ != runtimeGraphContext.shaderStage ||
        !lastRuntimeMaterialVariantUsage_.has_value() ||
        *lastRuntimeMaterialVariantUsage_ != runtimeGraphContext.variantUsage) {
        runtimeResourceCache_.InvalidateMaterials(sceneRenderer_.get());
        lastRuntimeMaterialLightingPath_ = effectiveLightingConfig.lightingPath;
        lastRuntimeMaterialDebugView_ = effectiveLightingConfig.debugView;
        lastRuntimeMaterialQualityLevel_ = runtimeGraphContext.qualityLevel;
        lastRuntimeMaterialFeatureLevel_ = runtimeGraphContext.featureLevel;
        lastRuntimeMaterialShaderStage_ = runtimeGraphContext.shaderStage;
        lastRuntimeMaterialVariantUsage_ = runtimeGraphContext.variantUsage;
        WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport runtime material cache invalidated for lighting path/debug view change");
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
        .currentFrame = static_cast<std::uint64_t>(lastCompletedFrame_) + 1ULL,
    });
    {
        std::ostringstream message;
        message << "SubmitSceneToViewport EnsureSceneResources end materialLoaded=" << lastMaterialLoadedCount_
                << " materialFallback=" << lastMaterialFallbackCount_
                << " diagnostics=" << lastSceneDiagnostics_.events.size();
        WriteRendererBreadcrumb("renderer", message.str());
    }
    {
        std::ostringstream message;
        message << "SubmitSceneToViewport lighting resolved path=" << LightingPathName(effectiveLightingConfig.lightingPath)
                << " deferred=" << BoolText(deferredLighting)
                << " shadows=" << BoolText(effectiveLightingConfig.shadowsEnabled)
                << " debugView=" << DebugViewName(effectiveLightingConfig.debugView);
        WriteRendererBreadcrumb("renderer", message.str());
    }
    if (desc.target.viewport.viewportIndex >= sceneGBuffers_.size()) {
        WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport viewport index has no GBuffer owner");
        return false;
    }
    SceneGBuffer& sceneGBuffer = sceneGBuffers_[desc.target.viewport.viewportIndex];
    if (deferredLighting && !sceneGBuffer.Ensure(SceneGBufferDesc{ .extent = desc.target.viewport.extent })) {
        lastSceneDiagnostics_.events.push_back(SceneRenderDiagnosticEvent{
            .severity = SceneRenderDiagnosticSeverity::Error,
            .kind = SceneRenderDiagnosticKind::DeferredRendererUnavailable,
            .instanceCount = 1U,
        });
        WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport GBuffer Ensure failed");
        return false;
    }
    if (deferredLighting) {
        const SceneGBufferFormatSelection selection = sceneGBuffer.FormatSelection();
        std::ostringstream message;
        message << "SubmitSceneToViewport GBuffer Ensure end ok"
                << " fb=" << HandleValue(sceneGBuffer.FrameBuffer())
                << " albedoTex=" << HandleValue(sceneGBuffer.AlbedoTexture())
                << " normalTex=" << HandleValue(sceneGBuffer.NormalTexture())
                << " materialTex=" << HandleValue(sceneGBuffer.MaterialTexture())
                << " surfaceTex=" << HandleValue(sceneGBuffer.SurfaceTexture())
                << " depthTex=" << HandleValue(sceneGBuffer.DepthTexture())
                << " extent=" << sceneGBuffer.Width() << 'x' << sceneGBuffer.Height()
                << " formats=(" << SceneTextureFormatName(selection.albedoFormat)
                << ',' << SceneTextureFormatName(selection.normalFormat)
                << ',' << SceneTextureFormatName(selection.materialFormat)
                << ',' << SceneTextureFormatName(selection.surfaceFormat)
                << ',' << SceneTextureFormatName(selection.depth.format) << ')'
                << " targetFb=" << HandleValue(desc.target.frameBuffer)
                << " finalFb=" << HandleValue(desc.finalComposite.frameBuffer);
        WriteRendererBreadcrumb("renderer", message.str());
    }
    // LIB-136: resolve the selected ECS camera's clear settings (if any - cameraOverride
    // callers, e.g. the editor's fly camera, keep desc's own submission-level clear) BEFORE
    // configuring the opaque view's clear state below. This is a cheap, matrix-free lookup
    // (FindPrimaryCameraProxy, not the full BuildPrimaryCamera) so it does not duplicate the
    // real camera resolution work done later in this function. GBuffer/deferred clearing is
    // intentionally NOT affected - see CameraComponent.hpp's CameraClearMode doc comment.
    std::uint16_t opaqueClearFlags = BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH | BGFX_CLEAR_STENCIL;
    std::uint32_t opaqueClearRgba = desc.clearRgba;
    if (!desc.cameraOverride.has_value()) {
        const CameraRenderProxyDesc* clearCameraProxy = renderScene.FindPrimaryCameraProxy(desc.target.viewport.id.value);
        if (clearCameraProxy != nullptr) {
            switch (clearCameraProxy->clearMode) {
            case RenderCameraClearMode::SolidColor:
                opaqueClearFlags = BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH | BGFX_CLEAR_STENCIL;
                break;
            case RenderCameraClearMode::DepthOnly:
                opaqueClearFlags = BGFX_CLEAR_DEPTH | BGFX_CLEAR_STENCIL;
                break;
            case RenderCameraClearMode::DontClear:
                opaqueClearFlags = BGFX_CLEAR_NONE;
                break;
            }
            opaqueClearRgba = PackOpaqueRgba(clearCameraProxy->clearColor);
        }
    }
    if (worldBackdrop.has_value() && worldBackdrop->mode == SceneRenderWorldBackdropMode::SolidColor) {
        opaqueClearRgba = PackOpaqueRgba(worldBackdrop->color);
    }
    bgfx::TextureHandle worldBackdropEnvironment = BGFX_INVALID_HANDLE;
    if (worldBackdrop.has_value() &&
        worldBackdrop->mode == SceneRenderWorldBackdropMode::EnvironmentMap &&
        worldBackdrop->environmentAssetId != 0U) {
        const RenderTextureHandle textureHandle = sceneRenderer_->ResourceMap().ResolveTexture(
            worldBackdrop->environmentAssetId,
            RenderTextureColorSpace::Linear);
        if (const RenderTextureResource* texture = sceneRenderer_->Resources().FindTexture(textureHandle);
            texture != nullptr && texture->dimension == RenderTextureDimension::Texture2D) {
            worldBackdropEnvironment = texture->texture;
        }
    }
    if (deferredLighting) {
        WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport Configure GBuffer clear begin");
        RendererViewConfigurator::ConfigureGBufferClear(
            viewportPlan.viewIds.gbufferGeometry,
            sceneGBuffer.FrameBuffer(),
            desc.target.viewport.extent,
            desc.clearDepth,
            desc.clearStencil);
        WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport Configure GBuffer clear end");
    } else {
        WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport Configure opaque clear begin");
        RendererViewConfigurator::ConfigureSceneClear(viewportPlan.viewIds.opaqueScene, desc, opaqueClearFlags, opaqueClearRgba);
        WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport Configure opaque clear end");
    }
    WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport Configure transparent no-clear begin");
    RendererViewConfigurator::ConfigureSceneNoClear(viewportPlan.viewIds.transparentScene, desc, "KB Scene Transparent");
    WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport Configure transparent no-clear end");

    // MAT-80/#18b: expose the opaque scene depth to the transparent pass so depth-sampling graph materials
    // (SceneDepth / DepthFade) read real geometry depth. Deferred uses the GBuffer depth attachment.
    WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport scene texture bindings begin");
    sceneRenderer_->SetSceneDepthTexture(deferredLighting ? sceneGBuffer.DepthTexture() : desc.target.depthTexture);
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
    const std::optional<SceneRenderCamera> primaryCamera = desc.cameraOverride.has_value() ? std::optional<SceneRenderCamera>{} : renderScene.BuildPrimaryCamera(width, height, desc.target.viewport.id.value);
    const SceneRenderCamera* overlayCamera = desc.cameraOverride.has_value()
        ? &(*desc.cameraOverride)
        : (primaryCamera.has_value() ? &(*primaryCamera) : nullptr);
    // LIB-144: publish the CPU-side per-entity visibility/bounds feedback frame
    // (Renderer.IsVisible/GetBounds/TestFrustum's backing data) into the scene, computed
    // unconditionally (mirrors lastResolvedPostProcessSettings_ above - observable even for
    // a minimal offscreen submission) with the pre-jitter camera this submit actually
    // renders with. The const_cast follows the exact same convention EnsureSceneResources
    // already established a few lines up: a scene's runtime-derived caches are mutable
    // during its own submit. Mesh resources were ensured above, so bounds resolve this same
    // frame; when the same scene is submitted to several viewports, the last submit in the
    // frame's deterministic plan order wins (see SceneRenderFeedback.hpp's contract).
    SceneRenderVisibilityPublisher::BuildFrame(
        renderScene,
        overlayCamera,
        desc.target.viewport.id.value,
        desc.target.viewport.localUserId,
        width,
        height,
        &sceneRenderer_->Resources(),
        &sceneRenderer_->ResourceMap(),
        sceneRenderVisibilityScratch_);
    kb::scene::SceneRenderFeedback::Publish(const_cast<kb::scene::Scene&>(scene), sceneRenderVisibilityScratch_);
    // LIB-145: drive the scene's async screen-capture channel (finish a ready readback,
    // start a newly requested one) - same scene-mutable-during-its-own-submit convention
    // as the feedback publish above.
    screenCapture_->Process(scene, desc, viewportPlan.viewIds, static_cast<std::uint32_t>(lastCompletedFrame_));
    std::optional<SceneRenderCamera> jitteredCamera{};
    const std::uint64_t frameIndex = static_cast<std::uint64_t>(lastCompletedFrame_) + 1ULL;
    // LIB-142: an explicit per-submit override (desc.postProcessSettings) always wins, exactly
    // as before this ticket; only when the caller supplied none do we fall back to the
    // scene's own asset-based active PostProcessProfile, and only when that resolves to
    // nothing does defaultPostProcessSettings_ apply (unchanged pre-LIB-142 behavior).
    const std::optional<ScenePostProcessSettings> resolvedPostProcessSettings =
        desc.postProcessSettings.has_value() ? desc.postProcessSettings : ResolveScenePostProcessProfile(scene);
    lastResolvedPostProcessSettings_ = resolvedPostProcessSettings;
    const bool temporalAntiAliasingEnabled = desc.postProcessEnabled &&
        (resolvedPostProcessSettings.has_value()
                ? resolvedPostProcessSettings->temporalAntiAliasingEnabled
                : defaultPostProcessSettings_.temporalAntiAliasingEnabled);
    const bool temporalJitterEnabled = temporalAntiAliasingEnabled &&
        (resolvedPostProcessSettings.has_value()
                ? resolvedPostProcessSettings->temporalJitterEnabled
                : defaultPostProcessSettings_.temporalJitterEnabled);
    {
        std::ostringstream message;
        message << "Scene receive AA viewportId=" << desc.target.viewport.id.value
                << " viewportIndex=" << desc.target.viewport.viewportIndex
                << " postProcessEnabled=" << BoolText(desc.postProcessEnabled)
                << " postTargetsEnabled=" << BoolText(desc.postProcess.enabled)
                << " targetMsaaSamples=" << static_cast<unsigned>(desc.target.msaaSamples)
                << " overridePresent=" << BoolText(resolvedPostProcessSettings.has_value())
                << " defaultFxaa=" << BoolText(defaultPostProcessSettings_.fxaaEnabled)
                << " defaultTaa=" << BoolText(defaultPostProcessSettings_.temporalAntiAliasingEnabled)
                << " defaultJitter=" << BoolText(defaultPostProcessSettings_.temporalJitterEnabled)
                << " temporalTaaEnabled=" << BoolText(temporalAntiAliasingEnabled)
                << " temporalJitterEnabled=" << BoolText(temporalJitterEnabled)
                << " editorOverlays=" << BoolText(desc.editorSceneOverlaysEnabled);
        if (resolvedPostProcessSettings.has_value()) {
            message << " overrideFxaa=" << BoolText(resolvedPostProcessSettings->fxaaEnabled)
                    << " overrideTaa=" << BoolText(resolvedPostProcessSettings->temporalAntiAliasingEnabled)
                    << " overrideJitter=" << BoolText(resolvedPostProcessSettings->temporalJitterEnabled)
                    << " overrideBloom=" << BoolText(resolvedPostProcessSettings->bloomEnabled);
        }
        WriteRendererBreadcrumb("aa_trace", message.str());
    }
    const std::array<float, 2> jitter = RendererTemporalJitter::Compute(frameIndex, desc.target.viewport.extent, temporalJitterEnabled);
    {
        std::ostringstream message;
        message << "Temporal jitter resolve"
                << " frameIndex=" << frameIndex
                << " enabled=" << BoolText(temporalJitterEnabled)
                << " jitterX=" << jitter[0]
                << " jitterY=" << jitter[1]
                << " extent=" << desc.target.viewport.extent.width << 'x' << desc.target.viewport.extent.height;
        WriteRendererBreadcrumb("aa_trace", message.str());
    }
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
                    << " textureDimensionMismatch=" << stats.textureDimensionMismatchCount
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
                .gbuffer = &sceneGBuffer,
                .renderScene = &renderScene,
                .camera = sceneCamera,
                .lightingConfig = effectiveLightingConfig,
                .extent = desc.target.viewport.extent,
                .clearRgba = worldBackdrop.has_value() && worldBackdrop->mode == SceneRenderWorldBackdropMode::SolidColor
                    ? PackOpaqueRgba(worldBackdrop->color)
                    : desc.clearRgba,
                .shadowMap = shadowBinding.IsValid() ? &shadowBinding : nullptr,
                .worldBackdrop = worldBackdrop.has_value() ? &*worldBackdrop : nullptr,
                .worldBackdropEnvironment = worldBackdropEnvironment,
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
    if (desc.meshPassMode != SceneRenderMeshPassMode::OpaqueOnly) {
        const bool terrainLayersOnly = desc.meshPassMode == SceneRenderMeshPassMode::OpaqueAndTerrainLayers;
        if (!terrainLayersOnly && bgfx::isValid(desc.target.colorTexture) && bgfx::isValid(desc.postProcess.pingTexture)) {
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
            shadowBinding.IsValid() ? &shadowBinding : nullptr,
            terrainLayersOnly);
        if (!terrainLayersOnly && sceneRenderer_->LastSubmitStats().failedParticleBatchCount == 0U) {
            const auto& particleSnapshot = renderScene.ParticleRenderSnapshot();
            if (particleSnapshot != nullptr) {
                particleRenderSynchronizer_->Acknowledge(scene, particleSnapshot->FixedStepIndex());
            }
        }
        sceneRenderer_->SetSceneColorTexture(BGFX_INVALID_HANDLE);
        WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport transparent pass end");
    }

    RenderSceneSubmitDesc editorOverlayDesc = desc;
    if (deferredLighting) {
        editorOverlayDesc.editorOverlayDepthTexture = sceneGBuffer.DepthTexture();
    }
    {
        std::ostringstream message;
        message << "SubmitSceneToViewport scene grid submit"
                << " deferred=" << BoolText(deferredLighting)
                << " sceneCamera=" << BoolText(sceneCamera != nullptr)
                << " overlayEnabled=" << BoolText(desc.editorSceneOverlaysEnabled)
                << " targetFb=" << HandleValue(desc.target.frameBuffer)
                << " targetColor=" << HandleValue(desc.target.colorTexture)
                << " targetDepth=" << HandleValue(desc.target.depthTexture)
                << " gridDepth=" << HandleValue(editorOverlayDesc.SceneOverlayDepthTexture())
                << " msaaSamples=" << static_cast<unsigned>(desc.target.msaaSamples)
                << " finalComposite=" << BoolText(desc.finalComposite.enabled)
                << " postProcess=" << BoolText(desc.postProcessEnabled);
        WriteRendererBreadcrumb("grid_trace", message.str());
    }
    editorPassSubmitter_.SubmitSceneOverlays(
        viewportPlan,
        editorOverlayDesc,
        desc.editorSceneOverlaysEnabled ? sceneCamera : nullptr);

    bgfx::TextureHandle sampledSceneColor = desc.target.colorTexture;
    if (!ResolveSceneColorForSampling(viewportPlan, desc, sampledSceneColor)) {
        WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport MSAA color resolve failed");
        return false;
    }
    RenderSceneSubmitDesc sampledSceneDesc = editorOverlayDesc;
    sampledSceneDesc.target.colorTexture = sampledSceneColor;

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
            .color = sampledSceneColor,
            .extent = desc.target.viewport.extent,
            .producer = PostProcessPassKind::IdentityCopy,
            .colorSpace = PostProcessColorSpace::SceneHdr,
            .enabledPassCount = 0U,
            .passthrough = true,
            .gpuSubmitted = false,
            .sceneHdrPreserved = true,
            .tonemapEnabled = true,
        };
        bgfx::TextureHandle scenePostProcessOutput = sampledSceneColor;
        if (desc.postProcessEnabled) {
            if (scenePostProcessRenderer_ == nullptr) {
                WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport missing scenePostProcessRenderer");
                return false;
            }
            WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport postProcess Evaluate begin");
            postProcessOutput = postProcessChain_.Evaluate(PostProcessInput{
                .sceneColor = sampledSceneColor,
                .selectionMask = desc.postProcess.selectionMaskTexture,
                .outputFrameBuffer = desc.postProcess.finalFrameBuffer,
                .outputColor = desc.postProcess.finalTexture,
                .extent = desc.target.viewport.extent,
            });
            {
                std::ostringstream message;
                message << "PostProcess Evaluate raw"
                        << " valid=" << BoolText(postProcessOutput.IsValid())
                        << " enabledPassCount=" << postProcessOutput.enabledPassCount
                        << " rawFxaa=" << BoolText(postProcessOutput.fxaaEnabled)
                        << " rawTaa=" << BoolText(postProcessOutput.temporalAntiAliasingEnabled)
                        << " rawBloom=" << BoolText(postProcessOutput.bloomEnabled)
                        << " producer=" << PostProcessPassKindName(postProcessOutput.producer);
                WriteRendererBreadcrumb("aa_trace", message.str());
            }
            ApplyPostProcessSettingsOverride(postProcessOutput, resolvedPostProcessSettings);
            {
                std::ostringstream message;
                message << "PostProcess after override"
                        << " overridePresent=" << BoolText(resolvedPostProcessSettings.has_value())
                        << " finalFxaa=" << BoolText(postProcessOutput.fxaaEnabled)
                        << " finalTaa=" << BoolText(postProcessOutput.temporalAntiAliasingEnabled)
                        << " finalBloom=" << BoolText(postProcessOutput.bloomEnabled)
                        << " finalJitter=" << BoolText(postProcessOutput.postProcessSettings.temporalJitterEnabled)
                        << " historyBlend=" << postProcessOutput.postProcessSettings.temporalHistoryBlend
                        << " tonemap=" << BoolText(postProcessOutput.tonemapEnabled);
                WriteRendererBreadcrumb("aa_trace", message.str());
            }
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
                sampledSceneDesc,
                viewportPlan,
                renderScene,
                effectiveLightingConfig,
                lastCompletedFrame_));
            WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport exposure submit end");

            TemporalViewportState& temporalState = TemporalStateFor(desc.target.viewport.id, desc.target.viewport.viewportIndex);
            const bool temporalHistoryValid = temporalState.hasHistory && temporalState.extent == sampledSceneDesc.target.viewport.extent;
            const std::array<float, 16> currentUnjitteredViewProjection = overlayCamera == nullptr
                ? RendererMatrixMath::Identity()
                : RendererMatrixMath::ViewProjection(*overlayCamera);
            const std::array<float, 16> previousMotionViewProjection = temporalHistoryValid
                ? temporalState.previousViewProjection
                : currentUnjitteredViewProjection;
            {
                std::ostringstream message;
                message << "AA renderer submit"
                        << " viewport=" << desc.target.viewport.id.value << ':' << desc.target.viewport.viewportIndex
                        << " lighting=" << LightingPathName(effectiveLightingConfig.lightingPath)
                        << " fxaa=" << BoolText(postProcessOutput.fxaaEnabled)
                        << " taa=" << BoolText(postProcessOutput.temporalAntiAliasingEnabled)
                        << " jitter=" << BoolText(jitter[0] != 0.0F || jitter[1] != 0.0F)
                        << " historyValid=" << BoolText(temporalHistoryValid)
                        << " previousJitterX=" << temporalState.previousJitter[0]
                        << " previousJitterY=" << temporalState.previousJitter[1]
                        << " currentJitterX=" << jitter[0]
                        << " currentJitterY=" << jitter[1]
                        << " historyExtent=" << temporalState.extent.width << 'x' << temporalState.extent.height
                        << " targetExtent=" << sampledSceneDesc.target.viewport.extent.width << 'x' << sampledSceneDesc.target.viewport.extent.height
                        << " sourceColor=" << HandleValue(sampledSceneDesc.target.colorTexture)
                        << " targetColor=" << HandleValue(desc.target.colorTexture)
                        << " resolvedColor=" << HandleValue(desc.target.resolvedColorTexture)
                        << " targetDepth=" << HandleValue(sampledSceneDesc.target.depthTexture)
                        << " overlayDepth=" << HandleValue(sampledSceneDesc.editorOverlayDepthTexture)
                        << " sampledDepth=" << HandleValue(sampledSceneDesc.SceneOverlayDepthTexture())
                        << " motionTex=" << HandleValue(sampledSceneDesc.postProcess.motionVectorTexture)
                        << " historyTex=" << HandleValue(sampledSceneDesc.postProcess.temporalHistoryTextures[static_cast<std::uint8_t>(frameIndex & 1ULL)])
                        << " previousHistoryTex=" << HandleValue(sampledSceneDesc.postProcess.temporalHistoryTextures[static_cast<std::uint8_t>(1ULL - (frameIndex & 1ULL))])
                        << " postFinalTex=" << HandleValue(sampledSceneDesc.postProcess.finalTexture)
                        << " finalComposite=" << BoolText(sampledSceneDesc.finalComposite.enabled);
                const std::string trace = message.str();
                WriteRendererBreadcrumb("aa_trace", trace);
                std::ostringstream consoleMessage;
                consoleMessage << "AA renderer submit"
                               << " viewport=" << desc.target.viewport.id.value << ':' << desc.target.viewport.viewportIndex
                               << " lighting=" << LightingPathName(effectiveLightingConfig.lightingPath)
                               << " fxaa=" << BoolText(postProcessOutput.fxaaEnabled)
                               << " taa=" << BoolText(postProcessOutput.temporalAntiAliasingEnabled)
                               << " jitter=" << BoolText(jitter[0] != 0.0F || jitter[1] != 0.0F)
                               << " jitterComp=explicit"
                               << " historyValid=" << BoolText(temporalHistoryValid)
                               << " extent=" << sampledSceneDesc.target.viewport.extent.width << 'x' << sampledSceneDesc.target.viewport.extent.height
                               << " sampledDepth=" << HandleValue(sampledSceneDesc.SceneOverlayDepthTexture())
                               << " motionTex=" << HandleValue(sampledSceneDesc.postProcess.motionVectorTexture)
                               << " postFinalTex=" << HandleValue(sampledSceneDesc.postProcess.finalTexture)
                               << " finalComposite=" << BoolText(sampledSceneDesc.finalComposite.enabled);
                lastAaPipelineTraceLines_.push_back(consoleMessage.str());
            }
            WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport postProcess Submit begin");
            scenePostProcessOutput = RendererPostProcessSubmitter::Submit(RendererPostProcessSubmitDesc{
                .postProcessRenderer = *scenePostProcessRenderer_,
                .sceneDesc = sampledSceneDesc,
                .viewportPlan = viewportPlan,
                .postProcessOutput = postProcessOutput,
                .sceneCamera = sceneCamera,
                .unjitteredSceneCamera = overlayCamera,
                .jitter = jitter,
                .frameIndex = frameIndex,
                .temporalExtent = temporalState.extent,
                .previousViewProjection = temporalState.previousViewProjection,
                .previousJitter = temporalState.previousJitter,
                .hasTemporalHistory = temporalState.hasHistory,
            });
            if (!bgfx::isValid(scenePostProcessOutput)) {
                WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport postProcess Submit invalid output");
                return false;
            }
            if (postProcessOutput.temporalAntiAliasingEnabled && overlayCamera != nullptr &&
                bgfx::isValid(sampledSceneDesc.postProcess.motionVectorFrameBuffer) &&
                bgfx::isValid(sampledSceneDesc.SceneOverlayDepthTexture())) {
                sceneRenderer_->SetMotionVectorPreviousViewProjection(previousMotionViewProjection);
                const RendererMeshPassSubmitDesc motionVectorSubmitDesc{
                    .sceneRenderer = *sceneRenderer_,
                    .renderScene = renderScene,
                    .sceneDesc = sampledSceneDesc,
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
                RendererMeshPassSubmitter::SubmitViewportPass(
                    motionVectorSubmitDesc,
                    viewportPlan.viewIds.postProcessMotionVectors,
                    RenderPassKind::PostProcessMotionVectors,
                    MeshPassType::MotionVectors,
                    nullptr);
            }
            {
                std::ostringstream message;
                message << "SubmitSceneToViewport postProcess Submit end outputTex=" << HandleValue(scenePostProcessOutput);
                WriteRendererBreadcrumb("renderer", message.str());
            }
        }
        {
            std::ostringstream message;
            message << "AA renderer final"
                    << " viewport=" << desc.target.viewport.id.value << ':' << desc.target.viewport.viewportIndex
                    << " outputTex=" << HandleValue(scenePostProcessOutput)
                    << " producer=" << PostProcessPassKindName(postProcessOutput.producer)
                    << " fxaa=" << BoolText(postProcessOutput.fxaaEnabled)
                    << " taa=" << BoolText(postProcessOutput.temporalAntiAliasingEnabled)
                    << " bloom=" << BoolText(postProcessOutput.bloomEnabled)
                    << " tonemap=" << BoolText(postProcessOutput.tonemapEnabled)
                    << " outputTonemap=" << static_cast<int>(postProcessOutput.outputTransform.tonemap)
                    << " exposure=" << postProcessOutput.outputTransform.exposureStops
                    << " gamma=" << postProcessOutput.outputTransform.gamma
                    << " autoExposure=" << BoolText(postProcessOutput.outputTransform.autoExposure.enabled)
                    << " meteredLum=" << postProcessOutput.outputTransform.autoExposure.meteredAverageLuminance;
            const std::string trace = message.str();
            WriteRendererBreadcrumb("aa_trace", trace);
            lastAaPipelineTraceLines_.push_back(trace);
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
            editorOverlayDesc,
            overlayCamera,
            desc.selectionOutlineEnabled && postProcessOutput.selectionOutlineEnabled);
        WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport editor overlay submit end");
        WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport end ok finalComposite");
        return true;
    }

    WriteRendererBreadcrumb("renderer", "SubmitSceneToViewport editor overlay submit begin noFinalComposite");
    RendererEditorOverlaySubmitter::Submit(editorPassSubmitter_, viewportPlan, editorOverlayDesc, overlayCamera, true);
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

    editorPassSubmitter_.InvalidateFrameBuffers();
    defaultPostProcessTargets_.Shutdown();
    for (SceneGBuffer& gbuffer : sceneGBuffers_) {
        gbuffer.Shutdown();
    }
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

float Renderer::CurrentExposureLuminance() const noexcept {
    return sceneExposureMeter_.CurrentLuminance();
}

bool Renderer::HasExposureHistory() const noexcept {
    return sceneExposureMeter_.HasHistory();
}

void Renderer::PrimeExposureAdaptation(float luminance) noexcept {
    sceneExposureMeter_.Prime(luminance);
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

std::span<const std::string> Renderer::LastAaPipelineTraceLines() const noexcept {
    return lastAaPipelineTraceLines_;
}

const SceneRenderDiagnostics& Renderer::LastSceneDiagnostics() const noexcept {
    return lastSceneDiagnostics_;
}

const std::optional<ScenePostProcessSettings>& Renderer::LastResolvedPostProcessSettings() const noexcept {
    return lastResolvedPostProcessSettings_;
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
        .graphMaterialCpuFallbackCount = sceneRenderer_ != nullptr ? sceneRenderer_->GraphMaterialCpuFallbackDrawCount() : 0U,
        .graphMaterialGpuCount = sceneRenderer_ != nullptr ? sceneRenderer_->GraphMaterialGpuDrawCount() : 0U,
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
    {
        std::ostringstream message;
        message << "SetDefaultPostProcessSettings input"
                << " fxaa=" << BoolText(settings.fxaaEnabled)
                << " taa=" << BoolText(settings.temporalAntiAliasingEnabled)
                << " jitter=" << BoolText(settings.temporalJitterEnabled)
                << " historyBlend=" << settings.temporalHistoryBlend
                << " bloom=" << BoolText(settings.bloomEnabled);
        WriteRendererBreadcrumb("aa_trace", message.str());
    }
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
    {
        const std::optional<PostProcessPass> antiAliasing = postProcessChain_.FindPass(PostProcessPassKind::AntiAliasing);
        std::ostringstream message;
        message << "SetDefaultPostProcessSettings stored"
                << " fxaa=" << BoolText(defaultPostProcessSettings_.fxaaEnabled)
                << " taa=" << BoolText(defaultPostProcessSettings_.temporalAntiAliasingEnabled)
                << " jitter=" << BoolText(defaultPostProcessSettings_.temporalJitterEnabled)
                << " historyBlend=" << defaultPostProcessSettings_.temporalHistoryBlend
                << " bloom=" << BoolText(defaultPostProcessSettings_.bloomEnabled)
                << " aaPassPresent=" << BoolText(antiAliasing.has_value());
        if (antiAliasing.has_value()) {
            message << " aaPassEnabled=" << BoolText(antiAliasing->enabled)
                    << " aaPassFxaa=" << BoolText(antiAliasing->postProcessSettings.fxaaEnabled)
                    << " aaPassTaa=" << BoolText(antiAliasing->postProcessSettings.temporalAntiAliasingEnabled);
        }
        WriteRendererBreadcrumb("aa_trace", message.str());
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
    kb::scene::Scene& mutableScene = const_cast<kb::scene::Scene&>(scene);
    if (auxFrameRenderer_ != nullptr) {
        auxFrameRenderer_->ReleaseScene(scene.Id(), sceneRenderer_.get());
    }
    screenCapture_->ReleaseScene(mutableScene);
    particleRenderSynchronizer_->ReleaseScene(scene);
    if (sceneRenderer_ != nullptr) sceneRenderer_->ReleaseParticleScene(scene.Id());
    kb::scene::SceneRenderFeedback::Clear(mutableScene);
    runtimeResourceCache_.ReleaseScene(mutableScene, sceneRenderer_.get());
    renderSceneStore_.Release(scene.Id());
    runtimeAssetDiscovery_.ReleaseScene(scene.Id());
}

void Renderer::ReleaseAllScenes() noexcept {
    if (auxFrameRenderer_ != nullptr) {
        auxFrameRenderer_->Shutdown(sceneRenderer_.get());
    }
    screenCapture_->Shutdown();
    particleRenderSynchronizer_->Clear();
    if (sceneRenderer_ != nullptr) sceneRenderer_->ReleaseAllParticleScenes();
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
