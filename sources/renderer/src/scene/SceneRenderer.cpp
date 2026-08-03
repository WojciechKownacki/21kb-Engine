#include "kb/render/scene/SceneRenderer.hpp"

#include "kb/render/scene/RenderScene.hpp"
#include "scene/SceneMeshSubmitter.hpp"
#include "renderer/RendererDebugLog.hpp"

#include <bgfx/bgfx.h>

#include <cstdint>
#include <sstream>

namespace kb::render {
namespace {

[[nodiscard]] SceneRenderDrawBudget ResolveDrawBudget(SceneRenderDrawBudget requested, SceneRenderDrawBudget fallback) noexcept {
    return SceneRenderDrawBudget{
        .maxDrawCommands = requested.maxDrawCommands != 0U ? requested.maxDrawCommands : fallback.maxDrawCommands,
        .maxVisibleInstances = requested.maxVisibleInstances != 0U ? requested.maxVisibleInstances : fallback.maxVisibleInstances,
        .maxDroppedInstances = requested.maxDroppedInstances != 0U ? requested.maxDroppedInstances : fallback.maxDroppedInstances,
    };
}

[[nodiscard]] SceneRenderLightingConfig ResolveLightingConfig(SceneRenderLightingConfig requested, SceneRenderLightingConfig fallback) noexcept {
    constexpr SceneRenderLightingConfig defaultConfig{};
    return SceneRenderLightingConfig{
        .maxForwardLights = requested.maxForwardLights != defaultConfig.maxForwardLights ? requested.maxForwardLights : fallback.maxForwardLights,
        .lightingPath = requested.lightingPath != defaultConfig.lightingPath ? requested.lightingPath : fallback.lightingPath,
        .clusterDimensions = requested.clusterDimensions != defaultConfig.clusterDimensions ? requested.clusterDimensions : fallback.clusterDimensions,
        .ambientColor = requested.ambientColor != defaultConfig.ambientColor ? requested.ambientColor : fallback.ambientColor,
        .ambientIntensity = requested.ambientIntensity != defaultConfig.ambientIntensity ? requested.ambientIntensity : fallback.ambientIntensity,
        .environmentMode = requested.environmentMode != defaultConfig.environmentMode ? requested.environmentMode : fallback.environmentMode,
        .environmentZenithColor = requested.environmentZenithColor != defaultConfig.environmentZenithColor ? requested.environmentZenithColor : fallback.environmentZenithColor,
        .environmentGroundColor = requested.environmentGroundColor != defaultConfig.environmentGroundColor ? requested.environmentGroundColor : fallback.environmentGroundColor,
        .environmentDiffuseIntensity = requested.environmentDiffuseIntensity != defaultConfig.environmentDiffuseIntensity ? requested.environmentDiffuseIntensity : fallback.environmentDiffuseIntensity,
        .environmentSpecularIntensity = requested.environmentSpecularIntensity != defaultConfig.environmentSpecularIntensity ? requested.environmentSpecularIntensity : fallback.environmentSpecularIntensity,
        .editorPreviewKeyLightEnabled = requested.editorPreviewKeyLightEnabled != defaultConfig.editorPreviewKeyLightEnabled ? requested.editorPreviewKeyLightEnabled : fallback.editorPreviewKeyLightEnabled,
        .editorPreviewKeyLightDirection = requested.editorPreviewKeyLightDirection != defaultConfig.editorPreviewKeyLightDirection ? requested.editorPreviewKeyLightDirection : fallback.editorPreviewKeyLightDirection,
        .editorPreviewKeyLightColor = requested.editorPreviewKeyLightColor != defaultConfig.editorPreviewKeyLightColor ? requested.editorPreviewKeyLightColor : fallback.editorPreviewKeyLightColor,
        .editorPreviewKeyLightIntensity = requested.editorPreviewKeyLightIntensity != defaultConfig.editorPreviewKeyLightIntensity ? requested.editorPreviewKeyLightIntensity : fallback.editorPreviewKeyLightIntensity,
        .ibl = requested.ibl.HasEnvironment() || requested.ibl.reflectionProbeCount != 0U ? requested.ibl : fallback.ibl,
        .globalIllumination = requested.globalIllumination != defaultConfig.globalIllumination ? requested.globalIllumination : fallback.globalIllumination,
        .shadowMapSize = requested.shadowMapSize != defaultConfig.shadowMapSize ? requested.shadowMapSize : fallback.shadowMapSize,
        .shadowCascadeCount = requested.shadowCascadeCount != defaultConfig.shadowCascadeCount ? requested.shadowCascadeCount : fallback.shadowCascadeCount,
        .shadowAtlasSize = requested.shadowAtlasSize != defaultConfig.shadowAtlasSize ? requested.shadowAtlasSize : fallback.shadowAtlasSize,
        .shadowDistance = requested.shadowDistance != defaultConfig.shadowDistance ? requested.shadowDistance : fallback.shadowDistance,
        .shadowDepthBias = requested.shadowDepthBias != defaultConfig.shadowDepthBias ? requested.shadowDepthBias : fallback.shadowDepthBias,
        .shadowStrength = requested.shadowStrength != defaultConfig.shadowStrength ? requested.shadowStrength : fallback.shadowStrength,
        .shadowFilter = requested.shadowFilter != defaultConfig.shadowFilter ? requested.shadowFilter : fallback.shadowFilter,
        .shadowsEnabled = requested.shadowsEnabled != defaultConfig.shadowsEnabled ? requested.shadowsEnabled : fallback.shadowsEnabled,
        .stableShadowCascades = requested.stableShadowCascades != defaultConfig.stableShadowCascades ? requested.stableShadowCascades : fallback.stableShadowCascades,
        .perLightShadowCaching = requested.perLightShadowCaching != defaultConfig.perLightShadowCaching ? requested.perLightShadowCaching : fallback.perLightShadowCaching,
        .contactShadowsEnabled = requested.contactShadowsEnabled != defaultConfig.contactShadowsEnabled ? requested.contactShadowsEnabled : fallback.contactShadowsEnabled,
        .volumetricLightingEnabled = requested.volumetricLightingEnabled != defaultConfig.volumetricLightingEnabled ? requested.volumetricLightingEnabled : fallback.volumetricLightingEnabled,
        .debugView = requested.debugView != defaultConfig.debugView ? requested.debugView : fallback.debugView,
    };
}

[[nodiscard]] const char* MeshPassName(MeshPassType pass) noexcept {
    switch (pass) {
    case MeshPassType::Depth:
        return "Depth";
    case MeshPassType::BaseOpaque:
        return "BaseOpaque";
    case MeshPassType::GBuffer:
        return "GBuffer";
    case MeshPassType::BaseTransparent:
        return "BaseTransparent";
    case MeshPassType::ShadowDepth:
        return "ShadowDepth";
    case MeshPassType::MotionVectors:
        return "MotionVectors";
    case MeshPassType::SelectionId:
        return "SelectionId";
    case MeshPassType::EditorSelection:
        return "EditorSelection";
    case MeshPassType::Gizmo:
        return "Gizmo";
    }
    return "Unknown";
}

} // namespace

SceneRenderer::SceneRenderer() = default;

SceneRenderer::~SceneRenderer() {
    Shutdown();
}

bool SceneRenderer::Initialize() {
    if (initialized_) {
        return true;
    }

    meshSubmitter_ = std::make_unique<SceneMeshSubmitter>();
    if (!meshSubmitter_->Initialize()) {
        meshSubmitter_.reset();
        return false;
    }
    if (!graphShaderCacheRoot_.empty()) {
        meshSubmitter_->SetGraphShaderCacheRoot(graphShaderCacheRoot_);
    }
    meshSubmitter_->SetSkinningPaletteAllocator(&skinningPalettes_);

    initialized_ = true;
    return true;
}

void SceneRenderer::Shutdown() {
    if (meshSubmitter_ != nullptr) {
        meshSubmitter_->Shutdown();
        meshSubmitter_.reset();
    }
    resourceMap_.Clear();
    resources_.Shutdown();
    skinningPalettes_.Shutdown();
    initialized_ = false;
}

void SceneRenderer::Submit(bgfx::ViewId viewId, const RenderScene& renderScene, std::uint32_t viewportWidth, std::uint32_t viewportHeight, const SceneRenderCamera* cameraOverride, SceneRenderDrawBudget drawBudget, SceneRenderLightingConfig lightingConfig) const {
    SubmitMeshPass(viewId, MeshPassType::BaseOpaque, renderScene, viewportWidth, viewportHeight, cameraOverride, drawBudget, lightingConfig);
}

void SceneRenderer::SubmitMeshPass(
    bgfx::ViewId viewId,
    MeshPassType pass,
    const RenderScene& renderScene,
    std::uint32_t viewportWidth,
    std::uint32_t viewportHeight,
    const SceneRenderCamera* cameraOverride,
    SceneRenderDrawBudget drawBudget,
    SceneRenderLightingConfig lightingConfig,
    const SceneRenderShadowMapBinding* shadowMap,
    std::span<const std::uint64_t> selectedEntityIds,
    const SceneGpuDrivenFeatureSupport* gpuDrivenSupportOverride,
    bool terrainLayersOnly) const {
    lastSubmitStats_ = SceneRenderSubmitStats{};
    lastDiagnostics_.Clear();
    const SceneRenderDrawBudget effectiveDrawBudget = ResolveDrawBudget(drawBudget, defaultDrawBudget_);
    const SceneRenderLightingConfig effectiveLightingConfig = ResolveLightingConfig(lightingConfig, defaultLightingConfig_);
    const std::optional<SceneRenderCamera> primaryCamera = cameraOverride == nullptr ? renderScene.BuildPrimaryCamera(viewportWidth, viewportHeight) : std::optional<SceneRenderCamera>{};
    const SceneRenderCamera* camera = cameraOverride != nullptr ? cameraOverride : (primaryCamera.has_value() ? &(*primaryCamera) : nullptr);
    {
        std::ostringstream message;
        message << "SubmitMeshPass begin pass=" << MeshPassName(pass)
                << " viewId=" << viewId
                << " extent=" << viewportWidth << 'x' << viewportHeight
                << " initialized=" << (initialized_ ? "true" : "false")
                << " camera=" << (camera != nullptr ? "true" : "false")
                << " renderMeshes=" << renderScene.MeshProxyCount()
                << " renderLights=" << renderScene.LightProxyCount()
                << " selected=" << selectedEntityIds.size()
                << " budgetDraws=" << effectiveDrawBudget.maxDrawCommands
                << " budgetVisible=" << effectiveDrawBudget.maxVisibleInstances
                << " lightingPath=" << static_cast<int>(effectiveLightingConfig.lightingPath)
                << " shadows=" << (effectiveLightingConfig.shadowsEnabled ? "true" : "false")
                << " gpuDrivenOverride=" << (gpuDrivenSupportOverride != nullptr ? "true" : "false");
        WriteRendererDebugLog("scene_renderer", message.str());
        WriteRendererMaterialGraphDebugLog("scene", message.str());
    }
    if (!initialized_ || camera == nullptr || viewportWidth == 0U || viewportHeight == 0U) {
        lastSubmitStats_ = SceneMeshSubmitter::ValidateResourcesInto(
            renderScene,
            resources_,
            resourceMap_,
            validationPipelineScratch_,
            pass,
            camera,
            &lastDiagnostics_,
            effectiveDrawBudget,
            effectiveLightingConfig,
            selectedEntityIds,
            gpuDrivenSupportOverride == nullptr ? gpuDrivenRuntimeSupport_ : *gpuDrivenSupportOverride,
            terrainLayersOnly);
        std::ostringstream message;
        message << "SubmitMeshPass validation-only end pass=" << MeshPassName(pass)
                << " visible=" << lastSubmitStats_.visibleMeshCount
                << " submitted=" << lastSubmitStats_.submittedMeshCount
                << " missingMesh=" << lastSubmitStats_.missingMeshResourceCount
                << " missingMaterial=" << lastSubmitStats_.missingMaterialResourceCount
                << " diagnostics=" << lastDiagnostics_.events.size();
        WriteRendererDebugLog("scene_renderer", message.str());
        return;
    }

    const std::uint16_t width = static_cast<std::uint16_t>(viewportWidth > UINT16_MAX ? UINT16_MAX : viewportWidth);
    const std::uint16_t height = static_cast<std::uint16_t>(viewportHeight > UINT16_MAX ? UINT16_MAX : viewportHeight);

    bgfx::setViewName(viewId, "KB Scene ECS");
    bgfx::setViewMode(viewId, bgfx::ViewMode::Sequential);
    bgfx::setViewTransform(viewId, camera->view.data(), camera->projection.data());
    bgfx::setViewRect(viewId, 0, 0, width, height);
    bgfx::touch(viewId);
    {
        std::ostringstream message;
        message << "Mesh pass view transform pass=" << MeshPassName(pass)
                << " viewId=" << viewId
                << " proj8(jitterX*2)=" << camera->projection[8]
                << " proj9(jitterY*2)=" << camera->projection[9]
                << " proj0=" << camera->projection[0]
                << " proj5=" << camera->projection[5]
                << " proj10=" << camera->projection[10]
                << " proj14=" << camera->projection[14]
                << " view12=" << camera->view[12]
                << " view13=" << camera->view[13]
                << " view14=" << camera->view[14];
        WriteRendererDebugLog("mesh_taa_trace", message.str());
    }

    if (meshSubmitter_ != nullptr) {
        lastSubmitStats_ = meshSubmitter_->Submit(
            viewId,
            renderScene,
            resources_,
            resourceMap_,
            pass,
            camera,
            &lastDiagnostics_,
            effectiveDrawBudget,
            effectiveLightingConfig,
            shadowMap,
            selectedEntityIds,
            gpuDrivenSupportOverride == nullptr ? gpuDrivenRuntimeSupport_ : *gpuDrivenSupportOverride,
            FrameTimeConstants(),
            DynamicParameterConstants(),
            (pass == MeshPassType::BaseTransparent || pass == MeshPassType::MotionVectors)
                ? sceneDepthTexture_ : bgfx::TextureHandle{ bgfx::kInvalidHandle },
            pass == MeshPassType::BaseTransparent ? sceneColorTexture_ : bgfx::TextureHandle{ bgfx::kInvalidHandle },
            terrainLayersOnly,
            motionVectorPreviousViewProjection_);
    }
    {
        std::ostringstream message;
        message << "SubmitMeshPass end pass=" << MeshPassName(pass)
                << " visible=" << lastSubmitStats_.visibleMeshCount
                << " visibleGroups=" << lastSubmitStats_.visibleDrawGroupCount
                << " submittedMeshes=" << lastSubmitStats_.submittedMeshCount
                << " submittedGroups=" << lastSubmitStats_.submittedDrawGroupCount
                << " submittedDrawCalls=" << lastSubmitStats_.submittedDrawCallCount
                << " culled=" << lastSubmitStats_.culledInstanceCount
                << " dropped=" << lastSubmitStats_.droppedInstanceCount
                << " missingMeshBinding=" << lastSubmitStats_.missingMeshBindingCount
                << " missingMeshResource=" << lastSubmitStats_.missingMeshResourceCount
                << " missingMaterialBinding=" << lastSubmitStats_.missingMaterialBindingCount
                << " missingMaterialResource=" << lastSubmitStats_.missingMaterialResourceCount
                << " missingTextureBinding=" << lastSubmitStats_.missingTextureBindingCount
                << " missingTextureResource=" << lastSubmitStats_.missingTextureResourceCount
                << " textureDimensionMismatch=" << lastSubmitStats_.textureDimensionMismatchCount
                << " diagnostics=" << lastDiagnostics_.events.size()
                << " instanceUploadBytes=" << lastSubmitStats_.instanceUploadBytes;
        WriteRendererDebugLog("scene_renderer", message.str());
        WriteRendererMaterialGraphDebugLog("scene", message.str());
    }
}

void SceneRenderer::SetDefaultDrawBudget(SceneRenderDrawBudget drawBudget) noexcept {
    defaultDrawBudget_ = drawBudget;
}

SceneRenderDrawBudget SceneRenderer::DefaultDrawBudget() const noexcept {
    return defaultDrawBudget_;
}

void SceneRenderer::SetDefaultLightingConfig(SceneRenderLightingConfig lightingConfig) noexcept {
    defaultLightingConfig_ = lightingConfig;
}

SceneRenderLightingConfig SceneRenderer::DefaultLightingConfig() const noexcept {
    return defaultLightingConfig_;
}

void SceneRenderer::SetGpuDrivenRuntimeSupport(SceneGpuDrivenFeatureSupport support) noexcept {
    gpuDrivenRuntimeSupport_ = support;
}

SceneGpuDrivenFeatureSupport SceneRenderer::GpuDrivenRuntimeSupport() const noexcept {
    return gpuDrivenRuntimeSupport_;
}

void SceneRenderer::SetGraphShaderCacheRoot(std::string root) {
    graphShaderCacheRoot_ = std::move(root);
    if (meshSubmitter_ != nullptr) {
        meshSubmitter_->SetGraphShaderCacheRoot(graphShaderCacheRoot_);
    }
}

const std::string& SceneRenderer::GraphShaderCacheRoot() const noexcept {
    return graphShaderCacheRoot_;
}

void SceneRenderer::AdvanceFrameTime(float deltaSeconds) noexcept {
    frameDeltaSeconds_ = deltaSeconds;
    frameTimeSeconds_ += deltaSeconds;
    ++frameTimeIndex_;
}

std::array<float, 4> SceneRenderer::FrameTimeConstants() const noexcept {
    return { frameTimeSeconds_, frameDeltaSeconds_, static_cast<float>(frameTimeIndex_), 0.0F };
}

void SceneRenderer::SetDynamicParameter(std::array<float, 4> parameter) noexcept {
    dynamicParameter_ = parameter;
}

std::array<float, 4> SceneRenderer::DynamicParameterConstants() const noexcept {
    return dynamicParameter_;
}

void SceneRenderer::SetSceneDepthTexture(bgfx::TextureHandle texture) noexcept {
    sceneDepthTexture_ = texture;
}

void SceneRenderer::SetSceneColorTexture(bgfx::TextureHandle texture) noexcept {
    sceneColorTexture_ = texture;
}

void SceneRenderer::SetMotionVectorPreviousViewProjection(const std::array<float, 16>& matrix) noexcept {
    motionVectorPreviousViewProjection_ = matrix;
}

bool SceneRenderer::BeginSkinningFrame(
    std::uint64_t frame, std::uint64_t completedFrame) noexcept {
    return initialized_ && skinningPalettes_.BeginFrame(frame, completedFrame);
}

RenderSkinningPaletteAllocator& SceneRenderer::SkinningPalettes() noexcept {
    return skinningPalettes_;
}

void SceneRenderer::TickFrame() noexcept {
    if (meshSubmitter_ != nullptr) {
        meshSubmitter_->EndFrame(frameTimeIndex_);
    }
    resources_.TickFrame();
    resourceMap_.PruneInvalidBindings(resources_);
}

bool SceneRenderer::IsInitialized() const noexcept {
    return initialized_;
}

RenderResourceRegistry& SceneRenderer::Resources() noexcept {
    return resources_;
}

const RenderResourceRegistry& SceneRenderer::Resources() const noexcept {
    return resources_;
}

SceneRenderResourceMap& SceneRenderer::ResourceMap() noexcept {
    return resourceMap_;
}

const SceneRenderResourceMap& SceneRenderer::ResourceMap() const noexcept {
    return resourceMap_;
}

SceneRenderSubmitStats SceneRenderer::ValidateSceneResources(const RenderScene& renderScene) const noexcept {
    return ValidateSceneResources(renderScene, MeshPassType::BaseOpaque);
}

SceneRenderSubmitStats SceneRenderer::ValidateSceneResources(const RenderScene& renderScene, MeshPassType pass) const noexcept {
    return SceneMeshSubmitter::ValidateResourcesInto(
        renderScene,
        resources_,
        resourceMap_,
        validationPipelineScratch_,
        pass,
        nullptr,
        nullptr,
        defaultDrawBudget_,
        defaultLightingConfig_,
        {},
        gpuDrivenRuntimeSupport_);
}

SceneRenderDiagnostics SceneRenderer::ValidateSceneDiagnostics(const RenderScene& renderScene) const {
    return ValidateSceneDiagnostics(renderScene, MeshPassType::BaseOpaque);
}

SceneRenderDiagnostics SceneRenderer::ValidateSceneDiagnostics(const RenderScene& renderScene, MeshPassType pass) const {
    SceneRenderDiagnostics diagnostics;
    static_cast<void>(SceneMeshSubmitter::ValidateResourcesInto(
        renderScene,
        resources_,
        resourceMap_,
        validationPipelineScratch_,
        pass,
        nullptr,
        &diagnostics,
        defaultDrawBudget_,
        defaultLightingConfig_,
        {},
        gpuDrivenRuntimeSupport_));
    return diagnostics;
}

SceneRenderSubmitStats SceneRenderer::LastSubmitStats() const noexcept {
    return lastSubmitStats_;
}

const SceneRenderDiagnostics& SceneRenderer::LastDiagnostics() const noexcept {
    return lastDiagnostics_;
}

MaterialProgramRegistryStats SceneRenderer::MaterialProgramStats() const noexcept {
    return meshSubmitter_ != nullptr ? meshSubmitter_->ProgramRegistryStats() : MaterialProgramRegistryStats{};
}

void SceneRenderer::ResetGraphMaterialDrawStats() const noexcept {
    if (meshSubmitter_ != nullptr) {
        meshSubmitter_->ResetGraphMaterialDrawStats();
    }
}

std::uint32_t SceneRenderer::GraphMaterialGpuDrawCount() const noexcept {
    return meshSubmitter_ != nullptr ? meshSubmitter_->GraphMaterialDrawStats().gpuCount : 0U;
}

std::uint32_t SceneRenderer::GraphMaterialCpuFallbackDrawCount() const noexcept {
    return meshSubmitter_ != nullptr ? meshSubmitter_->GraphMaterialDrawStats().cpuFallbackCount : 0U;
}

} // namespace kb::render
