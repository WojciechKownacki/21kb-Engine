#include "engine/scene/SceneNavigation.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/navigation/NavMeshAsset.hpp"
#include "engine/scene/CharacterControllerComponent.hpp"
#include "engine/scene/ContentInstanceComponent.hpp"
#include "engine/scene/PhysicsBackend.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/StreamFocusComponent.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneSystemContext.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/entities/SceneEntityCounter.hpp"
#include "scene/navigation/SceneNavigationState.hpp"
#include "scene/systems/NavigationSceneSystem.hpp"
#include "scene/transform/SceneTransformPrecision.hpp"
#include "scene/transform/SceneTransformResiduals.hpp"
#include "navigation/NavCrowd.hpp"
#include "navigation/NavMeshRuntime.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace kb::scene {
namespace {

namespace nav = kb::navigation;
using kb::math::Quat;
using kb::math::Vec3;

// Agents look this far ahead for collisions with other agents and obstacles they steer around.
constexpr float kAvoidanceTimeHorizonSeconds = 2.0F;
// How strongly a candidate velocity is penalised per 1/second of time to collision, against its
// deviation (in m/s) from the preferred velocity.
constexpr float kCollisionPenaltyWeight = 1.0F;
constexpr float kOverlapPenalty = 1.0e6F;
constexpr float kEpsilon = 1.0e-5F;

struct ObstacleVolume {
    std::uint64_t entity = 0U;
    NavObstacle obstacle{};
    Vec3 center{};
    Quat rotation{};
    float halfX = 0.5F;
    float halfZ = 0.5F;
    float radius = 0.5F;
    float halfHeight = 0.5F;
};

struct AgentState {
    SceneEntity entity{};
    NavAgent agent{};
    Vec3 position{};
    Quat rotation{};
    bool character = false;
};

[[nodiscard]] float HorizontalLength(Vec3 value) noexcept {
    return std::sqrt(value.x * value.x + value.z * value.z);
}

[[nodiscard]] float HorizontalDistance(Vec3 a, Vec3 b) noexcept {
    return HorizontalLength(b - a);
}

[[nodiscard]] Vec3 Horizontal(Vec3 value) noexcept {
    return Vec3{ value.x, 0.0F, value.z };
}

[[nodiscard]] Vec3 ToObstacleSpace(const ObstacleVolume& volume, Vec3 point) noexcept {
    return kb::math::Rotate(kb::math::Inverse(volume.rotation), point - volume.center);
}

// The vertical span an agent standing between the two heights occupies overlaps the obstacle.
[[nodiscard]] bool OverlapsVertically(const ObstacleVolume& volume, float lowY, float highY, float agentHeight) noexcept {
    const float bottom = std::min(lowY, highY);
    const float top = std::max(lowY, highY) + std::max(agentHeight, 0.0F);
    return top >= volume.center.y - volume.halfHeight && bottom <= volume.center.y + volume.halfHeight;
}

[[nodiscard]] bool PointInside(const ObstacleVolume& volume, Vec3 point, float inflate, float agentHeight) noexcept {
    if (!OverlapsVertically(volume, point.y, point.y, agentHeight)) return false;
    const Vec3 local = ToObstacleSpace(volume, point);
    if (volume.obstacle.shape == NavObstacleShape::Cylinder) {
        const float reach = volume.radius + inflate;
        return local.x * local.x + local.z * local.z <= reach * reach;
    }
    return std::fabs(local.x) <= volume.halfX + inflate && std::fabs(local.z) <= volume.halfZ + inflate;
}

[[nodiscard]] bool SegmentCrosses(const ObstacleVolume& volume, Vec3 from, Vec3 to, float inflate, float agentHeight) noexcept {
    if (!OverlapsVertically(volume, from.y, to.y, agentHeight)) return false;
    const Vec3 a = ToObstacleSpace(volume, from);
    const Vec3 b = ToObstacleSpace(volume, to);
    const float dx = b.x - a.x;
    const float dz = b.z - a.z;
    if (volume.obstacle.shape == NavObstacleShape::Cylinder) {
        const float lengthSquared = dx * dx + dz * dz;
        float t = lengthSquared > kEpsilon ? -(a.x * dx + a.z * dz) / lengthSquared : 0.0F;
        t = std::clamp(t, 0.0F, 1.0F);
        const float px = a.x + dx * t;
        const float pz = a.z + dz * t;
        const float reach = volume.radius + inflate;
        return px * px + pz * pz <= reach * reach;
    }
    // Slab test of the segment against the inflated rectangle.
    float enter = 0.0F;
    float exit = 1.0F;
    const auto slab = [&enter, &exit](float start, float delta, float half) {
        if (std::fabs(delta) < kEpsilon) return std::fabs(start) <= half;
        float t0 = (-half - start) / delta;
        float t1 = (half - start) / delta;
        if (t0 > t1) std::swap(t0, t1);
        enter = std::max(enter, t0);
        exit = std::min(exit, t1);
        return enter <= exit;
    };
    return slab(a.x, dx, volume.halfX + inflate) && slab(a.z, dz, volume.halfZ + inflate);
}

// A circle that holds the obstacle's footprint, for agents steering around it.
[[nodiscard]] float FootprintRadius(const ObstacleVolume& volume) noexcept {
    return volume.obstacle.shape == NavObstacleShape::Cylinder
        ? volume.radius
        : std::sqrt(volume.halfX * volume.halfX + volume.halfZ * volume.halfZ);
}

// Obstacles and agents are placed in the graph's space (world space minus `origin`).
[[nodiscard]] std::vector<ObstacleVolume> CollectObstacles(Scene& scene, SceneState& state, const kb::math::DVec3& origin) {
    std::vector<std::pair<SceneEntity, NavObstacle>> authored;
    state.world.CreateQuery<NavObstacle>().ForEach(
        [](SceneEntity entity, const NavObstacle& obstacle, void* context) {
            static_cast<std::vector<std::pair<SceneEntity, NavObstacle>>*>(context)->emplace_back(entity, obstacle);
        }, &authored);
    std::vector<ObstacleVolume> volumes;
    volumes.reserve(authored.size());
    for (const auto& [entity, obstacle] : authored) {
        if (!obstacle.enabled) continue;
        const TransformComponent* transform = scene.Transforms().TryGet(entity);
        if (transform == nullptr) continue;
        const Vec3 scale{ std::fabs(transform->worldScale.x), std::fabs(transform->worldScale.y), std::fabs(transform->worldScale.z) };
        ObstacleVolume volume{
            .entity = entity.Id(),
            .obstacle = obstacle,
            .center = kb::math::RelativeTo(scene.Transforms().WorldTranslation(entity, *transform), origin) +
                kb::math::Rotate(transform->worldRotation, Vec3{ obstacle.center.x * transform->worldScale.x,
                    obstacle.center.y * transform->worldScale.y, obstacle.center.z * transform->worldScale.z }),
            .rotation = transform->worldRotation,
        };
        if (obstacle.shape == NavObstacleShape::Cylinder) {
            volume.radius = std::max(obstacle.radius, 0.0F) * std::max(scale.x, scale.z);
            volume.halfHeight = std::max(obstacle.height, 0.0F) * 0.5F * scale.y;
        } else {
            volume.halfX = std::max(obstacle.size.x, 0.0F) * 0.5F * scale.x;
            volume.halfZ = std::max(obstacle.size.z, 0.0F) * 0.5F * scale.z;
            volume.halfHeight = std::max(obstacle.size.y, 0.0F) * 0.5F * scale.y;
        }
        volumes.push_back(volume);
    }
    std::ranges::sort(volumes, {}, &ObstacleVolume::entity);
    return volumes;
}

void HashBytes(std::uint64_t& hash, const void* data, std::size_t size) noexcept {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t index = 0U; index < size; ++index) {
        hash ^= bytes[index];
        hash *= 1099511628211ULL;
    }
}

