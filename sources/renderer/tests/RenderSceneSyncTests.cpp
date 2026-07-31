#include "RendererTestSupport.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/ParticleEffectAsset.hpp"
#include "engine/scene/ParticleEffectAssetIO.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneLightingAccess.hpp"
#include "engine/scene/SceneMaterialInstances.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/SceneParticleSystems.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/VisibilityComponent.hpp"
#include "engine/scene/WorldBackdropComponent.hpp"
#include "engine/scene/AmbientRadianceComponent.hpp"
#include "engine/scene/DetailSwitchComponent.hpp"
#include "engine/scene/FacingPanelComponent.hpp"
#include "kb/render/resources/BuiltInParticleQuadMesh.hpp"
#include "kb/render/scene/EcsRenderSceneSynchronizer.hpp"
#include "kb/render/scene/SceneParticleRenderSynchronizer.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/post/SceneExposureMeter.hpp"
#include "kb/render/scene/RenderInstanceBuffer.hpp"
#include "kb/render/scene/RenderScene.hpp"
#include "scene/RenderSceneProxyConverters.hpp"
#include "engine/ecs/WorkerPool.hpp"
#include "kb/render/scene/RenderBridgeTelemetry.hpp"
#include "kb/render/scene/SceneRenderResourceMap.hpp"
#include "kb/render/scene/SceneRenderer.hpp"
#include "kb/render/shadow/DirectionalShadowPassPlanner.hpp"
#include "scene/lighting/SceneForwardLightSelector.hpp"
#include "scene/lighting/SceneLightingPacker.hpp"
#include "scene/SceneLightColor.hpp"
#include "scene/SceneRenderVisibilityPublisher.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <span>
#include <system_error>
#include <vector>

namespace kb::render::tests {
namespace {

[[nodiscard]] kb::scene::TransformComponent TransformAt(float x, float y, float z) {
    return kb::scene::TransformComponent{
        .localPosition = kb::scene::Vec3{ x, y, z },
        .worldPosition = kb::scene::Vec3{ x, y, z },
        .worldDirty = false,
    };
}

[[nodiscard]] kb::scene::TransformComponent LocalOnlyTransformAt(float x, float y, float z) {
    return kb::scene::TransformComponent{
        .localPosition = kb::scene::Vec3{ x, y, z },
    };
}

void RunCreatesStableRenderProxiesTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneEntity camera = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Camera",
        .transform = TransformAt(0.0F, 2.0F, -6.0F),
    });
    scene.Components().Cameras().Set(camera, kb::scene::CameraComponent{ .primary = true });

    const kb::scene::SceneEntity mesh = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Mesh",
        .transform = TransformAt(1.0F, 2.0F, 3.0F),
    });
    scene.Components().MeshRenderers().Set(mesh, kb::scene::MeshRendererComponent{
        .meshAssetId = 42U,
        .materialAssetId = 7U,
    });

    RenderScene renderScene;
    EcsRenderSceneSynchronizer{}.Sync(scene, renderScene);

    const MeshRenderProxy* meshProxy = renderScene.FindMeshByEntity(mesh.Id());
    const CameraRenderProxy* cameraProxy = renderScene.FindCameraByEntity(camera.Id());
    Require(meshProxy != nullptr && meshProxy->id.IsValid(), "RenderScene did not create a mesh proxy for ECS mesh");
    Require(cameraProxy != nullptr && cameraProxy->id.IsValid(), "RenderScene did not create a camera proxy for ECS camera");
    Require(HasDirtyFlag(meshProxy->dirty, RenderProxyDirtyFlag::Mesh), "New mesh proxy was not marked dirty");

    const RenderProxyId stableMeshProxy = meshProxy->id;
    renderScene.ClearDirty();
    EcsRenderSceneSynchronizer{}.Sync(scene, renderScene);
    meshProxy = renderScene.FindMeshByEntity(mesh.Id());
    Require(meshProxy != nullptr && meshProxy->id == stableMeshProxy, "RenderScene did not preserve stable entity-to-proxy mapping");
    Require(meshProxy->dirty == RenderProxyDirtyFlag::None, "RenderScene dirtied an unchanged mesh proxy");

    SceneRenderSnapshot snapshot;
    renderScene.BuildSnapshotInto(1280, 720, snapshot);
    Require(snapshot.camera.has_value(), "RenderScene snapshot did not include the primary ECS camera");
    Require(snapshot.meshes.size() == 1U, "RenderScene snapshot did not include the visible ECS mesh");
    Require(snapshot.meshes[0].entityId == mesh.Id(), "RenderScene snapshot mesh entity id does not match ECS");
}

// LIB-135: proves CameraRenderProxyDesc::viewportId/priority actually change which camera
// RenderScene::BuildPrimaryCamera picks, not just that the fields exist and compile. Also
// proves the tie-break (highest priority, then lowest entityId) is deterministic - it used to
// be "first match in unordered_map iteration order", i.e. undefined between two primary
// cameras.
void RunRenderScenePrimaryCameraSelectionRespectsViewportAndPriorityTest() {
    RenderScene renderScene;

    // Camera A: viewportId=0 ("any viewport"), low priority - only candidate for viewport 0,
    // and the lowest-priority fallback candidate for every other viewport.
    static_cast<void>(renderScene.UpsertCamera(CameraRenderProxyDesc{
        .entityId = 1U,
        .verticalFovDegrees = 30.0F,
        .primary = true,
        .visible = true,
        .viewportId = 0U,
        .priority = 0,
    }));
    // Camera B: targets viewport 5 specifically, mid priority.
    static_cast<void>(renderScene.UpsertCamera(CameraRenderProxyDesc{
        .entityId = 2U,
        .verticalFovDegrees = 50.0F,
        .primary = true,
        .visible = true,
        .viewportId = 5U,
        .priority = 10,
    }));
    // Camera C: also targets viewport 5, highest priority - must win over B on viewport 5.
    static_cast<void>(renderScene.UpsertCamera(CameraRenderProxyDesc{
        .entityId = 3U,
        .verticalFovDegrees = 70.0F,
        .primary = true,
        .visible = true,
        .viewportId = 5U,
        .priority = 20,
    }));

    const std::optional<SceneRenderCamera> forDefaultViewport = renderScene.BuildPrimaryCamera(1280U, 720U, 0U);
    Require(forDefaultViewport.has_value(), "RenderScene did not select a camera for the default (0) viewport");
    const SceneRenderCamera expectedForDefaultViewport = RenderSceneCameraBuilder::Build(CameraRenderProxyDesc{ .verticalFovDegrees = 30.0F, .primary = true, .visible = true, .viewportId = 0U, .priority = 0 }, 1280U, 720U);
    Require(forDefaultViewport->projection == expectedForDefaultViewport.projection, "RenderScene BuildPrimaryCamera(viewport=0) did not select the only viewport-0 camera (A)");

    const std::optional<SceneRenderCamera> forViewportFive = renderScene.BuildPrimaryCamera(1280U, 720U, 5U);
    Require(forViewportFive.has_value(), "RenderScene did not select a camera for viewport 5");
    const SceneRenderCamera expectedForViewportFive = RenderSceneCameraBuilder::Build(CameraRenderProxyDesc{ .verticalFovDegrees = 70.0F, .primary = true, .visible = true, .viewportId = 5U, .priority = 20 }, 1280U, 720U);
    Require(forViewportFive->projection == expectedForViewportFive.projection, "RenderScene BuildPrimaryCamera(viewport=5) did not select the higher-priority camera (C) over the lower-priority one (B) targeting the same viewport");

    const std::optional<SceneRenderCamera> forUnrelatedViewport = renderScene.BuildPrimaryCamera(1280U, 720U, 99U);
    Require(forUnrelatedViewport.has_value(), "RenderScene did not fall back to the any-viewport camera for a viewport nothing specifically targets");
    Require(forUnrelatedViewport->projection == expectedForDefaultViewport.projection, "RenderScene BuildPrimaryCamera(viewport=99) did not fall back to the viewport-0 (any-viewport) camera (A)");

    // Deterministic tie-break: two cameras, same priority, both targeting viewport 0 - the
    // lower entityId must consistently win, proving selection no longer depends on
    // unordered_map iteration order.
    RenderScene tieBreakScene;
    static_cast<void>(tieBreakScene.UpsertCamera(CameraRenderProxyDesc{ .entityId = 200U, .verticalFovDegrees = 40.0F, .primary = true, .visible = true }));
    static_cast<void>(tieBreakScene.UpsertCamera(CameraRenderProxyDesc{ .entityId = 100U, .verticalFovDegrees = 90.0F, .primary = true, .visible = true }));
    const std::optional<SceneRenderCamera> tieBreakResult = tieBreakScene.BuildPrimaryCamera(1280U, 720U, 0U);
    Require(tieBreakResult.has_value(), "RenderScene tie-break scenario did not select a camera");
    const SceneRenderCamera expectedTieBreakWinner = RenderSceneCameraBuilder::Build(CameraRenderProxyDesc{ .verticalFovDegrees = 90.0F, .primary = true, .visible = true }, 1280U, 720U);
    Require(tieBreakResult->projection == expectedTieBreakWinner.projection, "RenderScene BuildPrimaryCamera priority tie-break did not deterministically pick the lowest entityId");
}

// LIB-136: proves EcsRenderSceneSynchronizer actually copies the new CameraComponent
// cullingMask/clearMode/clearColor and MeshRendererComponent layer fields into the renderer
// proxies - not just that the fields exist on both sides and compile.
void RunEcsSyncPropagatesCullingMaskAndClearSettingsTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneEntity camera = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Camera" });
    scene.Components().Cameras().Set(camera, kb::scene::CameraComponent{
        .primary = true,
        .cullingMask = 0x00000006U,
        .clearMode = kb::scene::CameraClearMode::DepthOnly,
        .clearColor = kb::scene::Vec3{ 0.25F, 0.5F, 0.75F },
    });

    const kb::scene::SceneEntity mesh = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Mesh" });
    scene.Components().MeshRenderers().Set(mesh, kb::scene::MeshRendererComponent{
        .meshAssetId = 5U,
        .materialAssetId = 9U,
        .layer = 0x00000004U,
    });

    RenderScene renderScene;
    EcsRenderSceneSynchronizer{}.Sync(scene, renderScene);

    const CameraRenderProxy* cameraProxy = renderScene.FindCameraByEntity(camera.Id());
    Require(cameraProxy != nullptr, "RenderScene did not create a camera proxy");
    Require(cameraProxy->desc.cullingMask == 0x00000006U, "EcsRenderSceneSynchronizer did not propagate CameraComponent::cullingMask");
    Require(cameraProxy->desc.clearMode == RenderCameraClearMode::DepthOnly, "EcsRenderSceneSynchronizer did not propagate CameraComponent::clearMode");
    Require(NearlyEqual(cameraProxy->desc.clearColor[0], 0.25F) && NearlyEqual(cameraProxy->desc.clearColor[1], 0.5F) && NearlyEqual(cameraProxy->desc.clearColor[2], 0.75F),
        "EcsRenderSceneSynchronizer did not propagate CameraComponent::clearColor");

    const MeshRenderProxy* meshProxy = renderScene.FindMeshByEntity(mesh.Id());
    Require(meshProxy != nullptr, "RenderScene did not create a mesh proxy");
    Require(meshProxy->desc.layer == 0x00000004U, "EcsRenderSceneSynchronizer did not propagate MeshRendererComponent::layer");

    const SceneRenderCamera builtCamera = RenderSceneCameraBuilder::Build(cameraProxy->desc, 1280U, 720U);
    Require(builtCamera.cullingMask == 0x00000006U, "RenderSceneCameraBuilder::Build did not carry cullingMask into the resolved SceneRenderCamera");
    Require(builtCamera.clearMode == SceneRenderCameraClearMode::DepthOnly, "RenderSceneCameraBuilder::Build did not carry clearMode into the resolved SceneRenderCamera");
}

void RunEcsSyncPropagatesDetailSwitchPolicyTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneEntity mesh = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Detail Mesh" });
    scene.Components().MeshRenderers().Set(mesh, kb::scene::MeshRendererComponent{ .meshAssetId = 9U });
    scene.Components().DetailSwitches().Set(mesh, kb::scene::SceneDetailSwitchComponent{
        .groupId = 41U, .minimumLod = 1U, .maximumLod = 3U, .promoteCoverage = 0.36F, .demoteCoverage = 0.18F, .enabled = true,
    });
    RenderScene renderScene;
    EcsRenderSceneSynchronizer{}.Sync(scene, renderScene);
    const MeshRenderProxy* proxy = renderScene.FindMeshByEntity(mesh.Id());
    Require(proxy != nullptr && proxy->desc.detailSwitchEnabled && proxy->desc.detailSwitchGroupId == 41U &&
            proxy->desc.detailSwitchMinimumLod == 1U && proxy->desc.detailSwitchMaximumLod == 3U &&
            NearlyEqual(proxy->desc.detailSwitchPromoteCoverage, 0.36F) && NearlyEqual(proxy->desc.detailSwitchDemoteCoverage, 0.18F),
        "ECS Detail Switch policy was not copied into the renderer-derived mesh proxy");
}

void RunEcsSyncResolvesWorldBackdropDeterministicallyTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneEntity low = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Backdrop Low" });
    const kb::scene::SceneEntity high = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Backdrop High" });
    kb::scene::WorldBackdropComponent lowBackdrop{};
    lowBackdrop.mode = kb::scene::WorldBackdropMode::SolidColor;
    lowBackdrop.color = kb::scene::Vec3{ 0.1F, 0.2F, 0.3F };
    lowBackdrop.priority = 2;
    scene.Components().WorldBackdrops().Set(low, lowBackdrop);
    kb::scene::WorldBackdropComponent highBackdrop{};
    highBackdrop.mode = kb::scene::WorldBackdropMode::VerticalGradient;
    highBackdrop.horizonColor = kb::scene::Vec3{ 0.2F, 0.3F, 0.4F };
    highBackdrop.zenithColor = kb::scene::Vec3{ 0.6F, 0.7F, 0.8F };
    highBackdrop.gradientExponent = 1.5F;
    highBackdrop.priority = 7;
    scene.Components().WorldBackdrops().Set(high, highBackdrop);

    RenderScene renderScene;
    EcsRenderSceneSynchronizer{}.Sync(scene, renderScene);
    Require(renderScene.WorldBackdrop().has_value(), "ECS sync did not derive a world backdrop render state");
    const SceneRenderWorldBackdrop& selected = *renderScene.WorldBackdrop();
    Require(selected.entityId == high.Id() && selected.mode == SceneRenderWorldBackdropMode::VerticalGradient,
        "ECS world backdrop sync did not choose the highest-priority enabled backdrop");
    Require(NearlyEqual(selected.horizonColor[0], 0.2F) && NearlyEqual(selected.zenithColor[2], 0.8F) && NearlyEqual(selected.gradientExponent, 1.5F),
        "ECS world backdrop sync lost authored gradient fields");

    highBackdrop.enabled = false;
    highBackdrop.priority = 99;
    scene.Components().WorldBackdrops().Set(high, highBackdrop);
    EcsRenderSceneSynchronizer{}.Sync(scene, renderScene);
    Require(renderScene.WorldBackdrop().has_value() && renderScene.WorldBackdrop()->entityId == low.Id(),
        "Disabled world backdrop was not excluded from deterministic runtime selection");
}

void RunEcsSyncResolvesAmbientRadianceDeterministicallyTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneEntity low = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Ambient Low" });
    const kb::scene::SceneEntity high = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Ambient High" });
    kb::scene::AmbientRadianceComponent lowAmbient{};
    lowAmbient.mode = kb::scene::AmbientRadianceMode::Constant;
    lowAmbient.color = kb::scene::Vec3{ 0.1F, 0.2F, 0.3F };
    lowAmbient.priority = 2;
    scene.Components().AmbientRadiances().Set(low, lowAmbient);
    kb::scene::AmbientRadianceComponent highAmbient{};
    highAmbient.mode = kb::scene::AmbientRadianceMode::EnvironmentMap;
    highAmbient.environmentAssetId = 77U;
    highAmbient.horizonColor = kb::scene::Vec3{ 0.2F, 0.3F, 0.4F };
    highAmbient.zenithColor = kb::scene::Vec3{ 0.6F, 0.7F, 0.8F };
    highAmbient.intensity = 1.5F;
    highAmbient.diffuseIntensity = 1.25F;
    highAmbient.specularIntensity = 0.5F;
    highAmbient.priority = 7;
    scene.Components().AmbientRadiances().Set(high, highAmbient);
    RenderScene renderScene;
    EcsRenderSceneSynchronizer{}.Sync(scene, renderScene);
    Require(renderScene.AmbientRadiance().has_value(), "ECS sync did not derive ambient radiance render state");
    const SceneRenderAmbientRadiance& selected = *renderScene.AmbientRadiance();
    Require(selected.entityId == high.Id() && selected.mode == SceneRenderAmbientRadianceMode::EnvironmentMap && selected.environmentAssetId == 77U,
        "ECS ambient radiance sync did not choose highest-priority enabled component");
    Require(NearlyEqual(selected.zenithColor[2], 0.8F) && NearlyEqual(selected.intensity, 1.5F) && NearlyEqual(selected.specularIntensity, 0.5F),
        "ECS ambient radiance sync lost authored fields");

    const kb::scene::SceneEntity backdropEntity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Captured Backdrop" });
    kb::scene::WorldBackdropComponent backdrop{};
    backdrop.mode = kb::scene::WorldBackdropMode::VerticalGradient;
    backdrop.color = kb::scene::Vec3{ 0.12F, 0.13F, 0.14F };
    backdrop.horizonColor = kb::scene::Vec3{ 0.21F, 0.22F, 0.23F };
    backdrop.zenithColor = kb::scene::Vec3{ 0.71F, 0.72F, 0.73F };
    scene.Components().WorldBackdrops().Set(backdropEntity, backdrop);
    highAmbient.mode = kb::scene::AmbientRadianceMode::CapturedEnvironment;
    scene.Components().AmbientRadiances().Set(high, highAmbient);
    EcsRenderSceneSynchronizer{}.Sync(scene, renderScene);
    Require(renderScene.AmbientRadiance().has_value() &&
            renderScene.AmbientRadiance()->mode == SceneRenderAmbientRadianceMode::CapturedEnvironment &&
            NearlyEqual(renderScene.AmbientRadiance()->horizonColor[1], 0.22F) &&
            NearlyEqual(renderScene.AmbientRadiance()->zenithColor[2], 0.73F),
        "Captured ambient radiance did not derive its frame input from the selected ECS world backdrop");

    highAmbient.enabled = false;
    scene.Components().AmbientRadiances().Set(high, highAmbient);
    EcsRenderSceneSynchronizer{}.Sync(scene, renderScene);
    Require(renderScene.AmbientRadiance().has_value() && renderScene.AmbientRadiance()->entityId == low.Id(),
        "Disabled ambient radiance was not excluded from deterministic selection");
}

void RunEcsSyncResolvesVisibilityGateHierarchyAndMaskTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneEntity parent = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Visibility Parent" });
    const kb::scene::SceneEntity child = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Visibility Child" });
    Require(scene.Hierarchy().SetParent(child, parent), "Visibility-gate test setup did not parent the child");
    scene.Components().MeshRenderers().Set(child, kb::scene::MeshRendererComponent{
        .meshAssetId = 5U,
        .materialAssetId = 9U,
        .layer = 0x000000FFU,
    });
    scene.Components().Visibility().Set(parent, kb::scene::VisibilityComponent{
        .mode = kb::scene::VisibilityMode::Hidden,
        .mask = 0x0000000FU,
        .visible = false,
    });
    scene.Components().Visibility().Set(child, kb::scene::VisibilityComponent{
        .mode = kb::scene::VisibilityMode::Inherit,
        .mask = 0x00000003U,
    });

    RenderScene renderScene;
    EcsRenderSceneSynchronizer{}.Sync(scene, renderScene);
    const MeshRenderProxy* proxy = renderScene.FindMeshByEntity(child.Id());
    Require(proxy != nullptr && !proxy->desc.visible,
        "EcsRenderSceneSynchronizer did not inherit a hidden Visibility Gate from the parent");
    Require(proxy->desc.layer == 0x00000003U,
        "EcsRenderSceneSynchronizer did not intersect the hierarchy Visibility Mask with the mesh layer");

    scene.Components().Visibility().Set(child, kb::scene::VisibilityComponent{
        .mode = kb::scene::VisibilityMode::Visible,
        .mask = 0x00000003U,
    });
    EcsRenderSceneSynchronizer{}.Sync(scene, renderScene);
    proxy = renderScene.FindMeshByEntity(child.Id());
    Require(proxy != nullptr && proxy->desc.visible,
        "EcsRenderSceneSynchronizer did not let the child's explicit Visible mode override the parent gate");
}

