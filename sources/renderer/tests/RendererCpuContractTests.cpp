#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/ecs/World.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneLightingAccess.hpp"
#include "engine/scene/SceneMaterialInstances.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/WorldBackdropComponent.hpp"
#include "engine/scene/AmbientRadianceComponent.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/RenderSurface.hpp"
#include "kb/render/DisplayConfig.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMeshAssetBuilder.hpp"
#include "kb/render/resources/RenderMeshAssetLoader.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"
#include "kb/render/runtime/RuntimeMaterialResolver.hpp"
#include "kb/render/scene/EcsRenderSceneSynchronizer.hpp"
#include "kb/render/scene/RenderScene.hpp"

#include <flecs.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {
using kb::scene::Scene;
using kb::scene::SceneEntity;
using kb::scene::TransformComponent;
using kb::render::EcsRenderSceneSynchronizer;
using kb::render::RenderScene;

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

TransformComponent At(float x, float y = 0.0F, float z = 0.0F) {
    return TransformComponent{
        .localPosition = { x, y, z }, .worldPosition = { x, y, z }, .worldDirty = false,
    };
}

SceneEntity Create(Scene& scene, float x = 0.0F, float y = 0.0F, float z = 0.0F) {
    return scene.Entities().CreateEntity({ .name = "Renderer contract", .transform = At(x, y, z) });
}

enum class Kind { Camera, Light, Mesh, Blocker, Swarm, Cast, Stroke, Panel };
constexpr std::array kinds{ Kind::Camera, Kind::Light, Kind::Mesh, Kind::Blocker,
    Kind::Swarm, Kind::Cast, Kind::Stroke, Kind::Panel };

void Add(Scene& scene, SceneEntity entity, Kind kind) {
    auto components = scene.Components();
    switch (kind) {
    case Kind::Camera:
        components.Cameras().Set(entity, { .verticalFovDegrees = 73.0F, .primary = true,
            .viewportId = 0U, .priority = 7, .cullingMask = 7U,
            .clearMode = kb::scene::CameraClearMode::DepthOnly, .clearColor = { 0.25F, 0.5F, 0.75F } });
        break;
    case Kind::Light:
        components.Lights().Set(entity, { .intensity = 4.5F });
        break;
    case Kind::Mesh:
        components.MeshRenderers().Set(entity, { .meshAssetId = 42U, .materialAssetId = 7U });
        break;
    case Kind::Blocker:
        components.VisibilityBlockers().Set(entity, {});
        break;
    case Kind::Swarm:
        components.GeometrySwarms().Set(entity, { .meshAssetId = 43U, .materialAssetId = 8U,
            .instanceCount = 2U, .columns = 2U, .enabled = true });
        break;
    case Kind::Cast:
        components.RegionShapes().Set(entity, {});
        components.SurfaceCasts().Set(entity, { .materialAssetId = 10U, .enabled = true });
        break;
    case Kind::Stroke: {
        kb::scene::GuideCurveComponent curve{};
        curve.controlPointCount = 2U;
        curve.controlPoints[0] = { -0.5F, 0.0F, 0.0F };
        curve.controlPoints[1] = { 0.5F, 0.0F, 0.0F };
        components.GuideCurves().Set(entity, curve);
        components.SpaceStrokes().Set(entity, { .meshAssetId = 44U, .materialAssetId = 9U,
            .mode = kb::scene::SpaceStrokeMode::Beam, .width = 0.2F, .enabled = true });
        break;
    }
    case Kind::Panel:
        components.MeshRenderers().Set(entity, { .meshAssetId = 42U, .materialAssetId = 7U });
        components.FacingPanels().Set(entity, { .mode = kb::scene::FacingPanelMode::View, .enabled = true });
        break;
    }
}