// Changes whenever an obstacle that shapes paths appears, moves, resizes or goes away.
[[nodiscard]] std::uint64_t ObstacleSignature(std::span<const ObstacleVolume> volumes) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const ObstacleVolume& volume : volumes) {
        if (!volume.obstacle.carve && volume.obstacle.area == kDefaultNavArea) continue;
        const std::array<float, 11U> values{ volume.center.x, volume.center.y, volume.center.z, volume.rotation.x, volume.rotation.y,
            volume.rotation.z, volume.rotation.w, volume.halfX, volume.halfZ, volume.radius, volume.halfHeight };
        HashBytes(hash, &volume.entity, sizeof(volume.entity));
        HashBytes(hash, values.data(), sizeof(float) * values.size());
        const std::array<std::uint8_t, 3U> flags{ static_cast<std::uint8_t>(volume.obstacle.shape),
            static_cast<std::uint8_t>(volume.obstacle.carve), volume.obstacle.area };
        HashBytes(hash, flags.data(), flags.size());
    }
    return hash;
}

[[nodiscard]] std::optional<std::uint32_t> NearestUsableNode(const NavMesh& mesh, Vec3 position, const std::vector<bool>& usable) {
    std::optional<std::uint32_t> best;
    float bestDistance = std::numeric_limits<float>::infinity();
    for (std::uint32_t index = 0U; index < mesh.nodes.size(); ++index) {
        if (!usable[index]) continue;
        const Vec3 delta = mesh.nodes[index].position - position;
        const float distance = kb::math::Dot(delta, delta);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = index;
        }
    }
    return best;
}

[[nodiscard]] bool ObstaclesCross(std::span<const ObstacleVolume> volumes, Vec3 from, Vec3 to, const NavAgent& agent) noexcept {
    return std::ranges::any_of(volumes, [&](const ObstacleVolume& volume) {
        return volume.obstacle.carve && SegmentCrosses(volume, from, to, agent.radius, agent.height);
    });
}

// Plans the agent's path on the graph: the usable nodes are those its areas allow and no carving obstacle
// covers, the usable edges those no carving obstacle stands on. An unreachable destination gives a partial
// path to the reachable node closest to it.
void PlanPath(const NavMesh& sourceMesh, std::span<const ObstacleVolume> volumes, const AgentState& agent, NavAgentRuntimeRecord& record) {
    record.corners.clear();
    record.nextCorner = 0U;
    record.status = NavPathStatus::Failed;
    if (sourceMesh.nodes.empty()) return;

    // Obstacles that do not carve but name an area turn the nodes under them into that area.
    const NavMesh* mesh = &sourceMesh;
    NavMesh areaMesh;
    for (const ObstacleVolume& volume : volumes) {
        if (volume.obstacle.carve || volume.obstacle.area == kDefaultNavArea || !IsValidNavArea(volume.obstacle.area)) continue;
        if (mesh == &sourceMesh) {
            areaMesh = sourceMesh;
            mesh = &areaMesh;
        }
        for (NavMeshNode& node : areaMesh.nodes) {
            if (PointInside(volume, node.position, 0.0F, 0.0F)) node.area = volume.obstacle.area;
        }
    }

    NavQueryFilter filter;
    filter.SetIncludedAreas(agent.agent.areaMask);
    const std::size_t count = mesh->nodes.size();
    std::vector<bool> usable(count, false);
    for (std::size_t index = 0U; index < count; ++index) {
        const NavMeshNode& node = mesh->nodes[index];
        usable[index] = filter.Allows(node.area) && std::ranges::none_of(volumes, [&](const ObstacleVolume& volume) {
            return volume.obstacle.carve && PointInside(volume, node.position, agent.agent.radius, agent.agent.height);
        });
    }
    std::vector<std::uint64_t> blocked;
    for (std::uint32_t from = 0U; from < count; ++from) {
        for (const std::uint32_t to : mesh->nodes[from].neighbours) {
            if (to >= count) continue;
            if (!usable[from] || !usable[to] ||
                ObstaclesCross(volumes, mesh->nodes[from].position, mesh->nodes[to].position, agent.agent)) {
                blocked.push_back(NavEdgeKey(from, to));
            }
        }
    }
    std::ranges::sort(blocked);

    const std::optional<std::uint32_t> start = NearestUsableNode(*mesh, agent.position, usable);
    const std::optional<std::uint32_t> goal = NearestUsableNode(*mesh, agent.agent.destination, usable);
    if (!start.has_value() || !goal.has_value()) return;
    NavPath path = FindNavPath(*mesh, *start, *goal, filter, blocked);
    NavPathStatus status = NavPathStatus::Complete;
    if (!path.Succeeded()) {
        // Breadth-first over the walkable edges, then the reachable node nearest the destination.
        std::vector<bool> reached(count, false);
        std::vector<std::uint32_t> frontier{ *start };
        reached[*start] = true;
        for (std::size_t cursor = 0U; cursor < frontier.size(); ++cursor) {
            const std::uint32_t from = frontier[cursor];
            for (const std::uint32_t to : mesh->nodes[from].neighbours) {
                if (to >= count || reached[to] || std::ranges::binary_search(blocked, NavEdgeKey(from, to))) continue;
                reached[to] = true;
                frontier.push_back(to);
            }
        }
        std::vector<bool> reachable(count, false);
        for (const std::uint32_t node : frontier) reachable[node] = true;
        const std::optional<std::uint32_t> closest = NearestUsableNode(*mesh, agent.agent.destination, reachable);
        if (!closest.has_value()) return;
        path = FindNavPath(*mesh, *start, *closest, filter, blocked);
        if (!path.Succeeded()) return;
        status = NavPathStatus::Partial;
    }
    record.corners = std::move(path.corners);
    if (status == NavPathStatus::Complete) {
        const Vec3 last = record.corners.back();
        const Vec3 offset = agent.agent.destination - last;
        if (kb::math::Dot(offset, offset) > kEpsilon * kEpsilon) {
            if (ObstaclesCross(volumes, last, agent.agent.destination, agent.agent)) {
                status = NavPathStatus::Partial;
            } else {
                record.corners.push_back(agent.agent.destination);
            }
        }
    }
    // Starting beside the second corner, the agent does not walk back to the first.
    if (record.corners.size() >= 2U &&
        HorizontalDistance(agent.position, record.corners[1]) <= HorizontalDistance(record.corners[0], record.corners[1]) &&
        !ObstaclesCross(volumes, agent.position, record.corners[1], agent.agent)) {
        record.corners.erase(record.corners.begin());
    }
    record.status = status;
}