// LIB-139/LIB-140: proves EcsRenderSceneSynchronizer::SyncMesh actually resolves a live
// materialInstanceHandle - winning over the authored materialAssetId - and honestly falls
// back to "no material" (0) once the instance is Release()d - not a crash, not silently
// keeping the stale parent. Since LIB-140, the proxy carries the RAW instance handle
// (unresolved), not the parent asset id - see EcsRenderSceneSynchronizer.cpp's
// ResolveMaterialAssetId doc comment for why (RuntimeMaterialResourceEnsurer needs the raw
// handle to recognize + resolve parameter overrides).
void RunEcsSyncResolvesMaterialInstanceHandleTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneEntity mesh = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Instanced Mesh" });
    scene.Components().MeshRenderers().Set(mesh, kb::scene::MeshRendererComponent{
        .meshAssetId = 5U,
        .materialAssetId = 42U,
    });

    const std::uint64_t instance = scene.MaterialInstances().Create(777U);
    Require(instance != 0U, "RenderScene sync test setup failed to create a material instance");
    scene.Components().MeshRenderers().TryGet(mesh)->materialInstanceHandle = instance;

    RenderScene renderScene;
    EcsRenderSceneSynchronizer{}.Sync(scene, renderScene);
    const MeshRenderProxy* liveProxy = renderScene.FindMeshByEntity(mesh.Id());
    Require(liveProxy != nullptr && liveProxy->desc.materialAssetId == instance,
        "EcsRenderSceneSynchronizer did not pass through the live materialInstanceHandle unresolved (must win over the authored materialAssetId)");
    Require(scene.MaterialInstances().Parent(instance) == 777U,
        "RenderScene sync test setup's instance did not resolve to its parent material asset id via SceneMaterialInstances().Parent");

    Require(scene.MaterialInstances().Release(instance), "RenderScene sync test setup failed to release the material instance");
    EcsRenderSceneSynchronizer{}.Sync(scene, renderScene);
    const MeshRenderProxy* afterReleaseProxy = renderScene.FindMeshByEntity(mesh.Id());
    Require(afterReleaseProxy != nullptr && afterReleaseProxy->desc.materialAssetId == 0U,
        "EcsRenderSceneSynchronizer must honestly resolve a released materialInstanceHandle to 0 (no material), not silently keep the stale parent or fall back to materialAssetId");
}

void RunRenderSceneSyncsLightPipelineFieldsTest() {
    kb::scene::Scene scene;
    kb::scene::SceneLightingAccess::SetBasicLightingEnabled(scene, true);
    const kb::scene::SceneEntity light = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Key Light",
        .transform = TransformAt(2.0F, 3.0F, 4.0F),
    });
    scene.Components().Lights().Set(light, kb::scene::LightComponent{
        .kind = kb::scene::LightKind::Spot,
        .color = kb::scene::Vec3{ 0.7F, 0.8F, 0.9F },
        .intensity = 6.0F,
        .range = 25.0F,
        .innerConeDegrees = 20.0F,
        .outerConeDegrees = 40.0F,
        .areaWidth = 3.0F,
        .areaHeight = 1.5F,
        .contactShadowLength = 0.25F,
        .volumetricScattering = 0.4F,
        .castsShadow = false,
        .layerMask = 3U,
    });

    RenderScene renderScene;
    EcsRenderSceneSynchronizer{}.Sync(scene, renderScene);

    const LightRenderProxy* proxy = renderScene.FindLightByEntity(light.Id());
    Require(proxy != nullptr && proxy->id.IsValid(), "RenderScene did not create a light proxy for ECS light");
    Require(proxy->desc.kind == RenderLightKind::Spot, "RenderScene did not preserve ECS light kind");
    Require(NearlyEqual(proxy->desc.position[0], 2.0F) && NearlyEqual(proxy->desc.position[1], 3.0F) && NearlyEqual(proxy->desc.position[2], 4.0F), "RenderScene did not preserve ECS light position");
    Require(NearlyEqual(proxy->desc.color[0], 0.7F) && NearlyEqual(proxy->desc.color[1], 0.8F) && NearlyEqual(proxy->desc.color[2], 0.9F), "RenderScene did not preserve ECS light color when useColorTemperature is false");
    Require(NearlyEqual(proxy->desc.intensity, 6.0F), "RenderScene did not preserve ECS light intensity");
    Require(NearlyEqual(proxy->desc.range, 25.0F), "RenderScene did not preserve ECS light range");
    Require(NearlyEqual(proxy->desc.areaWidth, 3.0F) && NearlyEqual(proxy->desc.areaHeight, 1.5F), "RenderScene did not preserve ECS light area size");
    Require(NearlyEqual(proxy->desc.contactShadowLength, 0.25F), "RenderScene did not preserve ECS light contact shadow length");
    Require(NearlyEqual(proxy->desc.volumetricScattering, 0.4F), "RenderScene did not preserve ECS light volumetric scattering");
    Require(!proxy->desc.castsShadow, "RenderScene did not preserve ECS light shadow flag");
    Require(proxy->desc.layer == 3U, "RenderScene did not preserve ECS light layerMask");

    SceneRenderSnapshot snapshot;
    renderScene.BuildSnapshotInto(1280, 720, snapshot);
    Require(snapshot.lights.size() == 1U, "RenderScene snapshot did not include visible ECS light");
    Require(snapshot.lights[0].kind == RenderLightKind::Spot, "RenderScene snapshot did not preserve light kind");
    Require(NearlyEqual(snapshot.lights[0].position[0], 2.0F), "RenderScene snapshot did not preserve light position");
    Require(NearlyEqual(snapshot.lights[0].direction[2], 1.0F), "RenderScene snapshot did not publish light direction");
    Require(NearlyEqual(snapshot.lights[0].intensity, 6.0F), "RenderScene snapshot did not preserve light intensity");
    Require(snapshot.lights[0].innerConeCos > snapshot.lights[0].outerConeCos, "RenderScene snapshot did not publish ordered spot cone cosines");
    Require(NearlyEqual(snapshot.lights[0].areaWidth, 3.0F) && NearlyEqual(snapshot.lights[0].areaHeight, 1.5F), "RenderScene snapshot did not publish area light size");
    Require(NearlyEqual(snapshot.lights[0].contactShadowLength, 0.25F), "RenderScene snapshot did not publish contact shadow length");
    Require(NearlyEqual(snapshot.lights[0].volumetricScattering, 0.4F), "RenderScene snapshot did not publish volumetric scattering");
    Require(!snapshot.lights[0].castsShadow, "RenderScene snapshot did not publish light shadow flag");
}

void RunRenderSceneIgnoresLightsWithoutBasicLightingProviderTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneEntity light = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Provider Gated Light",
        .transform = TransformAt(0.0F, 3.0F, 0.0F),
    });
    scene.Components().Lights().Set(light, kb::scene::LightComponent{
        .kind = kb::scene::LightKind::Directional,
        .intensity = 4.0F,
    });

    RenderScene renderScene;
    EcsRenderSceneSynchronizer{}.Sync(scene, renderScene);

    Require(renderScene.LightProxyCount() == 0U, "RenderScene synced a light while Basic Lighting provider was inactive");
    SceneRenderSnapshot snapshot;
    renderScene.BuildSnapshotInto(1280, 720, snapshot);
    Require(snapshot.lights.empty(), "RenderScene snapshot exposed lights while Basic Lighting provider was inactive");
}

// LIB-141: proves EcsRenderSceneSynchronizer::SyncLight actually calls
// SceneLightColor::Resolve when building the proxy (integration wiring) - not just that the
// pure math itself is correct (already proven in isolation by
// RunSceneLightColorResolvesTemperatureTest). A single-light scene keeps this independent
// of RunRenderSceneSyncsLightPipelineFieldsTest's own byte-for-byte color pass-through
// assertion (useColorTemperature=false), which proves the opposite direction: zero behavior
// change for existing (non-temperature) content.
void RunRenderSceneSyncResolvesLightColorTemperatureTest() {
    kb::scene::Scene scene;
    kb::scene::SceneLightingAccess::SetBasicLightingEnabled(scene, true);
    const kb::scene::SceneEntity light = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Temperature Light",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().Lights().Set(light, kb::scene::LightComponent{
        .kind = kb::scene::LightKind::Point,
        .color = kb::scene::Vec3{ 1.0F, 1.0F, 1.0F },
        .intensity = 1.0F,
        .range = 5.0F,
        .useColorTemperature = true,
        .colorTemperatureKelvin = 1000.0F,
    });

    RenderScene renderScene;
    EcsRenderSceneSynchronizer{}.Sync(scene, renderScene);

    const LightRenderProxy* proxy = renderScene.FindLightByEntity(light.Id());
    Require(proxy != nullptr, "RenderScene did not create a light proxy for the color-temperature ECS light");
    Require(proxy->desc.color[2] < proxy->desc.color[0] * 0.5F,
        "EcsRenderSceneSynchronizer::SyncLight must actually resolve useColorTemperature into the proxy's color, not pass the raw authored color through");
}

void RunTracksUpdatesWithoutReplacingProxyTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneEntity mesh = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Mesh",
        .transform = TransformAt(1.0F, 2.0F, 3.0F),
    });
    scene.Components().MeshRenderers().Set(mesh, kb::scene::MeshRendererComponent{
        .meshAssetId = 1U,
        .materialAssetId = 2U,
    });

    RenderScene renderScene;
    EcsRenderSceneSynchronizer synchronizer;
    synchronizer.Sync(scene, renderScene);
    const RenderProxyId initialProxyId = renderScene.FindMeshByEntity(mesh.Id())->id;
    renderScene.ClearDirty();

    scene.Transforms().Set(mesh, TransformAt(4.0F, 5.0F, 6.0F));
    scene.Components().MeshRenderers().Set(mesh, kb::scene::MeshRendererComponent{
        .meshAssetId = 10U,
        .materialAssetId = 20U,
    });
    synchronizer.Sync(scene, renderScene);

    const MeshRenderProxy* meshProxy = renderScene.FindMeshByEntity(mesh.Id());
    Require(meshProxy != nullptr, "RenderScene lost mesh proxy after ECS update");
    Require(meshProxy->id == initialProxyId, "RenderScene replaced a proxy instead of updating it");
    Require(HasDirtyFlag(meshProxy->dirty, RenderProxyDirtyFlag::Transform), "RenderScene did not track dirty transform");
    Require(HasDirtyFlag(meshProxy->dirty, RenderProxyDirtyFlag::Mesh), "RenderScene did not track dirty mesh");
    Require(HasDirtyFlag(meshProxy->dirty, RenderProxyDirtyFlag::Material), "RenderScene did not track dirty material");

    SceneRenderSnapshot snapshot;
    renderScene.BuildSnapshotInto(1280, 720, snapshot);
    Require(snapshot.meshes.size() == 1U, "RenderScene update snapshot lost the mesh");
    Require(NearlyEqual(snapshot.meshes[0].model[12], 4.0F), "RenderScene update did not publish transform X in the same sync");
}

void RunSyncEntitiesUpdatesOnlyRequestedProxyTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneEntity first = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "First",
        .transform = TransformAt(1.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(first, kb::scene::MeshRendererComponent{ .meshAssetId = 10U });

    const kb::scene::SceneEntity second = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Second",
        .transform = TransformAt(2.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(second, kb::scene::MeshRendererComponent{ .meshAssetId = 20U });

    RenderScene renderScene;
    EcsRenderSceneSynchronizer synchronizer;
    synchronizer.Sync(scene, renderScene);
    const RenderProxyId firstProxyId = renderScene.FindMeshByEntity(first.Id())->id;
    const RenderProxyId secondProxyId = renderScene.FindMeshByEntity(second.Id())->id;
    renderScene.ClearDirty();

    scene.Transforms().Set(first, TransformAt(11.0F, 0.0F, 0.0F));
    scene.Transforms().Set(second, TransformAt(22.0F, 0.0F, 0.0F));
    const std::array<std::uint64_t, 1U> dirty{ first.Id() };
    synchronizer.SyncEntities(scene, renderScene, std::span<const std::uint64_t>{ dirty.data(), dirty.size() });

    const MeshRenderProxy* firstProxy = renderScene.FindMeshByEntity(first.Id());
    const MeshRenderProxy* secondProxy = renderScene.FindMeshByEntity(second.Id());
    Require(firstProxy != nullptr && firstProxy->id == firstProxyId, "Incremental sync lost the requested mesh proxy");
    Require(secondProxy != nullptr && secondProxy->id == secondProxyId, "Incremental sync lost an untouched mesh proxy");
    Require(HasDirtyFlag(firstProxy->dirty, RenderProxyDirtyFlag::Transform), "Incremental sync did not dirty the requested transform");
    Require(secondProxy->dirty == RenderProxyDirtyFlag::None, "Incremental sync dirtied an untouched proxy");
    Require(NearlyEqual(firstProxy->desc.model[12], 11.0F), "Incremental sync did not update requested mesh transform");
    Require(NearlyEqual(secondProxy->desc.model[12], 2.0F), "Incremental sync updated an entity that was not requested");
}

void RunMeshRendererModifiedRuntimeQueueInvalidatesMaterialProxyTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneEntity mesh = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "MaterialDirtyMesh",
        .transform = TransformAt(1.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(mesh, kb::scene::MeshRendererComponent{
        .meshAssetId = 10U,
        .materialAssetId = 20U,
    });

    RenderScene renderScene;
    EcsRenderSceneSynchronizer synchronizer;
    synchronizer.Sync(scene, renderScene);
    const std::vector<SceneRenderDrawGroup>& initialGroups = renderScene.DrawGroups();
    Require(initialGroups.size() == 1U && initialGroups[0].materialAssetId == 20U, "KBMAT-UE-0012: setup did not build the initial material draw group");
    renderScene.ClearDirty();

    static_cast<void>(scene.Runtime().Update(0.016F));
    kb::scene::MeshRendererComponent* renderer = scene.Components().MeshRenderers().TryGet(mesh);
    Require(renderer != nullptr, "KBMAT-UE-0012: setup lost the mesh renderer");
    renderer->materialAssetId = 30U;
    scene.Components().MeshRenderers().MarkModified(mesh);
    Require(scene.Runtime().MeshRendererRenderProxyUpdateEntities().size() == 1U, "KBMAT-UE-0012: MeshRenderer::MarkModified did not enqueue a render proxy update");

    synchronizer.SyncMeshRendererUpdates(scene, renderScene);
    const MeshRenderProxy* proxy = renderScene.FindMeshByEntity(mesh.Id());
    Require(proxy != nullptr, "KBMAT-UE-0012: mesh renderer update sync lost the mesh proxy");
    Require(HasDirtyFlag(proxy->dirty, RenderProxyDirtyFlag::Material), "KBMAT-UE-0012: mesh renderer material change did not mark the render proxy Material dirty");
    Require(!HasDirtyFlag(proxy->dirty, RenderProxyDirtyFlag::Mesh), "KBMAT-UE-0012: pure material change should not dirty the mesh payload");
    Require(!HasDirtyFlag(proxy->dirty, RenderProxyDirtyFlag::Transform), "KBMAT-UE-0012: pure material change should not dirty the transform payload");

    const std::vector<SceneRenderDrawGroup>& materialGroups = renderScene.DrawGroups();
    Require(materialGroups.size() == 1U && materialGroups[0].materialAssetId == 30U, "KBMAT-UE-0012: material dirty sync did not rebuild draw groups with the new material");
}

void RunSyncTransformUpdatesUsesRuntimeCacheTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject first = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "First",
        .transform = LocalOnlyTransformAt(1.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(first.Entity(), kb::scene::MeshRendererComponent{ .meshAssetId = 10U });
    const kb::scene::SceneObject second = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Second",
        .transform = LocalOnlyTransformAt(2.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(second.Entity(), kb::scene::MeshRendererComponent{ .meshAssetId = 20U });

    RenderScene renderScene;
    EcsRenderSceneSynchronizer synchronizer;
    static_cast<void>(scene.Runtime().Update(0.016F));
    synchronizer.Sync(scene, renderScene);
    renderScene.ClearDirty();

    kb::scene::TransformComponent firstTransform = scene.Transforms().Get(first);
    firstTransform.localPosition = kb::scene::Vec3{ 11.0F, 0.0F, 0.0F };
    scene.Transforms().Set(first, firstTransform);
    static_cast<void>(scene.Runtime().Update(0.016F));
    synchronizer.SyncTransformUpdates(scene, renderScene);

    const MeshRenderProxy* firstProxy = renderScene.FindMeshByEntity(first.Entity().Id());
    const MeshRenderProxy* secondProxy = renderScene.FindMeshByEntity(second.Entity().Id());
    Require(firstProxy != nullptr && secondProxy != nullptr, "Transform update sync lost mesh proxies");
    Require(HasDirtyFlag(firstProxy->dirty, RenderProxyDirtyFlag::Transform), "Transform update sync did not dirty changed proxy");
    Require(secondProxy->dirty == RenderProxyDirtyFlag::None, "Transform update sync dirtied unchanged proxy");
    Require(NearlyEqual(firstProxy->desc.model[12], 11.0F), "Transform update sync did not publish changed transform");
    Require(NearlyEqual(secondProxy->desc.model[12], 2.0F), "Transform update sync changed an unrelated transform");
    Require(synchronizer.Stats().transformUpdateEntityCount == scene.Runtime().TransformRenderProxyUpdateEntities().size(), "Transform update sync stats did not mirror runtime cache size");
}

void RunSyncEntitiesRemovesDestroyedProxyTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneEntity mesh = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(mesh, kb::scene::MeshRendererComponent{ .meshAssetId = 42U });

    RenderScene renderScene;
    EcsRenderSceneSynchronizer synchronizer;
    synchronizer.Sync(scene, renderScene);
    Require(renderScene.FindMeshByEntity(mesh.Id()) != nullptr, "Incremental delete setup did not create mesh proxy");

    scene.Entities().Destroy(mesh);
    const std::array<std::uint64_t, 1U> dirty{ mesh.Id() };
    synchronizer.SyncEntities(scene, renderScene, std::span<const std::uint64_t>{ dirty.data(), dirty.size() });

    Require(renderScene.FindMeshByEntity(mesh.Id()) == nullptr, "Incremental sync kept a destroyed mesh proxy");
}

void RunVisibilityKeepsProxyButRemovesSnapshotInstanceTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneEntity mesh = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(mesh, kb::scene::MeshRendererComponent{});

    RenderScene renderScene;
    EcsRenderSceneSynchronizer{}.Sync(scene, renderScene);
    const RenderProxyId proxyId = renderScene.FindMeshByEntity(mesh.Id())->id;
    renderScene.ClearDirty();

    scene.Components().Visibility().Set(mesh, kb::scene::VisibilityComponent{ .visible = false });
    EcsRenderSceneSynchronizer{}.Sync(scene, renderScene);

    const MeshRenderProxy* meshProxy = renderScene.FindMeshByEntity(mesh.Id());
    Require(meshProxy != nullptr && meshProxy->id == proxyId, "RenderScene removed hidden mesh proxy instead of preserving mapping");
    Require(HasDirtyFlag(meshProxy->dirty, RenderProxyDirtyFlag::Visibility), "RenderScene did not track dirty visibility");
    SceneRenderSnapshot snapshot;
    renderScene.BuildSnapshotInto(1280, 720, snapshot);
    Require(snapshot.meshes.empty(), "RenderScene snapshot included a hidden mesh");
}

void RunDeletesRemovedComponentsAndEntitiesTest() {
    kb::scene::Scene scene;
    kb::scene::SceneLightingAccess::SetBasicLightingEnabled(scene, true);
    const kb::scene::SceneEntity mesh = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(mesh, kb::scene::MeshRendererComponent{});

    const kb::scene::SceneEntity light = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Light",
        .transform = TransformAt(0.0F, 5.0F, 0.0F),
    });
    scene.Components().Lights().Set(light, kb::scene::LightComponent{});

    RenderScene renderScene;
    EcsRenderSceneSynchronizer synchronizer;
    synchronizer.Sync(scene, renderScene);
    Require(renderScene.MeshProxyCount() == 1U, "RenderScene did not create mesh proxy before delete");
    Require(renderScene.LightProxyCount() == 1U, "RenderScene did not create light proxy before delete");

    scene.Components().MeshRenderers().Remove(mesh);
    scene.Entities().Destroy(light);
    synchronizer.Sync(scene, renderScene);

    Require(renderScene.FindMeshByEntity(mesh.Id()) == nullptr, "RenderScene kept a removed mesh component proxy");
    Require(renderScene.FindLightByEntity(light.Id()) == nullptr, "RenderScene kept a destroyed light proxy");
    SceneRenderSnapshot snapshot;
    renderScene.BuildSnapshotInto(1280, 720, snapshot);
    Require(snapshot.meshes.empty(), "RenderScene snapshot included a removed mesh component");
    Require(snapshot.lights.empty(), "RenderScene snapshot included a destroyed light");
}