bool Has(const RenderScene& render, SceneEntity entity, Kind kind) {
    switch (kind) {
    case Kind::Camera: return render.FindCameraByEntity(entity.Id()) != nullptr;
    case Kind::Light: return render.FindLightByEntity(entity.Id()) != nullptr;
    case Kind::Mesh:
    case Kind::Panel: return render.FindMeshByEntity(entity.Id()) != nullptr;
    case Kind::Blocker: return render.VisibilityBlockerProxies().contains(entity.Id());
    case Kind::Swarm: return render.GeometrySwarmProxies().contains(entity.Id());
    case Kind::Cast: return render.SurfaceCastProxies().contains(entity.Id());
    case Kind::Stroke: return render.SpaceStrokeProxies().contains(entity.Id());
    }
    return false;
}

void Counts(const RenderScene& render, bool populated) {
    Require(render.MeshProxyCount() == (populated ? 2U : 0U)
        && render.CameraProxyCount() == (populated ? 1U : 0U)
        && render.LightProxyCount() == (populated ? 1U : 0U)
        && render.VisibilityBlockerProxyCount() == (populated ? 1U : 0U)
        && render.GeometrySwarmProxyCount() == (populated ? 1U : 0U)
        && render.SurfaceCastProxyCount() == (populated ? 1U : 0U)
        && render.SpaceStrokeProxyCount() == (populated ? 1U : 0U),
        "Reconciliation lost or manufactured one of the seven proxy families");
}

void Position(const std::array<float, 16U>& model, float x, float y, float z) {
    Require(model[12] == x && model[13] == y && model[14] == z,
        "Renderer model did not consume the requested world translation");
}