struct Preferred {
    Vec3 velocity{};
    Vec3 target{};
    float remaining = 0.0F;
    bool finalCorner = false;
    bool arrived = false;
};

[[nodiscard]] Preferred PreferredVelocity(const AgentState& agent, NavAgentRuntimeRecord& record, std::span<const ObstacleVolume> volumes) {
    Preferred result;
    if (record.corners.empty() || record.status == NavPathStatus::Failed) {
        result.arrived = record.status != NavPathStatus::Failed;
        return result;
    }
    // A corner counts as reached within half the agent's radius, or once the agent, pushed aside while
    // avoiding, has crossed the line through the corner across its path close beside it.
    const float reach = std::max(agent.agent.radius * 0.5F, 0.05F);
    const float beside = 2.0F * agent.agent.radius + 0.5F;
    while (record.nextCorner + 1U < record.corners.size()) {
        const Vec3 corner = record.corners[record.nextCorner];
        const float distance = HorizontalDistance(agent.position, corner);
        bool reached = distance <= reach;
        if (!reached && record.nextCorner > 0U && distance <= beside) {
            const Vec3 previous = record.corners[record.nextCorner - 1U];
            reached = kb::math::Dot(Horizontal(agent.position - corner), Horizontal(corner - previous)) >= 0.0F &&
                !ObstaclesCross(volumes, agent.position, record.corners[record.nextCorner + 1U], agent.agent);
        }
        // A corner under an obstacle the agent steers around cannot be stood on; the agent heads past it.
        reached = reached || std::ranges::any_of(volumes, [&](const ObstacleVolume& volume) {
            return !volume.obstacle.carve && PointInside(volume, corner, agent.agent.radius, agent.agent.height);
        });
        if (!reached) break;
        ++record.nextCorner;
    }
    result.target = record.corners[record.nextCorner];
    result.finalCorner = record.nextCorner + 1U == record.corners.size();
    result.remaining = HorizontalDistance(agent.position, result.target);
    for (std::size_t index = record.nextCorner; index + 1U < record.corners.size(); ++index) {
        result.remaining += HorizontalDistance(record.corners[index], record.corners[index + 1U]);
    }
    const float stopping = std::max(agent.agent.stoppingDistance, 1.0e-3F);
    if (result.remaining <= stopping) {
        result.arrived = true;
        return result;
    }
    const float distance = HorizontalDistance(agent.position, result.target);
    if (distance <= kEpsilon) return result;
    const Vec3 direction = Horizontal(result.target - agent.position) * (1.0F / distance);
    // Slow down in time to stop at the stopping distance with the agent's acceleration.
    const float braking = std::sqrt(2.0F * std::max(agent.agent.acceleration, 0.0F) * (result.remaining - stopping));
    result.velocity = direction * std::min(std::max(agent.agent.maxSpeed, 0.0F), braking);
    return result;
}

struct Neighbour {
    Vec3 position{};
    Vec3 velocity{};
    float radius = 0.0F;
    bool reciprocal = false;
};

// Seconds until the two discs touch when `self` moves at `relative` against the neighbour; infinity when
// they never do within the horizon, 0 when they already overlap and move closer.
[[nodiscard]] float TimeToCollision(Vec3 offset, Vec3 relative, float radius) noexcept {
    const float distanceSquared = offset.x * offset.x + offset.z * offset.z;
    const float approach = offset.x * relative.x + offset.z * relative.z;
    if (distanceSquared < radius * radius) return approach > 0.0F ? 0.0F : std::numeric_limits<float>::infinity();
    const float speedSquared = relative.x * relative.x + relative.z * relative.z;
    if (speedSquared <= kEpsilon || approach <= 0.0F) return std::numeric_limits<float>::infinity();
    const float discriminant = approach * approach - speedSquared * (distanceSquared - radius * radius);
    if (discriminant < 0.0F) return std::numeric_limits<float>::infinity();
    return (approach - std::sqrt(discriminant)) / speedSquared;
}

// Reciprocal velocity obstacles by sampling: candidate velocities around the preferred one are scored by
// their deviation from it plus a penalty that grows as the earliest collision with another agent or a
// non-carving obstacle comes closer. Against another agent the candidate is judged as the average of
// the agent's current and candidate velocity (each side takes half of the avoidance).
[[nodiscard]] Vec3 AvoidingVelocity(const AgentState& self, Vec3 currentVelocity, Vec3 preferred, std::span<const Neighbour> neighbours) {
    if (neighbours.empty()) return preferred;
    const float maxSpeed = std::max(self.agent.maxSpeed, 0.0F);
    std::vector<Vec3> candidates;
    candidates.reserve(3U * 16U + 2U);
    candidates.push_back(preferred);
    const float preferredSpeed = HorizontalLength(preferred);
    const float heading = preferredSpeed > kEpsilon ? std::atan2(preferred.z, preferred.x)
        : (HorizontalLength(currentVelocity) > kEpsilon ? std::atan2(currentVelocity.z, currentVelocity.x) : 0.0F);
    constexpr std::array<float, 3U> kSpeedScales{ 1.0F, 0.66F, 0.33F };
    for (const float scale : kSpeedScales) {
        const float speed = maxSpeed * scale;
        // Turning left before right at each step keeps two agents meeting head-on from mirroring each other.
        for (int step = 1; step <= 8; ++step) {
            for (const int side : { 1, -1 }) {
                if (step == 8 && side == -1) continue;
                const float angle = heading + static_cast<float>(side * step) * (kb::math::kPi / 8.0F);
                candidates.push_back(Vec3{ std::cos(angle) * speed, 0.0F, std::sin(angle) * speed });
            }
        }
        candidates.push_back(Vec3{ std::cos(heading) * speed, 0.0F, std::sin(heading) * speed });
    }
    candidates.push_back(Vec3{});

    Vec3 best = preferred;
    float bestPenalty = std::numeric_limits<float>::infinity();
    for (const Vec3& candidate : candidates) {
        float earliest = std::numeric_limits<float>::infinity();
        for (const Neighbour& neighbour : neighbours) {
            const Vec3 relative = neighbour.reciprocal
                ? candidate * 2.0F - currentVelocity - neighbour.velocity
                : candidate - neighbour.velocity;
            earliest = std::min(earliest, TimeToCollision(neighbour.position - self.position, relative, self.agent.radius + neighbour.radius));
        }
        float penalty = HorizontalLength(candidate - preferred);
        if (earliest <= 0.0F) {
            penalty += kOverlapPenalty;
        } else if (earliest < kAvoidanceTimeHorizonSeconds) {
            penalty += kCollisionPenaltyWeight / earliest;
        }
        if (penalty < bestPenalty) {
            bestPenalty = penalty;
            best = candidate;
        }
    }
    return best;
}

