#include "renderer/RendererShadowSubmitter.hpp"

#include "kb/render/scene/RenderScene.hpp"
#include "kb/render/scene/SceneRenderer.hpp"
#include "kb/render/shadow/DirectionalShadowPassPlanner.hpp"
#include "kb/render/shadow/ShadowMapResource.hpp"
#include "shadow/PointShadowPassPlanner.hpp"
#include "shadow/ShadowCasterChanges.hpp"
#include "renderer/RendererViewConfigurator.hpp"

#include <bx/math.h>

#include <algorithm>
#include <bit>

namespace kb::render {

namespace {

// Folds what a cached shadow was rendered for into one key; 0 stays free for "nothing cached".
class ShadowCacheKey {
public:
    void Mix(std::uint64_t value) noexcept { key_ = (key_ ^ value) * 1099511628211ULL; }
    void Mix(float value) noexcept { Mix(static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(value))); }
    [[nodiscard]] std::uint64_t Value() const noexcept { return key_ | 1U; }

private:
    std::uint64_t key_ = 1469598103934665603ULL;
};

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
    if (!shadowSetup.valid || !desc.shadowMap.Ensure(shadowSetup.atlasSize, desc.lightingConfig.compactShadows)) {
        desc.shadowMap.DiscardAllCachedCascades();
        return {};
    }

    shadowSetup.binding.depthTexture = desc.shadowMap.DepthTexture();
    shadowSetup.binding.params[2] = desc.shadowMap.Size() == 0U ? 0.0F : 1.0F / static_cast<float>(desc.shadowMap.Size());
    const std::uint32_t tileSize = shadowSetup.binding.cascadeCount > 1U ? desc.shadowMap.Size() / 2U : desc.shadowMap.Size();
    // A cascade keeps what it rendered while its window, the casters over it and the resources stay the same; the
    // sun's bias, filter and strength only change how the map is read.
    const bool caching = desc.lightingConfig.perLightShadowCaching && shadowSetup.cacheable;
    if (caching) {
        ShadowCasterChanges::ApplyToCascades(
            desc.renderScene, desc.sceneRenderer.Resources(), desc.sceneRenderer.ResourceMap(), desc.shadowMap);
    } else {
        desc.shadowMap.DiscardAllCachedCascades();
    }
    desc.aggregateSubmitStats.shadowLightEntityId = shadowSetup.lightEntityId;
    desc.aggregateSubmitStats.shadowMapAllocationBytes = desc.shadowMap.AllocationBytes();
    std::uint32_t submittedCasters = 0U;
    for (std::uint32_t cascade = 0U; cascade < shadowSetup.binding.cascadeCount; ++cascade) {
        const SceneRenderCamera& cascadeCamera = shadowSetup.cascadeCameras[cascade];
        std::uint64_t key = 0U;
        if (caching) {
            ShadowCacheKey mixer;
            mixer.Mix(shadowSetup.lightEntityId);
            mixer.Mix(desc.sceneRenderer.Resources().Revision());
            mixer.Mix(desc.sceneRenderer.ResourceMap().Revision());
            mixer.Mix(static_cast<std::uint64_t>(cameraCullingMask));
            mixer.Mix((static_cast<std::uint64_t>(tileSize) << 32U) ^ shadowSetup.atlasSize ^ (static_cast<std::uint64_t>(cascade) << 16U));
            for (const float value : cascadeCamera.view) mixer.Mix(value);
            for (const float value : cascadeCamera.projection) mixer.Mix(value);
            key = mixer.Value();
            const ShadowMapResource::CachedCascade& cached = desc.shadowMap.CachedCascadeAt(cascade);
            if (cached.key == key) {
                submittedCasters += cached.casterCount;
                continue;
            }
        }
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
            &cascadeCamera,
            desc.sceneDesc.drawBudget,
            desc.lightingConfig,
            nullptr,
            {},
            &desc.gpuDrivenSupport);

        SceneRenderSubmitStats shadowStats = desc.sceneRenderer.LastSubmitStats();
        shadowStats.shadowLightEntityId = shadowSetup.lightEntityId;
        shadowStats.shadowMapAllocationBytes = cascade == 0U ? desc.shadowMap.AllocationBytes() : 0U;
        submittedCasters += shadowStats.submittedShadowCasterCount;
        if (caching) {
            desc.shadowMap.SetCachedCascade(cascade, { .key = key, .casterCount = shadowStats.submittedShadowCasterCount,
                .halfExtent = shadowSetup.cascadeHalfExtent[cascade], .view = cascadeCamera.view });
        }
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
        desc.renderScene, desc.lightingConfig, cameraPosition, cameraCullingMask, BGFX_INVALID_HANDLE,
        desc.pointShadowMap.RowOwners());
    desc.aggregateSubmitStats.selectedShadowLightCount += setup.selectedLights;
    desc.aggregateSubmitStats.droppedShadowLightCount += setup.droppedLights;
    if (setup.droppedLights != 0U) {
        desc.diagnostics.events.push_back({.severity = SceneRenderDiagnosticSeverity::Warning,
            .kind = SceneRenderDiagnosticKind::ShadowLightLimit, .instanceCount = setup.droppedLights});
    }
    if (!setup.valid || !desc.pointShadowMap.Ensure(setup.atlasWidth, setup.atlasHeight)) {
        return;
    }
    setup.binding.depthTexture = desc.pointShadowMap.DepthTexture();
    // A light keeps the tiles it last rendered while its own settings, the atlas and the casters in its range
    // stay the same, however its row moves in the camera's ordering.
    const bool caching = desc.lightingConfig.perLightShadowCaching;
    if (caching) {
        ShadowCasterChanges::ApplyToLocalLights(
            desc.renderScene, desc.sceneRenderer.Resources(), desc.sceneRenderer.ResourceMap(), setup, desc.pointShadowMap);
    }
    for (std::uint32_t light = 0U; light < setup.lightCount; ++light) {
        if (setup.faceCount[light] == 0U) {
            continue;
        }
        std::uint64_t key = 0U;
        if (caching) {
            ShadowCacheKey mixer;
            mixer.Mix(setup.binding.entityId[light]);
            mixer.Mix(desc.sceneRenderer.Resources().Revision());
            mixer.Mix(desc.sceneRenderer.ResourceMap().Revision());
            mixer.Mix((static_cast<std::uint64_t>(setup.tileSize) << 32U) ^ setup.atlasWidth ^ (static_cast<std::uint64_t>(setup.atlasHeight) << 16U));
            for (std::uint32_t c = 0U; c < 4U; ++c) {
                for (const float value : { setup.binding.positionRange[light * 4U + c], setup.binding.spot[light * 4U + c],
                         setup.binding.depthParams[light * 4U + c] }) {
                    mixer.Mix(value);
                }
            }
            key = mixer.Value();
            if (desc.pointShadowMap.CachedTiles(light) == key) {
                continue;
            }
        }
        desc.pointShadowMap.DiscardCachedTiles(light);
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
        desc.pointShadowMap.SetCachedTiles(light, key);
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
    // The lighting shaders read the maps through comparison samplers (the hardware filters) unless a lookup needs the
    // stored depth itself; a handheld takes the four-tap soft kernel.
    binding.wideKernel = !desc.lightingConfig.compactShadows;
    binding.compareSampling = desc.lightingConfig.shadowComparisonSampling && bgfx::getRendererType() != bgfx::RendererType::Noop &&
        !binding.NeedsStoredDepth();
    return binding;
}

} // namespace kb::render