void RunRenderResourceMapRequiresExplicitBindingsTest() {
    SceneRenderResourceMap resources;
    resources.Reserve(SceneRenderResourceMapReserveDesc{
        .meshBindings = 128U,
        .materialBindings = 128U,
        .textureBindings = 128U,
    });
    Require(!resources.ResolveMesh(42U).IsValid(), "SceneRenderResourceMap resolved an unbound mesh asset");
    Require(!resources.ResolveMaterial(7U).IsValid(), "SceneRenderResourceMap resolved an unbound material asset");
    Require(!resources.ResolveTexture(9U).IsValid(), "SceneRenderResourceMap resolved an unbound texture asset");
    resources.BindMesh(100U, RenderMeshHandle{ 0x0000'0001'0000'0000ULL });
    resources.BindMaterial(101U, RenderMaterialHandle{ 0x0000'0001'0000'0000ULL });
    resources.BindTexture(102U, RenderTextureHandle{ 0x0000'0001'0000'0000ULL });
    Require(!resources.ResolveMesh(100U).IsValid(), "SceneRenderResourceMap bound a zero-slot mesh handle");
    Require(!resources.ResolveMaterial(101U).IsValid(), "SceneRenderResourceMap bound a zero-slot material handle");
    Require(!resources.ResolveTexture(102U).IsValid(), "SceneRenderResourceMap bound a zero-slot texture handle");

    resources.BindMesh(42U, RenderMeshHandle{ 0x0000'0001'0000'0002ULL });
    resources.BindMaterial(7U, RenderMaterialHandle{ 0x0000'0001'0000'0003ULL });
    resources.BindTexture(9U, RenderTextureHandle{ 0x0000'0001'0000'0004ULL });
    Require(resources.ResolveMesh(42U).IsValid(), "SceneRenderResourceMap did not resolve an explicitly bound mesh");
    Require(resources.ResolveMaterial(7U).IsValid(), "SceneRenderResourceMap did not resolve an explicitly bound material");
    Require(resources.ResolveTexture(9U).IsValid(), "SceneRenderResourceMap did not resolve an explicitly bound texture");
    resources.BindTexture(9U, RenderTextureColorSpace::Srgb, RenderTextureHandle{ 0x0000'0001'0000'0005ULL });
    Require(resources.ResolveTexture(9U, RenderTextureColorSpace::Linear).value == 0x0000'0001'0000'0004ULL, "SceneRenderResourceMap should keep linear texture bindings separate");
    Require(resources.ResolveTexture(9U, RenderTextureColorSpace::Srgb).value == 0x0000'0001'0000'0005ULL, "SceneRenderResourceMap should resolve sRGB texture bindings separately for Base Color");
    SceneRenderResourceMapStats stats = resources.Stats();
    Require(stats.meshBindingCount == 1U, "SceneRenderResourceMap stats did not count mesh bindings");
    Require(stats.materialBindingCount == 1U, "SceneRenderResourceMap stats did not count material bindings");
    Require(stats.textureBindingCount == 2U, "SceneRenderResourceMap stats did not count color-space texture bindings");

    resources.UnbindMesh(42U);
    resources.UnbindMaterial(7U);
    resources.UnbindTexture(9U);
    Require(!resources.ResolveMesh(42U).IsValid(), "SceneRenderResourceMap resolved an unbound mesh after removal");
    Require(!resources.ResolveMaterial(7U).IsValid(), "SceneRenderResourceMap resolved an unbound material after removal");
    Require(!resources.ResolveTexture(9U).IsValid(), "SceneRenderResourceMap resolved an unbound texture after removal");
    Require(resources.ResolveTexture(9U, RenderTextureColorSpace::Srgb).IsValid(), "SceneRenderResourceMap should not remove sRGB binding when only linear binding is removed");
    resources.UnbindTexture(9U, RenderTextureColorSpace::Srgb);
    Require(!resources.ResolveTexture(9U, RenderTextureColorSpace::Srgb).IsValid(), "SceneRenderResourceMap did not remove the sRGB texture binding");

    resources.BindMesh(42U, RenderMeshHandle{ 0x0000'0001'0000'0002ULL });
    resources.BindMaterial(7U, RenderMaterialHandle{ 0x0000'0001'0000'0003ULL });
    resources.BindTexture(9U, RenderTextureHandle{ 0x0000'0001'0000'0004ULL });
    RenderResourceRegistry emptyRegistry;
    resources.PruneInvalidBindings(emptyRegistry);
    Require(resources.Stats().meshBindingCount == 0U, "SceneRenderResourceMap did not prune stale mesh bindings");
    Require(resources.Stats().materialBindingCount == 0U, "SceneRenderResourceMap did not prune stale material bindings");
    Require(resources.Stats().textureBindingCount == 0U, "SceneRenderResourceMap did not prune stale texture bindings");

    resources.Reserve(SceneRenderResourceMapReserveDesc{
        .meshBindings = 64U,
        .materialBindings = 32U,
        .textureBindings = 16U,
    });
    stats = resources.Stats();
    Require(stats.meshBindingCapacity >= 64U, "SceneRenderResourceMap did not expose reserved mesh binding capacity");
    Require(stats.materialBindingCapacity >= 32U, "SceneRenderResourceMap did not expose reserved material binding capacity");
    Require(stats.textureBindingCapacity >= 16U, "SceneRenderResourceMap did not expose reserved texture binding capacity");
}

void RunRenderSceneBuildsMeshMaterialDrawGroupsTest() {
    RenderScene renderScene;
    static_cast<void>(renderScene.UpsertMesh(MeshRenderProxyDesc{
        .entityId = 1U,
        .meshAssetId = 42U,
        .materialAssetId = 7U,
        .visible = true,
    }));
    static_cast<void>(renderScene.UpsertMesh(MeshRenderProxyDesc{
        .entityId = 2U,
        .meshAssetId = 42U,
        .materialAssetId = 7U,
        .visible = true,
    }));
    static_cast<void>(renderScene.UpsertMesh(MeshRenderProxyDesc{
        .entityId = 3U,
        .meshAssetId = 42U,
        .materialAssetId = 8U,
        .visible = true,
    }));
    static_cast<void>(renderScene.UpsertMesh(MeshRenderProxyDesc{
        .entityId = 4U,
        .meshAssetId = 42U,
        .materialAssetId = 7U,
        .visible = false,
    }));

    std::vector<SceneRenderDrawGroup> groups;
    renderScene.BuildDrawGroups(groups);
    Require(groups.size() == 2U, "RenderScene did not group visible meshes by mesh/material");
    const auto materialSeven = std::ranges::find_if(groups, [](const SceneRenderDrawGroup& group) {
        return group.meshAssetId == 42U && group.materialAssetId == 7U;
    });
    const auto materialEight = std::ranges::find_if(groups, [](const SceneRenderDrawGroup& group) {
        return group.meshAssetId == 42U && group.materialAssetId == 8U;
    });
    Require(materialSeven != groups.end() && materialSeven->instances.size() == 2U, "RenderScene did not coalesce matching mesh/material instances");
    Require(materialEight != groups.end() && materialEight->instances.size() == 1U, "RenderScene draw group included the wrong instance count");
}

void RunRenderSceneBuildsLargeMeshMaterialDrawGroupsTest() {
    RenderScene renderScene;
    constexpr std::uint32_t uniqueGroupCount = 512U;
    constexpr std::uint32_t repeatedInstanceCount = 64U;

    for (std::uint32_t index = 0U; index < uniqueGroupCount; ++index) {
        static_cast<void>(renderScene.UpsertMesh(MeshRenderProxyDesc{
            .entityId = 1U + index,
            .meshAssetId = 10'000U + index,
            .materialAssetId = 20'000U + index,
            .visible = true,
        }));
    }
    for (std::uint32_t index = 0U; index < repeatedInstanceCount; ++index) {
        static_cast<void>(renderScene.UpsertMesh(MeshRenderProxyDesc{
            .entityId = 100'000U + index,
            .meshAssetId = 77U,
            .materialAssetId = 88U,
            .visible = true,
        }));
    }

    std::vector<SceneRenderDrawGroup> groups;
    renderScene.BuildDrawGroups(groups);
    Require(groups.size() == uniqueGroupCount + 1U, "RenderScene did not preserve one group per unique mesh/material key");
    const auto repeatedGroup = std::ranges::find_if(groups, [](const SceneRenderDrawGroup& group) {
        return group.meshAssetId == 77U && group.materialAssetId == 88U;
    });
    Require(repeatedGroup != groups.end(), "RenderScene did not preserve the repeated mesh/material draw group");
    Require(repeatedGroup->instances.size() == repeatedInstanceCount, "RenderScene did not coalesce repeated mesh/material instances");
}

void RunRenderSceneCachesDrawGroupsUntilMeshStateChangesTest() {
    RenderScene renderScene;
    static_cast<void>(renderScene.UpsertMesh(MeshRenderProxyDesc{
        .entityId = 1U,
        .meshAssetId = 42U,
        .materialAssetId = 7U,
        .visible = true,
    }));

    const std::vector<SceneRenderDrawGroup>& initialGroups = renderScene.DrawGroups();
    Require(initialGroups.size() == 1U && initialGroups[0].instances.size() == 1U, "RenderScene draw group cache setup failed");
    const SceneRenderDrawGroup* initialData = initialGroups.data();

    static_cast<void>(renderScene.UpsertMesh(MeshRenderProxyDesc{
        .entityId = 1U,
        .meshAssetId = 42U,
        .materialAssetId = 7U,
        .visible = true,
    }));
    const std::vector<SceneRenderDrawGroup>& sameMeshGroups = renderScene.DrawGroups();
    Require(sameMeshGroups.data() == initialData, "RenderScene rebuilt draw groups after an unchanged mesh upsert");

    static_cast<void>(renderScene.UpsertCamera(CameraRenderProxyDesc{
        .entityId = 10U,
        .primary = true,
    }));
    static_cast<void>(renderScene.UpsertLight(LightRenderProxyDesc{
        .entityId = 20U,
        .visible = true,
    }));
    const std::vector<SceneRenderDrawGroup>& unchangedGroups = renderScene.DrawGroups();
    Require(unchangedGroups.data() == initialData, "RenderScene rebuilt draw groups for non-mesh proxy changes");
    Require(unchangedGroups.size() == 1U && unchangedGroups[0].instances.size() == 1U, "RenderScene draw group cache changed after non-mesh proxy changes");

    static_cast<void>(renderScene.UpsertMesh(MeshRenderProxyDesc{
        .entityId = 1U,
        .meshAssetId = 42U,
        .materialAssetId = 7U,
        .visible = false,
    }));
    const std::vector<SceneRenderDrawGroup>& hiddenGroups = renderScene.DrawGroups();
    Require(hiddenGroups.empty(), "RenderScene did not invalidate cached draw groups after mesh visibility changed");

    static_cast<void>(renderScene.UpsertMesh(MeshRenderProxyDesc{
        .entityId = 1U,
        .meshAssetId = 99U,
        .materialAssetId = 7U,
        .visible = true,
    }));
    const std::vector<SceneRenderDrawGroup>& changedGroups = renderScene.DrawGroups();
    Require(changedGroups.size() == 1U && changedGroups[0].meshAssetId == 99U, "RenderScene did not refresh cached draw groups after mesh asset changed");

    static_cast<void>(renderScene.UpsertMesh(MeshRenderProxyDesc{
        .entityId = 1U,
        .meshAssetId = 99U,
        .materialAssetId = 13U,
        .visible = true,
    }));
    const std::vector<SceneRenderDrawGroup>& materialGroups = renderScene.DrawGroups();
    Require(materialGroups.size() == 1U && materialGroups[0].materialAssetId == 13U, "RenderScene did not refresh cached draw groups after material asset changed");

    MeshRenderProxyDesc transformedMesh{
        .entityId = 1U,
        .meshAssetId = 99U,
        .materialAssetId = 13U,
        .visible = true,
    };
    transformedMesh.model[12] = 3.0F;
    static_cast<void>(renderScene.UpsertMesh(transformedMesh));
    const std::vector<SceneRenderDrawGroup>& transformedGroups = renderScene.DrawGroups();
    Require(transformedGroups.size() == 1U && transformedGroups[0].instances.size() == 1U, "RenderScene lost the draw group after mesh transform changed");
    Require(transformedGroups[0].instances[0].model[12] == 3.0F, "RenderScene did not refresh cached draw group instances after mesh transform changed");
}

void RunRenderSceneReserveAndStatsExposeProxyCapacityTest() {
    RenderScene renderScene;
    renderScene.Reserve(RenderSceneReserveDesc{
        .meshProxies = 256U,
        .cameraProxies = 8U,
        .lightProxies = 32U,
        .drawGroupKeys = 128U,
    });

    const RenderSceneStats emptyStats = renderScene.Stats();
    Require(emptyStats.meshProxyCapacity >= 256U, "RenderScene did not expose reserved mesh proxy capacity");
    Require(emptyStats.cameraProxyCapacity >= 8U, "RenderScene did not expose reserved camera proxy capacity");
    Require(emptyStats.lightProxyCapacity >= 32U, "RenderScene did not expose reserved light proxy capacity");
    Require(emptyStats.drawGroupLookupCapacity >= 128U, "RenderScene did not expose reserved draw group lookup capacity");

    static_cast<void>(renderScene.UpsertMesh(MeshRenderProxyDesc{
        .entityId = 1U,
        .meshAssetId = 42U,
        .materialAssetId = 7U,
        .visible = true,
    }));
    static_cast<void>(renderScene.UpsertCamera(CameraRenderProxyDesc{
        .entityId = 2U,
        .primary = true,
    }));
    static_cast<void>(renderScene.UpsertLight(LightRenderProxyDesc{
        .entityId = 3U,
    }));

    std::vector<SceneRenderDrawGroup> groups;
    renderScene.BuildDrawGroups(groups);
    const RenderSceneStats stats = renderScene.Stats();
    Require(stats.meshProxyCount == 1U, "RenderScene stats did not count mesh proxies");
    Require(stats.cameraProxyCount == 1U, "RenderScene stats did not count camera proxies");
    Require(stats.lightProxyCount == 1U, "RenderScene stats did not count light proxies");
    Require(stats.meshProxyCapacity >= emptyStats.meshProxyCapacity, "RenderScene released mesh proxy capacity after upsert");
    Require(stats.drawGroupLookupCapacity >= emptyStats.drawGroupLookupCapacity, "RenderScene released draw group lookup capacity after draw group build");
}

void RunEcsSyncPropagatesMaterialSlotOverridesTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneEntity mesh = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Mesh",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(mesh, kb::scene::MeshRendererComponent{
        .meshAssetId = 42U,
        .materialAssetId = 7U,
        .materialSlotAssetIds = { 0U, 201U },
        .materialSlotOverrideCount = 2U,
    });

    RenderScene renderScene;
    EcsRenderSceneSynchronizer{}.Sync(scene, renderScene);

    std::vector<SceneRenderDrawGroup> groups;
    renderScene.BuildDrawGroups(groups);
    Require(groups.size() == 1U && groups[0].instances.size() == 1U, "RenderScene did not build the expected material override draw group");
    Require(groups[0].instances[0].materialSlotOverrideCount == 2U, "RenderScene did not propagate material slot override count");
    Require(groups[0].instances[0].materialSlotAssetIds[1] == 201U, "RenderScene did not propagate material slot override asset id");
}

void RunEcsSyncScratchCapacityIsReusableAndVisibleTest() {
    kb::scene::Scene scene;
    kb::scene::SceneLightingAccess::SetBasicLightingEnabled(scene, true);
    const kb::scene::SceneEntity camera = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Camera",
        .transform = TransformAt(0.0F, 0.0F, -5.0F),
    });
    scene.Components().Cameras().Set(camera, kb::scene::CameraComponent{ .primary = true });

    const kb::scene::SceneEntity firstMesh = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Mesh A",
        .transform = TransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(firstMesh, kb::scene::MeshRendererComponent{ .meshAssetId = 42U });

    const kb::scene::SceneEntity secondMesh = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Mesh B",
        .transform = TransformAt(1.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(secondMesh, kb::scene::MeshRendererComponent{ .meshAssetId = 42U });

    const kb::scene::SceneEntity light = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Light",
        .transform = TransformAt(0.0F, 3.0F, 0.0F),
    });
    scene.Components().Lights().Set(light, kb::scene::LightComponent{});

    RenderScene renderScene;
    EcsRenderSceneSynchronizer synchronizer;
    synchronizer.Reserve(EcsRenderSceneSynchronizerReserveDesc{
        .meshProxies = 8U,
        .cameraProxies = 2U,
        .lightProxies = 4U,
        .transformCacheEntries = 16U,
        .transformResolvingEntries = 16U,
    });
    synchronizer.Sync(scene, renderScene);

    EcsRenderSceneSynchronizerStats stats = synchronizer.Stats();
    Require(stats.meshSeenCount == 2U, "ECS render sync did not count seen mesh proxies");
    Require(stats.cameraSeenCount == 1U, "ECS render sync did not count seen camera proxies");
    Require(stats.lightSeenCount == 1U, "ECS render sync did not count seen light proxies");
    Require(stats.meshSeenCapacity >= 8U, "ECS render sync did not expose reserved mesh scratch capacity");
    Require(stats.cameraSeenCapacity >= 2U, "ECS render sync did not expose reserved camera scratch capacity");
    Require(stats.lightSeenCapacity >= 4U, "ECS render sync did not expose reserved light scratch capacity");
    Require(stats.transformCacheCount == 4U, "ECS render sync did not use transform cache for resolved render proxies");
    Require(stats.transformResolvingCount == 0U, "ECS render sync left transform resolving scratch dirty");
    Require(stats.transformCacheCapacity >= 16U, "ECS render sync did not expose reserved transform cache capacity");
    Require(stats.transformResolvingCapacity >= 16U, "ECS render sync did not expose reserved transform resolving capacity");
    Require(renderScene.MeshProxyCount() == 2U, "ECS render sync did not create the expected mesh proxies");

    scene.Components().MeshRenderers().Remove(secondMesh);
    synchronizer.Sync(scene, renderScene);
    stats = synchronizer.Stats();
    Require(stats.meshSeenCount == 1U, "ECS render sync did not rebuild seen mesh scratch after component removal");
    Require(stats.transformCacheCount == 3U, "ECS render sync did not rebuild transform cache after component removal");
    Require(renderScene.MeshProxyCount() == 1U, "ECS render sync did not remove stale mesh proxy without transient id lists");
    Require(stats.meshSeenCapacity >= 8U, "ECS render sync released reusable mesh scratch capacity");
    Require(stats.transformCacheCapacity >= 16U, "ECS render sync released reusable transform cache capacity");
}

void RunRenderInstanceBufferPacksModelAndColorTest() {
    SceneRenderMeshInstance instance{};
    instance.model[0] = 1.0F;
    instance.model[5] = 2.0F;
    instance.model[10] = 3.0F;
    instance.model[15] = 1.0F;
    instance.color = { 0.25F, 0.5F, 0.75F, 1.0F };

    const RenderInstanceData packed = RenderInstanceBuffer::Pack(instance);
    Require(RenderInstanceBuffer::Stride() == sizeof(RenderInstanceData), "RenderInstanceBuffer stride does not match packed data size");
    Require(NearlyEqual(packed.model[5], 2.0F), "RenderInstanceBuffer did not preserve model data");
    Require(NearlyEqual(packed.color[2], 0.75F), "RenderInstanceBuffer did not preserve color data");
    Require(packed.color[3] > 0.0F, "RenderInstanceBuffer did not default shadow receiver flag on");
    instance.receivesShadow = false;
    const RenderInstanceData noShadowPacked = RenderInstanceBuffer::Pack(instance);
    Require(noShadowPacked.color[3] < 0.0F, "RenderInstanceBuffer did not pack disabled shadow receiver flag");
    const RenderInstanceData shadowCasterPacked = RenderInstanceBuffer::Pack(instance, nullptr, false);
    Require(shadowCasterPacked.color[3] > 0.0F, "RenderInstanceBuffer leaked receiver flag into shadow caster alpha");
    instance.receivesShadow = true;

    RenderMaterialResource material{};
    material.baseColor[0] = 0.5F;
    material.baseColor[1] = 0.25F;
    material.baseColor[2] = 0.125F;
    material.baseColor[3] = 1.0F;
    const RenderInstanceData materialPacked = RenderInstanceBuffer::Pack(instance, &material);
    Require(NearlyEqual(materialPacked.color[0], 0.125F), "RenderInstanceBuffer did not apply material base color R");
    Require(NearlyEqual(materialPacked.color[1], 0.125F), "RenderInstanceBuffer did not apply material base color G");
    Require(NearlyEqual(materialPacked.color[2], 0.09375F), "RenderInstanceBuffer did not apply material base color B");
}

void RunRenderInstanceBufferPacksPerInstanceScalarsTest() {
    SceneRenderMeshInstance instanceA{};
    instanceA.model[0] = 1.0F;
    instanceA.model[5] = 1.0F;
    instanceA.model[10] = 1.0F;
    instanceA.model[15] = 1.0F;
    instanceA.entityId = 1001U;
    instanceA.worldBounds.radius = 2.5F;
    instanceA.fadeAmount = 0.35F;
    instanceA.customData0 = 0.65F;

    const RenderInstanceData packedA = RenderInstanceBuffer::Pack(instanceA);
    // MAT-77/MAT-47: per-instance scalars ride the affine model's .w lanes; shaders rebuild
    // the matrix with (0,0,0,1) before transforming vertices.
    Require(NearlyEqual(packedA.model[7], 2.5F), "KBMAT-MAT77: ObjectRadius must pack the world bounds radius into i_data1.w");
    Require(packedA.model[3] >= 0.0F && packedA.model[3] < 1.0F, "KBMAT-MAT77: PerInstanceRandom must lie in [0,1)");
    Require(NearlyEqual(packedA.model[11], 0.35F), "KBMAT-MAT47: PerInstanceFadeAmount must pack into i_data2.w");
    Require(NearlyEqual(packedA.model[15], 0.65F), "KBMAT-MAT47: PerInstanceCustomData0 must pack into i_data3.w");
    // The matrix basis/translation columns are untouched (only the unused .w lanes are repurposed).
    Require(NearlyEqual(packedA.model[0], 1.0F) && NearlyEqual(packedA.model[5], 1.0F) && NearlyEqual(packedA.model[10], 1.0F),
        "KBMAT-MAT77: packing per-instance scalars must not disturb the affine basis");

    SceneRenderMeshInstance instanceB = instanceA;
    instanceB.entityId = 1002U;
    const RenderInstanceData packedB = RenderInstanceBuffer::Pack(instanceB);
    Require(!NearlyEqual(packedA.model[3], packedB.model[3]), "KBMAT-MAT77: PerInstanceRandom must differ for different entities in a batch");

    const RenderInstanceData packedAgain = RenderInstanceBuffer::Pack(instanceA);
    Require(NearlyEqual(packedA.model[3], packedAgain.model[3]), "KBMAT-MAT77: PerInstanceRandom must be deterministic for the same entity across frames");
}

void RunRenderSyncSystemAliasResolvesTest() {
    RenderSyncSystem syncSystem;
    kb::scene::Scene scene;
    RenderScene renderScene;
    syncSystem.Sync(scene, renderScene);
    Require(renderScene.MeshProxyCount() == 0U, "RenderSyncSystem alias mutated an empty scene unexpectedly");
}

void RunSceneRendererReportsMissingMeshBindingTest() {
    RenderScene renderScene;
    static_cast<void>(renderScene.UpsertMesh(MeshRenderProxyDesc{
        .entityId = 11U,
        .meshAssetId = 42U,
        .visible = true,
    }));

    SceneRenderer renderer;
    const SceneRenderSubmitStats stats = renderer.ValidateSceneResources(renderScene);
    Require(stats.visibleMeshCount == 1U, "SceneRenderer did not count the visible mesh proxy");
    Require(stats.visibleDrawGroupCount == 1U, "SceneRenderer did not count the visible draw group");
    Require(stats.submittedMeshCount == 0U, "SceneRenderer reported a submitted mesh without a mesh binding");
    Require(stats.missingMeshBindingCount == 1U, "SceneRenderer did not report the missing mesh binding");
    Require(stats.HasMissingResources(), "SceneRenderer missing-resource summary was false for a missing mesh binding");
}

void RunSceneRendererValidationScratchIsReusableAndVisibleTest() {
    RenderScene renderScene;
    static_cast<void>(renderScene.UpsertMesh(MeshRenderProxyDesc{
        .entityId = 11U,
        .meshAssetId = 42U,
        .visible = true,
    }));
    static_cast<void>(renderScene.UpsertMesh(MeshRenderProxyDesc{
        .entityId = 12U,
        .meshAssetId = 42U,
        .visible = true,
    }));

    SceneRenderer renderer;
    SceneRenderSubmitStats stats = renderer.ValidateSceneResources(renderScene);
    const std::uint32_t drawGroupCapacity = stats.meshDrawGroupScratchCapacity;
    const std::uint32_t instanceCapacity = stats.meshDrawGroupInstanceScratchCapacity;
    Require(drawGroupCapacity >= 1U, "SceneRenderer validation did not expose draw group scratch capacity");
    Require(instanceCapacity >= 2U, "SceneRenderer validation did not expose draw group instance scratch capacity");

    static_cast<void>(renderScene.RemoveMesh(12U));
    stats = renderer.ValidateSceneResources(renderScene);
    Require(stats.meshDrawGroupScratchCapacity >= drawGroupCapacity, "SceneRenderer validation released draw group scratch capacity");
    Require(stats.meshDrawGroupInstanceScratchCapacity >= instanceCapacity, "SceneRenderer validation released draw group instance scratch capacity");
}

void RunSceneRendererEmitsMissingResourceDiagnosticsTest() {
    RenderScene renderScene;
    static_cast<void>(renderScene.UpsertMesh(MeshRenderProxyDesc{
        .entityId = 11U,
        .meshAssetId = 42U,
        .visible = true,
    }));
    static_cast<void>(renderScene.UpsertMesh(MeshRenderProxyDesc{
        .entityId = 12U,
        .meshAssetId = 42U,
        .visible = true,
    }));

    SceneRenderer renderer;
    const SceneRenderDiagnostics diagnostics = renderer.ValidateSceneDiagnostics(renderScene);
    Require(diagnostics.HasErrors(), "SceneRenderer diagnostics did not classify missing mesh bindings as errors");
    Require(diagnostics.events.size() == 2U, "SceneRenderer diagnostics did not emit one event per skipped mesh instance");
    Require(diagnostics.events[0].kind == SceneRenderDiagnosticKind::MissingMeshBinding, "SceneRenderer diagnostics emitted the wrong missing resource kind");
    Require(diagnostics.events[0].severity == SceneRenderDiagnosticSeverity::Error, "SceneRenderer diagnostics emitted the wrong missing resource severity");
    Require(diagnostics.events[0].entityId == 11U, "SceneRenderer diagnostics did not include the first skipped entity id");
    Require(diagnostics.events[1].entityId == 12U, "SceneRenderer diagnostics did not include the second skipped entity id");
    Require(diagnostics.events[0].meshAssetId == 42U, "SceneRenderer diagnostics did not include the missing mesh asset id");
}

void RunSceneRenderSubmitStatsAggregateFrameSubmissionsTest() {
    SceneRenderSubmitStats first{};
    first.visibleMeshCount = 2U;
    first.visibleDrawGroupCount = 1U;
    first.culledInstanceCount = 3U;
    first.submittedMeshCount = 1U;
    first.submittedDrawCallCount = 1U;
    first.meshPipelineCommandCount = 1U;
    first.meshPipelineCommandCapacity = 4U;
    first.meshPipelineSortKeyCount = 1U;
    first.meshDrawGroupScratchCapacity = 4U;
    first.meshDrawGroupInstanceScratchCapacity = 10U;
    first.meshDrawGroupLookupCapacity = 6U;
    first.meshCommandLookupCapacity = 9U;
    first.meshCachedDrawCommandCount = 2U;
    first.meshCachedDrawCommandCapacity = 4U;
    first.meshDrawCommandCacheLookupCapacity = 5U;
    first.meshDrawCommandCacheHitCount = 6U;
    first.meshDrawCommandCacheMissCount = 1U;
    first.meshDrawCommandCacheBuildCount = 1U;
    first.meshDrawCommandCachePruneCount = 2U;
    first.meshPipelineScratchInstanceCapacity = 8U;
    first.gpuDrivenDrawCandidateCount = 2U;
    first.indirectDrawCandidateCount = 1U;
    first.meshletCullingCandidateCount = 3U;
    first.gpuDrivenInputInstanceCount = 4U;
    first.gpuDrivenBufferCapacity = 64U;
    first.gpuDrivenUploadBytes = 272U;
    first.lodSelectionCount = 4U;
    first.gpuDrivenFeatureState = SceneGpuDrivenFeatureState::CpuValidationOnly;
    first.gpuDrivenCounterSource = SceneGpuDrivenCounterSource::CpuCandidates;
    first.gpuDrivenFallbackReason = SceneGpuDrivenFallbackReason::RuntimeGpuDispatchUnavailable;
    first.gpuDrivenFallbackCount = 1U;
    first.instanceUploadBytes = 128U;
    first.sceneLightCount = 2U;
    first.submittedForwardLightCount = 1U;
    first.skippedForwardLightCount = 1U;
    first.invalidLightCount = 2U;
    first.forwardLightCapacity = 2U;
    first.submittedEnvironmentLightingCount = 1U;
    first.environmentLightingMode = static_cast<std::uint32_t>(SceneRenderEnvironmentMode::Constant) + 1U;
    first.environmentLightingSampleCount = 1U;
    first.shadowCasterCount = 5U;
    first.submittedShadowCasterCount = 4U;
    first.submittedShadowDrawCallCount = 1U;
    first.shadowMapSize = 512U;
    first.shadowFilterSampleCount = 1U;
    first.shadowLightEntityId = 11U;
    first.shadowMapAllocationBytes = 512ULL * 512ULL * 4ULL;
    first.missingMeshBindingCount = 1U;
    first.unsupportedMeshVertexFormatCount = 2U;
    first.missingTextureBindingCount = 1U;
    first.textureDimensionMismatchCount = 1U;

    SceneRenderSubmitStats second{};
    second.visibleMeshCount = 3U;
    second.visibleDrawGroupCount = 2U;
    second.culledInstanceCount = 4U;
    second.submittedMeshCount = 2U;
    second.submittedDrawGroupCount = 2U;
    second.submittedDrawCallCount = 2U;
    second.meshPipelineCommandCount = 2U;
    second.meshPipelineCommandCapacity = 8U;
    second.meshPipelineSortKeyCount = 2U;
    second.meshDrawGroupScratchCapacity = 7U;
    second.meshDrawGroupInstanceScratchCapacity = 12U;
    second.meshDrawGroupLookupCapacity = 11U;
    second.meshCommandLookupCapacity = 13U;
    second.meshCachedDrawCommandCount = 3U;
    second.meshCachedDrawCommandCapacity = 8U;
    second.meshDrawCommandCacheLookupCapacity = 7U;
    second.meshDrawCommandCacheHitCount = 4U;
    second.meshDrawCommandCacheMissCount = 2U;
    second.meshDrawCommandCacheBuildCount = 2U;
    second.meshDrawCommandCachePruneCount = 3U;
    second.meshPipelineScratchInstanceCapacity = 16U;
    second.gpuDrivenDrawCandidateCount = 5U;
    second.indirectDrawCandidateCount = 4U;
    second.meshletCullingCandidateCount = 7U;
    second.gpuDrivenInputInstanceCount = 6U;
    second.gpuDrivenBufferCapacity = 128U;
    second.gpuDrivenUploadBytes = 400U;
    second.lodSelectionCount = 8U;
    second.gpuDrivenFeatureState = SceneGpuDrivenFeatureState::ComputeCulling;
    second.gpuDrivenCounterSource = SceneGpuDrivenCounterSource::GpuDispatchCounters;
    second.gpuDrivenFallbackReason = SceneGpuDrivenFallbackReason::IndirectDrawUnsupported;
    second.gpuDrivenFallbackCount = 2U;
    second.gpuCullingDispatchCount = 2U;
    second.instanceUploadBytes = 256U;
    second.sceneLightCount = 3U;
    second.submittedForwardLightCount = 2U;
    second.skippedForwardLightCount = 1U;
    second.invalidLightCount = 3U;
    second.forwardLightCapacity = 3U;
    second.submittedEnvironmentLightingCount = 1U;
    second.environmentLightingMode = static_cast<std::uint32_t>(SceneRenderEnvironmentMode::Hemisphere) + 1U;
    second.environmentLightingSampleCount = 2U;
    second.shadowCasterCount = 7U;
    second.submittedShadowCasterCount = 6U;
    second.submittedShadowDrawCallCount = 2U;
    second.shadowMapSize = 1024U;
    second.shadowFilterSampleCount = 9U;
    second.shadowLightEntityId = 22U;
    second.shadowMapAllocationBytes = 1024ULL * 1024ULL * 4ULL;
    second.missingMaterialResourceCount = 1U;
    second.missingTextureResourceCount = 2U;
    second.textureDimensionMismatchCount = 2U;

    first += second;
    Require(first.visibleMeshCount == 5U, "SceneRenderSubmitStats did not aggregate visible meshes");
    Require(first.visibleDrawGroupCount == 3U, "SceneRenderSubmitStats did not aggregate draw groups");
    Require(first.culledInstanceCount == 7U, "SceneRenderSubmitStats did not aggregate culled instances");
    Require(first.submittedMeshCount == 3U, "SceneRenderSubmitStats did not aggregate submitted meshes");
    Require(first.submittedDrawGroupCount == 2U, "SceneRenderSubmitStats did not aggregate submitted draw groups");
    Require(first.submittedDrawCallCount == 3U, "SceneRenderSubmitStats did not aggregate draw calls");
    Require(first.meshPipelineCommandCount == 3U, "SceneRenderSubmitStats did not aggregate mesh pipeline command count");
    Require(first.meshPipelineCommandCapacity == 12U, "SceneRenderSubmitStats did not aggregate mesh pipeline command capacity");
    Require(first.meshPipelineSortKeyCount == 3U, "SceneRenderSubmitStats did not aggregate mesh pipeline sort key count");
    Require(first.meshDrawGroupScratchCapacity == 11U, "SceneRenderSubmitStats did not aggregate draw group scratch capacity");
    Require(first.meshDrawGroupInstanceScratchCapacity == 22U, "SceneRenderSubmitStats did not aggregate draw group instance scratch capacity");
    Require(first.meshDrawGroupLookupCapacity == 17U, "SceneRenderSubmitStats did not aggregate draw group lookup capacity");
    Require(first.meshCommandLookupCapacity == 22U, "SceneRenderSubmitStats did not aggregate command lookup capacity");
    Require(first.meshCachedDrawCommandCount == 5U, "SceneRenderSubmitStats did not aggregate cached draw command count");
    Require(first.meshCachedDrawCommandCapacity == 12U, "SceneRenderSubmitStats did not aggregate cached draw command capacity");
    Require(first.meshDrawCommandCacheLookupCapacity == 12U, "SceneRenderSubmitStats did not aggregate draw command cache lookup capacity");
    Require(first.meshDrawCommandCacheHitCount == 10U, "SceneRenderSubmitStats did not aggregate draw command cache hits");
    Require(first.meshDrawCommandCacheMissCount == 3U, "SceneRenderSubmitStats did not aggregate draw command cache misses");
    Require(first.meshDrawCommandCacheBuildCount == 3U, "SceneRenderSubmitStats did not aggregate draw command cache builds");
    Require(first.meshDrawCommandCachePruneCount == 5U, "SceneRenderSubmitStats did not aggregate draw command cache prunes");
    Require(first.meshPipelineScratchInstanceCapacity == 24U, "SceneRenderSubmitStats did not aggregate scratch instance capacity");
    Require(first.gpuDrivenDrawCandidateCount == 7U, "SceneRenderSubmitStats did not aggregate GPU-driven CPU candidate count");
    Require(first.indirectDrawCandidateCount == 5U, "SceneRenderSubmitStats did not aggregate indirect draw candidate count");
    Require(first.meshletCullingCandidateCount == 10U, "SceneRenderSubmitStats did not aggregate meshlet culling candidate count");
    Require(first.gpuDrivenInputInstanceCount == 10U, "SceneRenderSubmitStats did not aggregate GPU-driven input instance count");
    Require(first.gpuDrivenBufferCapacity == 128U, "SceneRenderSubmitStats did not preserve GPU-driven buffer capacity high watermark");
    Require(first.gpuDrivenUploadBytes == 672U, "SceneRenderSubmitStats did not aggregate GPU-driven upload bytes");
    Require(first.lodSelectionCount == 12U, "SceneRenderSubmitStats did not aggregate LOD selection count");
    Require(first.gpuDrivenFeatureState == SceneGpuDrivenFeatureState::ComputeCulling, "SceneRenderSubmitStats did not preserve the highest GPU-driven feature state");
    Require(first.gpuDrivenCounterSource == SceneGpuDrivenCounterSource::GpuDispatchCounters, "SceneRenderSubmitStats did not preserve the highest-fidelity GPU-driven counter source");
    Require(first.gpuDrivenFallbackReason == SceneGpuDrivenFallbackReason::IndirectDrawUnsupported, "SceneRenderSubmitStats did not preserve the highest-priority GPU-driven fallback reason");
    Require(first.gpuDrivenFallbackCount == 3U, "SceneRenderSubmitStats did not aggregate GPU-driven fallback count");
    Require(first.gpuCullingDispatchCount == 2U, "SceneRenderSubmitStats did not aggregate GPU culling dispatch count");
    Require(first.instanceUploadBytes == 384U, "SceneRenderSubmitStats did not aggregate instance upload bytes");
    Require(first.sceneLightCount == 5U, "SceneRenderSubmitStats did not aggregate scene light count");
    Require(first.submittedForwardLightCount == 3U, "SceneRenderSubmitStats did not aggregate submitted forward light count");
    Require(first.skippedForwardLightCount == 2U, "SceneRenderSubmitStats did not aggregate skipped forward light count");
    Require(first.invalidLightCount == 5U, "SceneRenderSubmitStats did not aggregate invalid light count");
    Require(first.forwardLightCapacity == 5U, "SceneRenderSubmitStats did not aggregate forward light capacity");
    Require(first.submittedEnvironmentLightingCount == 2U, "SceneRenderSubmitStats did not aggregate environment lighting submissions");
    Require(first.environmentLightingMode == static_cast<std::uint32_t>(SceneRenderEnvironmentMode::Hemisphere) + 1U, "SceneRenderSubmitStats did not preserve the latest environment mode");
    Require(first.environmentLightingSampleCount == 2U, "SceneRenderSubmitStats did not preserve the latest environment sample count");
    Require(first.shadowCasterCount == 12U, "SceneRenderSubmitStats did not aggregate shadow caster count");
    Require(first.submittedShadowCasterCount == 10U, "SceneRenderSubmitStats did not aggregate submitted shadow casters");
    Require(first.submittedShadowDrawCallCount == 3U, "SceneRenderSubmitStats did not aggregate shadow draw calls");
    Require(first.shadowMapSize == 1024U, "SceneRenderSubmitStats did not preserve the latest shadow map size");
    Require(first.shadowFilterSampleCount == 9U, "SceneRenderSubmitStats did not preserve the latest shadow filter sample count");
    Require(first.shadowLightEntityId == 22U, "SceneRenderSubmitStats did not preserve the latest shadow light entity id");
    Require(first.shadowMapAllocationBytes == 1024ULL * 1024ULL * 4ULL, "SceneRenderSubmitStats did not preserve the latest shadow map allocation bytes");
    Require(first.missingMeshBindingCount == 1U, "SceneRenderSubmitStats lost missing mesh bindings");
    Require(first.unsupportedMeshVertexFormatCount == 2U, "SceneRenderSubmitStats lost unsupported mesh vertex format count");
    Require(first.missingMaterialResourceCount == 1U, "SceneRenderSubmitStats lost missing material resources");
    Require(first.missingTextureBindingCount == 1U, "SceneRenderSubmitStats lost missing texture bindings");
    Require(first.missingTextureResourceCount == 2U, "SceneRenderSubmitStats lost missing texture resources");
    Require(first.textureDimensionMismatchCount == 3U, "SceneRenderSubmitStats lost texture dimension mismatches");
    Require(first.HasMissingResources(), "SceneRenderSubmitStats aggregate did not preserve missing-resource status");

    SceneRenderSubmitStats dimensionMismatchOnly{};
    Require(!dimensionMismatchOnly.HasMissingResources(), "Empty SceneRenderSubmitStats reported missing resources");
    dimensionMismatchOnly.textureDimensionMismatchCount = 1U;
    Require(dimensionMismatchOnly.HasMissingResources(),
        "Texture dimension mismatch must mark SceneRenderSubmitStats as having a missing-compatible resource");
}

void RunRendererRuntimeResourceStatsExposeCacheRetentionPolicyTest() {
    Renderer renderer;
    const Renderer::RuntimeSceneResourceStats stats = renderer.RuntimeResourceStats();
    Require(stats.cachedMeshCount == 0U, "Renderer runtime stats reported cached meshes before initialization");
    Require(stats.cachedMaterialCount == 0U, "Renderer runtime stats reported cached materials before initialization");
    Require(stats.cachedTextureCount == 0U, "Renderer runtime stats reported cached textures before initialization");
    Require(stats.cachedMeshCapacity >= stats.cachedMeshCount, "Renderer runtime stats reported invalid mesh cache capacity");
    Require(stats.cachedMaterialCapacity >= stats.cachedMaterialCount, "Renderer runtime stats reported invalid material cache capacity");
    Require(stats.cachedTextureCapacity >= stats.cachedTextureCount, "Renderer runtime stats reported invalid texture cache capacity");
    Require(stats.referencedMeshAssetCount == 0U, "Renderer runtime stats reported referenced meshes before submit");
    Require(stats.referencedMeshAssetCapacity >= stats.referencedMeshAssetCount, "Renderer runtime stats reported invalid referenced mesh capacity");
    Require(stats.scenePassSubmitStatsCapacity >= renderer.LastScenePassSubmitStats().size(), "Renderer runtime stats reported invalid pass stats capacity");
    Require(stats.registeredRuntimeAssetLoaderSceneCount == 0U, "Renderer runtime stats reported registered loader scenes before submit");
    Require(stats.runtimeAssetDiscoverySceneCount == 0U, "Renderer runtime stats reported discovery scenes before submit");
    Require(stats.runtimeAssetDiscoverySceneCapacity >= stats.runtimeAssetDiscoverySceneCount, "Renderer runtime stats reported invalid discovery scene capacity");
    Require(stats.renderSceneCount == 0U, "Renderer runtime stats reported render scenes before submit");
    Require(stats.renderSceneCapacity >= stats.renderSceneCount, "Renderer runtime stats reported invalid render scene capacity");
    Require(stats.renderSceneMeshProxyCount == 0U, "Renderer runtime stats reported render scene mesh proxies before submit");
    Require(stats.renderSceneCameraProxyCount == 0U, "Renderer runtime stats reported render scene camera proxies before submit");
    Require(stats.renderSceneLightProxyCount == 0U, "Renderer runtime stats reported render scene light proxies before submit");
    Require(stats.renderSceneMeshProxyCapacity == 0U, "Renderer runtime stats reported render scene mesh proxy capacity before submit");
    Require(stats.renderSceneCameraProxyCapacity == 0U, "Renderer runtime stats reported render scene camera proxy capacity before submit");
    Require(stats.renderSceneLightProxyCapacity == 0U, "Renderer runtime stats reported render scene light proxy capacity before submit");
    Require(stats.renderSceneDrawGroupLookupCapacity == 0U, "Renderer runtime stats reported render scene draw group capacity before submit");
    Require(stats.meshResourceSlotCapacity == 0U, "Renderer runtime stats reported mesh resource capacity before initialization");
    Require(stats.materialResourceSlotCapacity == 0U, "Renderer runtime stats reported material resource capacity before initialization");
    Require(stats.textureResourceSlotCapacity == 0U, "Renderer runtime stats reported texture resource capacity before initialization");
    Require(stats.meshBindingCapacity == 0U, "Renderer runtime stats reported mesh binding capacity before initialization");
    Require(stats.materialBindingCapacity == 0U, "Renderer runtime stats reported material binding capacity before initialization");
    Require(stats.textureBindingCapacity == 0U, "Renderer runtime stats reported texture binding capacity before initialization");
    Require(stats.syncMeshSeenCount == 0U, "Renderer runtime stats reported sync mesh count before initialization");
    Require(stats.syncCameraSeenCount == 0U, "Renderer runtime stats reported sync camera count before initialization");
    Require(stats.syncLightSeenCount == 0U, "Renderer runtime stats reported sync light count before initialization");
    Require(stats.syncMeshSeenCapacity == 0U, "Renderer runtime stats reported sync mesh capacity before initialization");
    Require(stats.syncCameraSeenCapacity == 0U, "Renderer runtime stats reported sync camera capacity before initialization");
    Require(stats.syncLightSeenCapacity == 0U, "Renderer runtime stats reported sync light capacity before initialization");
    Require(stats.syncTransformCacheCount == 0U, "Renderer runtime stats reported sync transform cache count before initialization");
    Require(stats.syncTransformResolvingCount == 0U, "Renderer runtime stats reported sync transform resolving count before initialization");
    Require(stats.syncTransformCacheCapacity == 0U, "Renderer runtime stats reported sync transform cache capacity before initialization");
    Require(stats.syncTransformResolvingCapacity == 0U, "Renderer runtime stats reported sync transform resolving capacity before initialization");
    Require(stats.retentionFrames == Renderer::kRuntimeAssetRetentionFrames, "Renderer runtime stats did not expose cache retention policy");
    Require(stats.assetDiscoveryIntervalFrames == Renderer::kRuntimeAssetDiscoveryIntervalFrames, "Renderer runtime stats did not expose asset discovery policy");
}

void RunRendererReservesRuntimeSceneResourceScratchTest() {
    Renderer renderer;
    renderer.ReserveRuntimeSceneResources(Renderer::RuntimeSceneResourceReserveDesc{
        .sceneCount = 4U,
        .cachedMeshes = 128U,
        .cachedMaterials = 256U,
        .cachedTextures = 64U,
        .frameReferencedMeshes = 128U,
        .frameReferencedMaterials = 256U,
        .frameReferencedTextures = 64U,
        .scenePassSubmitStats = 8U,
        .renderSceneMeshProxies = 512U,
        .renderSceneCameraProxies = 16U,
        .renderSceneLightProxies = 64U,
        .renderSceneDrawGroupKeys = 512U,
        .syncMeshProxies = 512U,
        .syncCameraProxies = 16U,
        .syncLightProxies = 64U,
        .syncTransformCacheEntries = 512U,
        .syncTransformResolvingEntries = 128U,
    });

    const Renderer::RuntimeSceneResourceStats stats = renderer.RuntimeResourceStats();
    Require(stats.cachedMeshCapacity >= 128U, "Renderer did not reserve runtime mesh cache capacity");
    Require(stats.cachedMaterialCapacity >= 256U, "Renderer did not reserve runtime material cache capacity");
    Require(stats.cachedTextureCapacity >= 64U, "Renderer did not reserve runtime texture cache capacity");
    Require(stats.referencedMeshAssetCapacity >= 128U, "Renderer did not reserve referenced mesh scratch capacity");
    Require(stats.referencedMaterialAssetCapacity >= 256U, "Renderer did not reserve referenced material scratch capacity");
    Require(stats.referencedTextureAssetCapacity >= 64U, "Renderer did not reserve referenced texture scratch capacity");
    Require(stats.scenePassSubmitStatsCapacity >= 8U, "Renderer did not reserve pass stats capacity");
    Require(stats.runtimeAssetDiscoverySceneCapacity >= 4U, "Renderer did not reserve discovery scene capacity");
    Require(stats.renderSceneCapacity >= 4U, "Renderer did not reserve render scene cache capacity");
}

void RunSceneRendererStoresDefaultDrawBudgetTest() {
    SceneRenderer renderer;
    renderer.SetDefaultDrawBudget(SceneRenderDrawBudget{
        .maxDrawCommands = 17U,
        .maxVisibleInstances = 33U,
    });

    const SceneRenderDrawBudget budget = renderer.DefaultDrawBudget();
    Require(budget.maxDrawCommands == 17U, "SceneRenderer did not store the configured draw command budget");
    Require(budget.maxVisibleInstances == 33U, "SceneRenderer did not store the configured visible instance budget");
}

void RunSceneRendererStoresDefaultLightingConfigTest() {
    SceneRenderer renderer;
    renderer.SetDefaultLightingConfig(SceneRenderLightingConfig{
        .maxForwardLights = 2U,
        .ambientColor = { 0.1F, 0.2F, 0.3F },
        .ambientIntensity = 0.75F,
        .environmentMode = SceneRenderEnvironmentMode::Hemisphere,
        .environmentZenithColor = { 0.4F, 0.5F, 0.6F },
        .environmentGroundColor = { 0.05F, 0.06F, 0.07F },
        .environmentDiffuseIntensity = 1.5F,
        .environmentSpecularIntensity = 0.35F,
        .shadowFilter = SceneRenderShadowFilter::Hard,
    });

    const SceneRenderLightingConfig config = renderer.DefaultLightingConfig();
    Require(config.maxForwardLights == 2U, "SceneRenderer did not store the configured forward light budget");
    Require(NearlyEqual(config.ambientColor[1], 0.2F), "SceneRenderer did not store configured ambient color");
    Require(NearlyEqual(config.ambientIntensity, 0.75F), "SceneRenderer did not store configured ambient intensity");
    Require(config.environmentMode == SceneRenderEnvironmentMode::Hemisphere, "SceneRenderer did not store configured environment mode");
    Require(NearlyEqual(config.environmentZenithColor[2], 0.6F), "SceneRenderer did not store configured environment zenith color");
    Require(NearlyEqual(config.environmentGroundColor[1], 0.06F), "SceneRenderer did not store configured environment ground color");
    Require(NearlyEqual(config.environmentDiffuseIntensity, 1.5F), "SceneRenderer did not store configured environment diffuse intensity");
    Require(NearlyEqual(config.environmentSpecularIntensity, 0.35F), "SceneRenderer did not store configured environment specular intensity");
    Require(config.shadowFilter == SceneRenderShadowFilter::Hard, "SceneRenderer did not store configured shadow filter");
}

void RunRendererStoresDefaultSceneDrawBudgetTest() {
    Renderer renderer;
    renderer.SetDefaultSceneDrawBudget(SceneRenderDrawBudget{
        .maxDrawCommands = 23U,
        .maxVisibleInstances = 41U,
    });

    const SceneRenderDrawBudget budget = renderer.DefaultSceneDrawBudget();
    Require(budget.maxDrawCommands == 23U, "Renderer did not store the configured scene draw command budget");
    Require(budget.maxVisibleInstances == 41U, "Renderer did not store the configured scene visible instance budget");
}

void RunRendererStoresDefaultSceneLightingConfigTest() {
    Renderer renderer;
    renderer.SetDefaultSceneLightingConfig(SceneRenderLightingConfig{
        .maxForwardLights = 3U,
        .ambientColor = { 0.2F, 0.3F, 0.4F },
        .ambientIntensity = 1.25F,
        .environmentMode = SceneRenderEnvironmentMode::Hemisphere,
        .environmentZenithColor = { 0.45F, 0.5F, 0.55F },
        .environmentGroundColor = { 0.08F, 0.09F, 0.1F },
        .environmentDiffuseIntensity = 1.25F,
        .environmentSpecularIntensity = 0.5F,
        .shadowFilter = SceneRenderShadowFilter::Hard,
    });

    const SceneRenderLightingConfig config = renderer.DefaultSceneLightingConfig();
    Require(config.maxForwardLights == 3U, "Renderer did not store the configured scene forward light budget");
    Require(NearlyEqual(config.ambientColor[2], 0.4F), "Renderer did not store configured scene ambient color");
    Require(NearlyEqual(config.ambientIntensity, 1.25F), "Renderer did not store configured scene ambient intensity");
    Require(config.environmentMode == SceneRenderEnvironmentMode::Hemisphere, "Renderer did not store configured scene environment mode");
    Require(NearlyEqual(config.environmentZenithColor[2], 0.55F), "Renderer did not store configured scene environment zenith color");
    Require(NearlyEqual(config.environmentGroundColor[0], 0.08F), "Renderer did not store configured scene environment ground color");
    Require(NearlyEqual(config.environmentDiffuseIntensity, 1.25F), "Renderer did not store configured scene environment diffuse intensity");
    Require(NearlyEqual(config.environmentSpecularIntensity, 0.5F), "Renderer did not store configured scene environment specular intensity");
    Require(config.shadowFilter == SceneRenderShadowFilter::Hard, "Renderer did not store configured scene shadow filter");
    Require(renderer.RuntimeResourceStats().defaultForwardLightCapacity == 3U, "Renderer runtime stats did not expose the configured scene forward light budget");
    Require(renderer.RuntimeResourceStats().defaultEnvironmentLightingMode == static_cast<std::uint32_t>(SceneRenderEnvironmentMode::Hemisphere) + 1U, "Renderer runtime stats did not expose the configured scene environment mode");
    Require(renderer.RuntimeResourceStats().defaultEnvironmentLightingSampleCount == 2U, "Renderer runtime stats did not expose the configured scene environment sample count");
}

void RunSceneLightingPackerAddsEditorPreviewKeyLightTest() {
    RenderScene scene;
    SceneRenderer renderer;
    renderer.SetDefaultLightingConfig(
        SceneRenderLightingConfig{
            .editorPreviewKeyLightEnabled = true,
            .editorPreviewKeyLightDirection = { 0.35F, -0.62F, 0.70F },
            .editorPreviewKeyLightColor = { 1.0F, 0.96F, 0.90F },
            .editorPreviewKeyLightIntensity = 1.85F,
        });
    const SceneRenderSubmitStats stats = renderer.ValidateSceneResources(scene);

    Require(stats.sceneLightCount == 0U, "Editor preview key light should not be counted as a scene light");
    Require(stats.submittedForwardLightCount == 1U, "SceneLightingPacker did not submit the editor preview key light");
    Require(stats.skippedForwardLightCount == 0U, "Editor preview key light should not produce skipped scene light stats");
}

void RunRenderSceneExpandsGeometrySwarmIntoExistingDrawGroupTest() {
    RenderScene renderScene;
    static_cast<void>(renderScene.UpsertGeometrySwarm(GeometrySwarmRenderProxyDesc{
        .entityId = 77U, .meshAssetId = 42U, .materialAssetId = 9U,
        .model = { 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F },
        .instanceCount = 4U, .columns = 2U, .rows = 2U, .layers = 1U,
        .spacing = { 2.0F, 4.0F, 1.0F }, .instanceScale = 1.5F,
    }));
    const std::vector<SceneRenderDrawGroup>& groups = renderScene.DrawGroups();
    Require(groups.size() == 1U && groups[0].instances.size() == 4U, "Geometry Swarm did not expand into the canonical mesh draw group");
    Require(groups[0].instances[0].entityId != groups[0].instances[1].entityId, "Geometry Swarm did not generate deterministic unique per-instance identifiers");
    Require(NearlyEqual(groups[0].instances[0].model[0], 1.5F), "Geometry Swarm did not apply per-instance scale to the generated model");
    SceneRenderSnapshot snapshot;
    renderScene.BuildSnapshotInto(1280U, 720U, snapshot);
    Require(snapshot.meshes.size() == 4U, "Geometry Swarm did not reach the snapshot consumer used by the runtime renderer");
    const float originalFirstInstanceX = groups[0].instances[0].model[12];
    const std::array<float, 16> movedModel{ 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 6.0F, 0.0F, 0.0F, 1.0F };
    Require(renderScene.UpdateGeometrySwarmTransform(77U, movedModel), "Geometry Swarm transform update did not update the renderer-derived proxy");
    Require(NearlyEqual(renderScene.DrawGroups()[0].instances[0].model[12], originalFirstInstanceX + 6.0F), "Geometry Swarm transform update did not rebuild derived instances");
    Require(renderScene.RemoveGeometrySwarm(77U), "Geometry Swarm proxy removal failed");
    Require(renderScene.DrawGroups().empty(), "Geometry Swarm proxy removal left derived render instances behind");
}

void RunRenderSceneAppliesSurfaceCastByRegionLayerAndOrderTest() {
    RenderScene renderScene;
    const std::array<float, 16> identity{ 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F };
    std::array<float, 16> inside = identity; inside[12] = 0.25F;
    std::array<float, 16> outside = identity; outside[12] = 3.0F;
    static_cast<void>(renderScene.UpsertMesh(MeshRenderProxyDesc{ .entityId = 1U, .meshAssetId = 7U, .materialAssetId = 10U, .model = inside, .layer = 2U }));
    static_cast<void>(renderScene.UpsertMesh(MeshRenderProxyDesc{ .entityId = 2U, .meshAssetId = 7U, .materialAssetId = 10U, .model = outside, .layer = 2U }));
    static_cast<void>(renderScene.UpsertMesh(MeshRenderProxyDesc{ .entityId = 3U, .meshAssetId = 7U, .materialAssetId = 10U, .model = inside, .layer = 4U }));
    static_cast<void>(renderScene.UpsertSurfaceCast(SurfaceCastRenderProxyDesc{ .entityId = 11U, .materialAssetId = 20U, .model = identity, .size = { 2.0F, 2.0F, 2.0F }, .receiverLayerMask = 2U, .order = 1, .region = RenderSurfaceCastRegion::Box }));
    static_cast<void>(renderScene.UpsertSurfaceCast(SurfaceCastRenderProxyDesc{ .entityId = 12U, .materialAssetId = 30U, .model = identity, .size = { 2.0F, 2.0F, 2.0F }, .receiverLayerMask = 2U, .order = 2, .region = RenderSurfaceCastRegion::Box }));
    SceneRenderSnapshot snapshot;
    renderScene.BuildSnapshotInto(1280U, 720U, snapshot);
    const auto material = [&snapshot](std::uint64_t entityId) { for (const SceneRenderMeshInstance& instance : snapshot.meshes) if (instance.entityId == entityId) return instance.materialAssetId; return std::uint64_t{ 0U }; };
    Require(material(1U) == 30U, "Surface Cast did not apply the highest-order matching material");
    Require(material(2U) == 10U, "Surface Cast ignored its region boundary");
    Require(material(3U) == 10U, "Surface Cast ignored its receiver-layer filter");
    Require(renderScene.RemoveSurfaceCast(12U), "Surface Cast proxy removal failed");
    renderScene.BuildSnapshotInto(1280U, 720U, snapshot);
    Require(material(1U) == 20U, "Surface Cast removal did not restore the preceding ordered cast");
}

void RunRenderSceneAppliesFacingPanelModesWithoutMutatingEcsTransformTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneEntity camera = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Camera", .transform = TransformAt(0.0F, 0.0F, -5.0F) });
    scene.Components().Cameras().Set(camera, kb::scene::CameraComponent{ .primary = true });
    const kb::scene::SceneEntity panelEntity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Panel", .transform = TransformAt(0.0F, 0.0F, 0.0F) });
    scene.Components().MeshRenderers().Set(panelEntity, kb::scene::MeshRendererComponent{ .meshAssetId = 7U, .materialAssetId = 9U });
    kb::scene::FacingPanelComponent panel{ .mode = kb::scene::FacingPanelMode::View, .enabled = true };
    scene.Components().FacingPanels().Set(panelEntity, panel);
    EcsRenderSceneSynchronizer synchronizer;
    RenderScene renderScene;
    synchronizer.Sync(scene, renderScene);
    const MeshRenderProxy* proxy = renderScene.FindMeshByEntity(panelEntity.Id());
    Require(proxy != nullptr && proxy->desc.model[10] < -0.99F, "Facing Panel view mode did not orient the flat mesh toward the primary camera");
    panel.mode = kb::scene::FacingPanelMode::Point;
    panel.targetPoint = kb::scene::Vec3{ 5.0F, 0.0F, 0.0F };
    scene.Components().FacingPanels().Set(panelEntity, panel);
    synchronizer.Sync(scene, renderScene);
    proxy = renderScene.FindMeshByEntity(panelEntity.Id());
    Require(proxy != nullptr && proxy->desc.model[8] > 0.99F, "Facing Panel point mode did not orient the flat mesh toward its point");
    panel.mode = kb::scene::FacingPanelMode::Axis;
    panel.axis = kb::scene::Vec3{ -1.0F, 0.0F, 0.0F };
    scene.Components().FacingPanels().Set(panelEntity, panel);
    synchronizer.Sync(scene, renderScene);
    proxy = renderScene.FindMeshByEntity(panelEntity.Id());
    Require(proxy != nullptr && proxy->desc.model[8] < -0.99F, "Facing Panel axis mode did not use the authored axis");
    panel.mode = kb::scene::FacingPanelMode::Fixed;
    scene.Components().FacingPanels().Set(panelEntity, panel);
    synchronizer.Sync(scene, renderScene);
    proxy = renderScene.FindMeshByEntity(panelEntity.Id());
    Require(proxy != nullptr && proxy->desc.model[10] > 0.99F, "Facing Panel fixed mode did not preserve the authored orientation");
    const kb::scene::TransformComponent* canonicalTransform = scene.Transforms().TryGet(panelEntity);
    Require(canonicalTransform != nullptr && canonicalTransform->worldRotation.z == 0.0F && canonicalTransform->worldRotation.w == 1.0F, "Facing Panel renderer consumer mutated canonical ECS TransformComponent");
}

void RunRenderSceneSyncsAllSurfaceEmitterKindsTest() {
    kb::scene::Scene scene;
    kb::scene::SceneLightingAccess::SetBasicLightingEnabled(scene, true);
    constexpr std::array<kb::scene::LightKind, 3U> kinds{
        kb::scene::LightKind::AreaRect,
        kb::scene::LightKind::AreaDisk,
        kb::scene::LightKind::Tube,
    };
    constexpr std::array<RenderLightKind, 3U> expectedKinds{
        RenderLightKind::AreaRect,
        RenderLightKind::AreaDisk,
        RenderLightKind::Tube,
    };
    std::array<kb::scene::SceneEntity, 3U> entities{};
    for (std::size_t index = 0U; index < kinds.size(); ++index) {
        const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
            .name = "Surface Emitter",
            .transform = TransformAt(static_cast<float>(index), 1.0F, 2.0F),
        });
        scene.Components().Lights().Set(entity, kb::scene::LightComponent{
            .kind = kinds[index],
            .intensity = 2.0F,
            .range = 12.0F,
            .areaWidth = 2.0F + static_cast<float>(index),
            .areaHeight = 1.0F + static_cast<float>(index),
        });
        entities[index] = entity;
    }

    RenderScene renderScene;
    EcsRenderSceneSynchronizer{}.Sync(scene, renderScene);
    Require(renderScene.LightProxies().size() == kinds.size(), "ECS synchronization did not create every surface emitter proxy");
    for (std::size_t index = 0U; index < entities.size(); ++index) {
        const LightRenderProxy* proxy = renderScene.FindLightByEntity(entities[index].Id());
        Require(proxy != nullptr, "ECS synchronization did not preserve a surface emitter entity association");
        if (proxy == nullptr) {
            continue;
        }
        Require(proxy->desc.kind == expectedKinds[index], "ECS synchronization changed a surface emitter kind");
        Require(NearlyEqual(proxy->desc.areaWidth, 2.0F + static_cast<float>(index)) &&
                NearlyEqual(proxy->desc.areaHeight, 1.0F + static_cast<float>(index)),
            "ECS synchronization changed surface emitter dimensions");
    }
}

void RunSceneLightingPackerPreservesSurfaceEmitterGeometryTest() {
    RenderScene scene;
    static_cast<void>(scene.UpsertLight(LightRenderProxyDesc{
        .entityId = 1U,
        .kind = RenderLightKind::AreaRect,
        .intensity = 1.0F,
        .range = 10.0F,
        .areaWidth = 6.0F,
        .areaHeight = 2.0F,
        .visible = true,
    }));
    static_cast<void>(scene.UpsertLight(LightRenderProxyDesc{
        .entityId = 2U,
        .kind = RenderLightKind::AreaDisk,
        .intensity = 1.0F,
        .range = 10.0F,
        .areaWidth = 4.0F,
        .areaHeight = 4.0F,
        .visible = true,
    }));
    static_cast<void>(scene.UpsertLight(LightRenderProxyDesc{
        .entityId = 3U,
        .kind = RenderLightKind::Tube,
        .intensity = 1.0F,
        .range = 10.0F,
        .areaWidth = 8.0F,
        .areaHeight = 1.5F,
        .visible = true,
    }));

    SceneRenderSubmitStats stats{};
    const PackedSceneLighting packed = SceneLightingPacker::Build(scene, stats, SceneRenderLightingConfig{ .maxForwardLights = 3U }, nullptr);
    Require(stats.submittedForwardLightCount == 3U, "Surface emitters were not submitted to production lighting");
    Require(NearlyEqual(packed.dirKind[3U], 3.0F) && NearlyEqual(packed.dirKind[7U], 4.0F) && NearlyEqual(packed.dirKind[11U], 5.0F),
        "Surface emitter kinds were not preserved in GPU lighting data");
    Require(NearlyEqual(packed.spot[2U], 6.0F) && NearlyEqual(packed.spot[3U], 2.0F) &&
            NearlyEqual(packed.spot[6U], 4.0F) && NearlyEqual(packed.spot[7U], 4.0F) &&
            NearlyEqual(packed.spot[10U], 8.0F) && NearlyEqual(packed.spot[11U], 1.5F),
        "Surface emitter dimensions were not preserved in GPU lighting data");
    Require(NearlyEqual(packed.areaRight[0U], 1.0F) && NearlyEqual(packed.areaRight[5U], 0.0F) && NearlyEqual(packed.areaRight[10U], 0.0F),
        "Surface emitter orientation was not packed for GPU evaluation");
}

// LIB-141: proves SceneForwardLightSelector::Select filters lights by the light-side
// layer bitmask against the camera's cullingMask - the light-side mirror of
// MeshPipelinePassPolicy's mesh-vs-camera cullingMask filtering (LIB-136). A masked-out
// light must not be selected (it never reaches the camera's forward lighting), but must
// also not count toward validLightCount (it was never a candidate for THIS camera), and a
// default (all-bits) cullingMask must keep selecting every layer, matching pre-LIB-141
// behavior.
void RunSceneForwardLightSelectorAppliesLayerMaskTest() {
    RenderScene scene;
    const RenderProxyId layerOneLight = scene.UpsertLight(LightRenderProxyDesc{
        .entityId = 1U,
        .kind = RenderLightKind::Point,
        .position = { 0.0F, 0.0F, 0.0F },
        .color = { 1.0F, 1.0F, 1.0F },
        .intensity = 2.0F,
        .range = 10.0F,
        .visible = true,
        .layer = 1U,
    });
    const RenderProxyId layerTwoLight = scene.UpsertLight(LightRenderProxyDesc{
        .entityId = 2U,
        .kind = RenderLightKind::Point,
        .position = { 0.0F, 0.0F, 0.0F },
        .color = { 1.0F, 1.0F, 1.0F },
        .intensity = 2.0F,
        .range = 10.0F,
        .visible = true,
        .layer = 2U,
    });
    Require(layerOneLight.IsValid() && layerTwoLight.IsValid(), "Light layer mask test setup failed to create both lights");

    SceneRenderSubmitStats restrictedStats{};
    const SceneForwardLightSelection restricted = SceneForwardLightSelector::Select(
        scene.LightProxies(), 8U, { 0.0F, 0.0F, 0.0F, 0.0F }, restrictedStats, SceneRenderLightingConfig{}, 0x1U);
    Require(restricted.selectedCount == 1U, "A camera cullingMask of 0x1 must select exactly the layer-1 light, not both");
    Require(restricted.selected[0].light != nullptr && restricted.selected[0].light->kind == RenderLightKind::Point &&
            restricted.selected[0].entityId == 1U,
        "A camera cullingMask of 0x1 must select the layer-1 light specifically, not the layer-2 one");
    Require(restricted.validLightCount == 1U, "A layer-masked-out light must not count toward validLightCount for this camera");

    SceneRenderSubmitStats unrestrictedStats{};
    const SceneForwardLightSelection unrestricted = SceneForwardLightSelector::Select(
        scene.LightProxies(), 8U, { 0.0F, 0.0F, 0.0F, 0.0F }, unrestrictedStats, SceneRenderLightingConfig{}, 0xFFFFFFFFU);
    Require(unrestricted.selectedCount == 2U, "A default (all-bits) camera cullingMask must keep selecting every light layer, matching pre-LIB-141 behavior");
}

// LIB-141: proves SceneLightColor::Resolve leaves `color` untouched when
// useColorTemperature is false (the default - existing content sees zero behavior change),
// and actually tints `color` by a blackbody-radiator RGB when enabled - a very warm (1000K,
// heavily red-shifted) temperature must reduce the blue channel well below the red channel,
// proving the Kelvin value genuinely drives the result rather than being accepted-but-ignored.
void RunSceneLightColorResolvesTemperatureTest() {
    kb::scene::LightComponent plain;
    plain.color = { 0.5F, 0.6F, 0.7F };
    plain.useColorTemperature = false;
    plain.colorTemperatureKelvin = 1000.0F;
    const std::array<float, 3> plainResolved = SceneLightColor::Resolve(plain);
    Require(NearlyEqual(plainResolved[0], 0.5F) && NearlyEqual(plainResolved[1], 0.6F) && NearlyEqual(plainResolved[2], 0.7F),
        "SceneLightColor::Resolve must leave color untouched when useColorTemperature is false");

    kb::scene::LightComponent warm;
    warm.color = { 1.0F, 1.0F, 1.0F };
    warm.useColorTemperature = true;
    warm.colorTemperatureKelvin = 1000.0F;
    const std::array<float, 3> warmResolved = SceneLightColor::Resolve(warm);
    Require(warmResolved[2] < warmResolved[0] * 0.5F,
        "SceneLightColor::Resolve must tint a very warm (1000K) light toward red - blue channel must be well below red");

    kb::scene::LightComponent daylight;
    daylight.color = { 1.0F, 1.0F, 1.0F };
    daylight.useColorTemperature = true;
    daylight.colorTemperatureKelvin = 6500.0F;
    const std::array<float, 3> daylightResolved = SceneLightColor::Resolve(daylight);
    Require(daylightResolved[0] > 0.9F && daylightResolved[1] > 0.9F && daylightResolved[2] > 0.9F,
        "SceneLightColor::Resolve must render the default 6500K daylight temperature as close to neutral white");
}

void RunRendererStoresDefaultPostProcessSettingsTest() {
    Renderer renderer;
    Require(renderer.ConfigurePostProcessChain(PostProcessChain::DefaultSceneChainDesc()), "Renderer rejected default post-process chain before setting bloom options");
    renderer.SetDefaultPostProcessSettings(ScenePostProcessSettings{
        .bloomEnabled = false,
        .bloomStrength = -1.0F,
        .bloomThreshold = -2.0F,
        .bloomSoftKnee = 2.0F,
        .bloomRadiusPixels = -3.0F,
        .temporalAntiAliasingEnabled = false,
        .temporalJitterEnabled = false,
        .fxaaEnabled = true,
    });

    const ScenePostProcessSettings settings = renderer.DefaultPostProcessSettings();
    Require(!settings.bloomEnabled, "Renderer did not store configured post-process bloom toggle");
    Require(NearlyEqual(settings.bloomStrength, 0.0F), "Renderer did not clamp post-process bloom strength");
    Require(NearlyEqual(settings.bloomThreshold, 0.0F), "Renderer did not clamp post-process bloom threshold");
    Require(NearlyEqual(settings.bloomSoftKnee, 1.0F), "Renderer did not clamp post-process bloom soft knee");
    Require(NearlyEqual(settings.bloomRadiusPixels, 0.0F), "Renderer did not clamp post-process bloom radius");
    Require(settings.tonemapEnabled, "Renderer should keep tonemap enabled by default");
    Require(settings.outputTransform.autoExposure.enabled, "Renderer default post-process settings should keep auto exposure enabled");
    Require(settings.fxaaEnabled, "Renderer did not store configured FXAA toggle");
    Require(!settings.temporalAntiAliasingEnabled, "Renderer did not store configured TAA toggle");

    const auto antiAliasingPass = std::ranges::find_if(renderer.PostProcessPasses(), [](const PostProcessPass& pass) {
        return pass.kind == PostProcessPassKind::AntiAliasing;
    });
    Require(antiAliasingPass != renderer.PostProcessPasses().end(), "Renderer post-process chain is missing anti-aliasing after settings update");
    Require(antiAliasingPass->enabled, "Renderer did not enable anti-aliasing pass for FXAA");
    Require(antiAliasingPass->postProcessSettings.fxaaEnabled, "Renderer did not synchronize FXAA to the anti-aliasing pass");
    Require(!antiAliasingPass->postProcessSettings.temporalAntiAliasingEnabled, "Renderer did not synchronize disabled TAA to the anti-aliasing pass");

    const auto bloomPass = std::ranges::find_if(renderer.PostProcessPasses(), [](const PostProcessPass& pass) {
        return pass.kind == PostProcessPassKind::Bloom;
    });
    Require(bloomPass != renderer.PostProcessPasses().end(), "Renderer post-process chain is missing bloom after settings update");
    Require(!bloomPass->enabled, "Renderer did not synchronize bloom pass enabled state with default settings");
    Require(NearlyEqual(bloomPass->postProcessSettings.bloomStrength, 0.0F), "Renderer did not synchronize clamped bloom strength to the bloom pass");
    Require(NearlyEqual(bloomPass->postProcessSettings.bloomSoftKnee, 1.0F), "Renderer did not synchronize clamped bloom soft knee to the bloom pass");

    const auto tonemapPass = std::ranges::find_if(renderer.PostProcessPasses(), [](const PostProcessPass& pass) {
        return pass.kind == PostProcessPassKind::Tonemap;
    });
    Require(tonemapPass != renderer.PostProcessPasses().end(), "Renderer post-process chain is missing tonemap after settings update");
    Require(tonemapPass->enabled, "Renderer disabled tonemap while applying bloom-only settings");
    Require(tonemapPass->outputTransform.autoExposure.enabled, "Renderer did not synchronize auto exposure to the tonemap pass");
}

void RunRendererSynchronizesTonemapPostProcessSettingsTest() {
    Renderer renderer;
    Require(renderer.ConfigurePostProcessChain(PostProcessChain::DefaultSceneChainDesc()), "Renderer rejected default post-process chain before setting tonemap options");
    renderer.SetDefaultPostProcessSettings(ScenePostProcessSettings{
        .tonemapEnabled = true,
        .outputTransform = SceneDisplayOutputTransform{
            .gamma = -1.0F,
            .tonemap = SceneDisplayTonemapOperator::AgxApprox,
            .colorGradingLutStrength = 4.0F,
            .autoExposure = FullscreenTextureAutoExposureSettings{
                .enabled = true,
                .meteredAverageLuminance = 0.09F,
                .middleGray = 0.18F,
                .minExposureStops = -4.0F,
                .maxExposureStops = 4.0F,
                .biasStops = 0.5F,
            },
        },
    });

    const ScenePostProcessSettings settings = renderer.DefaultPostProcessSettings();
    Require(NearlyEqual(settings.outputTransform.gamma, 0.001F), "Renderer did not clamp tonemap gamma");
    Require(NearlyEqual(settings.outputTransform.colorGradingLutStrength, 1.0F), "Renderer did not clamp color grading strength");
    Require(NearlyEqual(ResolveFullscreenTextureExposureStops(settings.outputTransform), 1.5F), "Renderer stored the wrong auto exposure transform");

    const std::optional<PostProcessPass> tonemapPass = renderer.FindPostProcessPass(PostProcessPassKind::Tonemap);
    Require(tonemapPass.has_value(), "Renderer could not find tonemap pass after settings update");
    Require(tonemapPass->outputTransform.tonemap == SceneDisplayTonemapOperator::AgxApprox, "Renderer did not synchronize tonemap operator");
    Require(NearlyEqual(ResolveFullscreenTextureExposureStops(tonemapPass->outputTransform), 1.5F), "Renderer did not synchronize auto exposure to tonemap pass");
}

void RunSceneExposureMeterEstimatesLightingLuminanceTest() {
    RenderScene scene;
    const SceneRenderLightingConfig dimConfig{
        .ambientColor = { 0.01F, 0.01F, 0.01F },
        .ambientIntensity = 1.0F,
        .environmentMode = SceneRenderEnvironmentMode::Disabled,
    };
    const float dimLuminance = SceneExposureMeter::EstimateAverageLuminance(scene, dimConfig);
    Require(dimLuminance >= 0.0001F, "SceneExposureMeter returned an invalid minimum luminance");

    static_cast<void>(scene.UpsertLight(LightRenderProxyDesc{
        .entityId = 10U,
        .kind = RenderLightKind::Directional,
        .color = { 1.0F, 1.0F, 1.0F },
        .intensity = 4.0F,
        .visible = true,
    }));
    const float brightLuminance = SceneExposureMeter::EstimateAverageLuminance(scene, dimConfig);
    Require(brightLuminance > dimLuminance, "SceneExposureMeter did not react to a visible directional light");

    static_cast<void>(scene.UpsertLight(LightRenderProxyDesc{
        .entityId = 10U,
        .kind = RenderLightKind::Directional,
        .color = { 1.0F, 1.0F, 1.0F },
        .intensity = 100.0F,
        .visible = false,
    }));
    const float hiddenLuminance = SceneExposureMeter::EstimateAverageLuminance(scene, dimConfig);
    Require(NearlyEqual(hiddenLuminance, dimLuminance), "SceneExposureMeter counted an invisible light");
}

void RunSceneExposureMeterBuildsHistogramTest() {
    RenderScene scene;
    const SceneRenderLightingConfig config{
        .ambientColor = { 0.02F, 0.02F, 0.02F },
        .ambientIntensity = 1.0F,
        .environmentMode = SceneRenderEnvironmentMode::Disabled,
    };

    const SceneExposureHistogram dimHistogram = SceneExposureMeter::BuildLightingHistogram(scene, config);
    Require(dimHistogram.totalWeight > 0.0F, "SceneExposureMeter did not seed a dim lighting histogram");
    const float dimMetered = SceneExposureMeter::MeterAverageLuminance(dimHistogram);

    static_cast<void>(scene.UpsertLight(LightRenderProxyDesc{
        .entityId = 20U,
        .kind = RenderLightKind::Directional,
        .color = { 1.0F, 1.0F, 1.0F },
        .intensity = 16.0F,
        .visible = true,
    }));

    const SceneExposureHistogram brightHistogram = SceneExposureMeter::BuildLightingHistogram(scene, config);
    const float brightMetered = SceneExposureMeter::MeterAverageLuminance(brightHistogram);
    Require(brightHistogram.totalWeight > dimHistogram.totalWeight, "SceneExposureMeter did not accumulate lighting histogram samples");
    Require(brightMetered > dimMetered, "SceneExposureMeter histogram did not meter brighter lighting");
}

void RunSceneExposureMeterBuildsHdrReadbackHistogramTest() {
    std::vector<std::uint8_t> dimPixels(SceneExposureMeter::kHdrReadbackByteCount, 0U);
    for (std::uint32_t pixel = 0; pixel < SceneExposureMeter::kHdrReadbackPixelCount; ++pixel) {
        const std::uint32_t offset = pixel * SceneExposureMeter::kHdrReadbackBytesPerPixel;
        dimPixels[offset] = 24U;
        dimPixels[offset + 3U] = 255U;
    }

    SceneExposureHistogram dimHistogram = SceneExposureMeter::BuildHdrReadbackHistogram(dimPixels);
    Require(dimHistogram.totalWeight == static_cast<float>(SceneExposureMeter::kHdrReadbackPixelCount), "HDR readback histogram did not count valid pixels");
    const float dimMetered = SceneExposureMeter::MeterAverageLuminance(dimHistogram);

    std::vector<std::uint8_t> brightPixels = dimPixels;
    for (std::uint32_t pixel = SceneExposureMeter::kHdrReadbackPixelCount / 2U; pixel < SceneExposureMeter::kHdrReadbackPixelCount; ++pixel) {
        const std::uint32_t offset = pixel * SceneExposureMeter::kHdrReadbackBytesPerPixel;
        brightPixels[offset] = 220U;
    }

    const SceneExposureHistogram brightHistogram = SceneExposureMeter::BuildHdrReadbackHistogram(brightPixels);
    const float brightMetered = SceneExposureMeter::MeterAverageLuminance(brightHistogram);
    Require(brightMetered > dimMetered, "HDR readback histogram did not meter brighter encoded HDR samples");
}

void RunSceneExposureMeterTemporalAdaptationTest() {
    SceneExposureMeter meter;
    const float initial = meter.Update(0.18F, SceneExposureAdaptationDesc{.enabled = true, .deltaSeconds = 1.0F});
    Require(NearlyEqual(initial, 0.18F), "SceneExposureMeter should initialize directly to the first luminance sample");
    Require(meter.HasHistory(), "SceneExposureMeter did not mark initialized exposure history");

    const float adapted = meter.Update(18.0F, SceneExposureAdaptationDesc{
        .enabled = true,
        .deltaSeconds = 1.0F / 60.0F,
        .brightAdaptationRate = 1.0F,
        .darkAdaptationRate = 1.0F,
    });
    Require(adapted > initial, "SceneExposureMeter temporal adaptation did not move toward a brighter sample");
    Require(adapted < 18.0F, "SceneExposureMeter temporal adaptation jumped directly to the target sample");

    const float reset = meter.Update(0.09F, SceneExposureAdaptationDesc{.enabled = false});
    Require(NearlyEqual(reset, 0.09F), "SceneExposureMeter should bypass temporal adaptation when disabled");
}

void RunRendererExposesPostProcessChainConfigurationTest() {
    Renderer renderer;
    Require(renderer.ConfigurePostProcessChain(PostProcessChainDesc{
        .passes = {
            PostProcessChain::kDefaultIdentityPass,
            PostProcessPass{.kind = PostProcessPassKind::Tonemap, .enabled = false},
        },
    }), "Renderer rejected a valid custom post-process chain");
    Require(renderer.PostProcessPasses().size() == 2U, "Renderer did not store the custom post-process chain");
    Require(renderer.SetPostProcessPassEnabled(PostProcessPassKind::Tonemap, true), "Renderer did not toggle a post-process pass");
    Require(renderer.PostProcessPasses()[1].enabled, "Renderer did not persist a toggled post-process pass");
    Require(renderer.SetPostProcessPass(PostProcessPass{
        .kind = PostProcessPassKind::Tonemap,
        .enabled = true,
        .outputTransform = SceneDisplayOutputTransform{
            .exposureStops = 1.0F,
            .gamma = 2.0F,
            .tonemap = SceneDisplayTonemapOperator::AgxApprox,
        },
    }), "Renderer did not replace an existing post-process pass");
    Require(NearlyEqual(renderer.PostProcessPasses()[1].outputTransform.exposureStops, 1.0F), "Renderer did not persist post-process tonemap settings");
    Require(renderer.InsertPostProcessPass(1U, PostProcessPass{.kind = PostProcessPassKind::Bloom, .enabled = false}), "Renderer did not insert a new post-process pass");
    Require(renderer.PostProcessPasses()[1].kind == PostProcessPassKind::Bloom, "Renderer inserted a post-process pass at the wrong index");
    Require(renderer.FindPostProcessPass(PostProcessPassKind::Bloom).has_value(), "Renderer did not find an inserted post-process pass");
    Require(renderer.RemovePostProcessPass(PostProcessPassKind::Bloom), "Renderer did not remove an inserted post-process pass");
    Require(!renderer.FindPostProcessPass(PostProcessPassKind::Bloom).has_value(), "Renderer found a removed post-process pass");
    Require(renderer.AddPostProcessPass(PostProcessPass{.kind = PostProcessPassKind::Bloom, .enabled = true}), "Renderer did not append a post-process pass");
    Require(!renderer.ConfigurePostProcessChain(PostProcessChainDesc{
        .passes = {
            PostProcessPass{.kind = PostProcessPassKind::Bloom, .enabled = true},
            PostProcessPass{.kind = PostProcessPassKind::Bloom, .enabled = false},
        },
    }), "Renderer accepted a post-process chain with duplicate pass kinds");
}

void RunSceneRendererAppliesForwardLightBudgetInValidationStatsTest() {
    RenderScene renderScene;
    for (std::uint32_t index = 0U; index < 5U; ++index) {
        static_cast<void>(renderScene.UpsertLight(LightRenderProxyDesc{
            .entityId = 1U + index,
            .kind = RenderLightKind::Point,
            .intensity = 1.0F,
            .visible = true,
        }));
    }

    SceneRenderer renderer;
    renderer.SetDefaultLightingConfig(SceneRenderLightingConfig{
        .maxForwardLights = 2U,
    });
    const SceneRenderSubmitStats stats = renderer.ValidateSceneResources(renderScene);
    Require(stats.sceneLightCount == 5U, "SceneRenderer validation did not count scene lights");
    Require(stats.forwardLightCapacity == 2U, "SceneRenderer validation did not report the configured forward light capacity");
    Require(stats.submittedForwardLightCount == 2U, "SceneRenderer validation did not apply the configured forward light budget");
    Require(stats.skippedForwardLightCount == 3U, "SceneRenderer validation did not report lights skipped by forward light budget");
}

void RunSceneRendererReportsEnvironmentLightingStatsTest() {
    RenderScene renderScene;
    SceneRenderer renderer;
    renderer.SetDefaultLightingConfig(SceneRenderLightingConfig{
        .environmentMode = SceneRenderEnvironmentMode::Hemisphere,
        .environmentDiffuseIntensity = 1.4F,
        .environmentSpecularIntensity = 0.45F,
    });
    const SceneRenderSubmitStats stats = renderer.ValidateSceneResources(renderScene);
    Require(stats.submittedEnvironmentLightingCount == 1U, "SceneRenderer validation did not report active environment lighting");
    Require(stats.environmentLightingMode == static_cast<std::uint32_t>(SceneRenderEnvironmentMode::Hemisphere) + 1U, "SceneRenderer validation did not report environment lighting mode");
    Require(stats.environmentLightingSampleCount == 2U, "SceneRenderer validation did not report hemisphere environment sample count");

    renderer.SetDefaultLightingConfig(SceneRenderLightingConfig{
        .environmentMode = SceneRenderEnvironmentMode::Disabled,
    });
    const SceneRenderSubmitStats disabledStats = renderer.ValidateSceneResources(renderScene);
    Require(disabledStats.submittedEnvironmentLightingCount == 0U, "SceneRenderer validation reported disabled environment lighting as active");
    Require(disabledStats.environmentLightingMode == static_cast<std::uint32_t>(SceneRenderEnvironmentMode::Disabled) + 1U, "SceneRenderer validation did not report disabled environment mode");
    Require(disabledStats.environmentLightingSampleCount == 0U, "SceneRenderer validation reported disabled environment samples");
}

void RunSceneRendererReportsClusteredIblAndAdvancedLightStatsTest() {
    RenderScene renderScene;
    static_cast<void>(renderScene.UpsertLight(LightRenderProxyDesc{
        .entityId = 1U,
        .kind = RenderLightKind::AreaRect,
        .intensity = 2.0F,
        .range = 12.0F,
        .areaWidth = 4.0F,
        .areaHeight = 2.0F,
        .contactShadowLength = 0.5F,
        .volumetricScattering = 0.75F,
        .visible = true,
    }));
    for (std::uint32_t index = 0U; index < 5U; ++index) {
        static_cast<void>(renderScene.UpsertLight(LightRenderProxyDesc{
            .entityId = 2U + index,
            .kind = RenderLightKind::Point,
            .position = { static_cast<float>(index) * 0.35F, 1.0F, 2.0F },
            .intensity = 1.0F,
            .range = 10.0F,
            .visible = true,
        }));
    }

    SceneRenderLightingConfig lighting{};
    lighting.maxForwardLights = 6U;
    lighting.lightingPath = SceneRenderLightingPath::ClusteredForwardPlus;
    lighting.clusterDimensions = { 4U, 3U, 2U };
    lighting.environmentMode = SceneRenderEnvironmentMode::ImageBased;
    lighting.globalIllumination = SceneRenderGlobalIlluminationMode::ProbeGrid;
    lighting.ibl.reflectionProbeCount = 2U;
    lighting.ibl.reflectionProbes[0].shape = SceneRenderReflectionProbeShape::Box;
    lighting.ibl.reflectionProbes[0].parallaxCorrection = true;
    lighting.ibl.reflectionProbes[1].shape = SceneRenderReflectionProbeShape::Infinite;
    lighting.contactShadowsEnabled = true;
    lighting.volumetricLightingEnabled = true;

    SceneRenderer renderer;
    renderer.SetDefaultLightingConfig(lighting);
    const SceneRenderSubmitStats stats = renderer.ValidateSceneResources(renderScene);
    Require(stats.lightingPath == static_cast<std::uint32_t>(SceneRenderLightingPath::ClusteredForwardPlus) + 1U, "SceneRenderer validation did not report clustered lighting path");
    Require(stats.lightClusterCount == 24U, "SceneRenderer validation did not report clustered light grid size");
    Require(stats.forwardLightCapacity == 6U, "KBMAT-MAT64: ClusteredForwardPlus did not use the expanded forward+ light budget");
    Require(stats.submittedForwardLightCount == 6U, "KBMAT-MAT64: ClusteredForwardPlus did not submit all lights within its expanded budget");
    Require(stats.skippedForwardLightCount == 0U, "KBMAT-MAT64: ClusteredForwardPlus incorrectly skipped lights within its expanded budget");
    Require(stats.lightingPathProduction, "KBMAT-MAT64: ClusteredForwardPlus must be reported as a production lighting path");
    Require(IsSceneRenderLightingPathProduction(SceneRenderLightingPath::ClusteredForwardPlus) &&
            IsSceneRenderLightingPathProduction(SceneRenderLightingPath::Deferred) &&
            !IsSceneRenderLightingPathProduction(SceneRenderLightingPath::VisibilityBuffer) &&
            IsSceneRenderLightingPathProduction(SceneRenderLightingPath::Forward),
            "KBMAT-MAT64: Forward, Forward+ and Deferred lighting paths are production; visibility is non-production");
    Require(stats.environmentLightingMode == static_cast<std::uint32_t>(SceneRenderEnvironmentMode::ImageBased) + 1U, "SceneRenderer validation did not report IBL environment mode");
    Require(stats.environmentLightingSampleCount == 4U, "SceneRenderer validation did not report IBL sample count");
    Require(stats.reflectionProbeCount == 2U, "SceneRenderer validation did not report reflection probes");
    Require(stats.localReflectionProbeCount == 1U, "SceneRenderer validation did not report local reflection probes");
    Require(stats.parallaxCorrectedProbeCount == 2U, "SceneRenderer validation did not report parallax corrected probes");
    Require(stats.globalIlluminationMode == static_cast<std::uint32_t>(SceneRenderGlobalIlluminationMode::ProbeGrid) + 1U, "SceneRenderer validation did not report GI mode");
    Require(stats.submittedAreaLightCount == 1U, "SceneRenderer validation did not report submitted area lights");
    Require(stats.submittedVolumetricLightCount == 1U, "SceneRenderer validation did not report volumetric lights");
    Require(stats.contactShadowLightCount == 1U, "SceneRenderer validation did not report contact shadow lights");
}

void RunSceneRendererReportsInvalidLightsSeparatelyFromBudgetSkipsTest() {
    RenderScene renderScene;
    static_cast<void>(renderScene.UpsertLight(LightRenderProxyDesc{
        .entityId = 1U,
        .kind = RenderLightKind::Directional,
        .intensity = 1.0F,
        .visible = true,
    }));
    static_cast<void>(renderScene.UpsertLight(LightRenderProxyDesc{
        .entityId = 2U,
        .kind = RenderLightKind::Point,
        .intensity = 2.0F,
        .range = 10.0F,
        .visible = true,
    }));
    static_cast<void>(renderScene.UpsertLight(LightRenderProxyDesc{
        .entityId = 3U,
        .intensity = 0.0F,
        .visible = true,
    }));
    static_cast<void>(renderScene.UpsertLight(LightRenderProxyDesc{
        .entityId = 4U,
        .intensity = 1.0F,
        .visible = false,
    }));
    static_cast<void>(renderScene.UpsertLight(LightRenderProxyDesc{
        .entityId = 5U,
        .kind = RenderLightKind::Point,
        .intensity = 1.0F,
        .range = 0.0F,
        .visible = true,
    }));

    SceneRenderer renderer;
    renderer.SetDefaultLightingConfig(SceneRenderLightingConfig{
        .maxForwardLights = 1U,
    });
    const SceneRenderSubmitStats stats = renderer.ValidateSceneResources(renderScene);
    Require(stats.sceneLightCount == 5U, "SceneRenderer validation did not count all light proxies");
    Require(stats.forwardLightCapacity == 1U, "SceneRenderer validation did not report light selection capacity");
    Require(stats.submittedForwardLightCount == 1U, "SceneRenderer validation selected the wrong number of forward lights");
    Require(stats.skippedForwardLightCount == 1U, "SceneRenderer validation did not report valid lights skipped by selection budget");
    Require(stats.invalidLightCount == 3U, "SceneRenderer validation did not report invalid lights separately");
}

void RunDirectionalShadowPlannerSkipsNonShadowCastingLightsTest() {
    RenderScene renderScene;
    static_cast<void>(renderScene.UpsertMesh(MeshRenderProxyDesc{
        .entityId = 1U,
        .meshAssetId = 42U,
        .visible = true,
        .castsShadow = true,
    }));
    static_cast<void>(renderScene.UpsertLight(LightRenderProxyDesc{
        .entityId = 10U,
        .kind = RenderLightKind::Directional,
        .intensity = 100.0F,
        .castsShadow = false,
        .visible = true,
    }));
    static_cast<void>(renderScene.UpsertLight(LightRenderProxyDesc{
        .entityId = 11U,
        .kind = RenderLightKind::Directional,
        .intensity = 1.0F,
        .castsShadow = true,
        .visible = true,
    }));
    static_cast<void>(renderScene.UpsertLight(LightRenderProxyDesc{
        .entityId = 12U,
        .kind = RenderLightKind::Directional,
        .intensity = 1000.0F,
        .castsShadow = true,
        .visible = true,
        .layer = 2U,
    }));

    RenderResourceRegistry resources;
    SceneRenderResourceMap resourceMap;
    const DirectionalShadowSetup setup = DirectionalShadowPassPlanner{}.Build(
        renderScene,
        resources,
        resourceMap,
        SceneRenderLightingConfig{},
        BGFX_INVALID_HANDLE,
        1U);

    Require(setup.valid, "Directional shadow planner did not build setup for the shadow-casting directional light");
    Require(setup.lightEntityId == 11U, "Directional shadow planner selected a directional light with castsShadow disabled");
}

void RunRendererStoresRuntimeAssetDiscoveryIntervalTest() {
    Renderer renderer;
    Require(renderer.RuntimeAssetDiscoveryIntervalFrames() == Renderer::kRuntimeAssetDiscoveryIntervalFrames, "Renderer did not expose the default asset discovery interval");
    renderer.SetRuntimeAssetDiscoveryIntervalFrames(0U);
    Require(renderer.RuntimeAssetDiscoveryIntervalFrames() == 0U, "Renderer did not store immediate asset discovery interval");
    Require(renderer.RuntimeResourceStats().assetDiscoveryIntervalFrames == 0U, "Renderer runtime stats did not reflect the configured discovery interval");
}

void RunSceneRenderDiagnosticsAggregateFrameSubmissionsTest() {
    SceneRenderDiagnostics first{};
    first.events.push_back(SceneRenderDiagnosticEvent{
        .severity = SceneRenderDiagnosticSeverity::Error,
        .kind = SceneRenderDiagnosticKind::MissingMeshBinding,
        .entityId = 1U,
        .meshAssetId = 42U,
        .instanceCount = 1U,
    });

    SceneRenderDiagnostics second{};
    second.events.push_back(SceneRenderDiagnosticEvent{
        .severity = SceneRenderDiagnosticSeverity::Warning,
        .kind = SceneRenderDiagnosticKind::DroppedInstances,
        .entityId = 2U,
        .meshAssetId = 42U,
        .instanceCount = 1U,
    });

    first += second;
    Require(first.events.size() == 2U, "SceneRenderDiagnostics did not aggregate frame submission events");
    Require(first.HasErrors(), "SceneRenderDiagnostics aggregate lost error status");
    Require(first.events[1].kind == SceneRenderDiagnosticKind::DroppedInstances, "SceneRenderDiagnostics aggregate changed event order");
}

void RunSyncUsesFreshLocalTransformsWithoutRuntimeUpdateTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneEntity camera = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Camera",
        .transform = LocalOnlyTransformAt(0.0F, 2.0F, -6.0F),
    });
    scene.Components().Cameras().Set(camera, kb::scene::CameraComponent{ .primary = true });

    const kb::scene::SceneEntity mesh = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Mesh",
        .transform = LocalOnlyTransformAt(3.0F, 4.0F, 5.0F),
    });
    scene.Components().MeshRenderers().Set(mesh, kb::scene::MeshRendererComponent{ .meshAssetId = 42U });

    RenderScene renderScene;
    EcsRenderSceneSynchronizer{}.Sync(scene, renderScene);

    const MeshRenderProxy* meshProxy = renderScene.FindMeshByEntity(mesh.Id());
    const CameraRenderProxy* cameraProxy = renderScene.FindCameraByEntity(camera.Id());
    Require(meshProxy != nullptr, "RenderScene did not sync local-only mesh transform");
    Require(cameraProxy != nullptr, "RenderScene did not sync local-only camera transform");
    Require(NearlyEqual(meshProxy->desc.model[12], 3.0F), "RenderScene ignored dirty local mesh X without runtime update");
    Require(NearlyEqual(meshProxy->desc.model[13], 4.0F), "RenderScene ignored dirty local mesh Y without runtime update");
    Require(NearlyEqual(meshProxy->desc.model[14], 5.0F), "RenderScene ignored dirty local mesh Z without runtime update");
    Require(NearlyEqual(cameraProxy->desc.position[1], 2.0F), "RenderScene ignored dirty local camera Y without runtime update");
}

void RunSyncComposesHierarchyWithoutRuntimeUpdateTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneEntity parent = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Parent",
        .transform = LocalOnlyTransformAt(10.0F, 1.0F, 0.0F),
    });
    const kb::scene::SceneEntity child = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Child",
        .transform = LocalOnlyTransformAt(2.0F, 3.0F, 4.0F),
    });
    static_cast<void>(scene.Hierarchy().SetParent(child, parent));
    scene.Components().MeshRenderers().Set(child, kb::scene::MeshRendererComponent{ .meshAssetId = 42U });

    RenderScene renderScene;
    EcsRenderSceneSynchronizer{}.Sync(scene, renderScene);

    const MeshRenderProxy* childProxy = renderScene.FindMeshByEntity(child.Id());
    Require(childProxy != nullptr, "RenderScene did not sync child mesh proxy");
    Require(NearlyEqual(childProxy->desc.model[12], 12.0F), "RenderScene did not compose parent and child local X without runtime update");
    Require(NearlyEqual(childProxy->desc.model[13], 4.0F), "RenderScene did not compose parent and child local Y without runtime update");
    Require(NearlyEqual(childProxy->desc.model[14], 4.0F), "RenderScene did not compose parent and child local Z without runtime update");
}

void RunUpdateMeshTransformRefreshesInstanceInPlaceTest() {
    // H2/H7: a transform-only update refreshes the cached instance matrix in
    // place when the draw-group cache is clean, with no rebuild.
    RenderScene renderScene;
    MeshRenderProxyDesc desc{};
    desc.entityId = 5U;
    desc.meshAssetId = 42U;
    desc.materialAssetId = 7U;
    desc.model[12] = 1.0F;
    static_cast<void>(renderScene.UpsertMesh(desc));

    const std::vector<SceneRenderDrawGroup>& groups = renderScene.DrawGroups();
    Require(groups.size() == 1U && groups[0].instances.size() == 1U, "RenderScene did not build a single draw group instance");
    Require(NearlyEqual(groups[0].instances[0].model[12], 1.0F), "RenderScene draw group did not carry initial transform");

    std::array<float, 16> model{};
    model[12] = 9.0F;
    Require(renderScene.UpdateMeshTransform(5U, model), "UpdateMeshTransform did not find the mesh proxy");
    const std::vector<SceneRenderDrawGroup>& refreshed = renderScene.DrawGroups();
    Require(NearlyEqual(refreshed[0].instances[0].model[12], 9.0F), "UpdateMeshTransform did not refresh the instance in place");
    Require(renderScene.Stats().transformInPlaceUpdateCount == 1U, "UpdateMeshTransform did not take the in-place fast path");
    Require(renderScene.Stats().transformFallbackUpdateCount == 0U, "UpdateMeshTransform fell back unexpectedly");
    Require(!renderScene.UpdateMeshTransform(999U, model), "UpdateMeshTransform should return false for a missing entity");

    // After a structural change (new proxy) the cache is dirty, so the next
    // transform update falls back to invalidation.
    MeshRenderProxyDesc second{};
    second.entityId = 6U;
    second.meshAssetId = 42U;
    second.materialAssetId = 7U;
    static_cast<void>(renderScene.UpsertMesh(second));
    Require(renderScene.UpdateMeshTransform(5U, model), "UpdateMeshTransform did not find the mesh proxy after structural change");
    Require(renderScene.Stats().transformFallbackUpdateCount == 1U, "UpdateMeshTransform did not fall back when the cache was dirty");
}

void RunSyncMeshWorldAffinesPushesColumnarTransformTest() {
    // H2: the columnar world-affine path pushes a precomputed affine straight into
    // the render instance stream, matching the column-major model layout.
    kb::scene::Scene scene;
    const kb::scene::SceneEntity mesh = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Mesh",
        .transform = LocalOnlyTransformAt(0.0F, 0.0F, 0.0F),
    });
    scene.Components().MeshRenderers().Set(mesh, kb::scene::MeshRendererComponent{ .meshAssetId = 42U });

    RenderScene renderScene;
    EcsRenderSceneSynchronizer synchronizer;
    synchronizer.Sync(scene, renderScene);
    static_cast<void>(renderScene.DrawGroups()); // ensure the cache is clean with stable instance locations

    kb::scene::WorldTransformAffine3x4 affine;
    affine.values[0] = 2.0F; // scale x
    affine.values[4] = 2.0F; // scale y
    affine.values[8] = 2.0F; // scale z
    affine.values[9] = 5.0F; // translation x
    affine.values[10] = 6.0F; // translation y
    affine.values[11] = 7.0F; // translation z
    const std::array<kb::scene::SceneEntity, 1U> entities{ mesh };
    const std::array<kb::scene::WorldTransformAffine3x4, 1U> affines{ affine };
    synchronizer.SyncMeshWorldAffines(
        renderScene,
        std::span<const kb::scene::SceneEntity>{ entities },
        std::span<const kb::scene::WorldTransformAffine3x4>{ affines });

    const std::vector<SceneRenderDrawGroup>& groups = renderScene.DrawGroups();
    Require(groups.size() == 1U && groups[0].instances.size() == 1U, "Columnar affine sync lost the draw group instance");
    const std::array<float, 16>& model = groups[0].instances[0].model;
    Require(NearlyEqual(model[0], 2.0F) && NearlyEqual(model[5], 2.0F) && NearlyEqual(model[10], 2.0F), "Columnar affine sync did not apply scale");
    Require(NearlyEqual(model[12], 5.0F) && NearlyEqual(model[13], 6.0F) && NearlyEqual(model[14], 7.0F), "Columnar affine sync did not apply translation");
    Require(NearlyEqual(model[15], 1.0F), "Columnar affine sync produced a non-affine homogeneous row");
    Require(renderScene.Stats().transformInPlaceUpdateCount >= 1U, "Columnar affine sync did not refresh the instance in place");
}

void RunSyncMeshWorldAffinesParallelTest() {
    // H6: the columnar affine sync runs over a shared WorkerPool; distinct
    // entities update their distinct proxies/instances concurrently.
    constexpr std::size_t kCount = 5000U;
    kb::scene::Scene scene;
    std::vector<kb::scene::SceneEntity> entities;
    entities.reserve(kCount);
    for (std::size_t index = 0; index < kCount; ++index) {
        const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Mesh" });
        scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{ .meshAssetId = 42U });
        entities.push_back(entity);
    }

    RenderScene renderScene;
    EcsRenderSceneSynchronizer synchronizer;
    synchronizer.Sync(scene, renderScene);
    static_cast<void>(renderScene.DrawGroups()); // clean cache + stamp instance locations

    std::vector<kb::scene::WorldTransformAffine3x4> affines(kCount);
    for (std::size_t index = 0; index < kCount; ++index) {
        affines[index].values[0] = 1.0F;
        affines[index].values[4] = 1.0F;
        affines[index].values[8] = 1.0F;
        affines[index].values[9] = static_cast<float>(index); // distinct translation x
    }

    kb::ecs::WorkerPool pool{ kb::ecs::WorkerPoolConfig{} };
    if (!pool.Running()) {
        pool.Start(kb::ecs::WorkerPoolConfig{});
    }
    synchronizer.SyncMeshWorldAffinesParallel(
        renderScene,
        std::span<const kb::scene::SceneEntity>{ entities },
        std::span<const kb::scene::WorldTransformAffine3x4>{ affines },
        pool);
    pool.Stop();

    Require(renderScene.Stats().transformInPlaceUpdateCount == kCount, "Parallel affine sync did not update every instance in place");
    Require(renderScene.Stats().transformFallbackUpdateCount == 0U, "Parallel affine sync fell back unexpectedly");
    for (std::size_t index = 0; index < kCount; ++index) {
        const MeshRenderProxy* proxy = renderScene.FindMeshByEntity(entities[index].Id());
        Require(proxy != nullptr, "Parallel affine sync lost a mesh proxy");
        Require(NearlyEqual(proxy->desc.model[12], static_cast<float>(index)), "Parallel affine sync applied the wrong translation");
    }
}

