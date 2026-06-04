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
    const RenderSceneSubmitDesc& sceneDesc;
    const RenderViewportPlan& viewportPlan;
    const SceneRenderLightingConfig& lightingConfig;
    SceneRenderSubmitStats& aggregateSubmitStats;
    SceneRenderDiagnostics& diagnostics;
    std::vector<SceneRenderPassSubmitStats>& passSubmitStats;
};

class RendererShadowSubmitter final {
public:
    [[nodiscard]] static SceneRenderShadowMapBinding Submit(const RendererShadowSubmitDesc& desc);
};

} // namespace kb::render
