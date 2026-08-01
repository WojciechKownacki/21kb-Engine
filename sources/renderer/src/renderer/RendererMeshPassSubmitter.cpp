#include "renderer/RendererMeshPassSubmitter.hpp"

#include "kb/render/scene/RenderScene.hpp"
#include "kb/render/scene/SceneRenderer.hpp"
#include "renderer/RendererViewportPassResolver.hpp"

namespace kb::render {

void RendererMeshPassSubmitter::SubmitViewportPass(
    const RendererMeshPassSubmitDesc& desc,
    bgfx::ViewId viewId,
    RenderPassKind passKind,
    MeshPassType fallback,
    const SceneRenderShadowMapBinding* shadowMap,
    bool terrainLayersOnly) {
    const MeshPassType meshPass = RendererViewportPassResolver::MeshPassFor(desc.viewportPlan, passKind, fallback);
    desc.sceneRenderer.SubmitMeshPass(
        viewId,
        meshPass,
        desc.renderScene,
        desc.width,
        desc.height,
        desc.sceneCamera,
        desc.sceneDesc.drawBudget,
        desc.lightingConfig,
        shadowMap,
        {},
        &desc.gpuDrivenSupport,
        terrainLayersOnly);
    desc.aggregateSubmitStats += desc.sceneRenderer.LastSubmitStats();
    desc.diagnostics += desc.sceneRenderer.LastDiagnostics();
    desc.passSubmitStats.push_back(SceneRenderPassSubmitStats{
        .viewportId = desc.sceneDesc.target.viewport.id.value,
        .viewportIndex = desc.sceneDesc.target.viewport.viewportIndex,
        .renderPass = passKind,
        .pass = meshPass,
        .stats = desc.sceneRenderer.LastSubmitStats(),
    });
}

void RendererMeshPassSubmitter::SubmitSelectionMask(const RendererMeshPassSubmitDesc& desc) {
    if (!desc.sceneDesc.postProcess.enabled || !bgfx::isValid(desc.sceneDesc.postProcess.selectionMaskFrameBuffer) || desc.sceneDesc.selectedEntityIds.empty()) {
        return;
    }

    desc.sceneRenderer.SubmitMeshPass(
        desc.viewportPlan.viewIds.selectionMask,
        MeshPassType::SelectionId,
        desc.renderScene,
        desc.width,
        desc.height,
        desc.sceneCamera,
        desc.sceneDesc.drawBudget,
        desc.lightingConfig,
        nullptr,
        desc.sceneDesc.selectedEntityIds,
        &desc.gpuDrivenSupport);
    desc.diagnostics += desc.sceneRenderer.LastDiagnostics();
    desc.passSubmitStats.push_back(SceneRenderPassSubmitStats{
        .viewportId = desc.sceneDesc.target.viewport.id.value,
        .viewportIndex = desc.sceneDesc.target.viewport.viewportIndex,
        .renderPass = RenderPassKind::EditorSelectionMask,
        .pass = MeshPassType::SelectionId,
        .stats = desc.sceneRenderer.LastSubmitStats(),
    });
}

} // namespace kb::render