void RunRenderBridgeTelemetryAggregatesBridgeStatsTest() {
    // H9: bridge telemetry aggregates synchronizer + render-scene stats into one
    // report with fast-path ratios, and exports to JSON.
    EcsRenderSceneSynchronizerStats syncStats{};
    syncStats.transformPrecomputedReadCount = 90U;
    syncStats.transformResolvedFallbackCount = 10U;
    RenderSceneStats sceneStats{};
    sceneStats.transformInPlaceUpdateCount = 75U;
    sceneStats.transformFallbackUpdateCount = 25U;
    sceneStats.meshProxyCount = 4U;
    sceneStats.cameraProxyCount = 1U;
    sceneStats.lightProxyCount = 2U;

    const RenderBridgeTelemetry telemetry = BuildRenderBridgeTelemetry(syncStats, sceneStats);
    Require(telemetry.worldTransformPrecomputedReads == 90U, "Bridge telemetry lost precomputed read count");
    Require(telemetry.transformInPlaceUpdates == 75U, "Bridge telemetry lost in-place update count");
    Require(telemetry.meshProxies == 4U, "Bridge telemetry lost mesh proxy count");
    Require(NearlyEqual(static_cast<float>(telemetry.PrecomputedReadRatio()), 0.9F), "Bridge telemetry computed the wrong precomputed read ratio");
    Require(NearlyEqual(static_cast<float>(telemetry.InPlaceUpdateRatio()), 0.75F), "Bridge telemetry computed the wrong in-place update ratio");

    const std::string json = RenderBridgeTelemetryToJsonString(telemetry);
    Require(json.find("\"schema\": \"kb.render.bridge_telemetry.v1\"") != std::string::npos, "Bridge telemetry JSON omitted schema");
    Require(json.find("\"world_transform_precomputed_reads\": 90") != std::string::npos, "Bridge telemetry JSON omitted precomputed reads");
    Require(json.find("\"transform_in_place_updates\": 75") != std::string::npos, "Bridge telemetry JSON omitted in-place updates");

    // Empty bridge reports a 1.0 (fully optimal / nothing to fall back on) ratio.
    const RenderBridgeTelemetry empty = BuildRenderBridgeTelemetry(EcsRenderSceneSynchronizerStats{}, RenderSceneStats{});
    Require(NearlyEqual(static_cast<float>(empty.PrecomputedReadRatio()), 1.0F), "Empty bridge telemetry should report a neutral ratio");
}

