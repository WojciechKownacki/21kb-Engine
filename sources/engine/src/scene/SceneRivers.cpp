#include "engine/scene/SceneRivers.hpp"

#include "engine/scene/ParticleEffectComponent.hpp"
#include "engine/scene/RiverGeometry.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneMeshRendererComponents.hpp"
#include "engine/scene/SceneSurfaceGeometry.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace kb::scene {
namespace {

constexpr std::string_view kStreamName = "River Water";
constexpr std::string_view kFallPrefix = "River Waterfall ";
// The mist of one waterfall stays within the budget of a handheld: at most this many particles at once.
constexpr std::uint32_t kMistParticleBudget = 480U;
// The mist effect lays its particles along a line of this length; a wider fall asks for a higher rate.
constexpr float kMistLineLength = 4.0F;

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
    Mix(hash, &river.mistEffectAssetId, sizeof(river.mistEffectAssetId));
    return hash;
}

[[nodiscard]] std::string FallName(std::size_t index) { return std::string{ kFallPrefix } + std::to_string(index + 1U); }

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

void RemoveChild(Scene& scene, SceneEntity child, std::vector<std::uint64_t>& touched) {
    if (!child.IsValid()) return;
    touched.push_back(child.Id());
    SceneSurfaceGeometry::Remove(scene, child);
    scene.Entities().Destroy(child);
}

// The stream and the waterfalls numbered above `keepFalls` go; with keepStream false the stream goes too.
void RemoveWaterChildren(Scene& scene, SceneEntity owner, bool keepStream, std::size_t keepFalls, std::vector<std::uint64_t>& touched) {
    for (const SceneEntity child : scene.Hierarchy().ChildEntities(owner)) {
        const std::string name = scene.Entities().Name(child);
        if (name == kStreamName) {
            if (!keepStream) RemoveChild(scene, child, touched);
        } else if (name.starts_with(kFallPrefix) &&
            static_cast<std::size_t>(std::strtoull(name.c_str() + kFallPrefix.size(), nullptr, 10)) > keepFalls) {
            RemoveChild(scene, child, touched);
        }
    }
}

// The child `name` of `owner` draws these triangles with this material.
[[nodiscard]] SceneEntity PublishWater(Scene& scene, SceneEntity owner, const std::string& name, std::uint64_t material,
    const std::vector<SurfaceGeometryVertex>& vertices, const std::vector<std::uint32_t>& indices, std::string& error) {
    SceneEntity child = FindChild(scene, owner, name);
    if (!child.IsValid()) {
        child = scene.Entities().CreateEntity(SceneObjectDesc{ .name = name, .parent = scene.Entities().Object(owner) });
    }
    MeshRendererComponent renderer{};
    renderer.materialAssetId = material;
    renderer.castsShadow = false;
    renderer.receivesShadow = false;
    renderer.transient = true;
    scene.Components().MeshRenderers().Set(child, renderer);
    return SceneSurfaceGeometry::Publish(scene, child, vertices, indices, error) ? child : SceneEntity{};
}

// A waterfall as an object of its own: it stands at the foot of the fall, turned across the current, so that its
// mist (a particle effect along the object's x axis) rises where the water lands and its mesh is made in that frame.
struct FallFrame {
    Vec3 origin{};
    Quat rotation{};
    float width = 0.0F;
};

[[nodiscard]] FallFrame FrameOfFall(const RiverSurface& surface, const RiverWaterfall& fall) {
    const RiverSample& foot = surface.samples[fall.last];
    const float yaw = std::atan2(foot.direction.x, foot.direction.z);
    return { foot.position, Quat{ 0.0F, std::sin(yaw * 0.5F), 0.0F, std::cos(yaw * 0.5F) }, foot.width };
}

// The fall's triangles, from the river's space into the frame of the fall (positions and the current in the normals).
[[nodiscard]] std::vector<SurfaceGeometryVertex> InFrame(const std::vector<SurfaceGeometryVertex>& vertices, const FallFrame& frame) {
    const Quat inverse = kb::math::Conjugate(frame.rotation);
    std::vector<SurfaceGeometryVertex> local = vertices;
    for (SurfaceGeometryVertex& vertex : local) {
        vertex.position = kb::math::Rotate(inverse, vertex.position - frame.origin);
        vertex.normal = kb::math::Rotate(inverse, vertex.normal);
    }
    return local;
}

} // namespace