void RunAllProxyLifecycle() {
    Scene scene;
    kb::scene::SceneLightingAccess::SetBasicLightingEnabled(scene, true);
    EcsRenderSceneSynchronizer sync;
    RenderScene render;
    std::array<SceneEntity, kinds.size()> entities{};
    for (std::size_t index = 0U; index < kinds.size(); ++index) {
        entities[index] = Create(scene, static_cast<float>(index), 1.0F, index == 0U ? -5.0F : 0.0F);
        Add(scene, entities[index], kinds[index]);
    }
    std::vector<std::uint64_t> ids;
    for (SceneEntity entity : entities) { ids.push_back(entity.Id()); ids.push_back(entity.Id()); }
    sync.SyncEntities(scene, render, ids); // Every map begins empty; earlier IDs create entries.
    Counts(render, true);
    for (std::size_t index = 0U; index < kinds.size(); ++index)
        Require(Has(render, entities[index], kinds[index]), "Incremental all-kind sync omitted an entity");
    const auto* camera = render.FindCameraByEntity(entities[0].Id());
    Require(camera->desc.verticalFovDegrees == 73.0F && camera->desc.viewportId == 0U
        && camera->desc.priority == 7 && camera->desc.cullingMask == 7U
        && camera->desc.clearMode == kb::render::RenderCameraClearMode::DepthOnly
        && camera->desc.clearColor == std::array<float, 3U>{ 0.25F, 0.5F, 0.75F },
        "Camera settings changed during all-kind entity reconciliation");
    Require(render.FindPrimaryCameraProxy(5U)->entityId == entities[0].Id()
        && render.FindPrimaryCameraProxy(99U)->entityId == entities[0].Id(), "Any-viewport camera selection changed");
    Require(render.FindLightByEntity(entities[1].Id())->desc.intensity == 4.5F, "Light property was lost");
    const auto* panel = render.FindMeshByEntity(entities[7].Id());
    Require(panel->desc.model[10] < 0.0F, "Facing panel did not consume the primary camera");
    Require(scene.Transforms().TryGet(entities[7])->worldRotation.w == 1.0F,
        "Facing panel changed the canonical ECS rotation");
    const SceneEntity targetedCamera = Create(scene, 0.0F, 0.0F, -10.0F);
    scene.Components().Cameras().Set(targetedCamera, { .primary = true, .viewportId = 5U, .priority = 10 });
    const std::array targetedId{ targetedCamera.Id() };
    sync.SyncEntities(scene, render, targetedId);
    Require(render.FindPrimaryCameraProxy(5U)->entityId == targetedCamera.Id()
        && render.FindPrimaryCameraProxy(99U)->entityId == entities[0].Id(),
        "Targeted camera priority or any-viewport fallback changed");
    scene.Entities().Destroy(targetedCamera);
    sync.SyncEntities(scene, render, targetedId);
    Counts(render, true);

    // A plain batch still reconciles global policies and must not manufacture local proxies.
    const SceneEntity plain = Create(scene);
    scene.Components().WorldBackdrops().Set(plain, { .color = { 0.25F, 0.5F, 0.75F } });
    scene.Components().AmbientRadiances().Set(plain, { .intensity = 2.5F });
    const auto setVersion = render.MeshSetVersion();
    const std::array plainIds{ plain.Id(), plain.Id() };
    sync.SyncEntities(scene, render, plainIds);
    Counts(render, true);
    Require(render.WorldBackdrop().has_value() && render.AmbientRadiance().has_value(),
        "A plain entity batch skipped global rendering policies");
    Require(render.MeshSetVersion() == setVersion, "A plain batch changed the mesh proxy set");

    const std::array lightId{ entities[1].Id() };
    kb::scene::SceneLightingAccess::SetBasicLightingEnabled(scene, false);
    sync.SyncEntities(scene, render, lightId);
    Require(!Has(render, entities[1], Kind::Light), "Disabled basic lighting retained a light proxy");
    kb::scene::SceneLightingAccess::SetBasicLightingEnabled(scene, true);
    sync.SyncEntities(scene, render, lightId);
    Counts(render, true);

    // Every old proxy is removed by its full handle, while its recycled replacement survives.
    for (std::size_t index = 0U; index < kinds.size(); ++index) {
        const SceneEntity old = entities[index];
        scene.Entities().Destroy(old);
        const SceneEntity next = Create(scene, static_cast<float>(index) + 20.0F, 2.0F, 0.0F);
        Require(next != old && ecs_strip_generation(next.Id()) == ecs_strip_generation(old.Id()),
            "All-kind fixture did not exercise generation reuse at the same entity index");
        Add(scene, next, kinds[index]);
        const std::array replaced{ old.Id(), next.Id(), old.Id(), next.Id() };
        sync.SyncEntities(scene, render, replaced);
        Require(!Has(render, old, kinds[index]) && Has(render, next, kinds[index]),
            "Old-generation reconciliation removed or aliased a replacement proxy");
        entities[index] = next;
        Counts(render, true);
    }

    // Missing Transform removes all consumers, including helpers with their own Transform read.
    auto& world = scene.Runtime().EcsWorld();
    ids.clear();
    for (SceneEntity entity : entities) {
        world.Remove<TransformComponent>(entity);
        ids.push_back(entity.Id());
    }
    sync.SyncEntities(scene, render, ids);
    Counts(render, false);
    sync.SyncEntities(scene, render, ids); // Current empty maps must remain safe on repeated removal.
    Counts(render, false);
    for (std::size_t index = 0U; index < kinds.size(); ++index) {
        scene.Transforms().Set(entities[index], At(static_cast<float>(index), 1.0F, -2.0F));
    }
    sync.SyncEntities(scene, render, ids);
    Counts(render, true);
    scene.Components().VisibilityBlockers().Remove(entities[3]);
    scene.Components().GeometrySwarms().Remove(entities[4]);
    scene.Components().RegionShapes().Remove(entities[5]);
    scene.Components().GuideCurves().Remove(entities[6]);
    const std::array invalidated{ entities[3].Id(), entities[4].Id(), entities[5].Id(), entities[6].Id() };
    sync.SyncEntities(scene, render, invalidated);
    Require(render.VisibilityBlockerProxyCount() == 0U && render.GeometrySwarmProxyCount() == 0U
        && render.SurfaceCastProxyCount() == 0U && render.SpaceStrokeProxyCount() == 0U,
        "Removing a required all-kind component retained its renderer-derived proxy");
}