[[nodiscard]] Vec3 LimitAcceleration(Vec3 current, Vec3 desired, float acceleration, float deltaSeconds, float maxSpeed) noexcept {
    Vec3 change = Horizontal(desired - current);
    const float maxChange = std::max(acceleration, 0.0F) * deltaSeconds;
    const float changeLength = HorizontalLength(change);
    if (changeLength > maxChange && changeLength > 0.0F) change = change * (maxChange / changeLength);
    Vec3 result = Horizontal(current) + change;
    const float speed = HorizontalLength(result);
    if (speed > maxSpeed && speed > 0.0F) result = result * (maxSpeed / speed);
    return result;
}

void WriteAgentTransform(Scene& scene, SceneState& state, const AgentState& agent, const kb::math::DVec3& origin) {
    TransformComponent* transform = scene.Transforms().TryGet(agent.entity);
    if (transform == nullptr) return;
    TransformComponent updated = *transform;
    // The agent's world position, in double precision.
    kb::math::DVec3 local = origin + agent.position;
    Quat localRotation = agent.rotation;
    const SceneEntity parent = scene.Hierarchy().Parent(agent.entity);
    if (parent.IsValid()) {
        if (const TransformComponent* parentTransform = scene.Transforms().TryGet(parent); parentTransform != nullptr) {
            const Quat inverse = kb::math::Inverse(parentTransform->worldRotation);
            const Vec3 scale = parentTransform->worldScale;
            const kb::math::DVec3 parentWorld = scene.Transforms().WorldTranslation(parent, *parentTransform);
            if (origin == kb::math::DVec3{} && parentWorld == kb::math::ToDVec3(parentTransform->worldPosition)) {
                // Everything is in float world space: the float result agents always had.
                const Vec3 relative = kb::math::Rotate(inverse, agent.position - parentTransform->worldPosition);
                local = kb::math::ToDVec3(Vec3{ std::fabs(scale.x) > kEpsilon ? relative.x / scale.x : relative.x,
                    std::fabs(scale.y) > kEpsilon ? relative.y / scale.y : relative.y,
                    std::fabs(scale.z) > kEpsilon ? relative.z / scale.z : relative.z });
            } else {
                const kb::math::DVec3 relative = kb::math::RotateDouble(inverse, local - parentWorld);
                local = kb::math::DVec3{ std::fabs(scale.x) > kEpsilon ? relative.x / scale.x : relative.x,
                    std::fabs(scale.y) > kEpsilon ? relative.y / scale.y : relative.y,
                    std::fabs(scale.z) > kEpsilon ? relative.z / scale.z : relative.z };
            }
            localRotation = inverse * agent.rotation;
        }
    }
    Vec3 residual{};
    SplitTranslation(local, updated.localPosition, residual);
    updated.localRotation = localRotation;
    scene.Transforms().Set(agent.entity, updated);
    SceneTransformPrecision::StoreLocalResidual(state, agent.entity, residual);
}

[[nodiscard]] nav::NavMeshRuntime& Polygons(SceneNavigationState& navigation) {
    if (!navigation.polygons) {
        navigation.polygons = std::make_shared<nav::NavMeshRuntime>();
        navigation.polygons->SetOrigin(navigation.mesh.origin);
    }
    return *navigation.polygons;
}

[[nodiscard]] bool HasPolygons(const SceneNavigationState& navigation) noexcept {
    return navigation.polygons && !navigation.polygons->Empty();
}

// Rotation about the vertical axis of a box obstacle's frame.
[[nodiscard]] float YawOf(Quat rotation) noexcept {
    const Vec3 axis = kb::math::Rotate(rotation, Vec3{ 1.0F, 0.0F, 0.0F });
    return std::atan2(-axis.z, axis.x);
}

// Places the scene's NavObstacles and NavLinks into the polygon meshes' space; the tiles they
// changed are rebuilt.
void SynchronizeOverlays(Scene& scene, SceneState& state) {
    SceneNavigationState& navigation = state.navigation;
    if (!HasPolygons(navigation)) return;
    nav::NavMeshRuntime& runtime = *navigation.polygons;
    runtime.SetOrigin(navigation.mesh.origin);
    std::vector<nav::NavRuntimeObstacle> obstacles;
    for (const ObstacleVolume& volume : CollectObstacles(scene, state, navigation.mesh.origin)) {
        obstacles.push_back(nav::NavRuntimeObstacle{
            .id = volume.entity,
            .shape = volume.obstacle.shape,
            .center = volume.center,
            .yaw = volume.obstacle.shape == NavObstacleShape::Box ? YawOf(volume.rotation) : 0.0F,
            .halfExtents = { volume.halfX, volume.halfHeight, volume.halfZ },
            .radius = volume.radius,
            .height = volume.halfHeight * 2.0F,
            .carve = volume.obstacle.carve,
            .area = volume.obstacle.area,
        });
    }
    runtime.SetObstacles(std::move(obstacles));

    std::vector<std::pair<SceneEntity, NavLink>> authored;
    state.world.CreateQuery<NavLink>().ForEach(
        [](SceneEntity entity, const NavLink& link, void* context) {
            static_cast<std::vector<std::pair<SceneEntity, NavLink>>*>(context)->emplace_back(entity, link);
        }, &authored);
    std::vector<nav::NavRuntimeLink> links;
    links.reserve(authored.size());
    for (const auto& [entity, link] : authored) {
        const TransformComponent* transform = scene.Transforms().TryGet(entity);
        if (!link.enabled || transform == nullptr || !IsValidNavArea(link.area) || !(link.radius > 0.0F)) continue;
        const kb::math::DVec3 world = scene.Transforms().WorldTranslation(entity, *transform);
        const auto place = [&](Vec3 offset) {
            const Vec3 scaled{ offset.x * transform->worldScale.x, offset.y * transform->worldScale.y, offset.z * transform->worldScale.z };
            return kb::math::RelativeTo(world + kb::math::ToDVec3(kb::math::Rotate(transform->worldRotation, scaled)), navigation.mesh.origin);
        };
        links.push_back(nav::NavRuntimeLink{
            .id = entity.Id(),
            .start = place(link.start),
            .end = place(link.end),
            .radius = link.radius,
            .kind = link.kind,
            .area = link.area,
            .bidirectional = link.bidirectional,
        });
    }
    runtime.SetLinks(std::move(links));
}

