#pragma once

#include "kb/render/frame/RenderFramePipeline.hpp"
#include "kb/render/frame/RenderSceneSubmitDesc.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"

#include <vector>

namespace kb::render {

class RenderScene;
class SceneRenderer;
class ShadowMapResource;

struct RendererShadowSubmitDesc {
    const RenderScene& renderScene;
    SceneRenderer& sceneRenderer;
    ShadowMapResource& shadowMap;
    ShadowMapResource& pointShadowMap;
    const RenderSceneSubmitDesc& sceneDesc;
    // The view the shadows are for, sized to its target (viewportHeight): terrain chunks drawn into a shadow map take the
    // level of detail this view picks.
    const SceneRenderCamera* viewCamera = nullptr;
    const RenderViewportPlan& viewportPlan;
    const SceneRenderLightingConfig& lightingConfig;
    SceneGpuDrivenFeatureSupport gpuDrivenSupport{};
    SceneRenderSubmitStats& aggregateSubmitStats;
    SceneRenderDiagnostics& diagnostics;
    std::vector<SceneRenderPassSubmitStats>& passSubmitStats;
};

class RendererShadowSubmitter final {
public:
    [[nodiscard]] static SceneRenderShadowMapBinding Submit(const RendererShadowSubmitDesc& desc);
};

} // namespace kb::render
