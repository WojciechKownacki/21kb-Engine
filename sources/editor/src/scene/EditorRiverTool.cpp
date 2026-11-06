#include "scene/EditorRiverTool.hpp"

#include "RiverWaterMaterials.hpp"
#include "TerrainAnalysis.hpp"
#include "TerrainChannel.hpp"
#include "console/EditorConsoleState.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "project/EditorProjectPaths.hpp"
#include "scene/EditorSceneContext.hpp"
#include "scene/EditorTerrainService.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

namespace kb::editor {
namespace {

using kb::scene::RiverComponent;
using kb::scene::RiverPlacement;
using kb::scene::SceneEntity;
using kb::scene::Vec3;

// Points of a river this close (in the plane) to a click are picked rather than a new one laid.
constexpr float kMinimumPickRadius = 1.5F;

[[nodiscard]] RiverPlacement PlacementOf(const kb::scene::Scene& scene, SceneEntity entity) {
    const kb::scene::TransformComponent transform = scene.Transforms().Get(entity);
    return { transform.worldPosition, transform.worldRotation, transform.worldScale };
}

[[nodiscard]] Vec3 Scaled(Vec3 value, Vec3 scale) noexcept { return { value.x * scale.x, value.y * scale.y, value.z * scale.z }; }

[[nodiscard]] float Divided(float value, float by) noexcept { return by != 0.0F ? value / by : value; }

[[nodiscard]] Vec3 ToWorld(const RiverPlacement& frame, Vec3 local) noexcept {
    return frame.position + kb::math::Rotate(frame.rotation, Scaled(local, frame.scale));
}

[[nodiscard]] Vec3 ToLocal(const RiverPlacement& frame, Vec3 world) noexcept {
    const Vec3 unrotated = kb::math::Rotate(kb::math::Conjugate(frame.rotation), world - frame.position);
    return { Divided(unrotated.x, frame.scale.x), Divided(unrotated.y, frame.scale.y), Divided(unrotated.z, frame.scale.z) };
}

[[nodiscard]] const RiverComponent* RiverOf(const kb::scene::Scene& scene, SceneEntity entity) {
    return entity.IsValid() && scene.Entities().IsAlive(entity) ? scene.Components().Rivers().TryGet(entity) : nullptr;
}

struct FirstRiver {
    SceneEntity entity{};
};

void TakeFirstRiver(SceneEntity entity, const RiverComponent&, void* context) {
    auto* found = static_cast<FirstRiver*>(context);
    if (!found->entity.IsValid()) found->entity = entity;
}

// A point of the terrain's plane in the space of the river `river`, on the ground.
[[nodiscard]] Vec3 GroundPointInRiver(
    const kb::scene::Scene& scene, SceneEntity terrain, const kb::assets::TerrainAsset& asset, SceneEntity river, float x, float z) {
    const Vec3 onGround{ x, kb::terrain_editor::SampleTerrainHeight(asset, x, z), z };
    return ToLocal(PlacementOf(scene, river), ToWorld(PlacementOf(scene, terrain), onGround));
}

// The point of the river nearest to (x, z) of the terrain plane, within `radius`; -1 when none is.
[[nodiscard]] std::int32_t NearestPoint(
    const kb::scene::Scene& scene, SceneEntity terrain, SceneEntity river, const RiverComponent& value, float x, float z, float radius) {
    const RiverPlacement terrainFrame = PlacementOf(scene, terrain);
    const RiverPlacement riverFrame = PlacementOf(scene, river);
    std::int32_t best = -1;
    float bestDistance = radius;
    for (std::uint32_t index = 0U; index < value.pointCount; ++index) {
        const Vec3 local = ToLocal(terrainFrame, ToWorld(riverFrame, value.points[index].position));
        const float distance = std::hypot(local.x - x, local.z - z);
        if (distance <= std::max(bestDistance, 0.0F) && (best < 0 || distance < bestDistance)) {
            best = static_cast<std::int32_t>(index);
            bestDistance = distance;
        }
    }
    return best;
}

[[nodiscard]] const kb::assets::AssetMetadata* FindAssetByFile(const kb::scene::Scene& scene, const std::filesystem::path& file) {
    std::error_code wantedError;
    const std::filesystem::path wanted = std::filesystem::weakly_canonical(file, wantedError);
    for (const kb::assets::AssetMetadata& candidate : scene.Assets().Manager().Registry().All()) {
        std::error_code error;
        if (!wantedError && std::filesystem::weakly_canonical(candidate.physicalPath, error) == wanted && !error) return &candidate;
    }
    return nullptr;
}

// The materials and the mist effect of a new river: the engine's, written into the project when they are not there.
[[nodiscard]] bool GiveDefaultWater(kb::scene::Scene& scene, RiverComponent& river, std::string& error) {
    kb::terrain_editor::RiverWaterMaterials files;
    if (!kb::terrain_editor::EnsureRiverWaterMaterials(EditorProjectPaths::AssetsRoot() / "Materials", files, error)) return false;
    static_cast<void>(scene.Assets().Discover());
    const auto idOf = [&](const std::filesystem::path& file) -> std::uint64_t {
        const kb::assets::AssetMetadata* metadata = FindAssetByFile(scene, file);
        return metadata != nullptr ? metadata->id.value : 0U;
    };
    river.materialAssetId = idOf(files.stream);
    river.waterfallMaterialAssetId = idOf(files.waterfall);
    river.mistEffectAssetId = idOf(files.mist);
    if (river.materialAssetId == 0U || river.waterfallMaterialAssetId == 0U || river.mistEffectAssetId == 0U) {
        error = "The river water assets were written but asset discovery did not register them.";
        return false;
    }
    return true;
}

} // namespace

SceneEntity EditorRiverTool::ActiveRiver(const EditorSceneContext& sceneContext) {
    EditorTerrainToolState& tool = EditorTerrainService::ToolState();
    const kb::scene::Scene& scene = sceneContext.Scene();
    if (RiverOf(scene, SceneEntity{ tool.riverEntityId }) != nullptr) return SceneEntity{ tool.riverEntityId };
    FirstRiver first;
    scene.Components().Rivers().ForEach(&TakeFirstRiver, &first);
    tool.riverEntityId = first.entity.IsValid() ? first.entity.Id() : 0U;
    return first.entity;
}

bool EditorRiverTool::Press(EditorSceneContext& sceneContext, SceneEntity terrain, float x, float z, bool remove) {
    EditorTerrainToolState& tool = EditorTerrainService::ToolState();
    const kb::assets::TerrainAsset* asset = sceneContext.TerrainForEditing(terrain);
    if (asset == nullptr) return false;
    kb::scene::Scene& scene = sceneContext.Scene();
    const SceneEntity river = ActiveRiver(sceneContext);

    if (river.IsValid()) {
        const RiverComponent value = *RiverOf(scene, river);
        const float radius = std::max(kMinimumPickRadius, value.points[0].width * 0.5F);
        const std::int32_t picked = NearestPoint(scene, terrain, river, value, x, z, radius);
        if (picked >= 0 && remove) {
            if (value.pointCount <= 2U) {
                sceneContext.Console().Warning("Terrain", "A river keeps at least two points.");
                return true;
            }
            return sceneContext.ExecuteSceneCommand("Remove River Point", [&scene, river, picked, value]() {
                RiverComponent shortened = value;
                for (std::uint32_t index = static_cast<std::uint32_t>(picked); index + 1U < shortened.pointCount; ++index) shortened.points[index] = shortened.points[index + 1U];
                --shortened.pointCount;
                scene.Components().Rivers().Set(river, shortened);
                return true;
            });
        }
        if (picked >= 0) {
            if (!sceneContext.BeginSceneEditTransaction("Move River Point")) return true;
            tool.riverDragPoint = picked;
            tool.strokeActive = true;
            return true;
        }
        if (remove) return true;
        if (value.pointCount >= RiverComponent::MaxPoints) {
            sceneContext.Console().Warning("Terrain", "A river has at most " + std::to_string(RiverComponent::MaxPoints) + " points.");
            return true;
        }
        const Vec3 point = GroundPointInRiver(scene, terrain, *asset, river, x, z);
        return sceneContext.ExecuteSceneCommand("Add River Point", [&scene, river, value, point]() {
            RiverComponent longer = value;
            kb::scene::RiverPoint added = longer.points[longer.pointCount - 1U];
            added.position = point;
            longer.points[longer.pointCount++] = added;
            scene.Components().Rivers().Set(river, longer);
            return true;
        });
    }

    if (remove) return true;
    if (!tool.riverPendingVisible) {
        tool.riverPendingVisible = true;
        tool.riverPendingX = x;
        tool.riverPendingZ = z;
        return true;
    }
    // The second click makes the river, in world space (the entity stands at the origin), with the engine's water.
    const float firstX = tool.riverPendingX;
    const float firstZ = tool.riverPendingZ;
    std::string error;
    SceneEntity made{};
    const bool done = sceneContext.ExecuteSceneCommand("Create River", [&]() {
        made = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "River" });
        if (!made.IsValid()) return false;
        RiverComponent created{};
        created.pointCount = 2U;
        const RiverPlacement terrainFrame = PlacementOf(scene, terrain);
        const float ground[2]{ kb::terrain_editor::SampleTerrainHeight(*asset, firstX, firstZ), kb::terrain_editor::SampleTerrainHeight(*asset, x, z) };
        created.points[0].position = ToWorld(terrainFrame, { firstX, ground[0], firstZ });
        created.points[1].position = ToWorld(terrainFrame, { x, ground[1], z });
        if (!GiveDefaultWater(scene, created, error)) return false;
        scene.Components().Rivers().Set(made, created);
        return true;
    });
    if (!done) {
        sceneContext.Console().Warning("Terrain", error.empty() ? "The river could not be made." : error);
        return true;
    }
    tool.riverEntityId = made.Id();
    tool.riverPendingVisible = false;
    return true;
}

