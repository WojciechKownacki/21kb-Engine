#include "Map2DView.hpp"
#include "Map2DCollision.hpp"

#include "Map2DEditing.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/ScenePostProcessAccess.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/ecs/World.hpp"
#include "kb/render/resources/PostProcessProfileAssetLoader.hpp"
#include "kb/render/resources/RenderMap2DMeshBuilder.hpp"

#include <array>
#include <set>
#include <vector>
#include <system_error>
#include <utility>

namespace kb::world2d {
namespace {

void SetError(std::string* output, std::string value) {
    if (output != nullptr) *output = std::move(value);
}

[[nodiscard]] bool WritePixelProfile(const std::filesystem::path& file) {
    kb::render::ScenePostProcessSettings settings{};
    constexpr std::array<std::pair<std::string_view, std::string_view>, 14U> kFields{ {
        // The output pass stays on for its sRGB encoding; only the tone curve goes.
        { "tonemapEnabled", "true" },
        { "outputTransform.tonemap", "None" },
        { "outputTransform.exposureStops", "0" },
        { "outputTransform.autoExposure.enabled", "false" },
        { "outputTransform.colorGradingLutStrength", "0" },
        { "bloomEnabled", "false" },
        { "ambientOcclusionEnabled", "false" },
        { "fogEnabled", "false" },
        { "volumetricFogEnabled", "false" },
        { "vignetteEnabled", "false" },
        { "temporalAntiAliasingEnabled", "false" },
        { "temporalJitterEnabled", "false" },
        { "fxaaEnabled", "false" },
        { "globalIllumination", "Off" },
    } };
    for (const auto& [key, value] : kFields) {
        // A field a renderer build does not know is left at its default.
        static_cast<void>(kb::render::PostProcessProfileAssetLoader::SetField(settings, key, value));
    }
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    return kb::render::PostProcessProfileAssetLoader::SaveProfile(file, settings);
}

} // namespace

void SetUpMap2DComponents(kb::scene::Scene& scene, kb::scene::SceneEntity entity,
    std::uint64_t mapAssetId, const kb::assets::Map2DAsset& map) {
    if (!scene.Entities().IsAlive(entity)) return;
    auto components = scene.Components();
    const kb::scene::GridComponent grid{ .cellSize = map.tileSize, .floorHeight = map.floorHeight,
        .wall = map.orientation == kb::assets::Map2DOrientation::Wall };
    const auto* previousGrid = std::as_const(scene).Components().Grids().TryGet(entity);
    if (previousGrid == nullptr || *previousGrid != grid) components.Grids().Set(entity, grid);
    auto tilemap = kb::scene::TilemapComponent{ .mapAssetId = mapAssetId };
    if (const auto* previous = std::as_const(scene).Components().Tilemaps().TryGet(entity)) tilemap.locked = previous->locked;
    const auto* previous = std::as_const(scene).Components().Tilemaps().TryGet(entity);
    if (previous == nullptr || *previous != tilemap) components.Tilemaps().Set(entity, tilemap);
    if (!components.TilemapRenderers().Has(entity)) components.TilemapRenderers().Set(entity, {});
    if (!components.TilemapColliders().Has(entity)) components.TilemapColliders().Set(entity, {});
    if (!components.MeshRenderers().Has(entity)) {
        components.MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
            .meshAssetId = mapAssetId, .castsShadow = false, .receivesShadow = true });
    }
}

bool SetUpMap2DCollision(
    kb::scene::Scene& scene, kb::scene::SceneEntity mapEntity, const kb::assets::Map2DAsset& map,
    bool* changed, std::string* error, std::vector<kb::assets::AssetId>* runtimeAssets) {
    if (changed != nullptr) *changed = false;
    if (!scene.Entities().IsAlive(mapEntity) || !kb::assets::IsMap2DAssetValid(map, error)) return false;
    std::vector<kb::scene::ColliderComponent> boxes;
    if (!BuildMap2DCollision(scene, mapEntity, map, boxes, runtimeAssets, error)) return false;
    const auto owner = scene.Entities().Object(mapEntity);
    kb::scene::SceneObject group{};
    for (const auto& child : owner.Children()) if (child.Name() == kMap2DCollisionName) group = child;
    if (boxes.empty() && !group.IsValid()) return true;
    const auto children = group.IsValid() ? group.Children() : std::vector<kb::scene::SceneObject>{};
    const auto equal = [](const kb::scene::Vec3& a, const kb::scene::Vec3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; };
    bool same = children.size() == boxes.size();
    for (std::size_t index = 0U; same && index < boxes.size(); ++index) {
        const auto* current = scene.Components().Colliders().TryGet(children[index].Entity());
        same = current != nullptr && current->shape == boxes[index].shape && current->meshAssetId == boxes[index].meshAssetId &&
            current->trigger == boxes[index].trigger && current->friction == boxes[index].friction && current->restitution == boxes[index].restitution &&
            equal(current->center, boxes[index].center) && equal(current->boxSize, boxes[index].boxSize);
    }
    if (same) return true;
    if (group.IsValid()) scene.Entities().Destroy(group);
    if (!boxes.empty()) {
        group = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = std::string{ kMap2DCollisionName }, .parent = owner });
        for (const auto& box : boxes) {
            const auto child = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Solid", .parent = group });
            scene.Components().Colliders().Set(child.Entity(), box);
        }
    }
    if (changed != nullptr) *changed = true;
    return true;
}