// Where agents update fully: the focuses set from code, else every enabled Stream Focus.
[[nodiscard]] std::vector<Vec3> CrowdFocuses(Scene& scene, SceneState& state) {
    const SceneNavigationState& navigation = state.navigation;
    std::vector<Vec3> focuses;
    if (navigation.crowdFocusesSet) {
        for (const kb::math::DVec3& focus : navigation.crowdFocuses) focuses.push_back(kb::math::RelativeTo(focus, navigation.mesh.origin));
        return focuses;
    }
    std::vector<SceneEntity> entities;
    state.world.CreateQuery<StreamFocusComponent>().ForEach(
        [](SceneEntity entity, const StreamFocusComponent& focus, void* context) {
            if (focus.enabled) static_cast<std::vector<SceneEntity>*>(context)->push_back(entity);
        }, &entities);
    std::ranges::sort(entities, {}, &SceneEntity::Id);
    for (const SceneEntity entity : entities) {
        if (const TransformComponent* transform = scene.Transforms().TryGet(entity); transform != nullptr) {
            focuses.push_back(kb::math::RelativeTo(scene.Transforms().WorldTranslation(entity, *transform), navigation.mesh.origin));
        }
    }
    return focuses;
}

// Moves the agents over the polygon meshes as a crowd.
void StepPolygonNavigation(Scene& scene, SceneState& state, std::span<const std::pair<SceneEntity, NavAgent>> authored, float deltaSeconds,
    std::uint32_t steps) {
    SceneNavigationState& navigation = state.navigation;
    nav::NavMeshRuntime& runtime = *navigation.polygons;
    SynchronizeOverlays(scene, state);
    if (!navigation.crowd) navigation.crowd = std::make_shared<nav::NavCrowd>();

    std::vector<AgentState> agents;
    std::vector<nav::NavCrowdAgentInput> inputs;
    agents.reserve(authored.size());
    inputs.reserve(authored.size());
    for (const auto& [entity, agent] : authored) {
        const TransformComponent* transform = scene.Transforms().TryGet(entity);
        if (transform == nullptr) continue;
        const Vec3 position = kb::math::RelativeTo(scene.Transforms().WorldTranslation(entity, *transform), navigation.mesh.origin);
        agents.push_back(AgentState{
            .entity = entity,
            .agent = agent,
            .position = position,
            .rotation = transform->worldRotation,
            .character = scene.Components().CharacterControllers().Has(entity),
        });
        inputs.push_back(nav::NavCrowdAgentInput{ .id = entity.Id(), .agent = agent, .position = position });
    }
    std::vector<nav::NavCrowdCircle> circles;
    for (const nav::NavRuntimeObstacle& obstacle : runtime.Obstacles()) {
        if (obstacle.carve || obstacle.area != kDefaultNavArea) continue;
        const float radius = obstacle.shape == NavObstacleShape::Cylinder
            ? obstacle.radius
            : std::sqrt(obstacle.halfExtents.x * obstacle.halfExtents.x + obstacle.halfExtents.z * obstacle.halfExtents.z);
        const float half = obstacle.shape == NavObstacleShape::Cylinder ? obstacle.height * 0.5F : obstacle.halfExtents.y;
        circles.push_back(nav::NavCrowdCircle{ .center = obstacle.center, .radius = radius, .bottom = obstacle.center.y - half, .top = obstacle.center.y + half });
    }
    const std::vector<Vec3> focuses = CrowdFocuses(scene, state);
    std::vector<nav::NavCrowdAgentOutput> outputs;
    for (std::uint32_t step = 0U; step < steps; ++step) {
        navigation.crowd->Step(runtime, inputs, circles, focuses, navigation.crowdSettings, deltaSeconds, outputs);
        for (std::size_t index = 0U; index < inputs.size(); ++index) inputs[index].position = outputs[index].position;
    }
    const float elapsed = deltaSeconds * static_cast<float>(steps);
    for (std::size_t index = 0U; index < agents.size(); ++index) {
        AgentState& agent = agents[index];
        const nav::NavCrowdAgentOutput& output = outputs[index];
        if (NavAgent* component = state.componentStorage.Navigation().TryGetNavAgent(agent.entity); component != nullptr) {
            component->velocity = agent.agent.enabled ? output.velocity : Vec3{};
            component->remainingDistance = output.remainingDistance;
            component->pathStatus = output.status;
            state.componentStorage.Navigation().MarkNavAgentModified(agent.entity);
        }
        if (!agent.agent.enabled || (output.status == NavPathStatus::Failed && !output.onLink && HorizontalLength(output.velocity) <= 0.0F)) {
            continue;
        }
        if (agent.character && !output.onLink && PhysicsBackend::CharacterMove(scene, agent.entity, output.velocity)) continue;
        agent.position = output.position;
        const float speed = HorizontalLength(output.velocity);
        if (!agent.character && speed > kEpsilon) {
            const Vec3 forward = Vec3{ output.velocity.x, 0.0F, output.velocity.z } * (1.0F / speed);
            const Quat facing = kb::math::LookRotation(forward, Vec3{ 0.0F, 1.0F, 0.0F });
            agent.rotation = kb::math::RotateTowards(agent.rotation, facing,
                kb::math::ToRadians(kb::math::Degrees{ std::max(agent.agent.angularSpeedDegrees, 0.0F) * elapsed }));
        }
        WriteAgentTransform(scene, state, agent, navigation.mesh.origin);
    }
}