bool EditorRiverTool::Drag(EditorSceneContext& sceneContext, SceneEntity terrain, float x, float z) {
    EditorTerrainToolState& tool = EditorTerrainService::ToolState();
    if (tool.riverDragPoint < 0) return false;
    kb::scene::Scene& scene = sceneContext.Scene();
    const SceneEntity river{ tool.riverEntityId };
    const RiverComponent* current = RiverOf(scene, river);
    const kb::assets::TerrainAsset* asset = sceneContext.TerrainForEditing(terrain);
    if (current == nullptr || asset == nullptr || static_cast<std::uint32_t>(tool.riverDragPoint) >= current->pointCount) return false;
    RiverComponent value = *current;
    value.points[static_cast<std::uint32_t>(tool.riverDragPoint)].position = GroundPointInRiver(scene, terrain, *asset, river, x, z);
    scene.Components().Rivers().Set(river, value);
    return true;
}

void EditorRiverTool::Release(EditorSceneContext& sceneContext) {
    EditorTerrainToolState& tool = EditorTerrainService::ToolState();
    if (tool.riverDragPoint < 0) return;
    tool.riverDragPoint = -1;
    if (!sceneContext.CommitSceneEditTransaction()) sceneContext.CancelSceneEditTransaction();
}

bool EditorRiverTool::Carve(EditorSceneContext& sceneContext, SceneEntity terrain) {
    const kb::scene::Scene& scene = sceneContext.Scene();
    const SceneEntity river = ActiveRiver(sceneContext);
    const RiverComponent* value = RiverOf(scene, river);
    const kb::assets::TerrainAsset* asset = sceneContext.TerrainForEditing(terrain);
    if (value == nullptr || asset == nullptr) {
        sceneContext.Console().Warning("Terrain", "Lay a river with the River tool first.");
        return false;
    }
    const float cell = std::min(asset->worldSizeX / static_cast<float>(asset->width - 1U), asset->worldSizeZ / static_cast<float>(asset->height - 1U));
    const std::vector<kb::terrain_editor::TerrainChannelNode> nodes = kb::terrain_editor::RiverChannelNodes(
        *value, PlacementOf(scene, river), PlacementOf(scene, terrain), std::clamp(cell, 0.25F, 2.0F));
    std::string error;
    if (!sceneContext.ApplyTerrainChannel(terrain, nodes, kb::terrain_editor::TerrainChannelSettings{ .bankWidth = value->bankWidth },
            "Cut River Into Terrain", &error)) {
        sceneContext.Console().Warning("Terrain", error.empty() ? "The river could not be cut into the terrain." : error);
        return false;
    }
    return true;
}

bool EditorRiverTool::TerrainView(
    const EditorSceneContext& sceneContext, SceneEntity terrain, SceneEntity river,
    std::vector<kb::scene::RiverSample>& line, std::vector<Vec3>& points) {
    const kb::scene::Scene& scene = sceneContext.Scene();
    const RiverComponent* value = RiverOf(scene, river);
    if (value == nullptr || !scene.Entities().IsAlive(terrain)) return false;
    const RiverPlacement terrainFrame = PlacementOf(scene, terrain);
    const RiverPlacement riverFrame = PlacementOf(scene, river);
    if (!kb::scene::SampleRiverInFrame(*value, riverFrame, terrainFrame, 1.0F, 512U, line)) return false;
    points.clear();
    for (std::uint32_t index = 0U; index < value->pointCount; ++index) {
        points.push_back(ToLocal(terrainFrame, ToWorld(riverFrame, value->points[index].position)));
    }
    return true;
}

} // namespace kb::editor
