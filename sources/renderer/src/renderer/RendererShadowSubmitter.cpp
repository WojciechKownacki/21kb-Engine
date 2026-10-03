#include "renderer/RendererShadowSubmitter.hpp"

#include "kb/render/scene/RenderScene.hpp"
#include "kb/render/scene/SceneRenderer.hpp"
#include "kb/render/shadow/DirectionalShadowPassPlanner.hpp"
#include "kb/render/shadow/ShadowMapResource.hpp"
#include "shadow/PointShadowPassPlanner.hpp"
#include "renderer/RendererViewConfigurator.hpp"

#include <bx/math.h>

#include <algorithm>

namespace kb::render {

namespace {

[[nodiscard]] SceneRenderShadowMapBinding SubmitDirectionalShadows(
    const RendererShadowSubmitDesc& desc,
    std::uint32_t cameraCullingMask,
    const std::array<float, 3>* cameraPosition) {
    DirectionalShadowSetup shadowSetup = DirectionalShadowPassPlanner{}.Build(
        desc.renderScene,
        desc.sceneRenderer.Resources(),
        desc.sceneRenderer.ResourceMap(),
        desc.lightingConfig,
        BGFX_INVALID_HANDLE,
        cameraCullingMask,
        cameraPosition,
        1U + static_cast<std::uint32_t>(std::ranges::count_if(
            desc.viewportPlan.viewIds.shadowCascadeViews, [](std::uint16_t view) { return ViewId::IsValid(view); })));
    if (!shadowSetup.valid || !desc.shadowMap.Ensure(shadowSetup.atlasSize)) {
        return {};
    }

    shadowSetup.binding.depthTexture = desc.shadowMap.DepthTexture();
    shadowSetup.binding.params[2] = desc.shadowMap.Size() == 0U ? 0.0F : 1.0F / static_cast<float>(desc.shadowMap.Size());
    const std::uint32_t tileSize = shadowSetup.binding.cascadeCount > 1U ? desc.shadowMap.Size() / 2U : desc.shadowMap.Size();
    std::uint32_t submittedCasters = 0U;
    for (std::uint32_t cascade = 0U; cascade < shadowSetup.binding.cascadeCount; ++cascade) {
        const std::uint16_t viewId = cascade == 0U
            ? desc.viewportPlan.viewIds.shadowDepth
            : desc.viewportPlan.viewIds.shadowCascadeViews[cascade - 1U];
        RendererViewConfigurator::ConfigureShadowDepth(
            viewId, desc.shadowMap.FrameBuffer(), (cascade & 1U) * tileSize, (cascade >> 1U) * tileSize, tileSize);
        desc.sceneRenderer.SubmitMeshPass(
            viewId,
            MeshPassType::ShadowDepth,
            desc.renderScene,
            tileSize,
            tileSize,
            &shadowSetup.cascadeCameras[cascade],
            desc.sceneDesc.drawBudget,
            desc.lightingConfig,
            nullptr,
            {},
            &desc.gpuDrivenSupport);

        SceneRenderSubmitStats shadowStats = desc.sceneRenderer.LastSubmitStats();
        shadowStats.shadowLightEntityId = shadowSetup.lightEntityId;
        shadowStats.shadowMapAllocationBytes = cascade == 0U ? desc.shadowMap.AllocationBytes() : 0U;
        submittedCasters += shadowStats.submittedShadowCasterCount;
        desc.aggregateSubmitStats += shadowStats;
        desc.diagnostics += desc.sceneRenderer.LastDiagnostics();
        desc.passSubmitStats.push_back(SceneRenderPassSubmitStats{
            .viewportId = desc.sceneDesc.target.viewport.id.value,
            .viewportIndex = desc.sceneDesc.target.viewport.viewportIndex,
            .renderPass = RenderPassKind::ShadowDepth,
            .pass = MeshPassType::ShadowDepth,
            .stats = shadowStats,
        });
    }

    SceneRenderShadowMapBinding shadowBinding = shadowSetup.binding;
    shadowBinding.params[3] = submittedCasters == 0U ? 0.0F : shadowBinding.params[3];
    return shadowBinding;
}

void SubmitPointShadows(
    const RendererShadowSubmitDesc& desc,
    std::uint32_t cameraCullingMask,
    const std::array<float, 3>* cameraPosition,
    SceneRenderShadowMapBinding& binding) {
    PointShadowSetup setup = PointShadowPassPlanner::Build(
        desc.renderScene, desc.lightingConfig, cameraPosition, cameraCullingMask, BGFX_INVALID_HANDLE);
    if (!setup.valid || !desc.pointShadowMap.Ensure(setup.atlasWidth, setup.atlasHeight)) {
        return;
    }
    setup.binding.depthTexture = desc.pointShadowMap.DepthTexture();
    for (std::uint32_t light = 0U; light < setup.lightCount; ++light) {
        for (std::uint32_t face = 0U; face < setup.faceCount[light]; ++face) {
            const std::uint32_t index = light * ScenePointShadowBinding::kFaceCount + face;
            const std::uint16_t viewId = desc.viewportPlan.viewIds.pointShadowViews[index];
            if (!ViewId::IsValid(viewId)) {
                // A viewport without dedicated point-shadow views (a detached editor view) renders none.
                return;
            }
            PointShadowFace& tile = setup.faces[light][face];
            RendererViewConfigurator::ConfigureShadowDepth(
                viewId, desc.pointShadowMap.FrameBuffer(), tile.tileColumn * setup.tileSize, tile.tileRow * setup.tileSize, setup.tileSize);
            desc.sceneRenderer.SubmitMeshPass(
                viewId,
                MeshPassType::ShadowDepth,
                desc.renderScene,
                setup.tileSize,
                setup.tileSize,
                &tile.camera,
                desc.sceneDesc.drawBudget,
                desc.lightingConfig,
                nullptr,
                {},
                &desc.gpuDrivenSupport);
            SceneRenderSubmitStats stats = desc.sceneRenderer.LastSubmitStats();
            stats.shadowMapAllocationBytes = index == 0U ? desc.pointShadowMap.AllocationBytes() : 0U;
            desc.aggregateSubmitStats += stats;
            desc.diagnostics += desc.sceneRenderer.LastDiagnostics();
            desc.passSubmitStats.push_back(SceneRenderPassSubmitStats{
                .viewportId = desc.sceneDesc.target.viewport.id.value,
                .viewportIndex = desc.sceneDesc.target.viewport.viewportIndex,
                .renderPass = RenderPassKind::ShadowDepth,
                .pass = MeshPassType::ShadowDepth,
                .stats = stats,
            });
        }
    }
    binding.point = setup.binding;
}

} // namespace

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
    const std::array<float, 3>* cameraPositionPointer =
        desc.sceneDesc.cameraOverride.has_value() || sceneCamera != nullptr ? &cameraPosition : nullptr;

    SceneRenderShadowMapBinding binding = SubmitDirectionalShadows(desc, cameraCullingMask, cameraPositionPointer);
    SubmitPointShadows(desc, cameraCullingMask, cameraPositionPointer, binding);
    return binding;
}

} // namespace kb::render