void RunScenesHaveStableUniqueIdsTest() {
    kb::scene::Scene first;
    kb::scene::Scene second;
    Require(first.Id() != 0U, "Scene id must be non-zero");
    Require(second.Id() != 0U, "Second scene id must be non-zero");
    Require(first.Id() != second.Id(), "Scenes must not share render cache ids");
}

void RunSyncConsumesPrecomputedWorldTransformTest() {
    // H3: when the batched transform system has already produced world transforms
    // (worldDirty == false), the render bridge consumes them directly without a
    // recursive resolve.
    kb::scene::Scene scene;
    const kb::scene::SceneEntity camera = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Camera",
        .transform = TransformAt(0.0F, 2.0F, -6.0F),
    });
    scene.Components().Cameras().Set(camera, kb::scene::CameraComponent{ .primary = true });
    const kb::scene::SceneEntity mesh = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Mesh",
        .transform = LocalOnlyTransformAt(7.0F, 8.0F, 9.0F),
    });
    scene.Components().MeshRenderers().Set(mesh, kb::scene::MeshRendererComponent{ .meshAssetId = 42U });

    // Run the batched transform hierarchy so world transforms are computed once
    // and worldDirty is cleared; the render bridge must then consume them.
    scene.Runtime().SynchronizeTransforms();

    RenderScene renderScene;
    EcsRenderSceneSynchronizer synchronizer;
    synchronizer.Sync(scene, renderScene);

    const MeshRenderProxy* meshProxy = renderScene.FindMeshByEntity(mesh.Id());
    Require(meshProxy != nullptr, "RenderScene did not sync precomputed mesh transform");
    Require(NearlyEqual(meshProxy->desc.model[12], 7.0F), "Render bridge did not consume precomputed world X");
    Require(NearlyEqual(meshProxy->desc.model[13], 8.0F), "Render bridge did not consume precomputed world Y");
    Require(NearlyEqual(meshProxy->desc.model[14], 9.0F), "Render bridge did not consume precomputed world Z");

    const EcsRenderSceneSynchronizerStats stats = synchronizer.Stats();
    Require(stats.transformPrecomputedReadCount >= 2U, "Render bridge did not take the precomputed fast path for clean transforms");
    Require(stats.transformResolvedFallbackCount == 0U, "Render bridge fell back to resolve for already-computed transforms");
}

