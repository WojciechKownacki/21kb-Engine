#include "scene/systems/RiverSceneSystem.hpp"

#include "engine/scene/RiverGeometry.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneMeshRendererComponents.hpp"
#include "engine/scene/SceneSurfaceGeometry.hpp"
#include "engine/scene/SceneSystemContext.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace kb::scene {
namespace {

constexpr std::string_view kStreamName = "River Water";
constexpr std::string_view kFallName = "River Waterfalls";

struct RiverEntry {
    SceneEntity entity{};
    RiverComponent river{};
};

void CollectRiver(SceneEntity entity, const RiverComponent& river, void* context) {
    static_cast<std::vector<RiverEntry>*>(context)->push_back({ entity, river });
}

void Mix(std::uint64_t& hash, const void* data, std::size_t size) noexcept {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t index = 0U; index < size; ++index) {
        hash ^= bytes[index];
        hash *= 1099511628211ULL;
    }
}

// What the water is made of: the points in use, the slope that makes a waterfall and the materials.
[[nodiscard]] std::uint64_t RiverKey(const RiverComponent& river) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    Mix(hash, &river.pointCount, sizeof(river.pointCount));
    Mix(hash, river.points.data(), sizeof(RiverPoint) * std::min(river.pointCount, RiverComponent::MaxPoints));
    Mix(hash, &river.waterfallSlopeDegrees, sizeof(river.waterfallSlopeDegrees));
    Mix(hash, &river.materialAssetId, sizeof(river.materialAssetId));
    Mix(hash, &river.waterfallMaterialAssetId, sizeof(river.waterfallMaterialAssetId));
    return hash;
}

[[nodiscard]] SceneEntity FindChild(Scene& scene, SceneEntity owner, std::string_view name) {
    for (const SceneEntity child : scene.Hierarchy().ChildEntities(owner)) {
        if (scene.Entities().Name(child) == name) return child;
    }
    return {};
}

[[nodiscard]] bool HasSurface(const Scene& scene, SceneEntity entity) {
    const std::span<const SurfaceGeometry> surfaces = SceneSurfaceGeometry::All(scene);
    return std::ranges::any_of(surfaces, [entity](const SurfaceGeometry& surface) { return surface.owner == entity; });
}

void RemoveChild(Scene& scene, SceneEntity owner, std::string_view name) {
    const SceneEntity child = FindChild(scene, owner, name);
    if (!child.IsValid()) return;
    SceneSurfaceGeometry::Remove(scene, child);
    scene.Entities().Destroy(child);
}

// The child `name` of `owner` draws these triangles with this material.
[[nodiscard]] bool PublishWater(Scene& scene, SceneEntity owner, std::string_view name, std::uint64_t material,
    const std::vector<SurfaceGeometryVertex>& vertices, const std::vector<std::uint32_t>& indices, std::string& error) {
    SceneEntity child = FindChild(scene, owner, name);
    if (!child.IsValid()) {
        child = scene.Entities().CreateEntity(SceneObjectDesc{ .name = std::string{ name }, .parent = scene.Entities().Object(owner) });
    }
    MeshRendererComponent renderer{};
    renderer.materialAssetId = material;
    renderer.castsShadow = false;
    renderer.receivesShadow = false;
    renderer.transient = true;
    scene.Components().MeshRenderers().Set(child, renderer);
    return SceneSurfaceGeometry::Publish(scene, child, vertices, indices, error);
}

} // namespace

void RiverSceneSystem::OnUpdate(SceneSystemContext& context) {
    Scene& scene = context.GetScene();
    std::vector<RiverEntry> rivers;
    static_cast<const Scene&>(scene).Components().Rivers().ForEach(&CollectRiver, &rivers);
    if (rivers.empty() && builtKeys_.empty()) return;
    std::unordered_map<SceneEntity::IdType, std::uint64_t> current;
    for (const RiverEntry& entry : rivers) {
        const SceneEntity owner = entry.entity;
        if (!scene.Entities().IsAlive(owner)) continue;
        if (!entry.river.enabled || !IsRiverComponentValid(entry.river)) {
            RemoveChild(scene, owner, kStreamName);
            RemoveChild(scene, owner, kFallName);
            continue;
        }
        const std::uint64_t key = RiverKey(entry.river);
        current.emplace(owner.Id(), key);
        const auto built = builtKeys_.find(owner.Id());
        const SceneEntity stream = FindChild(scene, owner, kStreamName);
        if (built != builtKeys_.end() && built->second == key && stream.IsValid() && HasSurface(scene, stream)) continue;
        RiverSurface surface;
        if (!BuildRiverSurface(entry.river, surface)) {
            context.ReportError("River: the water could not be built from the points of entity " + std::to_string(owner.Id()));
            continue;
        }
        std::string error;
        if (!surface.streamVertices.empty() &&
            !PublishWater(scene, owner, kStreamName, entry.river.materialAssetId, surface.streamVertices, surface.streamIndices, error)) {
            context.ReportError("River stream: " + error);
            continue;
        }
        if (surface.streamVertices.empty()) RemoveChild(scene, owner, kStreamName);
        if (surface.fallVertices.empty()) {
            RemoveChild(scene, owner, kFallName);
        } else if (!PublishWater(scene, owner, kFallName, entry.river.waterfallMaterialAssetId, surface.fallVertices, surface.fallIndices, error)) {
            context.ReportError("River waterfalls: " + error);
            continue;
        }
    }
    // A river that is gone takes its water with it.
    for (const auto& [id, key] : builtKeys_) {
        if (current.contains(id)) continue;
        const SceneEntity owner{ id };
        if (scene.Entities().IsAlive(owner)) {
            RemoveChild(scene, owner, kStreamName);
            RemoveChild(scene, owner, kFallName);
        }
    }
    builtKeys_ = std::move(current);
}

void RiverSceneSystem::OnDestroy(SceneSystemContext&) {
    builtKeys_.clear();
}

} // namespace kb::scene