// Adds the navigation meshes ContentInstances of kind NavigationMesh place while the scene plays,
// and removes them when their owner goes away, changes asset or the scene stops.
void SynchronizePlacedNavMeshes(Scene& scene, SceneState& state, bool playing) {
    SceneNavigationState& navigation = state.navigation;
    std::map<std::uint64_t, std::uint64_t> wanted;
    if (playing) {
        state.world.CreateQuery<ContentInstanceComponent>().ForEach(
            [](SceneEntity entity, const ContentInstanceComponent& content, void* context) {
                if (content.active && content.kind == ContentInstanceKind::NavigationMesh && content.assetId != 0U) {
                    static_cast<std::map<std::uint64_t, std::uint64_t>*>(context)->emplace(entity.Id(), content.assetId);
                }
            }, &wanted);
    }
    SceneNavigation api{ scene };
    for (auto record = navigation.placedMeshes.begin(); record != navigation.placedMeshes.end();) {
        const auto found = wanted.find(record->first);
        if (found != wanted.end() && found->second == record->second.assetId) {
            ++record;
            continue;
        }
        if (record->second.handle != 0U) static_cast<void>(api.RemoveNavMesh(record->second.handle));
        record = navigation.placedMeshes.erase(record);
    }
    if (wanted.empty()) return;
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    for (const auto& [owner, assetId] : wanted) {
        PlacedNavMeshRecord& record = navigation.placedMeshes[owner];
        record.assetId = assetId;
        if (record.handle != 0U || record.failed) continue;
        const kb::assets::AssetId id{ assetId };
        if (!record.requested) {
            if (!manager.LoadAsync<nav::NavMeshAsset>(id)) {
                record.failed = true;
                continue;
            }
            record.requested = true;
        }
        manager.PumpAsyncLoads();
        const kb::assets::AssetHandle<nav::NavMeshAsset> handle = manager.AcquireLoaded<nav::NavMeshAsset>(id);
        if (!handle.IsLoaded()) {
            record.failed = manager.AsyncLoadStatus(id) == kb::assets::AsyncAssetLoadStatus::Failed;
            continue;
        }
        record.handle = api.AddNavMesh(handle.Shared());
        record.failed = record.handle == 0U;
    }
}

} // namespace

void StepSceneNavigation(Scene& scene, float deltaSeconds, std::uint32_t steps) {
    SceneState& state = SceneAccess::State(scene);
    SceneNavigationState& navigation = state.navigation;
    std::vector<std::pair<SceneEntity, NavAgent>> authored;
    state.world.CreateQuery<NavAgent>().ForEach(
        [](SceneEntity entity, const NavAgent& agent, void* context) {
            static_cast<std::vector<std::pair<SceneEntity, NavAgent>>*>(context)->emplace_back(entity, agent);
        }, &authored);
    std::ranges::sort(authored, {}, [](const auto& entry) { return entry.first.Id(); });

    // Forget agents that are gone.
    for (auto record = navigation.agents.begin(); record != navigation.agents.end();) {
        const bool present = std::ranges::binary_search(authored, record->first, {}, [](const auto& entry) { return entry.first.Id(); });
        record = present ? std::next(record) : navigation.agents.erase(record);
    }
    if (authored.empty() || !(deltaSeconds > 0.0F) || steps == 0U) return;
    if (HasPolygons(navigation)) {
        StepPolygonNavigation(scene, state, authored, deltaSeconds, steps);
        return;
    }

    std::vector<AgentState> agents;
    agents.reserve(authored.size());
    for (const auto& [entity, agent] : authored) {
        const TransformComponent* transform = scene.Transforms().TryGet(entity);
        if (transform == nullptr) continue;
        agents.push_back(AgentState{
            .entity = entity,
            .agent = agent,
            .position = kb::math::RelativeTo(scene.Transforms().WorldTranslation(entity, *transform), navigation.mesh.origin),
            .rotation = transform->worldRotation,
            .character = scene.Components().CharacterControllers().Has(entity),
        });
    }
    const std::vector<ObstacleVolume> volumes = CollectObstacles(scene, state, navigation.mesh.origin);
    const std::uint64_t signature = ObstacleSignature(volumes);

    std::vector<Neighbour> neighbours;
    std::vector<Vec3> nextVelocities(agents.size());
    for (std::uint32_t step = 0U; step < steps; ++step) {
        for (std::size_t index = 0U; index < agents.size(); ++index) {
            AgentState& agent = agents[index];
            NavAgentRuntimeRecord& record = navigation.agents[agent.entity.Id()];
            nextVelocities[index] = Vec3{};
            if (!agent.agent.enabled) continue;
            const bool replan = !record.planned || record.plannedMeshRevision != navigation.mesh.revision ||
                record.plannedObstacleSignature != signature || record.plannedAreas != agent.agent.areaMask ||
                record.plannedRadius != agent.agent.radius ||
                std::memcmp(&record.plannedDestination, &agent.agent.destination, sizeof(Vec3)) != 0;
            if (replan) {
                PlanPath(navigation.mesh, volumes, agent, record);
                record.planned = true;
                record.plannedMeshRevision = navigation.mesh.revision;
                record.plannedObstacleSignature = signature;
                record.plannedAreas = agent.agent.areaMask;
                record.plannedRadius = agent.agent.radius;
                record.plannedDestination = agent.agent.destination;
            }
            const Preferred preferred = PreferredVelocity(agent, record, volumes);
            agent.agent.pathStatus = record.status;
            agent.agent.remainingDistance = preferred.remaining;
            if (preferred.arrived) continue;

            neighbours.clear();
            const float maxSpeed = std::max(agent.agent.maxSpeed, 0.0F);
            for (std::size_t other = 0U; other < agents.size(); ++other) {
                if (other == index) continue;
                const AgentState& neighbour = agents[other];
                const float range = 2.0F * maxSpeed * kAvoidanceTimeHorizonSeconds + agent.agent.radius + neighbour.agent.radius;
                if (HorizontalDistance(agent.position, neighbour.position) > range) continue;
                neighbours.push_back(Neighbour{ .position = neighbour.position, .velocity = neighbour.agent.enabled ? neighbour.agent.velocity : Vec3{},
                    .radius = neighbour.agent.radius, .reciprocal = neighbour.agent.enabled });
            }
            for (const ObstacleVolume& volume : volumes) {
                if (volume.obstacle.carve) continue;
                const float range = maxSpeed * kAvoidanceTimeHorizonSeconds + agent.agent.radius + FootprintRadius(volume);
                if (HorizontalDistance(agent.position, volume.center) > range ||
                    !OverlapsVertically(volume, agent.position.y, agent.position.y, agent.agent.height)) continue;
                neighbours.push_back(Neighbour{ .position = volume.center, .radius = FootprintRadius(volume) });
            }
            const Vec3 desired = AvoidingVelocity(agent, agent.agent.velocity, preferred.velocity, neighbours);
            Vec3 velocity = LimitAcceleration(agent.agent.velocity, desired, agent.agent.acceleration, deltaSeconds, maxSpeed);
            if (preferred.finalCorner) {
                // Never step past the end of the path.
                const float distance = HorizontalDistance(agent.position, preferred.target);
                const float speed = HorizontalLength(velocity);
                if (speed * deltaSeconds > distance && speed > 0.0F) velocity = velocity * (distance / (speed * deltaSeconds));
            }
            nextVelocities[index] = velocity;
        }
        // Every agent chose from the same snapshot; now all of them move.
        for (std::size_t index = 0U; index < agents.size(); ++index) {
            AgentState& agent = agents[index];
            const Vec3 velocity = nextVelocities[index];
            agent.agent.velocity = velocity;
            const float stepLength = HorizontalLength(velocity) * deltaSeconds;
            if (stepLength <= 0.0F) continue;
            NavAgentRuntimeRecord& record = navigation.agents[agent.entity.Id()];
            Vec3 next = agent.position + velocity * deltaSeconds;
            if (!record.corners.empty()) {
                // Follow the path's height between corners.
                const Vec3 target = record.corners[std::min(record.nextCorner, record.corners.size() - 1U)];
                const float distance = HorizontalDistance(agent.position, target);
                next.y = distance > kEpsilon ? agent.position.y + (target.y - agent.position.y) * std::min(1.0F, stepLength / distance) : target.y;
            }
            agent.position = next;
            if (!agent.character) {
                const Vec3 forward = Vec3{ velocity.x, 0.0F, velocity.z } * (1.0F / HorizontalLength(velocity));
                const Quat facing = kb::math::LookRotation(forward, Vec3{ 0.0F, 1.0F, 0.0F });
                agent.rotation = kb::math::RotateTowards(agent.rotation, facing,
                    kb::math::ToRadians(kb::math::Degrees{ std::max(agent.agent.angularSpeedDegrees, 0.0F) * deltaSeconds }));
            }
        }
    }

    for (const AgentState& agent : agents) {
        if (NavAgent* component = state.componentStorage.Navigation().TryGetNavAgent(agent.entity); component != nullptr) {
            component->velocity = agent.agent.velocity;
            component->remainingDistance = agent.agent.remainingDistance;
            component->pathStatus = agent.agent.pathStatus;
            state.componentStorage.Navigation().MarkNavAgentModified(agent.entity);
        }
        if (!agent.agent.enabled) continue;
        // A character's own physics moves it; everything else is placed directly.
        if (agent.character && PhysicsBackend::CharacterMove(scene, agent.entity, agent.agent.velocity)) continue;
        WriteAgentTransform(scene, state, agent, navigation.mesh.origin);
    }
}