void RunSyncFallsBackToResolveForDirtyTransformTest() {
    // H3 fallback: an entity whose world transform was not yet computed
    // (worldDirty == true) is resolved on demand, preserving correctness.
    kb::scene::Scene scene;
    const kb::scene::SceneEntity mesh = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Mesh",
        .transform = LocalOnlyTransformAt(3.0F, 4.0F, 5.0F),
    });
    scene.Components().MeshRenderers().Set(mesh, kb::scene::MeshRendererComponent{ .meshAssetId = 42U });

    RenderScene renderScene;
    EcsRenderSceneSynchronizer synchronizer;
    synchronizer.Sync(scene, renderScene);

    const MeshRenderProxy* meshProxy = renderScene.FindMeshByEntity(mesh.Id());
    Require(meshProxy != nullptr, "RenderScene did not sync dirty mesh transform");
    Require(NearlyEqual(meshProxy->desc.model[12], 3.0F), "Render bridge resolve fallback lost world X");
    const EcsRenderSceneSynchronizerStats stats = synchronizer.Stats();
    Require(stats.transformResolvedFallbackCount >= 1U, "Render bridge did not fall back to resolve for a dirty transform");
}

// LIB-143: proves SceneParticleRenderSynchronizer actually produces real, GPU-visible
// MeshRenderProxy entries for live particles (mesh/material/shadow flags correct) and
// correctly removes stale proxy slots both when particles die naturally (count shrinks) and
// when the whole instance is released (owner destroyed) - the exact two cleanup paths the
// class's own doc comment calls out as necessary because synthetic particle proxy ids are not
// real ECS entities.
void RunSceneParticleRenderSynchronizerTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_renderer_particle_sync_lib143";
    std::error_code resetError;
    std::filesystem::remove_all(root, resetError);
    std::filesystem::create_directories(root / "Assets" / "Fx", resetError);
    Require(!resetError, "LIB-143 particle render sync test project root could not be prepared");

    kb::scene::ParticleEffectAsset effect{};
    effect.materialReference = kb::assets::ToString(kb::assets::AssetId{ 535353U });
    effect.looping = true;
    effect.emissionRatePerSecond = 1000.0F;
    effect.startSpeedMin = 0.0F;
    effect.startSpeedMax = 0.0F;
    effect.startLifetimeMin = 0.05F;
    effect.startLifetimeMax = 0.05F;
    effect.spreadDegrees = 0.0F;
    effect.gravityScale = 0.0F;
    effect.maxParticles = 4U;
    const std::filesystem::path effectPath = root / "Assets" / "Fx" / "Sync.kbvfx";
    Require(kb::scene::ParticleEffectAssetIO::Save(effectPath, effect), "LIB-143 particle render sync test effect asset must write to disk");

    kb::scene::Scene scene;
    Require(scene.Assets().MountProject(root), "LIB-143 particle render sync test project mount failed");
    Require(scene.Assets().Discover() == 1U, "LIB-143 particle render sync test did not discover exactly the effect asset");
    const kb::assets::AssetMetadata* effectMetadata = scene.Assets().Manager().Registry().FindByPath("/Game/Fx/Sync.kbvfx");
    Require(effectMetadata != nullptr, "LIB-143 particle render sync test discovered wrong effect metadata");
    const std::uint64_t effectAssetId = effectMetadata->id.value;

    // Registered AFTER Discover() - see ScriptRuntimeTests.cpp's own note on why a synthetic,
    // file-less asset would otherwise be swept away by DiscoverMountedAssets' cleanup pass.
    Require(scene.Assets().Manager().RegisterAsset(kb::assets::AssetMetadata{
                .id = kb::assets::AssetId{ 535353U },
                .type = "RenderMaterial",
                .name = "FakeParticleSyncMaterial",
                .virtualPath = "/Game/FakeParticleSyncMaterial.kbmat",
                .physicalPath = "FakeParticleSyncMaterial.kbmat",
                .contentHash = 1U,
            }),
        "LIB-143 particle render sync test fake material registration failed");

    const kb::scene::SceneEntity owner = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "ParticleSyncOwner",
        .transform = LocalOnlyTransformAt(2.0F, 0.0F, 0.0F),
    });
    Require(owner.IsValid(), "LIB-143 particle render sync test owner entity creation failed");
    scene.Runtime().SynchronizeTransforms();

    const std::uint64_t instance = scene.Particles().Create(effectAssetId, owner);
    Require(instance != 0U, "LIB-143 particle render sync test instance creation failed");
    Require(scene.Particles().Play(instance), "LIB-143 particle render sync test Play failed");
    scene.Particles().Advance(0.01F);
    scene.Particles().Advance(0.01F);
    const std::uint32_t liveAfterSpawn = scene.Particles().LiveParticleCount(instance);
    Require(liveAfterSpawn > 0U && liveAfterSpawn <= 4U, "LIB-143 particle render sync test did not spawn any particles to sync");

    RenderScene renderScene;
    SceneParticleRenderSynchronizer synchronizer;
    synchronizer.Sync(scene, renderScene, 0U);

    const kb::assets::AssetId quadMeshAssetId = BuiltInParticleQuadMeshAssetId();
    std::uint32_t particleProxyCount = 0U;
    for (const auto& [proxyId, proxy] : renderScene.MeshProxies()) {
        if (proxy.desc.meshAssetId != quadMeshAssetId.value) {
            continue;
        }
        ++particleProxyCount;
        Require(proxy.desc.materialAssetId == 535353U, "SceneParticleRenderSynchronizer must submit the instance's resolved material asset id");
        Require(!proxy.desc.castsShadow, "SceneParticleRenderSynchronizer must submit particle billboards as non-shadow-casting");
        Require(proxy.desc.receivesShadow, "SceneParticleRenderSynchronizer must submit particle billboards as shadow-receiving");
        Require(proxy.desc.visible, "SceneParticleRenderSynchronizer must submit particle billboards as visible");
    }
    Require(particleProxyCount == liveAfterSpawn, "SceneParticleRenderSynchronizer must submit exactly one mesh proxy per live particle");
    Require(renderScene.MeshProxyCount() == liveAfterSpawn, "SceneParticleRenderSynchronizer must not leave any unrelated mesh proxies behind");

    // A second scene starts its instance ids at 1 too. Synchronizing it through the same
    // renderer-owned bridge must not overwrite scene A's stale-slot history.
    kb::scene::Scene secondScene;
    Require(secondScene.Assets().MountProject(root), "LIB-143 second-scene project mount failed");
    Require(secondScene.Assets().Discover() == 1U, "LIB-143 second-scene discovery failed");
    Require(secondScene.Assets().Manager().RegisterAsset(kb::assets::AssetMetadata{
                .id = kb::assets::AssetId{ 535353U },
                .type = "RenderMaterial",
                .name = "FakeParticleSyncMaterial",
                .virtualPath = "/Game/FakeParticleSyncMaterial.kbmat",
                .physicalPath = "FakeParticleSyncMaterial.kbmat",
                .contentHash = 1U,
            }),
        "LIB-143 second-scene material registration failed");
    const kb::scene::SceneEntity secondOwner = secondScene.Entities().CreateEntity();
    secondScene.Runtime().SynchronizeTransforms();
    const std::uint64_t secondInstance = secondScene.Particles().Create(effectAssetId, secondOwner);
    Require(secondInstance == instance, "LIB-143 regression setup requires colliding per-scene instance ids");
    Require(secondScene.Particles().Emit(secondInstance, 1U), "LIB-143 second-scene emit failed");
    RenderScene secondRenderScene;
    synchronizer.Sync(secondScene, secondRenderScene, 0U);
    Require(secondRenderScene.MeshProxyCount() == 1U, "LIB-143 second scene did not synchronize independently");

    // Stop first (halts new emission only, per SceneParticleSystems::Stop's own contract) -
    // otherwise emissionRatePerSecond=1000 would keep replacing dying particles with new
    // ones and LiveParticleCount would never reach 0. All particles' lifetime is 0.05s, so
    // advancing well past that kills the already-live batch (count shrinks to 0 while the
    // instance itself stays alive), proving the "currentCount < previousCount" stale-slot
    // cleanup path.
    Require(scene.Particles().Stop(instance), "LIB-143 particle render sync test Stop failed");
    scene.Particles().Advance(0.2F);
    Require(scene.Particles().LiveParticleCount(instance) == 0U, "LIB-143 particle render sync test particles did not die as expected");
    synchronizer.Sync(scene, renderScene, 0U);
    Require(renderScene.MeshProxyCount() == 0U, "SceneParticleRenderSynchronizer must remove stale proxy slots once particles die");

    // Re-spawn, sync once so proxies exist again, then release the whole instance via owner
    // destruction - proving the OTHER cleanup path (an instance disappearing between frames
    // entirely, not just shrinking).
    Require(scene.Particles().Emit(instance, 2U), "LIB-143 particle render sync test re-emit failed");
    Require(scene.Particles().LiveParticleCount(instance) == 2U, "LIB-143 particle render sync test re-emit did not spawn the requested count");
    synchronizer.Sync(scene, renderScene, 0U);
    Require(renderScene.MeshProxyCount() == 2U, "LIB-143 particle render sync test re-emitted particles were not synced");

    scene.Entities().Destroy(owner);
    scene.Particles().Advance(0.01F);
    Require(!scene.Particles().Exists(instance), "LIB-143 particle render sync test instance did not auto-release with its owner");
    synchronizer.Sync(scene, renderScene, 0U);
    Require(renderScene.MeshProxyCount() == 0U, "SceneParticleRenderSynchronizer must remove all proxy slots for an instance released since last frame");
}

