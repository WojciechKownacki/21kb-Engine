#include "kb/render/Renderer.hpp"

#include "kb/render/BgfxContext.hpp"
#include "kb/render/RenderSurface.hpp"
#include "kb/render/ViewIdPolicy.hpp"
#include "engine/scene/Scene.hpp"
#include "kb/render/scene/EcsRenderSceneSynchronizer.hpp"
#include "kb/render/scene/RenderScene.hpp"
#include "kb/render/scene/SceneRenderer.hpp"
#include "kb/render/shadow/DirectionalShadowPassPlanner.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <memory>
#include <span>
#include <vector>

namespace kb::render {
namespace {

[[nodiscard]] std::uint16_t ClampToViewExtent(std::uint32_t value) noexcept {
    return static_cast<std::uint16_t>(value > UINT16_MAX ? UINT16_MAX : value);
}

[[nodiscard]] std::array<float, 16> IdentityMatrix() noexcept {
    return std::array<float, 16>{
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F,
    };
}

void ApplyViewOrder(std::span<const std::uint16_t> viewOrder) {
    if (!viewOrder.empty()) {
        bgfx::setViewOrder(0U, static_cast<std::uint16_t>(viewOrder.size()), viewOrder.data());
    }
}

void ConfigureSceneViewClear(bgfx::ViewId viewId, const RenderSceneSubmitDesc& desc) {
    const std::array<float, 16> identity = IdentityMatrix();
    const std::uint16_t width = ClampToViewExtent(desc.target.viewport.extent.width);
    const std::uint16_t height = ClampToViewExtent(desc.target.viewport.extent.height);

    bgfx::setViewName(viewId, "KB Scene Target");
    bgfx::setViewFrameBuffer(viewId, desc.target.frameBuffer);
    bgfx::setViewTransform(viewId, identity.data(), identity.data());
    bgfx::setViewClear(viewId, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH | BGFX_CLEAR_STENCIL, desc.clearRgba, desc.clearDepth, desc.clearStencil);
    bgfx::setViewRect(viewId, 0, 0, width, height);
    bgfx::touch(viewId);
}

void ConfigureSceneViewNoClear(bgfx::ViewId viewId, const RenderSceneSubmitDesc& desc, const char* name) {
    const std::array<float, 16> identity = IdentityMatrix();
    const std::uint16_t width = ClampToViewExtent(desc.target.viewport.extent.width);
    const std::uint16_t height = ClampToViewExtent(desc.target.viewport.extent.height);

    bgfx::setViewName(viewId, name);
    bgfx::setViewFrameBuffer(viewId, desc.target.frameBuffer);
    bgfx::setViewTransform(viewId, identity.data(), identity.data());
    bgfx::setViewClear(viewId, BGFX_CLEAR_NONE);
    bgfx::setViewRect(viewId, 0, 0, width, height);
}

void ConfigureShadowDepthView(bgfx::ViewId viewId, bgfx::FrameBufferHandle frameBuffer, std::uint32_t size) {
    const std::uint16_t extent = ClampToViewExtent(size);
    bgfx::setViewName(viewId, "KB Shadow Depth");
    bgfx::setViewFrameBuffer(viewId, frameBuffer);
    bgfx::setViewClear(viewId, BGFX_CLEAR_DEPTH, 0U, SceneDepthPolicy::ClearDepth(), 0U);
    bgfx::setViewRect(viewId, 0, 0, extent, extent);
    bgfx::touch(viewId);
}

[[nodiscard]] MeshPassType MeshPassForViewportPass(const RenderViewportPlan& viewportPlan, RenderPassKind kind, MeshPassType fallback) noexcept {
    for (const RenderPassDesc& pass : viewportPlan.passes) {
        if (pass.kind == kind) {
            return pass.meshPass.value_or(fallback);
        }
    }
    return fallback;
}

[[nodiscard]] SceneRenderLightingConfig ResolveSceneLightingConfig(SceneRenderLightingConfig requested, SceneRenderLightingConfig fallback) noexcept {
    constexpr SceneRenderLightingConfig defaultConfig{};
    return SceneRenderLightingConfig{
        .maxForwardLights = requested.maxForwardLights != defaultConfig.maxForwardLights ? requested.maxForwardLights : fallback.maxForwardLights,
        .ambientColor = requested.ambientColor != defaultConfig.ambientColor ? requested.ambientColor : fallback.ambientColor,
        .ambientIntensity = requested.ambientIntensity != defaultConfig.ambientIntensity ? requested.ambientIntensity : fallback.ambientIntensity,
        .environmentMode = requested.environmentMode != defaultConfig.environmentMode ? requested.environmentMode : fallback.environmentMode,
        .environmentZenithColor = requested.environmentZenithColor != defaultConfig.environmentZenithColor ? requested.environmentZenithColor : fallback.environmentZenithColor,
        .environmentGroundColor = requested.environmentGroundColor != defaultConfig.environmentGroundColor ? requested.environmentGroundColor : fallback.environmentGroundColor,
        .environmentDiffuseIntensity = requested.environmentDiffuseIntensity != defaultConfig.environmentDiffuseIntensity ? requested.environmentDiffuseIntensity : fallback.environmentDiffuseIntensity,
        .environmentSpecularIntensity = requested.environmentSpecularIntensity != defaultConfig.environmentSpecularIntensity ? requested.environmentSpecularIntensity : fallback.environmentSpecularIntensity,
        .shadowMapSize = requested.shadowMapSize != defaultConfig.shadowMapSize ? requested.shadowMapSize : fallback.shadowMapSize,
        .shadowDistance = requested.shadowDistance != defaultConfig.shadowDistance ? requested.shadowDistance : fallback.shadowDistance,
        .shadowDepthBias = requested.shadowDepthBias != defaultConfig.shadowDepthBias ? requested.shadowDepthBias : fallback.shadowDepthBias,
        .shadowStrength = requested.shadowStrength != defaultConfig.shadowStrength ? requested.shadowStrength : fallback.shadowStrength,
        .shadowFilter = requested.shadowFilter != defaultConfig.shadowFilter ? requested.shadowFilter : fallback.shadowFilter,
        .shadowsEnabled = requested.shadowsEnabled != defaultConfig.shadowsEnabled ? requested.shadowsEnabled : fallback.shadowsEnabled,
    };
}

[[nodiscard]] std::uint32_t ShadowFilterSampleCount(SceneRenderShadowFilter filter) noexcept {
    switch (filter) {
    case SceneRenderShadowFilter::Hard:
        return 1U;
    case SceneRenderShadowFilter::Pcf3x3:
        return 9U;
    }
    return 9U;
}

[[nodiscard]] std::uint32_t EnvironmentSampleCount(SceneRenderEnvironmentMode mode) noexcept {
    switch (mode) {
    case SceneRenderEnvironmentMode::Disabled:
        return 0U;
    case SceneRenderEnvironmentMode::Constant:
        return 1U;
    case SceneRenderEnvironmentMode::Hemisphere:
        return 2U;
    }
    return 1U;
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
    renderSceneSynchronizer_ = std::make_unique<EcsRenderSceneSynchronizer>();
    ApplyRuntimeSceneResourceReserve();

    scenePostProcessRenderer_ = std::make_unique<ScenePostProcessRenderer>();
    if (!scenePostProcessRenderer_->Initialize()) {
        Shutdown();
        return false;
    }

    finalCompositePass_ = std::make_unique<FinalCompositePass>();
    if (!finalCompositePass_->Initialize()) {
        Shutdown();
        return false;
    }

    postProcessChain_.Clear();
    if (!postProcessChain_.AddPass(PostProcessChain::kDefaultIdentityPass)) {
        Shutdown();
        return false;
    }

    return true;
}

void Renderer::Shutdown() {
    frameActive_ = false;
    lastSceneSubmitStats_ = SceneRenderSubmitStats{};
    lastScenePassSubmitStats_.clear();
    lastSceneDiagnostics_.Clear();
    frameState_.Reset();
    renderSceneStore_.ReleaseAll();
    runtimeResourceCache_.DestroyAll(sceneRenderer_.get());
    frameReferences_.Clear();
    runtimeAssetDiscovery_.Clear();
    defaultShadowMap_.Shutdown();
    defaultPostProcessTargets_.Shutdown();
    defaultSceneTarget_.Shutdown();
    renderSceneSynchronizer_.reset();
    if (finalCompositePass_ != nullptr) {
        finalCompositePass_->Shutdown();
        finalCompositePass_.reset();
    }
    if (sceneRenderer_ != nullptr) {
        sceneRenderer_->Shutdown();
        sceneRenderer_.reset();
    }
    if (scenePostProcessRenderer_ != nullptr) {
        scenePostProcessRenderer_->Shutdown();
        scenePostProcessRenderer_.reset();
    }
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

    const std::array<float, 16> identity = IdentityMatrix();
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
    lastScenePassSubmitStats_.reserve(submissions.size() * 3U);
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
    ApplyViewOrder(frameState_.ViewOrder());

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

    ConfigureSceneViewClear(viewportPlan.viewIds.opaqueScene, desc);
    ConfigureSceneViewNoClear(viewportPlan.viewIds.transparentScene, desc, "KB Scene Transparent");

    const std::uint32_t width = desc.target.viewport.extent.width;
    const std::uint32_t height = desc.target.viewport.extent.height;
    if (renderSceneSynchronizer_ == nullptr) {
        return false;
    }
    RenderScene& renderScene = RenderSceneFor(scene);
    renderSceneSynchronizer_->Sync(scene, renderScene);
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
    const SceneRenderLightingConfig effectiveLightingConfig = ResolveSceneLightingConfig(desc.lightingConfig, defaultSceneLightingConfig_);
    DirectionalShadowSetup shadowSetup{};
    SceneRenderShadowMapBinding shadowBinding{};
    if (effectiveLightingConfig.shadowsEnabled) {
        shadowSetup = DirectionalShadowPassPlanner{}.Build(
            renderScene,
            sceneRenderer_->Resources(),
            sceneRenderer_->ResourceMap(),
            effectiveLightingConfig,
            BGFX_INVALID_HANDLE);
        if (shadowSetup.valid && defaultShadowMap_.Ensure(effectiveLightingConfig.shadowMapSize)) {
            shadowSetup.binding.depthTexture = defaultShadowMap_.DepthTexture();
            shadowSetup.binding.params[2] = defaultShadowMap_.Size() == 0U ? 0.0F : 1.0F / static_cast<float>(defaultShadowMap_.Size());
            ConfigureShadowDepthView(viewportPlan.viewIds.shadowDepth, defaultShadowMap_.FrameBuffer(), defaultShadowMap_.Size());
            sceneRenderer_->SubmitMeshPass(
                viewportPlan.viewIds.shadowDepth,
                MeshPassType::ShadowDepth,
                renderScene,
                defaultShadowMap_.Size(),
                defaultShadowMap_.Size(),
                &shadowSetup.camera,
                desc.drawBudget,
                effectiveLightingConfig);
            SceneRenderSubmitStats shadowStats = sceneRenderer_->LastSubmitStats();
            shadowStats.shadowLightEntityId = shadowSetup.lightEntityId;
            shadowStats.shadowMapAllocationBytes = defaultShadowMap_.AllocationBytes();
            lastSceneSubmitStats_ += shadowStats;
            lastSceneDiagnostics_ += sceneRenderer_->LastDiagnostics();
            lastScenePassSubmitStats_.push_back(SceneRenderPassSubmitStats{
                .viewportId = desc.target.viewport.id.value,
                .viewportIndex = desc.target.viewport.viewportIndex,
                .pass = MeshPassType::ShadowDepth,
                .stats = shadowStats,
            });
            shadowBinding = shadowSetup.binding;
            shadowBinding.params[3] = shadowStats.submittedShadowCasterCount == 0U ? 0.0F : shadowBinding.params[3];
        }
    }

    const auto submitMeshPass = [&](bgfx::ViewId viewId, RenderPassKind passKind, MeshPassType fallback, const SceneRenderShadowMapBinding* shadowMap) {
        const MeshPassType meshPass = MeshPassForViewportPass(viewportPlan, passKind, fallback);
        sceneRenderer_->SubmitMeshPass(
            viewId,
            meshPass,
            renderScene,
            width,
            height,
            desc.cameraOverride.has_value() ? &(*desc.cameraOverride) : nullptr,
            desc.drawBudget,
            effectiveLightingConfig,
            shadowMap);
        lastSceneSubmitStats_ += sceneRenderer_->LastSubmitStats();
        lastSceneDiagnostics_ += sceneRenderer_->LastDiagnostics();
        lastScenePassSubmitStats_.push_back(SceneRenderPassSubmitStats{
            .viewportId = desc.target.viewport.id.value,
            .viewportIndex = desc.target.viewport.viewportIndex,
            .pass = meshPass,
            .stats = sceneRenderer_->LastSubmitStats(),
        });
    };

    submitMeshPass(
        viewportPlan.viewIds.opaqueScene,
        RenderPassKind::OpaqueScene,
        MeshPassType::BaseOpaque,
        shadowBinding.IsValid() ? &shadowBinding : nullptr);
    if (desc.meshPassMode == SceneRenderMeshPassMode::OpaqueAndTransparent) {
        submitMeshPass(
            viewportPlan.viewIds.transparentScene,
            RenderPassKind::TransparentScene,
            MeshPassType::BaseTransparent,
            shadowBinding.IsValid() ? &shadowBinding : nullptr);
    }
    editorPassSubmitter_.SubmitSelectionMask(viewportPlan, desc);

    if (desc.finalComposite.enabled && finalCompositePass_ != nullptr && scenePostProcessRenderer_ != nullptr) {
        const PostProcessOutput postProcessOutput = postProcessChain_.Evaluate(PostProcessInput{
            .sceneColor = desc.target.colorTexture,
            .outputFrameBuffer = desc.postProcess.finalFrameBuffer,
            .outputColor = desc.postProcess.finalTexture,
            .extent = desc.target.viewport.extent,
        });
        if (!postProcessOutput.IsValid()) {
            return false;
        }

        const bgfx::TextureHandle scenePostProcessOutput = scenePostProcessRenderer_->Submit(ScenePostProcessSubmitDesc{
            .sceneColor = desc.target.colorTexture,
            .target = desc.postProcess,
            .viewIds = viewportPlan.viewIds,
        });
        if (!bgfx::isValid(scenePostProcessOutput)) {
            return false;
        }

        editorPassSubmitter_.SubmitSceneOverlays(viewportPlan, desc);

        const FinalCompositePassDesc compositeDesc{
            .viewId = viewportPlan.viewIds.finalComposite,
            .postProcessColor = scenePostProcessOutput,
            .frameBuffer = desc.finalComposite.frameBuffer,
            .extent = desc.finalComposite.extent,
        };
        if (!finalCompositePass_->Submit(compositeDesc)) {
            return false;
        }

        editorPassSubmitter_.SubmitUiComposite(viewportPlan, desc);
        return true;
    }

    editorPassSubmitter_.SubmitSceneOverlays(viewportPlan, desc);
    editorPassSubmitter_.SubmitUiComposite(viewportPlan, desc);
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

const SceneRenderDiagnostics& Renderer::LastSceneDiagnostics() const noexcept {
    return lastSceneDiagnostics_;
}

Renderer::RuntimeSceneResourceStats Renderer::RuntimeResourceStats() const noexcept {
    RenderResourceRegistryStats resourceStats{};
    SceneRenderResourceMapStats resourceMapStats{};
    EcsRenderSceneSynchronizerStats syncStats{};
    const RuntimeRenderAssetDiscoveryStats discoveryStats = runtimeAssetDiscovery_.Stats();
    const RuntimeFrameResourceReferenceStats referenceStats = frameReferences_.Stats();
    const RuntimeRenderResourceCacheStats cacheStats = runtimeResourceCache_.Stats();
    const RenderSceneStoreStats storeStats = renderSceneStore_.Stats();
    if (sceneRenderer_ != nullptr) {
        resourceStats = sceneRenderer_->Resources().Stats();
        resourceMapStats = sceneRenderer_->ResourceMap().Stats();
    }
    if (renderSceneSynchronizer_ != nullptr) {
        syncStats = renderSceneSynchronizer_->Stats();
    }
    return RuntimeSceneResourceStats{
        .cachedMeshCount = cacheStats.meshCount,
        .cachedMaterialCount = cacheStats.materialCount,
        .cachedTextureCount = cacheStats.textureCount,
        .cachedMeshCapacity = cacheStats.meshCapacity,
        .cachedMaterialCapacity = cacheStats.materialCapacity,
        .cachedTextureCapacity = cacheStats.textureCapacity,
        .referencedMeshAssetCount = referenceStats.meshCount,
        .referencedMaterialAssetCount = referenceStats.materialCount,
        .referencedTextureAssetCount = referenceStats.textureCount,
        .unresolvedMaterialTexturePathCount = lastUnresolvedMaterialTexturePathCount_,
        .referencedMeshAssetCapacity = referenceStats.meshCapacity,
        .referencedMaterialAssetCapacity = referenceStats.materialCapacity,
        .referencedTextureAssetCapacity = referenceStats.textureCapacity,
        .scenePassSubmitStatsCapacity = static_cast<std::uint32_t>(lastScenePassSubmitStats_.capacity()),
        .registeredRuntimeAssetLoaderSceneCount = discoveryStats.registeredSceneCount,
        .runtimeAssetDiscoverySceneCount = discoveryStats.discoverySceneCount,
        .runtimeAssetDiscoverySceneCapacity = discoveryStats.discoverySceneCapacity,
        .renderSceneCount = storeStats.sceneCount,
        .renderSceneCapacity = storeStats.sceneCapacity,
        .renderSceneMeshProxyCount = storeStats.renderSceneStats.meshProxyCount,
        .renderSceneCameraProxyCount = storeStats.renderSceneStats.cameraProxyCount,
        .renderSceneLightProxyCount = storeStats.renderSceneStats.lightProxyCount,
        .renderSceneMeshProxyCapacity = storeStats.renderSceneStats.meshProxyCapacity,
        .renderSceneCameraProxyCapacity = storeStats.renderSceneStats.cameraProxyCapacity,
        .renderSceneLightProxyCapacity = storeStats.renderSceneStats.lightProxyCapacity,
        .renderSceneDrawGroupLookupCapacity = storeStats.renderSceneStats.drawGroupLookupCapacity,
        .meshResourceSlotCapacity = resourceStats.meshSlotCapacity,
        .materialResourceSlotCapacity = resourceStats.materialSlotCapacity,
        .textureResourceSlotCapacity = resourceStats.textureSlotCapacity,
        .meshBindingCapacity = resourceMapStats.meshBindingCapacity,
        .materialBindingCapacity = resourceMapStats.materialBindingCapacity,
        .textureBindingCapacity = resourceMapStats.textureBindingCapacity,
        .shadowMapSize = defaultShadowMap_.Size(),
        .shadowMapAllocationBytes = defaultShadowMap_.AllocationBytes(),
        .shadowMapAllocated = defaultShadowMap_.IsAllocated(),
        .syncMeshSeenCount = syncStats.meshSeenCount,
        .syncCameraSeenCount = syncStats.cameraSeenCount,
        .syncLightSeenCount = syncStats.lightSeenCount,
        .syncMeshSeenCapacity = syncStats.meshSeenCapacity,
        .syncCameraSeenCapacity = syncStats.cameraSeenCapacity,
        .syncLightSeenCapacity = syncStats.lightSeenCapacity,
        .syncTransformCacheCount = syncStats.transformCacheCount,
        .syncTransformResolvingCount = syncStats.transformResolvingCount,
        .syncTransformCacheCapacity = syncStats.transformCacheCapacity,
        .syncTransformResolvingCapacity = syncStats.transformResolvingCapacity,
        .defaultForwardLightCapacity = defaultSceneLightingConfig_.maxForwardLights,
        .defaultEnvironmentLightingMode = static_cast<std::uint32_t>(defaultSceneLightingConfig_.environmentMode) + 1U,
        .defaultEnvironmentLightingSampleCount = EnvironmentSampleCount(defaultSceneLightingConfig_.environmentMode),
        .defaultShadowFilterSampleCount = ShadowFilterSampleCount(defaultSceneLightingConfig_.shadowFilter),
        .retentionFrames = kRuntimeAssetRetentionFrames,
        .assetDiscoveryIntervalFrames = runtimeAssetDiscovery_.DiscoveryIntervalFrames(),
    };
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
