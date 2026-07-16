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
#include "engine/scene/TransformComponent.hpp"
#include "engine/scene/VisibilityComponent.hpp"
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

[[nodiscard]] bool IsVisible(const kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
    const kb::scene::VisibilityComponent* visibility = scene.Components().Visibility().TryGet(entity);
    return visibility == nullptr || visibility->visible;
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
        .visible = IsVisible(*sync->scene, entity),
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
    static_cast<void>(sync->renderScene->UpsertMesh(MeshRenderProxyDesc{
        .entityId = entity.Id(),
        .meshAssetId = renderer.meshAssetId,
        .materialAssetId = ResolveMaterialAssetId(*sync->scene, renderer),
        .materialSlotAssetIds = materialSlotAssetIds,
        .materialSlotOverrideCount = materialSlotOverrideCount,
        .model = SceneTransformMatrices::Model(renderTransform),
        .color = NeutralInstanceColor(),
        .visible = IsVisible(*sync->scene, entity),
        .castsShadow = renderer.castsShadow,
        .receivesShadow = renderer.receivesShadow,
        .layer = renderer.layer,
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
        .visible = IsVisible(*sync->scene, entity),
        .layer = light.layerMask,
    }));
    static_cast<void>(transform);
}

void SyncEntity(kb::scene::SceneEntity entity, SyncContext& context) {
    if (context.scene == nullptr || context.renderScene == nullptr || context.transforms == nullptr ||
        context.meshes == nullptr || context.cameras == nullptr || context.lights == nullptr) {
        return;
    }

    if (!entity.IsValid() || !context.scene->Entities().IsAlive(entity)) {
        static_cast<void>(context.renderScene->RemoveMesh(entity.Id()));
        static_cast<void>(context.renderScene->RemoveCamera(entity.Id()));
        static_cast<void>(context.renderScene->RemoveLight(entity.Id()));
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
        .basicLightingEnabled = kb::scene::SceneLightingAccess::BasicLightingEnabled(scene),
    };

    const kb::scene::SceneComponentVisitors visitors = scene.Components().Visitors();
    visitors.ForEachCamera(&SyncCamera, &context);
    visitors.ForEachMeshRenderer(&SyncMesh, &context);
    if (context.basicLightingEnabled) {
        visitors.ForEachLight(&SyncLight, &context);
    }
    transformPrecomputedReadCount_ = worldReader.PrecomputedReadCount();
    transformResolvedFallbackCount_ = worldReader.ResolvedFallbackCount();

    std::ranges::sort(seenMeshes_);
    std::ranges::sort(seenCameras_);
    std::ranges::sort(seenLights_);
    static_cast<void>(renderScene.RemoveMeshesNotInSorted(std::span<const std::uint64_t>{ seenMeshes_ }));
    static_cast<void>(renderScene.RemoveCamerasNotInSorted(std::span<const std::uint64_t>{ seenCameras_ }));
    static_cast<void>(renderScene.RemoveLightsNotInSorted(std::span<const std::uint64_t>{ seenLights_ }));
}

void EcsRenderSceneSynchronizer::SyncEntities(
    const kb::scene::Scene& scene,
    RenderScene& renderScene,
    std::span<const std::uint64_t> entityIds) const {
    seenMeshes_.clear();
    seenCameras_.clear();
    seenLights_.clear();
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
        .basicLightingEnabled = kb::scene::SceneLightingAccess::BasicLightingEnabled(scene),
    };

    for (const std::uint64_t entityId : entityIds) {
        SyncEntity(kb::scene::SceneEntity{ entityId }, context);
    }
    transformPrecomputedReadCount_ = worldReader.PrecomputedReadCount();
    transformResolvedFallbackCount_ = worldReader.ResolvedFallbackCount();
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
    const std::size_t resolvedGrain = grainSize == 0U ? count : grainSize;
    workerPool.ParallelForChunks(count, resolvedGrain, [&renderScene, entities, worldAffines, &inPlace, &fallback](
        kb::ecs::WorkerContext, const kb::ecs::WorkerPoolChunk& chunk) {
        std::uint64_t localInPlace = 0U;
        std::uint64_t localFallback = 0U;
        const std::size_t end = chunk.begin + chunk.count;
        for (std::size_t index = chunk.begin; index < end; ++index) {
            const std::array<float, 16> model = ModelFromWorldAffine3x4(worldAffines[index]);
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
        .meshSeenCapacity = static_cast<std::uint32_t>(seenMeshes_.capacity()),
        .cameraSeenCapacity = static_cast<std::uint32_t>(seenCameras_.capacity()),
        .lightSeenCapacity = static_cast<std::uint32_t>(seenLights_.capacity()),
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
