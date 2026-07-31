#include "kb/render/scene/EcsRenderSceneSynchronizer.hpp"

#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneMaterialInstances.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneComponentVisitors.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneLightingAccess.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/SceneVisibilityResolution.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "engine/scene/VisibilityComponent.hpp"
#include "engine/scene/WorldBackdropComponent.hpp"
#include "engine/scene/AmbientRadianceComponent.hpp"
#include "engine/scene/DetailSwitchComponent.hpp"
#include "engine/scene/VisibilityBlockerComponent.hpp"
#include "engine/scene/GeometrySwarmComponent.hpp"
#include "engine/scene/SurfaceCastComponent.hpp"
#include "engine/scene/RegionShapeComponent.hpp"
#include "engine/ecs/Query.hpp"
#include "engine/ecs/UnsafeHotQuery.hpp"
#include "engine/ecs/World.hpp"
#include "kb/render/scene/RenderScene.hpp"
#include "engine/ecs/WorkerPool.hpp"
#include "scene/EcsRenderTransformResolver.hpp"
#include "scene/SceneLightColor.hpp"
#include "scene/SceneRenderWorldTransformReader.hpp"
#include "scene/SceneTransformMatrices.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <span>

namespace kb::render {
namespace {

[[nodiscard]] std::array<float, 4> NeutralInstanceColor() noexcept {
    return { 0.76F, 0.80F, 0.86F, 1.0F };
}

[[nodiscard]] std::uint32_t CopyMaterialSlotOverrides(
    const kb::scene::MeshRendererComponent& renderer,
    std::array<std::uint64_t, kMaxSceneMaterialSlotOverrides>& overrides) noexcept {
    const std::uint32_t count = std::min<std::uint32_t>(
        renderer.materialSlotOverrideCount,
        std::min<std::uint32_t>(kb::scene::kMaxMeshRendererMaterialSlotOverrides, kMaxSceneMaterialSlotOverrides));
    for (std::uint32_t index = 0U; index < count; ++index) {
        overrides[index] = renderer.materialSlotAssetIds[index];
    }
    return count;
}

[[nodiscard]] std::array<float, 3> PositionOf(const kb::scene::TransformComponent& transform) noexcept {
    return { transform.worldPosition.x, transform.worldPosition.y, transform.worldPosition.z };
}

[[nodiscard]] std::array<float, 4> RotationOf(const kb::scene::TransformComponent& transform) noexcept {
    return { transform.worldRotation.x, transform.worldRotation.y, transform.worldRotation.z, transform.worldRotation.w };
}

[[nodiscard]] RenderCameraProjection CameraProjectionOf(kb::scene::CameraProjection projection) noexcept {
    switch (projection) {
    case kb::scene::CameraProjection::Perspective:
        return RenderCameraProjection::Perspective;
    case kb::scene::CameraProjection::Orthographic:
        return RenderCameraProjection::Orthographic;
    }
    return RenderCameraProjection::Perspective;
}

[[nodiscard]] RenderCameraClearMode CameraClearModeOf(kb::scene::CameraClearMode clearMode) noexcept {
    switch (clearMode) {
    case kb::scene::CameraClearMode::SolidColor:
        return RenderCameraClearMode::SolidColor;
    case kb::scene::CameraClearMode::DepthOnly:
        return RenderCameraClearMode::DepthOnly;
    case kb::scene::CameraClearMode::DontClear:
        return RenderCameraClearMode::DontClear;
    }
    return RenderCameraClearMode::SolidColor;
}

[[nodiscard]] RenderLightKind LightKindOf(kb::scene::LightKind kind) noexcept {
    switch (kind) {
    case kb::scene::LightKind::Directional:
        return RenderLightKind::Directional;
    case kb::scene::LightKind::Point:
        return RenderLightKind::Point;
    case kb::scene::LightKind::Spot:
        return RenderLightKind::Spot;
    case kb::scene::LightKind::AreaRect:
        return RenderLightKind::AreaRect;
    case kb::scene::LightKind::AreaDisk:
        return RenderLightKind::AreaDisk;
    case kb::scene::LightKind::Tube:
        return RenderLightKind::Tube;
    }
    return RenderLightKind::Point;
}

[[nodiscard]] SceneRenderWorldBackdropMode WorldBackdropModeOf(kb::scene::WorldBackdropMode mode) noexcept {
    switch (mode) {
    case kb::scene::WorldBackdropMode::SolidColor: return SceneRenderWorldBackdropMode::SolidColor;
    case kb::scene::WorldBackdropMode::VerticalGradient: return SceneRenderWorldBackdropMode::VerticalGradient;
    case kb::scene::WorldBackdropMode::EnvironmentMap: return SceneRenderWorldBackdropMode::EnvironmentMap;
    case kb::scene::WorldBackdropMode::ProceduralSky: return SceneRenderWorldBackdropMode::ProceduralSky;
    }
    return SceneRenderWorldBackdropMode::SolidColor;
}

[[nodiscard]] std::optional<SceneRenderWorldBackdrop> ResolveWorldBackdrop(const kb::scene::Scene& scene) {
    struct SelectedBackdrop {
        SceneRenderWorldBackdrop desc{};
        std::int32_t priority = 0;
    };
    std::optional<SelectedBackdrop> selected;
    kb::ecs::Query<kb::scene::WorldBackdropComponent> query = const_cast<kb::scene::Scene&>(scene).Runtime().EcsWorld().CreateQuery<kb::scene::WorldBackdropComponent>();
    kb::ecs::UnsafeHotReadQuery<kb::scene::WorldBackdropComponent> hot;
    kb::ecs::QueryExecutionSettings settings{};
    settings.policy = kb::ecs::QueryExecutionPolicy::SingleThread;
    if (!query.IsValid() || !hot.Rebuild(query, settings)) return std::nullopt;
    hot.ForEachRange(settings.maxBatchSize, [&selected](const auto& batch) {
        const kb::scene::WorldBackdropComponent* backdrops = batch.template Components<0>();
        for (std::size_t index = 0U; index < batch.Count(); ++index) {
            const kb::scene::SceneEntity entity = batch.EntityAt(index);
            const kb::scene::WorldBackdropComponent& backdrop = backdrops[index];
            if (!entity.IsValid() || !backdrop.enabled || !kb::scene::IsWorldBackdropComponentValid(backdrop)) continue;
            const SceneRenderWorldBackdrop candidate{
                .entityId = entity.Id(),
                .mode = WorldBackdropModeOf(backdrop.mode),
                .color = { backdrop.color.x, backdrop.color.y, backdrop.color.z },
                .horizonColor = { backdrop.horizonColor.x, backdrop.horizonColor.y, backdrop.horizonColor.z },
                .zenithColor = { backdrop.zenithColor.x, backdrop.zenithColor.y, backdrop.zenithColor.z },
                .environmentAssetId = backdrop.environmentAssetId,
                .horizonHeight = backdrop.horizonHeight,
                .gradientExponent = backdrop.gradientExponent,
            };
            if (!selected.has_value() || backdrop.priority > selected->priority ||
                (backdrop.priority == selected->priority && candidate.entityId < selected->desc.entityId)) {
                selected = SelectedBackdrop{ .desc = candidate, .priority = backdrop.priority };
            }
        }
    });
    return selected.has_value() ? std::optional<SceneRenderWorldBackdrop>{ selected->desc } : std::nullopt;
}

[[nodiscard]] SceneRenderAmbientRadianceMode AmbientRadianceModeOf(kb::scene::AmbientRadianceMode mode) noexcept {
    switch (mode) {
    case kb::scene::AmbientRadianceMode::Constant: return SceneRenderAmbientRadianceMode::Constant;
    case kb::scene::AmbientRadianceMode::Gradient: return SceneRenderAmbientRadianceMode::Gradient;
    case kb::scene::AmbientRadianceMode::EnvironmentMap: return SceneRenderAmbientRadianceMode::EnvironmentMap;
    case kb::scene::AmbientRadianceMode::ProceduralSky: return SceneRenderAmbientRadianceMode::ProceduralSky;
    case kb::scene::AmbientRadianceMode::CapturedEnvironment: return SceneRenderAmbientRadianceMode::CapturedEnvironment;
    case kb::scene::AmbientRadianceMode::EstimatedEnvironment: return SceneRenderAmbientRadianceMode::EstimatedEnvironment;
    }
    return SceneRenderAmbientRadianceMode::Constant;
}

[[nodiscard]] std::optional<SceneRenderAmbientRadiance> ResolveAmbientRadiance(
    const kb::scene::Scene& scene,
    const std::optional<SceneRenderWorldBackdrop>& worldBackdrop) {
    struct SelectedAmbient { SceneRenderAmbientRadiance desc{}; std::int32_t priority = 0; };
    std::optional<SelectedAmbient> selected;
    kb::ecs::Query<kb::scene::AmbientRadianceComponent> query = const_cast<kb::scene::Scene&>(scene).Runtime().EcsWorld().CreateQuery<kb::scene::AmbientRadianceComponent>();
    kb::ecs::UnsafeHotReadQuery<kb::scene::AmbientRadianceComponent> hot;
    kb::ecs::QueryExecutionSettings settings{};
    settings.policy = kb::ecs::QueryExecutionPolicy::SingleThread;
    if (!query.IsValid() || !hot.Rebuild(query, settings)) return std::nullopt;
    hot.ForEachRange(settings.maxBatchSize, [&selected](const auto& batch) {
        const kb::scene::AmbientRadianceComponent* components = batch.template Components<0>();
        for (std::size_t index = 0U; index < batch.Count(); ++index) {
            const kb::scene::SceneEntity entity = batch.EntityAt(index);
            const kb::scene::AmbientRadianceComponent& ambient = components[index];
            if (!entity.IsValid() || !ambient.enabled || !kb::scene::IsAmbientRadianceComponentValid(ambient)) continue;
            const SceneRenderAmbientRadiance candidate{
                .entityId = entity.Id(), .mode = AmbientRadianceModeOf(ambient.mode),
                .color = { ambient.color.x, ambient.color.y, ambient.color.z },
                .horizonColor = { ambient.horizonColor.x, ambient.horizonColor.y, ambient.horizonColor.z },
                .zenithColor = { ambient.zenithColor.x, ambient.zenithColor.y, ambient.zenithColor.z },
                .environmentAssetId = ambient.environmentAssetId, .intensity = ambient.intensity,
                .diffuseIntensity = ambient.diffuseIntensity, .specularIntensity = ambient.specularIntensity,
            };
            if (!selected.has_value() || ambient.priority > selected->priority ||
                (ambient.priority == selected->priority && candidate.entityId < selected->desc.entityId)) {
                selected = SelectedAmbient{ .desc = candidate, .priority = ambient.priority };
            }
        }
    });
    if (!selected.has_value()) return std::nullopt;

    // Capture and estimation are derived each synchronization from the selected
    // authored backdrop. No copy is persisted into the ambient component, so ECS
    // remains the single source of truth for both authoring inputs.
    SceneRenderAmbientRadiance result = selected->desc;
    if ((result.mode == SceneRenderAmbientRadianceMode::CapturedEnvironment ||
         result.mode == SceneRenderAmbientRadianceMode::EstimatedEnvironment) &&
        worldBackdrop.has_value()) {
        const SceneRenderWorldBackdrop& backdrop = *worldBackdrop;
        result.color = backdrop.color;
        result.horizonColor = backdrop.horizonColor;
        result.zenithColor = backdrop.zenithColor;
        if (backdrop.mode == SceneRenderWorldBackdropMode::EnvironmentMap) {
            result.environmentAssetId = backdrop.environmentAssetId;
        }
    }
    return result;
}

// Converts the batched transform system's column-major 3x4 world affine into the
// column-major 4x4 model matrix the render instance stream expects. This mapping
// is identical, element for element, to SceneTransformMatrices::Model, so the
// columnar path renders bit-identically to the per-entity path.
[[nodiscard]] std::array<float, 16> ModelFromWorldAffine3x4(const kb::scene::WorldTransformAffine3x4& affine) noexcept {
    return std::array<float, 16>{
        affine.values[0], affine.values[1], affine.values[2], 0.0F,
        affine.values[3], affine.values[4], affine.values[5], 0.0F,
        affine.values[6], affine.values[7], affine.values[8], 0.0F,
        affine.values[9], affine.values[10], affine.values[11], 1.0F,
    };
}

struct SyncContext {
    const kb::scene::Scene* scene = nullptr;
    RenderScene* renderScene = nullptr;
    EcsRenderTransformResolver* transforms = nullptr;
    SceneRenderWorldTransformReader* worldReader = nullptr;
    std::vector<std::uint64_t>* meshes = nullptr;
    std::vector<std::uint64_t>* cameras = nullptr;
    std::vector<std::uint64_t>* lights = nullptr;
    std::vector<std::uint64_t>* visibilityBlockers = nullptr;
    std::vector<std::uint64_t>* geometrySwarms = nullptr;
    std::vector<std::uint64_t>* surfaceCasts = nullptr;
    bool basicLightingEnabled = false;
};

void SyncCamera(kb::scene::SceneEntity entity, const kb::scene::TransformComponent& transform, const kb::scene::CameraComponent& camera, void* context) {
    auto* sync = static_cast<SyncContext*>(context);
    const kb::scene::TransformComponent renderTransform = sync->worldReader->Read(entity, transform);
    sync->cameras->push_back(entity.Id());
    static_cast<void>(sync->renderScene->UpsertCamera(CameraRenderProxyDesc{
        .entityId = entity.Id(),
        .position = PositionOf(renderTransform),
        .rotation = RotationOf(renderTransform),
        .projection = CameraProjectionOf(camera.projection),
        .verticalFovDegrees = camera.verticalFovDegrees,
        .orthographicHeight = camera.orthographicHeight,
        .nearClip = camera.nearClip,
        .farClip = camera.farClip,
        .primary = camera.primary,
        .visible = kb::scene::ResolveVisibility(*sync->scene, entity).visible,
        .viewportId = camera.viewportId,
        .priority = camera.priority,
        .cullingMask = camera.cullingMask,
        .clearMode = CameraClearModeOf(camera.clearMode),
        .clearColor = { camera.clearColor.x, camera.clearColor.y, camera.clearColor.z },
    }));
    static_cast<void>(transform);
}

// LIB-139/LIB-140: a runtime MaterialInstance handle wins over the authored
// materialAssetId. LIB-140 changed what "wins" means here: the RAW instance
// handle is now passed through into MeshRenderProxyDesc::materialAssetId
// unresolved (rather than being resolved to its parent asset id at sync
// time), so RuntimeMaterialResourceEnsurer can recognize it
// (scene.MaterialInstances().Exists(...)) and resolve parent + live
// parameter overrides together as one unit - if this function resolved to
// the parent asset id here instead, a SetParameterScalar/SetParameterBool
// call made after this sync would have no way to invalidate the renderer's
// already-cached-by-parent-asset-id material, since the cache key would be
// indistinguishable from a plain (non-instance) material reference.
// Released/nonexistent handles still honestly resolve to 0 (no material),
// exactly the same shape RuntimeMaterialResourceEnsurer already handles for
// an authored-but-unresolvable materialAssetId.
[[nodiscard]] std::uint64_t ResolveMaterialAssetId(const kb::scene::Scene& scene, const kb::scene::MeshRendererComponent& renderer) noexcept {
    if (renderer.materialInstanceHandle == 0U) {
        return renderer.materialAssetId;
    }
    return scene.MaterialInstances().Exists(renderer.materialInstanceHandle) ? renderer.materialInstanceHandle : 0U;
}

void SyncMesh(kb::scene::SceneEntity entity, const kb::scene::TransformComponent& transform, const kb::scene::MeshRendererComponent& renderer, void* context) {
    auto* sync = static_cast<SyncContext*>(context);
    const kb::scene::TransformComponent renderTransform = sync->worldReader->Read(entity, transform);
    std::array<std::uint64_t, kMaxSceneMaterialSlotOverrides> materialSlotAssetIds{};
    const std::uint32_t materialSlotOverrideCount = CopyMaterialSlotOverrides(renderer, materialSlotAssetIds);
    sync->meshes->push_back(entity.Id());
    const kb::scene::ResolvedVisibility visibility = kb::scene::ResolveVisibility(*sync->scene, entity);
    const kb::scene::SceneDetailSwitchComponent* detailSwitch = sync->scene->Components().DetailSwitches().TryGet(entity);
    static_cast<void>(sync->renderScene->UpsertMesh(MeshRenderProxyDesc{
        .entityId = entity.Id(),
        .meshAssetId = renderer.meshAssetId,
        .materialAssetId = ResolveMaterialAssetId(*sync->scene, renderer),
        .materialSlotAssetIds = materialSlotAssetIds,
        .materialSlotOverrideCount = materialSlotOverrideCount,
        .model = SceneTransformMatrices::Model(renderTransform),
        .color = NeutralInstanceColor(),
        .visible = visibility.visible,
        .castsShadow = renderer.castsShadow,
        .receivesShadow = renderer.receivesShadow,
        .layer = renderer.layer & visibility.mask,
        .detailSwitchGroupId = detailSwitch != nullptr ? detailSwitch->groupId : 0U,
        .detailSwitchMinimumLod = detailSwitch != nullptr ? detailSwitch->minimumLod : 0U,
        .detailSwitchMaximumLod = detailSwitch != nullptr ? detailSwitch->maximumLod : 255U,
        .detailSwitchPromoteCoverage = detailSwitch != nullptr ? detailSwitch->promoteCoverage : 0.20F,
        .detailSwitchDemoteCoverage = detailSwitch != nullptr ? detailSwitch->demoteCoverage : 0.15F,
        .detailSwitchEnabled = detailSwitch != nullptr && detailSwitch->enabled && kb::scene::IsSceneDetailSwitchComponentValid(*detailSwitch),
    }));
    static_cast<void>(transform);
}

void SyncLight(kb::scene::SceneEntity entity, const kb::scene::TransformComponent& transform, const kb::scene::LightComponent& light, void* context) {
    auto* sync = static_cast<SyncContext*>(context);
    const kb::scene::TransformComponent renderTransform = sync->worldReader->Read(entity, transform);
    sync->lights->push_back(entity.Id());
    static_cast<void>(sync->renderScene->UpsertLight(LightRenderProxyDesc{
        .entityId = entity.Id(),
        .kind = LightKindOf(light.kind),
        .position = PositionOf(renderTransform),
        .rotation = RotationOf(renderTransform),
        .color = SceneLightColor::Resolve(light),
        .intensity = light.intensity,
        .range = light.range,
        .innerConeDegrees = light.innerConeDegrees,
        .outerConeDegrees = light.outerConeDegrees,
        .areaWidth = light.areaWidth,
        .areaHeight = light.areaHeight,
        .contactShadowLength = light.contactShadowLength,
        .volumetricScattering = light.volumetricScattering,
        .castsShadow = light.castsShadow,
        .visible = kb::scene::ResolveVisibility(*sync->scene, entity).visible,
        .layer = light.layerMask,
    }));
    static_cast<void>(transform);
}

void SyncVisibilityBlocker(kb::scene::SceneEntity entity, const kb::scene::TransformComponent& transform, const kb::scene::SceneVisibilityBlockerComponent& blocker, void* context) {
    auto* sync = static_cast<SyncContext*>(context);
    if (!blocker.enabled || !kb::scene::IsSceneVisibilityBlockerComponentValid(blocker)) return;
    const kb::scene::TransformComponent renderTransform = sync->worldReader->Read(entity, transform);
    sync->visibilityBlockers->push_back(entity.Id());
    static_cast<void>(sync->renderScene->UpsertVisibilityBlocker(VisibilityBlockerRenderProxyDesc{
        .entityId = entity.Id(), .model = SceneTransformMatrices::Model(renderTransform),
        .localCenter = { blocker.localCenter.x, blocker.localCenter.y, blocker.localCenter.z },
        .size = { blocker.size.x, blocker.size.y, blocker.size.z },
    }));
}

[[nodiscard]] RenderSurfaceCastRegion SurfaceCastRegionOf(kb::scene::RegionShapeKind kind) noexcept {
    switch (kind) {
    case kb::scene::RegionShapeKind::Circle2D: return RenderSurfaceCastRegion::Circle2D;
    case kb::scene::RegionShapeKind::Rectangle2D: return RenderSurfaceCastRegion::Rectangle2D;
    case kb::scene::RegionShapeKind::Sphere: return RenderSurfaceCastRegion::Sphere;
    case kb::scene::RegionShapeKind::Box: return RenderSurfaceCastRegion::Box;
    case kb::scene::RegionShapeKind::Capsule: return RenderSurfaceCastRegion::Capsule;
    }
    return RenderSurfaceCastRegion::Box;
}

void SyncGeometrySwarm(kb::scene::SceneEntity entity, const kb::scene::GeometrySwarmComponent& swarm, void* context) {
    auto* sync = static_cast<SyncContext*>(context);
    if (!swarm.enabled || !kb::scene::IsGeometrySwarmComponentValid(swarm)) return;
    const kb::scene::TransformComponent* transform = sync->scene->Transforms().TryGet(entity);
    if (transform == nullptr) return;
    const kb::scene::TransformComponent renderTransform = sync->worldReader->Read(entity, *transform);
    sync->geometrySwarms->push_back(entity.Id());
    const kb::scene::ResolvedVisibility visibility = kb::scene::ResolveVisibility(*sync->scene, entity);
    static_cast<void>(sync->renderScene->UpsertGeometrySwarm(GeometrySwarmRenderProxyDesc{
        .entityId = entity.Id(), .meshAssetId = swarm.meshAssetId, .materialAssetId = swarm.materialAssetId,
        .model = SceneTransformMatrices::Model(renderTransform), .instanceCount = swarm.instanceCount,
        .columns = swarm.columns, .rows = swarm.rows, .layers = swarm.layers,
        .spacing = { swarm.spacing.x, swarm.spacing.y, swarm.spacing.z }, .instanceScale = swarm.instanceScale,
        .visible = visibility.visible, .castsShadow = swarm.castsShadow, .receivesShadow = swarm.receivesShadow,
        .layer = swarm.layer & visibility.mask,
    }));
}

void SyncSurfaceCast(kb::scene::SceneEntity entity, const kb::scene::SurfaceCastComponent& surfaceCast, void* context) {
    auto* sync = static_cast<SyncContext*>(context);
    if (!surfaceCast.enabled || !kb::scene::IsSurfaceCastComponentValid(surfaceCast)) return;
    const kb::scene::RegionShapeComponent* shape = sync->scene->Components().RegionShapes().TryGet(entity);
    const kb::scene::TransformComponent* transform = sync->scene->Transforms().TryGet(entity);
    if (shape == nullptr || transform == nullptr || !shape->enabled || !kb::scene::IsRegionShapeKindValid(shape->kind)) return;
    const kb::scene::TransformComponent renderTransform = sync->worldReader->Read(entity, *transform);
    sync->surfaceCasts->push_back(entity.Id());
    const kb::scene::ResolvedVisibility visibility = kb::scene::ResolveVisibility(*sync->scene, entity);
    static_cast<void>(sync->renderScene->UpsertSurfaceCast(SurfaceCastRenderProxyDesc{
        .entityId = entity.Id(), .materialAssetId = surfaceCast.materialAssetId, .model = SceneTransformMatrices::Model(renderTransform),
        .localCenter = { shape->center.x, shape->center.y, shape->center.z }, .size = { shape->size.x, shape->size.y, shape->size.z },
        .radius = shape->radius, .height = shape->height, .receiverLayerMask = surfaceCast.receiverLayerMask,
        .order = surfaceCast.order, .region = SurfaceCastRegionOf(shape->kind), .visible = visibility.visible,
    }));
}

void SyncEntity(kb::scene::SceneEntity entity, SyncContext& context) {
    if (context.scene == nullptr || context.renderScene == nullptr || context.transforms == nullptr ||
        context.meshes == nullptr || context.cameras == nullptr || context.lights == nullptr || context.visibilityBlockers == nullptr || context.geometrySwarms == nullptr || context.surfaceCasts == nullptr) {
        return;
    }

    if (!entity.IsValid() || !context.scene->Entities().IsAlive(entity)) {
        static_cast<void>(context.renderScene->RemoveMesh(entity.Id()));
        static_cast<void>(context.renderScene->RemoveCamera(entity.Id()));
        static_cast<void>(context.renderScene->RemoveLight(entity.Id()));
        static_cast<void>(context.renderScene->RemoveVisibilityBlocker(entity.Id()));
        static_cast<void>(context.renderScene->RemoveGeometrySwarm(entity.Id()));
        static_cast<void>(context.renderScene->RemoveSurfaceCast(entity.Id()));
        return;
    }

    const kb::scene::TransformComponent* transform = context.scene->Transforms().TryGet(entity);
    const kb::scene::SceneComponentQueries components = context.scene->Components();

    if (const kb::scene::CameraComponent* camera = components.Cameras().TryGet(entity); camera != nullptr && transform != nullptr) {
        SyncCamera(entity, *transform, *camera, &context);
    } else {
        static_cast<void>(context.renderScene->RemoveCamera(entity.Id()));
    }

    if (const kb::scene::MeshRendererComponent* mesh = components.MeshRenderers().TryGet(entity); mesh != nullptr && transform != nullptr) {
        SyncMesh(entity, *transform, *mesh, &context);
    } else {
        static_cast<void>(context.renderScene->RemoveMesh(entity.Id()));
    }

    if (context.basicLightingEnabled) {
        if (const kb::scene::LightComponent* light = components.Lights().TryGet(entity); light != nullptr && transform != nullptr) {
            SyncLight(entity, *transform, *light, &context);
        } else {
            static_cast<void>(context.renderScene->RemoveLight(entity.Id()));
        }
    } else {
        static_cast<void>(context.renderScene->RemoveLight(entity.Id()));
    }
    if (const kb::scene::GeometrySwarmComponent* swarm = components.GeometrySwarms().TryGet(entity); swarm != nullptr) {
        SyncGeometrySwarm(entity, *swarm, &context);
    } else {
        static_cast<void>(context.renderScene->RemoveGeometrySwarm(entity.Id()));
    }
    if (const kb::scene::SurfaceCastComponent* surfaceCast = components.SurfaceCasts().TryGet(entity); surfaceCast != nullptr) {
        SyncSurfaceCast(entity, *surfaceCast, &context);
    } else {
        static_cast<void>(context.renderScene->RemoveSurfaceCast(entity.Id()));
    }
    if (const kb::scene::SceneVisibilityBlockerComponent* blocker = components.VisibilityBlockers().TryGet(entity); blocker != nullptr && transform != nullptr && blocker->enabled && kb::scene::IsSceneVisibilityBlockerComponentValid(*blocker)) {
        SyncVisibilityBlocker(entity, *transform, *blocker, &context);
    } else {
        static_cast<void>(context.renderScene->RemoveVisibilityBlocker(entity.Id()));
    }
}

} // namespace

void EcsRenderSceneSynchronizer::Reserve(const EcsRenderSceneSynchronizerReserveDesc& desc) {
    if (desc.meshProxies > 0U) {
        seenMeshes_.reserve(desc.meshProxies);
    }
    if (desc.cameraProxies > 0U) {
        seenCameras_.reserve(desc.cameraProxies);
    }
    if (desc.lightProxies > 0U) {
        seenLights_.reserve(desc.lightProxies);
    }
    if (desc.visibilityBlockerProxies > 0U) seenVisibilityBlockers_.reserve(desc.visibilityBlockerProxies);
    if (desc.geometrySwarmProxies > 0U) seenGeometrySwarms_.reserve(desc.geometrySwarmProxies);
    if (desc.surfaceCastProxies > 0U) seenSurfaceCasts_.reserve(desc.surfaceCastProxies);
    if (desc.transformCacheEntries > 0U) {
        transformCache_.reserve(desc.transformCacheEntries);
    }
    if (desc.transformResolvingEntries > 0U) {
        transformResolving_.reserve(desc.transformResolvingEntries);
    }
    if (desc.transformUpdateEntities > 0U) {
        transformUpdateEntities_.reserve(desc.transformUpdateEntities);
    }
}

void EcsRenderSceneSynchronizer::Sync(const kb::scene::Scene& scene, RenderScene& renderScene) const {
    seenMeshes_.clear();
    seenCameras_.clear();
    seenLights_.clear();
    seenVisibilityBlockers_.clear();
    seenGeometrySwarms_.clear();
    seenSurfaceCasts_.clear();
    transformCache_.clear();
    transformResolving_.clear();

    EcsRenderTransformResolver transforms{ scene, transformCache_, transformResolving_ };
    SceneRenderWorldTransformReader worldReader{ transforms };
    SyncContext context{
        .scene = &scene,
        .renderScene = &renderScene,
        .transforms = &transforms,
        .worldReader = &worldReader,
        .meshes = &seenMeshes_,
        .cameras = &seenCameras_,
        .lights = &seenLights_,
        .visibilityBlockers = &seenVisibilityBlockers_,
        .geometrySwarms = &seenGeometrySwarms_,
        .surfaceCasts = &seenSurfaceCasts_,
        .basicLightingEnabled = kb::scene::SceneLightingAccess::BasicLightingEnabled(scene),
    };

    const kb::scene::SceneComponentVisitors visitors = scene.Components().Visitors();
    visitors.ForEachCamera(&SyncCamera, &context);
    visitors.ForEachMeshRenderer(&SyncMesh, &context);
    scene.Components().GeometrySwarms().ForEach(&SyncGeometrySwarm, &context);
    scene.Components().SurfaceCasts().ForEach(&SyncSurfaceCast, &context);
    if (context.basicLightingEnabled) {
        visitors.ForEachLight(&SyncLight, &context);
    }
    kb::ecs::Query<kb::scene::SceneVisibilityBlockerComponent, kb::scene::TransformComponent> blockers = const_cast<kb::scene::Scene&>(scene).Runtime().EcsWorld().CreateQuery<kb::scene::SceneVisibilityBlockerComponent, kb::scene::TransformComponent>();
    kb::ecs::UnsafeHotReadQuery<kb::scene::SceneVisibilityBlockerComponent, kb::scene::TransformComponent> hotBlockers;
    kb::ecs::QueryExecutionSettings blockerSettings{};
    blockerSettings.policy = kb::ecs::QueryExecutionPolicy::SingleThread;
    if (blockers.IsValid() && hotBlockers.Rebuild(blockers, blockerSettings)) {
        hotBlockers.ForEachRange(blockerSettings.maxBatchSize, [&context](const auto& batch) {
            const auto* components = batch.template Components<0>();
            const auto* transforms = batch.template Components<1>();
            for (std::size_t index = 0U; index < batch.Count(); ++index) SyncVisibilityBlocker(batch.EntityAt(index), transforms[index], components[index], &context);
        });
    }
    transformPrecomputedReadCount_ = worldReader.PrecomputedReadCount();
    transformResolvedFallbackCount_ = worldReader.ResolvedFallbackCount();

    std::ranges::sort(seenMeshes_);
    std::ranges::sort(seenCameras_);
    std::ranges::sort(seenLights_);
    std::ranges::sort(seenVisibilityBlockers_);
    std::ranges::sort(seenGeometrySwarms_);
    std::ranges::sort(seenSurfaceCasts_);
    static_cast<void>(renderScene.RemoveMeshesNotInSorted(std::span<const std::uint64_t>{ seenMeshes_ }));
    static_cast<void>(renderScene.RemoveCamerasNotInSorted(std::span<const std::uint64_t>{ seenCameras_ }));
    static_cast<void>(renderScene.RemoveLightsNotInSorted(std::span<const std::uint64_t>{ seenLights_ }));
    static_cast<void>(renderScene.RemoveVisibilityBlockersNotInSorted(std::span<const std::uint64_t>{ seenVisibilityBlockers_ }));
    static_cast<void>(renderScene.RemoveGeometrySwarmsNotInSorted(std::span<const std::uint64_t>{ seenGeometrySwarms_ }));
    static_cast<void>(renderScene.RemoveSurfaceCastsNotInSorted(std::span<const std::uint64_t>{ seenSurfaceCasts_ }));
    const std::optional<SceneRenderWorldBackdrop> worldBackdrop = ResolveWorldBackdrop(scene);
    renderScene.SetWorldBackdrop(worldBackdrop);
    renderScene.SetAmbientRadiance(ResolveAmbientRadiance(scene, worldBackdrop));
}

void EcsRenderSceneSynchronizer::SyncEntities(
    const kb::scene::Scene& scene,
    RenderScene& renderScene,
    std::span<const std::uint64_t> entityIds) const {
    seenMeshes_.clear();
    seenCameras_.clear();
    seenLights_.clear();
    seenVisibilityBlockers_.clear();
    seenGeometrySwarms_.clear();
    seenSurfaceCasts_.clear();
    transformCache_.clear();
    transformResolving_.clear();

    EcsRenderTransformResolver transforms{ scene, transformCache_, transformResolving_ };
    SceneRenderWorldTransformReader worldReader{ transforms };
    SyncContext context{
        .scene = &scene,
        .renderScene = &renderScene,
        .transforms = &transforms,
        .worldReader = &worldReader,
        .meshes = &seenMeshes_,
        .cameras = &seenCameras_,
        .lights = &seenLights_,
        .visibilityBlockers = &seenVisibilityBlockers_,
        .geometrySwarms = &seenGeometrySwarms_,
        .surfaceCasts = &seenSurfaceCasts_,
        .basicLightingEnabled = kb::scene::SceneLightingAccess::BasicLightingEnabled(scene),
    };

    for (const std::uint64_t entityId : entityIds) {
        SyncEntity(kb::scene::SceneEntity{ entityId }, context);
    }
    transformPrecomputedReadCount_ = worldReader.PrecomputedReadCount();
    transformResolvedFallbackCount_ = worldReader.ResolvedFallbackCount();
    const std::optional<SceneRenderWorldBackdrop> worldBackdrop = ResolveWorldBackdrop(scene);
    renderScene.SetWorldBackdrop(worldBackdrop);
    renderScene.SetAmbientRadiance(ResolveAmbientRadiance(scene, worldBackdrop));
}

void EcsRenderSceneSynchronizer::SyncMeshWorldAffines(
    RenderScene& renderScene,
    std::span<const kb::scene::SceneEntity> entities,
    std::span<const kb::scene::WorldTransformAffine3x4> worldAffines) const {
    const std::size_t count = std::min(entities.size(), worldAffines.size());
    for (std::size_t index = 0; index < count; ++index) {
        const std::array<float, 16> model = ModelFromWorldAffine3x4(worldAffines[index]);
        // Returns false for entities without a mesh proxy (cameras, lights); those
        // are handled by the structural sync, so skipping them here is correct.
        static_cast<void>(renderScene.UpdateMeshTransform(entities[index].Id(), model));
        static_cast<void>(renderScene.UpdateVisibilityBlockerTransform(entities[index].Id(), model));
        static_cast<void>(renderScene.UpdateGeometrySwarmTransform(entities[index].Id(), model));
        static_cast<void>(renderScene.UpdateSurfaceCastTransform(entities[index].Id(), model));
    }
}

void EcsRenderSceneSynchronizer::SyncMeshWorldAffinesParallel(
    RenderScene& renderScene,
    std::span<const kb::scene::SceneEntity> entities,
    std::span<const kb::scene::WorldTransformAffine3x4> worldAffines,
    kb::ecs::WorkerPool& workerPool,
    std::size_t grainSize) const {
    const std::size_t count = std::min(entities.size(), worldAffines.size());
    if (count == 0U) {
        return;
    }

    std::atomic<std::uint64_t> inPlace{ 0U };
    std::atomic<std::uint64_t> fallback{ 0U };
    std::atomic<bool> geometrySwarmChanged{ false };
    const std::size_t resolvedGrain = grainSize == 0U ? count : grainSize;
    workerPool.ParallelForChunks(count, resolvedGrain, [&renderScene, entities, worldAffines, &inPlace, &fallback, &geometrySwarmChanged](
        kb::ecs::WorkerContext, const kb::ecs::WorkerPoolChunk& chunk) {
        std::uint64_t localInPlace = 0U;
        std::uint64_t localFallback = 0U;
        const std::size_t end = chunk.begin + chunk.count;
        for (std::size_t index = chunk.begin; index < end; ++index) {
            const std::array<float, 16> model = ModelFromWorldAffine3x4(worldAffines[index]);
            static_cast<void>(renderScene.UpdateVisibilityBlockerTransform(entities[index].Id(), model));
            if (renderScene.ApplyGeometrySwarmTransform(entities[index].Id(), model)) {
                geometrySwarmChanged.store(true, std::memory_order_relaxed);
            }
            switch (renderScene.ApplyMeshTransform(entities[index].Id(), model)) {
            case RenderScene::TransformUpdateOutcome::InPlace:
                ++localInPlace;
                break;
            case RenderScene::TransformUpdateOutcome::Fallback:
                ++localFallback;
                break;
            case RenderScene::TransformUpdateOutcome::NotFound:
                break;
            }
        }
        inPlace.fetch_add(localInPlace, std::memory_order_relaxed);
        fallback.fetch_add(localFallback, std::memory_order_relaxed);
    });

    const std::uint64_t fallbackCount = fallback.load(std::memory_order_relaxed);
    renderScene.InvalidateDrawGroupsIfFallback(
        fallbackCount > 0U ? RenderScene::TransformUpdateOutcome::Fallback : RenderScene::TransformUpdateOutcome::InPlace);
    if (geometrySwarmChanged.load(std::memory_order_relaxed)) {
        renderScene.InvalidateDrawGroupsIfFallback(RenderScene::TransformUpdateOutcome::Fallback);
    }
    renderScene.AddTransformUpdateCounts(inPlace.load(std::memory_order_relaxed), fallbackCount);
}

void EcsRenderSceneSynchronizer::SyncTransformUpdates(const kb::scene::Scene& scene, RenderScene& renderScene) const {
    transformUpdateEntities_.clear();
    const std::span<const kb::scene::SceneEntity> entities = scene.Runtime().TransformRenderProxyUpdateEntities();
    transformUpdateEntities_.reserve(entities.size());
    for (const kb::scene::SceneEntity entity : entities) {
        transformUpdateEntities_.push_back(entity.Id());
    }
    SyncEntities(scene, renderScene, std::span<const std::uint64_t>{ transformUpdateEntities_ });
}

void EcsRenderSceneSynchronizer::SyncMeshRendererUpdates(const kb::scene::Scene& scene, RenderScene& renderScene) const {
    transformUpdateEntities_.clear();
    const std::span<const kb::scene::SceneEntity> entities = scene.Runtime().MeshRendererRenderProxyUpdateEntities();
    transformUpdateEntities_.reserve(entities.size());
    for (const kb::scene::SceneEntity entity : entities) {
        transformUpdateEntities_.push_back(entity.Id());
    }
    SyncEntities(scene, renderScene, std::span<const std::uint64_t>{ transformUpdateEntities_ });
}

EcsRenderSceneSynchronizerStats EcsRenderSceneSynchronizer::Stats() const noexcept {
    return EcsRenderSceneSynchronizerStats{
        .meshSeenCount = static_cast<std::uint32_t>(seenMeshes_.size()),
        .cameraSeenCount = static_cast<std::uint32_t>(seenCameras_.size()),
        .lightSeenCount = static_cast<std::uint32_t>(seenLights_.size()),
        .visibilityBlockerSeenCount = static_cast<std::uint32_t>(seenVisibilityBlockers_.size()),
        .geometrySwarmSeenCount = static_cast<std::uint32_t>(seenGeometrySwarms_.size()),
        .surfaceCastSeenCount = static_cast<std::uint32_t>(seenSurfaceCasts_.size()),
        .meshSeenCapacity = static_cast<std::uint32_t>(seenMeshes_.capacity()),
        .cameraSeenCapacity = static_cast<std::uint32_t>(seenCameras_.capacity()),
        .lightSeenCapacity = static_cast<std::uint32_t>(seenLights_.capacity()),
        .visibilityBlockerSeenCapacity = static_cast<std::uint32_t>(seenVisibilityBlockers_.capacity()),
        .geometrySwarmSeenCapacity = static_cast<std::uint32_t>(seenGeometrySwarms_.capacity()),
        .surfaceCastSeenCapacity = static_cast<std::uint32_t>(seenSurfaceCasts_.capacity()),
        .transformCacheCount = static_cast<std::uint32_t>(transformCache_.size()),
        .transformResolvingCount = static_cast<std::uint32_t>(transformResolving_.size()),
        .transformUpdateEntityCount = static_cast<std::uint32_t>(transformUpdateEntities_.size()),
        .transformCacheCapacity = static_cast<std::uint32_t>(transformCache_.bucket_count()),
        .transformResolvingCapacity = static_cast<std::uint32_t>(transformResolving_.bucket_count()),
        .transformUpdateEntityCapacity = static_cast<std::uint32_t>(transformUpdateEntities_.capacity()),
        .transformPrecomputedReadCount = static_cast<std::uint32_t>(transformPrecomputedReadCount_),
        .transformResolvedFallbackCount = static_cast<std::uint32_t>(transformResolvedFallbackCount_),
    };
}

} // namespace kb::render