void RunInheritedAndNativeOnly() {
    Scene scene;
    EcsRenderSceneSynchronizer sync;
    RenderScene render;
    auto& world = scene.Runtime().EcsWorld();
    auto* backend = world.NativeHandle();
    const auto meshId = world.RegisterComponent<kb::scene::MeshRendererComponent>();
    const auto cameraId = world.RegisterComponent<kb::scene::CameraComponent>();
    const auto transformId = world.RegisterComponent<TransformComponent>();
    for (auto component : { meshId, cameraId, transformId }) ecs_add_pair(backend, component, EcsOnInstantiate, EcsInherit);
    const ecs_entity_t base = ecs_new(backend);
    ecs_add_id(backend, base, EcsPrefab);
    const auto setBase = [&](float x, std::uint64_t asset, float fov) {
        const auto transform = At(x, 2.0F, 3.0F);
        const kb::scene::MeshRendererComponent mesh{ .meshAssetId = asset, .materialAssetId = 7U };
        const kb::scene::CameraComponent camera{ .verticalFovDegrees = fov, .primary = true };
        ecs_set_id(backend, base, transformId, sizeof(transform), &transform);
        ecs_set_id(backend, base, meshId, sizeof(mesh), &mesh);
        ecs_set_id(backend, base, cameraId, sizeof(camera), &camera);
    };
    setBase(12.0F, 42U, 61.0F);
    const SceneEntity child = Create(scene);
    world.Remove<TransformComponent>(child);
    ecs_add_pair(backend, ecs_strip_generation(child.Id()), EcsIsA, base);
    const std::array childId{ child.Id(), child.Id() };
    sync.SyncEntities(scene, render, childId);
    Require(render.FindMeshByEntity(child.Id()) != nullptr && render.FindCameraByEntity(child.Id()) != nullptr,
        "SyncEntities discarded inherited backend components absent from native archetype/masks");
    Position(render.FindMeshByEntity(child.Id())->desc.model, 12.0F, 2.0F, 3.0F);
    Require(render.FindCameraByEntity(child.Id())->desc.verticalFovDegrees == 61.0F,
        "Inherited camera property was lost");
    setBase(22.0F, 43U, 81.0F);
    sync.SyncEntities(scene, render, childId);
    Require(render.FindMeshByEntity(child.Id())->desc.meshAssetId == 43U
        && render.FindCameraByEntity(child.Id())->desc.verticalFovDegrees == 81.0F,
        "A base property edit was hidden by a native absence certificate");
    Position(render.FindMeshByEntity(child.Id())->desc.model, 22.0F, 2.0F, 3.0F);
    scene.Components().MeshRenderers().Set(child, { .meshAssetId = 44U, .materialAssetId = 9U });
    sync.SyncEntities(scene, render, childId);
    Require(render.FindMeshByEntity(child.Id())->desc.meshAssetId == 44U, "Owned override did not replace inherited mesh");
    scene.Components().MeshRenderers().Remove(child);
    sync.SyncEntities(scene, render, childId);
    Require(render.FindMeshByEntity(child.Id())->desc.meshAssetId == 43U, "Removing an override did not reveal inherited mesh");
    ecs_remove_pair(backend, ecs_strip_generation(child.Id()), EcsIsA, base);
    sync.SyncEntities(scene, render, childId);
    Require(render.FindMeshByEntity(child.Id()) == nullptr && render.FindCameraByEntity(child.Id()) == nullptr,
        "Removing inheritance retained stale proxies");

    const std::array transforms{ At(31.0F, 4.0F, 5.0F) };
    const std::array cameras{ kb::scene::CameraComponent{ .primary = true } };
    const std::array views{
        kb::ecs::World::MakeBulkComponentView<TransformComponent>(transforms),
        kb::ecs::World::MakeBulkComponentView<kb::scene::CameraComponent>(cameras),
    };
    const auto native = world.CreateEntitiesNativeOnly(1U, views);
    Require(ecs_get_alive(backend, ecs_strip_generation(native[0].Id())) == 0U,
        "Native-only renderer fixture accidentally created a backend owner");
    const std::array nativeIds{ native[0].Id() };
    sync.SyncEntities(scene, render, nativeIds);
    Require(render.FindCameraByEntity(native[0].Id()) != nullptr
        && render.FindCameraByEntity(native[0].Id())->desc.position == std::array<float, 3U>{ 31.0F, 4.0F, 5.0F },
        "Native-only camera reconciliation was lost");
    const kb::scene::CameraComponent rawCamera{ .verticalFovDegrees = 87.0F, .primary = true };
    ecs_set_id(backend, ecs_strip_generation(child.Id()), cameraId, sizeof(rawCamera), &rawCamera);
    const auto rawTransform = At(41.0F, 6.0F, 7.0F);
    ecs_set_id(backend, ecs_strip_generation(child.Id()), transformId, sizeof(rawTransform), &rawTransform);
    sync.SyncEntities(scene, render, childId);
    Require(render.FindCameraByEntity(child.Id()) != nullptr
        && render.FindCameraByEntity(child.Id())->desc.verticalFovDegrees == 87.0F,
        "Raw backend mutation on a logical scene owner was discarded");
}