SceneRiverSynchronization SynchronizeSceneRivers(Scene& scene) {
    SceneRiverSynchronization result;
    std::vector<RiverEntry> rivers;
    static_cast<const Scene&>(scene).Components().Rivers().ForEach(&CollectRiver, &rivers);
    auto& builtKeys = SceneAccess::State(scene).riverBuildKeys;
    if (rivers.empty() && builtKeys.empty()) return result;
    std::unordered_map<SceneEntity::IdType, std::uint64_t> current;
    for (const RiverEntry& entry : rivers) {
        const SceneEntity owner = entry.entity;
        if (!scene.Entities().IsAlive(owner)) continue;
        if (!entry.river.enabled || !IsRiverComponentValid(entry.river)) {
            RemoveWaterChildren(scene, owner, false, 0U, result.entities);
            continue;
        }
        const std::uint64_t key = RiverKey(entry.river);
        current.emplace(owner.Id(), key);
        const auto built = builtKeys.find(owner.Id());
        const SceneEntity stream = FindChild(scene, owner, kStreamName);
        if (built != builtKeys.end() && built->second == key && stream.IsValid() && HasSurface(scene, stream)) continue;
        RiverSurface surface;
        if (!BuildRiverSurface(entry.river, surface)) {
            result.errors.push_back("River: the water could not be built from the points of entity " + std::to_string(owner.Id()));
            continue;
        }
        std::string error;
        if (!surface.streamVertices.empty()) {
            const SceneEntity water = PublishWater(scene, owner, std::string{ kStreamName }, entry.river.materialAssetId, surface.streamVertices, surface.streamIndices, error);
            if (!water.IsValid()) {
                result.errors.push_back("River stream: " + error);
                continue;
            }
            result.entities.push_back(water.Id());
        } else {
            RemoveWaterChildren(scene, owner, false, surface.waterfalls.size(), result.entities);
        }
        for (std::size_t index = 0U; index < surface.waterfalls.size(); ++index) {
            const FallFrame frame = FrameOfFall(surface, surface.waterfalls[index]);
            const RiverFallMesh& mesh = surface.fallMeshes[index];
            const SceneEntity fall = PublishWater(scene, owner, FallName(index), entry.river.waterfallMaterialAssetId,
                InFrame(mesh.vertices, frame), mesh.indices, error);
            if (!fall.IsValid()) {
                result.errors.push_back("River waterfall: " + error);
                continue;
            }
            result.entities.push_back(fall.Id());
            TransformComponent transform = scene.Transforms().Get(fall);
            transform.localPosition = frame.origin;
            transform.localRotation = frame.rotation;
            scene.Transforms().Set(fall, transform);
            if (entry.river.mistEffectAssetId != 0U) {
                ParticleEffectComponent mist{};
                mist.effectAssetId = entry.river.mistEffectAssetId;
                mist.maxParticlesOverride = kMistParticleBudget;
                mist.rateMultiplier = std::clamp(frame.width / kMistLineLength, 0.25F, 8.0F);
                mist.followTransform = true;
                scene.Components().ParticleEffects().Set(fall, mist);
            } else {
                scene.Components().ParticleEffects().Remove(fall);
            }
        }
        // Falls that are no more (a river that lost its steep stretch) take their objects away.
        RemoveWaterChildren(scene, owner, true, surface.waterfalls.size(), result.entities);
    }
    // A river that is gone takes its water with it.
    for (const auto& [id, key] : builtKeys) {
        if (current.contains(id)) continue;
        const SceneEntity owner{ id };
        if (scene.Entities().IsAlive(owner)) RemoveWaterChildren(scene, owner, false, 0U, result.entities);
    }
    builtKeys = std::move(current);
    return result;
}

} // namespace kb::scene