void NavigationSceneSystem::OnUpdate(SceneSystemContext& context) {
    Scene& scene = context.GetScene();
    SceneState& state = SceneAccess::State(scene);
    SceneNavigationState& navigation = state.navigation;
    SynchronizePlacedNavMeshes(scene, state, state.isPlaying && state.mode != SceneMode::PrefabPrivate);
    if (!state.isPlaying || SceneEntityCounter::CountWithComponent(state.world, state.components.NavAgentComponentId()) == 0U) {
        navigation.stepAccumulator = 0.0F;
        if (SceneEntityCounter::CountWithComponent(state.world, state.components.NavAgentComponentId()) == 0U) {
            navigation.agents.clear();
            if (navigation.crowd) navigation.crowd->Clear();
        }
        return;
    }
    // Agents step at the scene's fixed rate, so the same play time gives the same motion whatever the frame
    // rate was.
    const SceneRuntimeFixedStepSettings fixed = scene.Runtime().FixedStepSettings();
    const float step = fixed.fixedDeltaSeconds > 0.0F ? fixed.fixedDeltaSeconds : kSceneRuntimeDefaultFixedDeltaSeconds;
    const std::size_t maxSteps = fixed.maxFixedStepsPerFrame > 0U ? fixed.maxFixedStepsPerFrame : kSceneRuntimeDefaultMaxFixedStepsPerFrame;
    navigation.stepAccumulator += std::clamp(context.DeltaSeconds(), 0.0F, std::max(fixed.maxFrameDeltaSeconds, step));
    std::uint32_t steps = 0U;
    while (navigation.stepAccumulator >= step && steps < maxSteps) {
        navigation.stepAccumulator -= step;
        ++steps;
    }
    if (steps == maxSteps) navigation.stepAccumulator = std::min(navigation.stepAccumulator, step);
    if (steps != 0U) StepSceneNavigation(scene, step, steps);
}

SceneNavigation::SceneNavigation(Scene& scene) noexcept : scene_(scene) {}

void SceneNavigation::SetMesh(NavMesh mesh) {
    SceneNavigationState& navigation = SceneAccess::State(scene_).navigation;
    mesh.revision = std::max(mesh.revision, navigation.mesh.revision + 1U);
    navigation.mesh = std::move(mesh);
    if (navigation.polygons) navigation.polygons->SetOrigin(navigation.mesh.origin);
}

void SceneNavigation::ClearMesh() {
    SceneNavigationState& navigation = SceneAccess::State(scene_).navigation;
    const std::uint64_t revision = navigation.mesh.revision + 1U;
    navigation.mesh = NavMesh{};
    navigation.mesh.revision = revision;
}

const NavMesh& SceneNavigation::Mesh() const noexcept {
    return SceneAccess::State(scene_).navigation.mesh;
}

std::optional<std::uint32_t> SceneNavigation::NearestNode(kb::math::Vec3 position, NavAreaMask areas) const {
    const NavMesh& mesh = Mesh();
    NavQueryFilter filter;
    filter.SetIncludedAreas(areas);
    std::vector<bool> usable(mesh.nodes.size(), false);
    for (std::size_t index = 0U; index < mesh.nodes.size(); ++index) usable[index] = filter.Allows(mesh.nodes[index].area);
    return NearestUsableNode(mesh, position, usable);
}

std::vector<kb::math::Vec3> SceneNavigation::AgentPath(SceneEntity agent) const {
    const SceneNavigationState& navigation = SceneAccess::State(scene_).navigation;
    if (HasPolygons(navigation) && navigation.crowd) {
        return navigation.crowd->Corners(agent.Id());
    }
    const auto found = navigation.agents.find(agent.Id());
    if (found == navigation.agents.end() || found->second.nextCorner >= found->second.corners.size()) return {};
    return { found->second.corners.begin() + static_cast<std::ptrdiff_t>(found->second.nextCorner), found->second.corners.end() };
}

std::uint64_t SceneNavigation::AddNavMesh(std::shared_ptr<const kb::navigation::NavMeshAsset> asset, std::string* error) {
    std::string message;
    const std::uint64_t handle = Polygons(SceneAccess::State(scene_).navigation).AddTileSet(std::move(asset), message);
    if (error != nullptr) *error = std::move(message);
    return handle;
}

bool SceneNavigation::RemoveNavMesh(std::uint64_t handle) {
    SceneNavigationState& navigation = SceneAccess::State(scene_).navigation;
    return navigation.polygons && navigation.polygons->RemoveTileSet(handle);
}

bool SceneNavigation::HasNavMesh() const noexcept {
    return HasPolygons(SceneAccess::State(scene_).navigation);
}