// LIB-144: SceneRenderVisibilityPublisher's frame construction against a hand-built
// RenderScene - no Scene, no bgfx, no resources needed. Proves: deterministic
// entityId-sorted entries regardless of proxy-map iteration order, synthetic particle
// proxies skipped, the VisibilityComponent flag and the camera cullingMask both reflected
// in `visible`, the "no camera = invalid frustum = nothing culled" rule, and the
// "unresolvable mesh = invalid bounds = never frustum-culled" rule (real bounds resolution
// and real frustum culling are proven end-to-end through Renderer::SubmitScene in
// RendererRuntimeSubmitTests' RunRendererPublishesSceneVisibilityFeedbackTest).
void RunSceneRenderVisibilityPublisherBuildsFrameTest() {
    RenderScene renderScene;
    std::array<float, 16> identity{};
    identity[0] = identity[5] = identity[10] = identity[15] = 1.0F;

    static_cast<void>(renderScene.UpsertMesh(MeshRenderProxyDesc{ .entityId = 9U, .meshAssetId = 1U, .model = identity, .visible = true, .layer = 1U }));
    static_cast<void>(renderScene.UpsertMesh(MeshRenderProxyDesc{ .entityId = 3U, .meshAssetId = 1U, .model = identity, .visible = false, .layer = 1U }));
    static_cast<void>(renderScene.UpsertMesh(MeshRenderProxyDesc{ .entityId = 6U, .meshAssetId = 1U, .model = identity, .visible = true, .layer = 2U }));
    static_cast<void>(renderScene.UpsertMesh(MeshRenderProxyDesc{
        .entityId = SceneParticleRenderSynchronizer::kSyntheticProxyIdBase + 42U,
        .meshAssetId = 1U,
        .model = identity,
        .visible = true,
        .layer = 1U,
    }));

    // No camera: invalid frustum, all-bits mask - the authored visible flag alone decides.
    kb::scene::SceneRenderVisibilityFrame frame;
    SceneRenderVisibilityPublisher::BuildFrame(renderScene, nullptr, 5U, 0U, 64U, 64U, nullptr, nullptr, frame);
    Require(!frame.frustumValid, "LIB-144 publisher must report an invalid frustum when the submit had no camera");
    Require(!frame.cameraValid, "LIB-145 publisher must report no camera for a camera-less submit");
    Require(frame.viewportWidth == 64U && frame.viewportHeight == 64U, "LIB-145 publisher must record the submitted viewport extent");
    Require(frame.viewportId == 5U, "LIB-144 publisher must record the submitted viewport id");
    Require(frame.entries.size() == 3U, "LIB-144 publisher must skip synthetic particle proxies");
    Require(frame.entries[0].entityId == 3U && frame.entries[1].entityId == 6U && frame.entries[2].entityId == 9U,
        "LIB-144 publisher entries must be sorted by entityId regardless of proxy-map iteration order");
    Require(!frame.entries[0].visible, "LIB-144 publisher must report a VisibilityComponent-hidden proxy as not visible");
    Require(frame.entries[1].visible && frame.entries[2].visible, "LIB-144 publisher must report visible proxies as visible under an all-bits default mask");
    Require(!frame.entries[0].worldBounds.IsValid(), "LIB-144 publisher must keep invalid bounds for a proxy whose mesh resource is unresolvable");

    // Restrictive camera mask (layer bit 1 only): the layer=2 proxy is mask-rejected,
    // exactly like MeshPipelinePassPolicy would reject it from every pass of this camera.
    SceneRenderCamera camera{};
    camera.view = identity;
    camera.projection = identity;
    camera.cullingMask = 1U;
    SceneRenderVisibilityPublisher::BuildFrame(renderScene, &camera, 5U, 0U, 64U, 64U, nullptr, nullptr, frame);
    Require(frame.frustumValid, "LIB-144 publisher must extract a valid frustum from a real camera");
    Require(frame.cameraValid && frame.view == identity && frame.projection == identity,
        "LIB-145 publisher must copy the submit camera's view/projection matrices into the frame");
    Require(frame.entries.size() == 3U, "LIB-144 publisher must keep one entry per real mesh proxy under a camera");
    Require(!frame.entries[1].visible, "LIB-144 publisher must mask-reject a proxy whose layer is outside the camera's cullingMask");
    Require(frame.entries[2].visible, "LIB-144 publisher must keep a mask-passing, visible proxy visible (invalid bounds are never frustum-culled)");
}

} // namespace

