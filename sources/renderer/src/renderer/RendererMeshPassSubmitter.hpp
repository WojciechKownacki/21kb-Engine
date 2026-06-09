#pragma once

#include "kb/render/frame/RenderFramePipeline.hpp"
#include "kb/render/frame/RenderSceneSubmitDesc.hpp"
#include "kb/render/scene/MeshPipeline.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"

#include <bgfx/bgfx.h>

#include <cstdint>
#include <vector>

namespace kb::render {

class RenderScene;
class SceneRenderer;

struct RendererMeshPassSubmitDesc {
    SceneRenderer& sceneRenderer;
    const RenderScene& renderScene;
    const RenderSceneSubmitDesc& sceneDesc;
    const RenderViewportPlan& viewportPlan;
    const SceneRenderCamera* sceneCamera = nullptr;
    const SceneRenderLightingConfig& lightingConfig;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    SceneGpuDrivenFeatureSupport gpuDrivenSupport{};
    SceneRenderSubmitStats& aggregateSubmitStats;
    SceneRenderDiagnostics& diagnostics;
    std::vector<SceneRenderPassSubmitStats>& passSubmitStats;
};

class RendererMeshPassSubmitter final {
public:
    static void SubmitViewportPass(
        const RendererMeshPassSubmitDesc& desc,
        bgfx::ViewId viewId,
        RenderPassKind passKind,
        MeshPassType fallback,
        const SceneRenderShadowMapBinding* shadowMap);

    static void SubmitSelectionMask(const RendererMeshPassSubmitDesc& desc);
};

} // namespace kb::render