bool SetUpMap2DView(
    kb::scene::Scene& scene,
    kb::scene::SceneEntity mapEntity,
    const kb::assets::Map2DAsset& map,
    const std::filesystem::path& assetsRoot,
    Map2DViewResult* result,
    std::string* error) {
    if (!scene.Entities().IsAlive(mapEntity)) {
        SetError(error, "The map entity is not alive");
        return false;
    }
    kb::scene::SceneObject owner = scene.Entities().Object(mapEntity);
    const auto parent = scene.Hierarchy().Parent(mapEntity);
    if (scene.Components().Grids().Has(parent) && !scene.Components().Tilemaps().Has(parent)) {
        owner = scene.Entities().Object(parent);
        scene.Components().Grids().Set(parent, { .cellSize = map.tileSize, .floorHeight = map.floorHeight,
            .wall = map.orientation == kb::assets::Map2DOrientation::Wall });
    }
    const float height = static_cast<float>(map.height) * map.tileSize;
    const float distance = static_cast<float>(kb::assets::Map2DAsset::MaximumFloor + 1) * map.floorHeight +
        kb::render::RenderMap2DMeshBuilder::kMaximumSortingOrder * kb::render::RenderMap2DMeshBuilder::kSortingOrderLift + 10.0F;
    kb::scene::TransformComponent transform{};
    if (map.orientation == kb::assets::Map2DOrientation::Wall) {
        // No rotation: the camera looks along +Z at the wall, which faces -Z.
        transform.localPosition = kb::scene::Vec3{ 0.0F, height * 0.5F, -distance };
    } else {
        // A quarter turn about X: looking down -Y with +Z (north) at the top of the screen.
        transform.localPosition = kb::scene::Vec3{ 0.0F, distance, 0.0F };
        transform.localRotation = kb::scene::Quat{ 0.70710678F, 0.0F, 0.0F, 0.70710678F };
    }
    kb::scene::SceneObject camera{};
    for (const kb::scene::SceneObject& child : owner.Children()) {
        if (child.Name() == kMap2DCameraName) camera = child;
    }
    Map2DViewResult outcome{};
    if (!camera.IsValid()) {
        camera = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = std::string{ kMap2DCameraName }, .parent = owner, .transform = transform });
        outcome.cameraCreated = true;
    } else {
        camera.SetTransform(transform);
    }
    if (!camera.IsValid()) {
        SetError(error, "The map camera could not be created");
        return false;
    }
    kb::scene::CameraComponent lens{};
    lens.projection = kb::scene::CameraProjection::Orthographic;
    lens.orthographicHeight = static_cast<float>(map.viewHeight) * map.tileSize;
    lens.nearClip = 0.1F;
    lens.farClip = distance * 2.0F + 100.0F;
    lens.primary = true;
    // Above an ordinary primary camera, so Play looks through this one.
    lens.priority = 100;
    std::vector<kb::scene::SceneEntity> otherCameras;
    scene.Runtime().EcsWorld().CreateQuery<kb::scene::CameraComponent>().ForEach(
        [](kb::scene::SceneEntity entity, const kb::scene::CameraComponent& value, void* output) {
            if (value.primary) static_cast<std::vector<kb::scene::SceneEntity>*>(output)->push_back(entity);
        }, &otherCameras);
    for (const auto other : otherCameras) {
        if (other == camera.Entity() || scene.Entities().Name(other) != kMap2DCameraName ||
            !scene.Components().Grids().Has(scene.Hierarchy().Parent(other))) continue;
        auto inactive = *std::as_const(scene).Components().Cameras().TryGet(other);
        inactive.primary = false; scene.Components().Cameras().Set(other, inactive);
    }
    scene.Components().Cameras().Set(camera.Entity(), lens);
    outcome.camera = camera.Entity();

    outcome.profileAssigned = UseExactColors(scene, assetsRoot);
    if (result != nullptr) *result = outcome;
    return true;
}

bool UseExactColors(kb::scene::Scene& scene, const std::filesystem::path& assetsRoot) {
    if (kb::scene::ScenePostProcessAccess::ActiveProfile(scene) != 0U) return false;
    const std::filesystem::path file = Map2DAssetFile(assetsRoot, kMap2DProfilePath);
    std::error_code fileError;
    if (!std::filesystem::exists(file, fileError) && !WritePixelProfile(file)) return false;
    static_cast<void>(scene.Assets().Discover());
    const kb::assets::AssetId profile = kb::assets::MakeAssetId(
        kb::assets::NormalizeAssetPath(std::filesystem::path{ std::string{ kMap2DProfilePath } }) + ":" +
        std::string{ kb::render::kPostProcessProfileAssetType });
    kb::scene::ScenePostProcessAccess::SetActiveProfile(scene, profile.value);
    return true;
}

} // namespace kb::world2d