std::array<float, 16U> ExpectedMeshModel(const TransformComponent& transform) {
    // Corpus has identity rotation; explicit oracle preserves nonuniform/negative scale and translation.
    return { transform.worldScale.x, 0, 0, 0, 0, transform.worldScale.y, 0, 0,
        0, 0, transform.worldScale.z, 0, transform.worldPosition.x, transform.worldPosition.y,
        transform.worldPosition.z, 1 };
}

std::uint64_t LatestMeshRevisionStamp(const RenderScene& render) {
    // A topology rebuild stamps groups after the last mesh content/set revision.
    // This fixture owns the only active RenderScene and has no other proxy kinds.
    auto latest = std::max(render.MeshContentRevision(), render.MeshSetVersion());
    for (const auto& group : render.DrawGroups())
        latest = std::max({ latest, group.cacheId, group.contentRevision });
    return latest;
}

void RunMeshPullSlices() {
    Scene scene;
    EcsRenderSceneSynchronizer sync;
    RenderScene render;
    std::vector<SceneEntity> entities;
    const std::array sizes{ 0U, 1U, 1023U, 1024U, 1025U, 2049U, 8193U, 1025U, 0U, 2049U };
    for (const std::size_t count : sizes) {
        while (entities.size() > count) { scene.Entities().Destroy(entities.back()); entities.pop_back(); }
        while (entities.size() < count) {
            const std::size_t index = entities.size();
            const SceneEntity entity = Create(scene, static_cast<float>(index), 0.5F, 0.0F);
            scene.Components().MeshRenderers().Set(entity, { .meshAssetId = 42U + index % 8U,
                .materialAssetId = 7U + index % 8U });
            entities.push_back(entity);
        }
        static_cast<void>(scene.Runtime().Update(0.0F));
        sync.SyncStructural(scene, render);
        static_cast<void>(render.DrawGroups());
        sync.PullTransforms(scene, render);
        render.ClearDirty();
        const auto unchangedStats = render.Stats();
        const auto unchangedRevision = render.MeshContentRevision();
        sync.PullTransforms(scene, render);
        Require(render.Stats().transformInPlaceUpdateCount == unchangedStats.transformInPlaceUpdateCount
            && render.Stats().transformFallbackUpdateCount == unchangedStats.transformFallbackUpdateCount
            && render.MeshContentRevision() == unchangedRevision,
            "An unchanged pull consumed stale task counts or outputs");

        std::vector<std::uint64_t> revisions;
        for (const auto& group : render.DrawGroups()) revisions.push_back(group.contentRevision);
        for (std::size_t index = 0U; index < entities.size(); ++index) {
            auto transform = At(static_cast<float>(index) + 0.25F, 1.5F, -2.0F);
            transform.localScale = { index % 2U == 0U ? -2.0F : 2.0F, 3.0F, 4.0F };
            scene.Transforms().Set(entities[index], transform);
        }
        static_cast<void>(scene.Runtime().Update(0.0F));
        const auto before = render.Stats();
        const auto contentRevisionBefore = render.MeshContentRevision();
        const auto revisionBase = LatestMeshRevisionStamp(render);
        sync.PullTransforms(scene, render);
        const auto after = render.Stats();
        Require(after.transformInPlaceUpdateCount - before.transformInPlaceUpdateCount == entities.size()
            && after.transformFallbackUpdateCount == before.transformFallbackUpdateCount,
            "Composed multi-task pull missed rows or unexpectedly fell back");
        std::vector<unsigned> visits(entities.size(), 0U);
        std::unordered_map<std::uint64_t, std::size_t> idToIndex;
        for (std::size_t index = 0U; index < entities.size(); ++index) idToIndex.emplace(entities[index].Id(), index);
        const auto& groups = render.DrawGroups();
        Require(groups.size() == revisions.size(), "Transform-only pull changed group topology");
        Require(entities.empty() ? render.MeshContentRevision() == contentRevisionBefore
            : render.MeshContentRevision() == revisionBase + groups.size() + 1U,
            "Task reduction did not publish exactly one revision per changed group plus its batch");
        for (std::size_t groupIndex = 0U; groupIndex < groups.size(); ++groupIndex) {
            Require(groups[groupIndex].contentRevision != revisions[groupIndex], "A changed group was absent from task reduction");
            Require(groups[groupIndex].contentRevision == revisionBase + groupIndex + 1U,
                "Changed groups were duplicated or reduced in a different order");
            for (const auto& instance : groups[groupIndex].instances) {
                const auto found = idToIndex.find(instance.entityId);
                Require(found != idToIndex.end(), "Draw groups contain an unknown/stale full entity ID");
                const auto index = found->second;
                ++visits[index];
                const auto expected = ExpectedMeshModel(*scene.Transforms().TryGet(entities[index]));
                Require(instance.model == expected && instance.meshAssetId == 42U + index % 8U
                    && instance.materialAssetId == 7U + index % 8U, "In-place instance payload differs from scalar model oracle");
            }
        }
        for (std::size_t index = 0U; index < entities.size(); ++index) {
            const auto* proxy = render.FindMeshByEntity(entities[index].Id());
            const auto* transform = scene.Transforms().TryGet(entities[index]);
            Require(visits[index] == 1U && proxy != nullptr && proxy->pulledWorldVersion == transform->worldVersion,
                "Task slices skipped/duplicated a row or published a stale world version");
        }
        // One changed row crosses a task tail; only its group may acquire a new revision.
        if (!entities.empty()) {
            revisions.clear();
            for (const auto& group : render.DrawGroups()) revisions.push_back(group.contentRevision);
            const auto selected = entities.back();
            const auto changedGroup = render.FindMeshByEntity(selected.Id())->instanceGroupIndex;
            auto transform = *scene.Transforms().TryGet(selected);
            transform.localPosition.z += 1.0F;
            scene.Transforms().Set(selected, transform);
            static_cast<void>(scene.Runtime().Update(0.0F));
            const auto singleBefore = render.Stats();
            const auto singleRevision = render.MeshContentRevision();
            sync.PullTransforms(scene, render);
            Require(render.Stats().transformInPlaceUpdateCount == singleBefore.transformInPlaceUpdateCount + 1U,
                "A single changed tail row reused stale per-task outputs");
            for (std::size_t index = 0U; index < revisions.size(); ++index)
                Require((render.DrawGroups()[index].contentRevision != revisions[index]) == (index == changedGroup),
                    "Task reduction marked an untouched draw group");
            Require(render.MeshContentRevision() == singleRevision + 2U
                && render.DrawGroups()[changedGroup].contentRevision == singleRevision + 1U,
                "A single changed task tail published more than one group revision");
            // A write after composition exercises the caller-owned dirty-row resolver path.
            transform.localPosition.z += 1.0F;
            scene.Transforms().Set(selected, transform);
            sync.PullTransforms(scene, render);
            Position(render.FindMeshByEntity(selected.Id())->desc.model,
                transform.localPosition.x, transform.localPosition.y, transform.localPosition.z);
        }
        std::printf("MESH_SLICES count=%zu groups=%zu\n", count, groups.size());
        std::fflush(stdout);
    }
}

