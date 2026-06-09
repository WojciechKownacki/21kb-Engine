#include "kb/render/scene/EcsRenderSceneSynchronizer.hpp"

#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponentVisitors.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "engine/scene/VisibilityComponent.hpp"
#include "kb/render/scene/RenderScene.hpp"
#include "scene/EcsRenderTransformResolver.hpp"
#include "scene/SceneTransformMatrices.hpp"

#include <algorithm>
#include <array>
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

struct SyncContext {
    const kb::scene::Scene* scene = nullptr;
    RenderScene* renderScene = nullptr;
    EcsRenderTransformResolver* transforms = nullptr;
    std::vector<std::uint64_t>* meshes = nullptr;
    std::vector<std::uint64_t>* cameras = nullptr;
    std::vector<std::uint64_t>* lights = nullptr;
};

void SyncCamera(kb::scene::SceneEntity entity, const kb::scene::TransformComponent& transform, const kb::scene::CameraComponent& camera, void* context) {
    auto* sync = static_cast<SyncContext*>(context);
    const kb::scene::TransformComponent renderTransform = sync->transforms->Resolve(entity);
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
    }));
    static_cast<void>(transform);
}

void SyncMesh(kb::scene::SceneEntity entity, const kb::scene::TransformComponent& transform, const kb::scene::MeshRendererComponent& renderer, void* context) {
    auto* sync = static_cast<SyncContext*>(context);
    const kb::scene::TransformComponent renderTransform = sync->transforms->Resolve(entity);
    std::array<std::uint64_t, kMaxSceneMaterialSlotOverrides> materialSlotAssetIds{};
    const std::uint32_t materialSlotOverrideCount = CopyMaterialSlotOverrides(renderer, materialSlotAssetIds);
    sync->meshes->push_back(entity.Id());
    static_cast<void>(sync->renderScene->UpsertMesh(MeshRenderProxyDesc{
        .entityId = entity.Id(),
        .meshAssetId = renderer.meshAssetId,
        .materialAssetId = renderer.materialAssetId,
        .materialSlotAssetIds = materialSlotAssetIds,
        .materialSlotOverrideCount = materialSlotOverrideCount,
        .model = SceneTransformMatrices::Model(renderTransform),
        .color = NeutralInstanceColor(),
        .visible = IsVisible(*sync->scene, entity),
        .castsShadow = renderer.castsShadow,
        .receivesShadow = renderer.receivesShadow,
    }));
    static_cast<void>(transform);
}

void SyncLight(kb::scene::SceneEntity entity, const kb::scene::TransformComponent& transform, const kb::scene::LightComponent& light, void* context) {
    auto* sync = static_cast<SyncContext*>(context);
    const kb::scene::TransformComponent renderTransform = sync->transforms->Resolve(entity);
    sync->lights->push_back(entity.Id());
    static_cast<void>(sync->renderScene->UpsertLight(LightRenderProxyDesc{
        .entityId = entity.Id(),
        .kind = LightKindOf(light.kind),
        .position = PositionOf(renderTransform),
        .rotation = RotationOf(renderTransform),
        .color = { light.color.x, light.color.y, light.color.z },
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
    }));
    static_cast<void>(transform);
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
}

void EcsRenderSceneSynchronizer::Sync(const kb::scene::Scene& scene, RenderScene& renderScene) const {
    seenMeshes_.clear();
    seenCameras_.clear();
    seenLights_.clear();
    transformCache_.clear();
    transformResolving_.clear();

    EcsRenderTransformResolver transforms{ scene, transformCache_, transformResolving_ };
    SyncContext context{
        .scene = &scene,
        .renderScene = &renderScene,
        .transforms = &transforms,
        .meshes = &seenMeshes_,
        .cameras = &seenCameras_,
        .lights = &seenLights_,
    };

    const kb::scene::SceneComponentVisitors visitors = scene.Components().Visitors();
    visitors.ForEachCamera(&SyncCamera, &context);
    visitors.ForEachMeshRenderer(&SyncMesh, &context);
    visitors.ForEachLight(&SyncLight, &context);

    std::ranges::sort(seenMeshes_);
    std::ranges::sort(seenCameras_);
    std::ranges::sort(seenLights_);
    static_cast<void>(renderScene.RemoveMeshesNotInSorted(std::span<const std::uint64_t>{ seenMeshes_ }));
    static_cast<void>(renderScene.RemoveCamerasNotInSorted(std::span<const std::uint64_t>{ seenCameras_ }));
    static_cast<void>(renderScene.RemoveLightsNotInSorted(std::span<const std::uint64_t>{ seenLights_ }));
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
        .transformCacheCapacity = static_cast<std::uint32_t>(transformCache_.bucket_count()),
        .transformResolvingCapacity = static_cast<std::uint32_t>(transformResolving_.bucket_count()),
    };
}

} // namespace kb::render
