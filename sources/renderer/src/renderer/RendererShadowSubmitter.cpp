#include "renderer/RendererShadowSubmitter.hpp"

#include "kb/render/scene/RenderScene.hpp"
#include "kb/render/scene/SceneRenderer.hpp"
#include "kb/render/shadow/DirectionalShadowPassPlanner.hpp"
#include "kb/render/shadow/ShadowMapResource.hpp"
#include "renderer/RendererViewConfigurator.hpp"

#include <bx/math.h>

namespace kb::render {

SceneRenderShadowMapBinding RendererShadowSubmitter::Submit(const RendererShadowSubmitDesc& desc) {
    if (!desc.lightingConfig.shadowsEnabled) {
        return {};
    }

    const CameraRenderProxyDesc* sceneCamera =
        desc.renderScene.FindPrimaryCameraProxy(desc.sceneDesc.target.viewport.id.value);
    const std::uint32_t cameraCullingMask = desc.sceneDesc.cameraOverride.has_value()
        ? desc.sceneDesc.cameraOverride->cullingMask
        : (sceneCamera != nullptr ? sceneCamera->cullingMask : 0xFFFFFFFFU);
    std::array<float, 3> cameraPosition{};
    if (desc.sceneDesc.cameraOverride.has_value()) {
        std::array<float, 16> inverseView{};
        bx::mtxInverse(inverseView.data(), desc.sceneDesc.cameraOverride->view.data());
        cameraPosition = { inverseView[12], inverseView[13], inverseView[14] };
    } else if (sceneCamera != nullptr) {
        cameraPosition = sceneCamera->position;
    }

    DirectionalShadowSetup shadowSetup = DirectionalShadowPassPlanner{}.Build(
        desc.renderScene,
        desc.sceneRenderer.Resources(),
        desc.sceneRenderer.ResourceMap(),
        desc.lightingConfig,
        BGFX_INVALID_HANDLE,
        cameraCullingMask,
        desc.sceneDesc.cameraOverride.has_value() || sceneCamera != nullptr ? &cameraPosition : nullptr);
    if (!shadowSetup.valid || !desc.shadowMap.Ensure(desc.lightingConfig.shadowMapSize)) {
        return {};
    }

    shadowSetup.binding.depthTexture = desc.shadowMap.DepthTexture();
    shadowSetup.binding.params[2] = desc.shadowMap.Size() == 0U ? 0.0F : 1.0F / static_cast<float>(desc.shadowMap.Size());
    RendererViewConfigurator::ConfigureShadowDepth(desc.viewportPlan.viewIds.shadowDepth, desc.shadowMap.FrameBuffer(), desc.shadowMap.Size());
    desc.sceneRenderer.SubmitMeshPass(
        desc.viewportPlan.viewIds.shadowDepth,
        MeshPassType::ShadowDepth,
        desc.renderScene,
        desc.shadowMap.Size(),
        desc.shadowMap.Size(),
        &shadowSetup.camera,
        desc.sceneDesc.drawBudget,
        desc.lightingConfig,
        nullptr,
        {},
        &desc.gpuDrivenSupport);

    SceneRenderSubmitStats shadowStats = desc.sceneRenderer.LastSubmitStats();
    shadowStats.shadowLightEntityId = shadowSetup.lightEntityId;
    shadowStats.shadowMapAllocationBytes = desc.shadowMap.AllocationBytes();
    desc.aggregateSubmitStats += shadowStats;
    desc.diagnostics += desc.sceneRenderer.LastDiagnostics();
    desc.passSubmitStats.push_back(SceneRenderPassSubmitStats{
        .viewportId = desc.sceneDesc.target.viewport.id.value,
        .viewportIndex = desc.sceneDesc.target.viewport.viewportIndex,
        .renderPass = RenderPassKind::ShadowDepth,
        .pass = MeshPassType::ShadowDepth,
        .stats = shadowStats,
    });

    SceneRenderShadowMapBinding shadowBinding = shadowSetup.binding;
    shadowBinding.params[3] = shadowStats.submittedShadowCasterCount == 0U ? 0.0F : shadowBinding.params[3];
    return shadowBinding;
}

} // namespace kb::render
