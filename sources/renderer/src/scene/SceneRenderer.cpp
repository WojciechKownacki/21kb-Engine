#include "kb/render/scene/SceneRenderer.hpp"

#include "kb/render/scene/RenderScene.hpp"
#include "scene/SceneMeshSubmitter.hpp"

#include <bgfx/bgfx.h>

#include <cstdint>

namespace kb::render {
namespace {

[[nodiscard]] SceneRenderDrawBudget ResolveDrawBudget(SceneRenderDrawBudget requested, SceneRenderDrawBudget fallback) noexcept {
    return SceneRenderDrawBudget{
        .maxDrawCommands = requested.maxDrawCommands != 0U ? requested.maxDrawCommands : fallback.maxDrawCommands,
        .maxVisibleInstances = requested.maxVisibleInstances != 0U ? requested.maxVisibleInstances : fallback.maxVisibleInstances,
    };
}

[[nodiscard]] SceneRenderLightingConfig ResolveLightingConfig(SceneRenderLightingConfig requested, SceneRenderLightingConfig fallback) noexcept {
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
    initialized_ = false;
}

void SceneRenderer::Submit(bgfx::ViewId viewId, const RenderScene& renderScene, std::uint32_t viewportWidth, std::uint32_t viewportHeight, const SceneRenderCamera* cameraOverride, SceneRenderDrawBudget drawBudget, SceneRenderLightingConfig lightingConfig) const {
    SubmitMeshPass(viewId, MeshPassType::BaseOpaque, renderScene, viewportWidth, viewportHeight, cameraOverride, drawBudget, lightingConfig);
}

void SceneRenderer::SubmitMeshPass(bgfx::ViewId viewId, MeshPassType pass, const RenderScene& renderScene, std::uint32_t viewportWidth, std::uint32_t viewportHeight, const SceneRenderCamera* cameraOverride, SceneRenderDrawBudget drawBudget, SceneRenderLightingConfig lightingConfig, const SceneRenderShadowMapBinding* shadowMap) const {
    lastSubmitStats_ = SceneRenderSubmitStats{};
    lastDiagnostics_.Clear();
    const SceneRenderDrawBudget effectiveDrawBudget = ResolveDrawBudget(drawBudget, defaultDrawBudget_);
    const SceneRenderLightingConfig effectiveLightingConfig = ResolveLightingConfig(lightingConfig, defaultLightingConfig_);
    const std::optional<SceneRenderCamera> primaryCamera = cameraOverride == nullptr ? renderScene.BuildPrimaryCamera(viewportWidth, viewportHeight) : std::optional<SceneRenderCamera>{};
    const SceneRenderCamera* camera = cameraOverride != nullptr ? cameraOverride : (primaryCamera.has_value() ? &(*primaryCamera) : nullptr);
    if (!initialized_ || camera == nullptr || viewportWidth == 0U || viewportHeight == 0U) {
        lastSubmitStats_ = SceneMeshSubmitter::ValidateResourcesInto(
            renderScene,
            resources_,
            resourceMap_,
            validationDrawGroupsScratch_,
            validationPipelineScratch_,
            pass,
            camera,
            &lastDiagnostics_,
            effectiveDrawBudget,
            effectiveLightingConfig);
        return;
    }

    const std::uint16_t width = static_cast<std::uint16_t>(viewportWidth > UINT16_MAX ? UINT16_MAX : viewportWidth);
    const std::uint16_t height = static_cast<std::uint16_t>(viewportHeight > UINT16_MAX ? UINT16_MAX : viewportHeight);

    bgfx::setViewName(viewId, "KB Scene ECS");
    bgfx::setViewMode(viewId, bgfx::ViewMode::Sequential);
    bgfx::setViewTransform(viewId, camera->view.data(), camera->projection.data());
    bgfx::setViewRect(viewId, 0, 0, width, height);
    bgfx::touch(viewId);

    if (meshSubmitter_ != nullptr) {
        lastSubmitStats_ = meshSubmitter_->Submit(viewId, renderScene, resources_, resourceMap_, pass, camera, &lastDiagnostics_, effectiveDrawBudget, effectiveLightingConfig, shadowMap);
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

void SceneRenderer::TickFrame() noexcept {
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
        validationDrawGroupsScratch_,
        validationPipelineScratch_,
        pass,
        nullptr,
        nullptr,
        defaultDrawBudget_,
        defaultLightingConfig_);
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
        validationDrawGroupsScratch_,
        validationPipelineScratch_,
        pass,
        nullptr,
        &diagnostics,
        defaultDrawBudget_,
        defaultLightingConfig_));
    return diagnostics;
}

SceneRenderSubmitStats SceneRenderer::LastSubmitStats() const noexcept {
    return lastSubmitStats_;
}

const SceneRenderDiagnostics& SceneRenderer::LastDiagnostics() const noexcept {
    return lastDiagnostics_;
}

} // namespace kb::render