void RunRenderSceneSyncTests() {
    RunCreatesStableRenderProxiesTest();
    RunEcsSyncPropagatesDetailSwitchPolicyTest();
    RunRenderScenePrimaryCameraSelectionRespectsViewportAndPriorityTest();
    RunEcsSyncPropagatesCullingMaskAndClearSettingsTest();
    RunEcsSyncResolvesWorldBackdropDeterministicallyTest();
    RunEcsSyncResolvesAmbientRadianceDeterministicallyTest();
    RunEcsSyncResolvesVisibilityGateHierarchyAndMaskTest();
    RunEcsSyncResolvesMaterialInstanceHandleTest();
    RunRenderSceneSyncsLightPipelineFieldsTest();
    RunRenderSceneIgnoresLightsWithoutBasicLightingProviderTest();
    RunRenderSceneSyncsAllSurfaceEmitterKindsTest();
    RunRenderSceneSyncResolvesLightColorTemperatureTest();
    RunTracksUpdatesWithoutReplacingProxyTest();
    RunSyncEntitiesUpdatesOnlyRequestedProxyTest();
    RunMeshRendererModifiedRuntimeQueueInvalidatesMaterialProxyTest();
    RunSyncTransformUpdatesUsesRuntimeCacheTest();
    RunSyncEntitiesRemovesDestroyedProxyTest();
    RunVisibilityKeepsProxyButRemovesSnapshotInstanceTest();
    RunDeletesRemovedComponentsAndEntitiesTest();
    RunRenderResourceMapRequiresExplicitBindingsTest();
    RunRenderSceneBuildsMeshMaterialDrawGroupsTest();
    RunRenderSceneBuildsLargeMeshMaterialDrawGroupsTest();
    RunRenderSceneExpandsGeometrySwarmIntoExistingDrawGroupTest();
    RunRenderSceneAppliesSurfaceCastByRegionLayerAndOrderTest();
    RunRenderSceneAppliesFacingPanelModesWithoutMutatingEcsTransformTest();
    RunRenderSceneCachesDrawGroupsUntilMeshStateChangesTest();
    RunRenderSceneReserveAndStatsExposeProxyCapacityTest();
    RunEcsSyncPropagatesMaterialSlotOverridesTest();
    RunEcsSyncScratchCapacityIsReusableAndVisibleTest();
    RunRenderInstanceBufferPacksModelAndColorTest();
    RunRenderInstanceBufferPacksPerInstanceScalarsTest();
    RunRenderSyncSystemAliasResolvesTest();
    RunSceneRendererReportsMissingMeshBindingTest();
    RunSceneRendererValidationScratchIsReusableAndVisibleTest();
    RunSceneRendererEmitsMissingResourceDiagnosticsTest();
    RunSceneRenderSubmitStatsAggregateFrameSubmissionsTest();
    RunRendererRuntimeResourceStatsExposeCacheRetentionPolicyTest();
    RunRendererReservesRuntimeSceneResourceScratchTest();
    RunSceneRendererStoresDefaultDrawBudgetTest();
    RunSceneRendererStoresDefaultLightingConfigTest();
    RunRendererStoresDefaultSceneDrawBudgetTest();
    RunRendererStoresDefaultSceneLightingConfigTest();
    RunSceneLightingPackerAddsEditorPreviewKeyLightTest();
    RunSceneLightingPackerPreservesSurfaceEmitterGeometryTest();
    RunSceneForwardLightSelectorAppliesLayerMaskTest();
    RunSceneLightColorResolvesTemperatureTest();
    RunRendererStoresDefaultPostProcessSettingsTest();
    RunRendererSynchronizesTonemapPostProcessSettingsTest();
    RunSceneExposureMeterEstimatesLightingLuminanceTest();
    RunSceneExposureMeterBuildsHistogramTest();
    RunSceneExposureMeterBuildsHdrReadbackHistogramTest();
    RunSceneExposureMeterTemporalAdaptationTest();
    RunRendererExposesPostProcessChainConfigurationTest();
    RunSceneRendererAppliesForwardLightBudgetInValidationStatsTest();
    RunSceneRendererReportsEnvironmentLightingStatsTest();
    RunSceneRendererReportsClusteredIblAndAdvancedLightStatsTest();
    RunSceneRendererReportsInvalidLightsSeparatelyFromBudgetSkipsTest();
    RunDirectionalShadowPlannerSkipsNonShadowCastingLightsTest();
    RunRendererStoresRuntimeAssetDiscoveryIntervalTest();
    RunSceneRenderDiagnosticsAggregateFrameSubmissionsTest();
    RunSyncUsesFreshLocalTransformsWithoutRuntimeUpdateTest();
    RunSyncComposesHierarchyWithoutRuntimeUpdateTest();
    RunSyncConsumesPrecomputedWorldTransformTest();
    RunSyncFallsBackToResolveForDirtyTransformTest();
    RunUpdateMeshTransformRefreshesInstanceInPlaceTest();
    RunSyncMeshWorldAffinesPushesColumnarTransformTest();
    RunSyncMeshWorldAffinesParallelTest();
    RunRenderBridgeTelemetryAggregatesBridgeStatsTest();
    RunScenesHaveStableUniqueIdsTest();
    RunSceneParticleRenderSynchronizerTest();
    RunSceneRenderVisibilityPublisherBuildsFrameTest();
}

} // namespace kb::render::tests