std::vector<kb::navigation::NavAgentProfile> SceneNavigation::Profiles() const {
    const SceneNavigationState& navigation = SceneAccess::State(scene_).navigation;
    if (!HasPolygons(navigation) || navigation.polygons->Layout() == nullptr) return {};
    return navigation.polygons->Layout()->profiles;
}

std::size_t SceneNavigation::NavMeshTileCount(std::uint32_t profile) const noexcept {
    const SceneNavigationState& navigation = SceneAccess::State(scene_).navigation;
    return navigation.polygons ? navigation.polygons->ActiveTileCount(profile) : 0U;
}

bool SceneNavigation::HasNavMeshTile(std::uint32_t profile, kb::navigation::NavTileCoord coord) const noexcept {
    const SceneNavigationState& navigation = SceneAccess::State(scene_).navigation;
    return navigation.polygons && navigation.polygons->HasTile(profile, coord);
}

std::size_t SceneNavigation::NavMeshTileRebuilds() const noexcept {
    const SceneNavigationState& navigation = SceneAccess::State(scene_).navigation;
    return navigation.polygons ? navigation.polygons->TileRebuilds() : 0U;
}

NavPathResult SceneNavigation::FindPath(const kb::math::DVec3& start, const kb::math::DVec3& end, const NavQueryOptions& options) {
    SceneState& state = SceneAccess::State(scene_);
    SceneNavigationState& navigation = state.navigation;
    NavPathResult result;
    if (!HasPolygons(navigation) || options.profile >= navigation.polygons->ProfileCount()) return result;
    SynchronizeOverlays(scene_, state);
    nav::NavAreaFilter filter;
    navigation.polygons->ConfigureFilter(filter, options.areas);
    const kb::math::DVec3& origin = navigation.mesh.origin;
    const nav::NavStraightPath path = navigation.polygons->FindPath(options.profile, kb::math::RelativeTo(start, origin),
        kb::math::RelativeTo(end, origin), options.searchExtents, filter);
    result.status = path.status;
    for (std::size_t corner = 0U; corner < path.corners.size(); ++corner) {
        result.corners.push_back(origin + path.corners[corner]);
        if (corner != 0U) result.length += kb::math::Distance(path.corners[corner - 1U], path.corners[corner]);
    }
    return result;
}

NavRaycastResult SceneNavigation::Raycast(const kb::math::DVec3& start, const kb::math::DVec3& end, const NavQueryOptions& options) {
    SceneState& state = SceneAccess::State(scene_);
    SceneNavigationState& navigation = state.navigation;
    NavRaycastResult result;
    if (!HasPolygons(navigation) || options.profile >= navigation.polygons->ProfileCount()) return result;
    SynchronizeOverlays(scene_, state);
    nav::NavAreaFilter filter;
    navigation.polygons->ConfigureFilter(filter, options.areas);
    const kb::math::DVec3& origin = navigation.mesh.origin;
    const nav::NavRaycast ray = navigation.polygons->Raycast(options.profile, kb::math::RelativeTo(start, origin), kb::math::RelativeTo(end, origin),
        options.searchExtents, filter);
    result.valid = ray.valid;
    result.hit = ray.hit;
    result.fraction = ray.fraction;
    result.position = origin + ray.position;
    result.normal = ray.normal;
    return result;
}

std::optional<kb::math::DVec3> SceneNavigation::NearestPoint(const kb::math::DVec3& position, const NavQueryOptions& options) {
    SceneState& state = SceneAccess::State(scene_);
    SceneNavigationState& navigation = state.navigation;
    if (!HasPolygons(navigation) || options.profile >= navigation.polygons->ProfileCount()) return std::nullopt;
    SynchronizeOverlays(scene_, state);
    nav::NavAreaFilter filter;
    navigation.polygons->ConfigureFilter(filter, options.areas);
    const auto nearest = navigation.polygons->Nearest(options.profile, kb::math::RelativeTo(position, navigation.mesh.origin), options.searchExtents, filter);
    if (!nearest) return std::nullopt;
    return navigation.mesh.origin + nearest->second;
}

std::vector<kb::math::DVec3> SceneNavigation::NavMeshTriangles(std::uint32_t profile) const {
    const SceneNavigationState& navigation = SceneAccess::State(scene_).navigation;
    std::vector<kb::math::DVec3> triangles;
    if (!HasPolygons(navigation)) return triangles;
    for (const Vec3& corner : navigation.polygons->DebugTriangles(profile)) triangles.push_back(navigation.mesh.origin + corner);
    return triangles;
}

double SceneNavigation::NavMeshWalkableArea(std::uint32_t profile) const {
    const SceneNavigationState& navigation = SceneAccess::State(scene_).navigation;
    return HasPolygons(navigation) ? navigation.polygons->WalkableArea(profile) : 0.0;
}

void SceneNavigation::SetAreaCost(NavAreaId area, float cost) noexcept {
    Polygons(SceneAccess::State(scene_).navigation).SetAreaCost(area, cost);
}

float SceneNavigation::AreaCost(NavAreaId area) const noexcept {
    const SceneNavigationState& navigation = SceneAccess::State(scene_).navigation;
    if (!IsValidNavArea(area)) return 0.0F;
    return navigation.polygons ? navigation.polygons->AreaCost(area) : 1.0F;
}

void SceneNavigation::ConfigureCrowd(const NavCrowdSettings& settings) {
    if (!std::isfinite(settings.nearDistance) || settings.nearDistance < 0.0F || settings.farUpdateInterval == 0U ||
        settings.maxPathSearchesPerStep == 0U || !std::isfinite(settings.neighbourRangeRadii) || settings.neighbourRangeRadii <= 0.0F ||
        !std::isfinite(settings.separationWeight) || settings.separationWeight < 0.0F) {
        throw std::invalid_argument("Crowd settings need a non-negative near distance, a positive update interval, search budget and range");
    }
    SceneAccess::State(scene_).navigation.crowdSettings = settings;
}

NavCrowdSettings SceneNavigation::CrowdSettings() const noexcept {
    return SceneAccess::State(scene_).navigation.crowdSettings;
}

NavCrowdStats SceneNavigation::CrowdStats() const noexcept {
    const SceneNavigationState& navigation = SceneAccess::State(scene_).navigation;
    return navigation.crowd ? navigation.crowd->Stats() : NavCrowdStats{};
}

void SceneNavigation::SetCrowdFocuses(std::vector<kb::math::DVec3> focuses) {
    SceneNavigationState& navigation = SceneAccess::State(scene_).navigation;
    navigation.crowdFocuses = std::move(focuses);
    navigation.crowdFocusesSet = true;
}

bool SceneNavigation::AgentOnLink(SceneEntity agent) const noexcept {
    const SceneNavigationState& navigation = SceneAccess::State(scene_).navigation;
    return navigation.crowd && navigation.crowd->OnLink(agent.Id());
}

} // namespace kb::scene