class HeadlessSurface final : public kb::render::RenderSurface {
public:
    std::uint32_t Width() const noexcept override { return 64U; }
    std::uint32_t Height() const noexcept override { return 64U; }
    void* NativeWindowHandle() const noexcept override { return nullptr; }
    void* NativeDisplayHandle() const noexcept override { return nullptr; }
};

void RunRuntimeResources() {
    Scene scene;
    kb::scene::SceneLightingAccess::SetBasicLightingEnabled(scene, true);
    auto& assets = scene.Assets().Manager();
    Require(assets.RegisterLoader(std::make_unique<kb::render::RenderMeshAssetLoader>())
        && assets.RegisterLoader(std::make_unique<kb::render::RenderMaterialAssetLoader>())
        && assets.RegisterLoader(std::make_unique<kb::render::RenderTextureAssetLoader>()),
        "Could not register typed runtime asset loaders before publication");
    for (std::uint64_t id : { 42U, 43U, 44U, 45U }) {
        auto mesh = std::make_shared<kb::render::RenderMeshAssetData>();
        mesh->vertices = { { -0.5F, -0.5F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F },
            { 0.5F, -0.5F, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 0.0F },
            { 0.0F, 0.5F, 0.0F, 0.0F, 0.0F, 1.0F, 0.5F, 1.0F } };
        mesh->indices16 = { 0U, 1U, 2U };
        mesh->bounds.radius = 1.0F;
        mesh->RefreshDesc();
        Require(assets.RegisterAsset({ .id = kb::assets::AssetId{ id }, .type = "RenderMesh",
            .name = "Contract triangle", .virtualPath = "/Contract/triangle" + std::to_string(id) + ".obj", .runtimeLoadable = true })
            && assets.PublishRuntimeAsset(kb::assets::AssetId{ id }, mesh), "Could not publish runtime mesh fixture");
    }
    auto texture = std::make_shared<kb::render::RenderTextureAssetData>();
    texture->width = texture->height = 1U;
    texture->rgba8 = { 180U, 160U, 140U, 255U };
    Require(assets.RegisterAsset({ .id = kb::assets::AssetId{ 20U }, .type = "RenderTexture",
        .name = "Contract albedo", .virtualPath = "/Contract/albedo.kbtex", .runtimeLoadable = true })
        && assets.PublishRuntimeAsset(kb::assets::AssetId{ 20U }, texture), "Could not publish runtime texture fixture");
    for (std::uint64_t id : { 7U, 8U, 9U, 10U, 11U }) {
        auto material = std::make_shared<kb::render::RenderMaterialAssetData>();
        material->desc.albedoTextureAssetId = 20U;
        material->graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
        Require(assets.RegisterAsset({ .id = kb::assets::AssetId{ id }, .type = "RenderMaterial",
            .name = "Contract material", .virtualPath = "/Contract/material" + std::to_string(id) + ".kbmat", .runtimeLoadable = true })
            && assets.PublishRuntimeAsset(kb::assets::AssetId{ id }, material), "Could not publish runtime material fixture");
        const auto resolved = kb::render::RuntimeMaterialResolver{}.ResolveAsset(assets, kb::assets::AssetId{ id });
        Require(resolved.status == kb::render::RuntimeMaterialResolveStatus::Resolved
            && resolved.material.desc.albedoTextureAssetId == 20U,
            "Published runtime material fixture failed to resolve its authored albedo texture");
    }
    std::array<SceneEntity, kinds.size()> entities{};
    for (std::size_t index = 0U; index < kinds.size(); ++index) {
        entities[index] = Create(scene, 0.0F, 0.0F, index == 0U ? -5.0F : 0.0F);
        Add(scene, entities[index], kinds[index]);
    }
    // Even invisible proxies must ensure their own resource IDs, not only draw-group IDs.
    const auto hidden = Create(scene);
    scene.Components().MeshRenderers().Set(hidden, { .meshAssetId = 45U, .materialAssetId = 11U });
    scene.Components().Visibility().Set(hidden, { .visible = false });
    // A material slot has an independent reference which cannot be inferred from a group key.
    auto mesh = *scene.Components().MeshRenderers().TryGet(entities[2]);
    mesh.materialSlotOverrideCount = 1U;
    mesh.materialSlotAssetIds[0] = 8U;
    scene.Components().MeshRenderers().Set(entities[2], mesh);
    static_cast<void>(scene.Runtime().Update(0.0F));
    HeadlessSurface surface;
    kb::render::DisplayConfig config;
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);
    kb::render::Renderer renderer;
    renderer.SetRuntimeAssetDiscoveryEnabled(false);
    Require(renderer.Initialize(surface, &config), "Runtime fixture could not initialize the supported headless Noop backend");
    for (unsigned frame = 0U; frame < 2U; ++frame) {
        Require(renderer.BeginFrame(), "Runtime fixture could not begin a frame");
        Require(renderer.SubmitScene(scene), "Runtime fixture could not submit the real all-proxy scene");
        const auto stats = renderer.RuntimeResourceStats();
        const auto* map = renderer.SceneResourceMap();
        const auto* registry = renderer.SceneResources();
        Require(map != nullptr && registry != nullptr, "Runtime scene resource providers are unavailable");
        std::printf("RESOURCE_LEDGER frame=%u meshes=%u materials=%u textures=%u strokeMesh44=%d strokeMaterial9=%d hiddenMesh45=%d hiddenMaterial11=%d\n",
            frame, stats.cachedMeshCount, stats.cachedMaterialCount, stats.cachedTextureCount,
            registry->ContainsMesh(map->ResolveMesh(44U)) ? 1 : 0,
            registry->ContainsMaterial(map->ResolveMaterial(9U)) ? 1 : 0,
            registry->ContainsMesh(map->ResolveMesh(45U)) ? 1 : 0,
            registry->ContainsMaterial(map->ResolveMaterial(11U)) ? 1 : 0);
        std::fflush(stdout);
        Require(stats.cachedMeshCount == 4U && stats.cachedMaterialCount == 5U && stats.cachedTextureCount == 1U,
            "Runtime resource ensures omitted hidden/generated/cast/material-override references");
        Require(!renderer.LastSceneSubmitStats().HasMissingResources(), "All-proxy runtime submission reported missing resources");
        for (std::uint64_t id : { 42U, 43U, 44U, 45U })
            Require(registry->ContainsMesh(map->ResolveMesh(id)), "Runtime mesh binding did not resolve a live resource");
        for (std::uint64_t id : { 7U, 8U, 9U, 10U, 11U })
            Require(registry->ContainsMaterial(map->ResolveMaterial(id)), "Runtime material binding did not resolve a live resource");
        Require(registry->ContainsTexture(map->ResolveTexture(20U, kb::render::RenderTextureColorSpace::Srgb)),
            "Albedo texture color-space binding did not resolve a live resource");
        renderer.EndFrame();
        if (frame == 0U) {
            scene.Transforms().Set(entities[0], At(1.0F, 0.0F, -5.0F));
            scene.Components().MeshRenderers().TryGet(entities[2])->materialAssetId = 9U;
            scene.Components().MeshRenderers().MarkModified(entities[2]);
            static_cast<void>(scene.Runtime().Update(0.0F));
        }
    }
    renderer.Shutdown();
}

struct Test { const char* name; void (*run)(); };
} // namespace

int main(int argc, char** argv) {
    const std::array tests{
        Test{ "all-proxy", &RunAllProxyLifecycle }, Test{ "inherited", &RunInheritedAndNativeOnly },
        Test{ "mesh-slices", &RunMeshPullSlices }, Test{ "resources", &RunRuntimeResources },
    };
    try {
        bool selected = false;
        for (const auto& test : tests) {
            if (argc > 1 && std::string_view{ argv[1] } != test.name) continue;
            selected = true;
            std::printf("START %s\n", test.name);
            std::fflush(stdout);
            test.run();
            std::printf("PASS %s\n", test.name);
            std::fflush(stdout);
        }
        Require(selected, "Unknown renderer contract selector");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL renderer contracts: %s\n", error.what());
        return 1;
    }
}
