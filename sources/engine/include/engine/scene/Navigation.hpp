#pragma once

#include "engine/math/EngineMath.hpp"

#include <array>
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <limits>
#include <queue>
#include <mutex>
#include <span>
#include <thread>
#include <vector>

namespace kb::scene {

// LIB-183: navigation area ids are deliberately bounded to 32 so filters
// remain one machine-word mask in pathfinding hot paths. Area 0 is always
// available and represents ordinary walkable ground.
using NavAreaId = std::uint8_t;
inline constexpr NavAreaId kDefaultNavArea = 0U;
inline constexpr std::uint32_t kNavAreaCount = 32U;
using NavAreaMask = std::uint32_t;
inline constexpr NavAreaMask kAllNavAreas = std::numeric_limits<NavAreaMask>::max();

[[nodiscard]] constexpr bool IsValidNavArea(NavAreaId area) noexcept {
    return area < kNavAreaCount;
}

[[nodiscard]] constexpr NavAreaMask NavAreaBit(NavAreaId area) noexcept {
    return IsValidNavArea(area) ? (NavAreaMask{ 1U } << area) : 0U;
}

struct NavMeshNode {
    kb::math::Vec3 position{};
    NavAreaId area = kDefaultNavArea;
    // Baked, directed neighbour indices. Authoring/import must keep these in
    // ascending order; FindPath preserves that order when costs tie.
    std::vector<std::uint32_t> neighbours;
};

// Baked navigation graph. Nodes are immutable while a path query is running,
// so a worker can safely receive a copy in LIB-184's asynchronous request.
struct NavMesh {
    // Bumping revision after a bake/reload invalidates every retained path
    // built from the older topology. The mesh remains value-owned so a live
    // async request can still complete safely during scene unload.
    std::uint64_t revision = 1U;
    float agentRadius = 0.5F;
    float agentHeight = 2.0F;
    float agentMaxClimb = 0.4F;
    float agentMaxSlopeDegrees = 45.0F;
    float cellSize = 0.2F;
    float cellHeight = 0.1F;
    std::vector<NavMeshNode> nodes;
};

enum class NavPathStatus : std::uint8_t { Invalid, Pending, Complete, Partial, Failed, Cancelled };

struct NavPath {
    NavPathStatus status = NavPathStatus::Invalid;
    std::vector<kb::math::Vec3> corners;
    float totalCost = 0.0F;
    std::uint64_t meshRevision = 0U;
    [[nodiscard]] bool Succeeded() const noexcept { return status == NavPathStatus::Complete || status == NavPathStatus::Partial; }
    [[nodiscard]] bool IsCurrent(const NavMesh& mesh) const noexcept { return Succeeded() && meshRevision == mesh.revision; }
};

struct NavAgent {
    float radius = 0.5F;
    float height = 2.0F;
    float maxSpeed = 3.5F;
    float acceleration = 8.0F;
    float angularSpeedDegrees = 360.0F;
    float stoppingDistance = 0.1F;
    NavAreaMask areaMask = kAllNavAreas;
    kb::math::Vec3 destination{};
    kb::math::Vec3 velocity{};
    float remainingDistance = 0.0F;
    NavPathStatus pathStatus = NavPathStatus::Invalid;
    bool enabled = true;
};

enum class NavObstacleShape : std::uint8_t {
    Box,
    Cylinder,
};

struct NavObstacle {
    NavObstacleShape shape = NavObstacleShape::Box;
    kb::math::Vec3 center{};
    kb::math::Vec3 size{ 1.0F, 1.0F, 1.0F };
    float radius = 0.5F;
    float height = 1.0F;
    NavAreaId area = kDefaultNavArea;
    bool carve = true;
    bool enabled = true;
};

struct NavSteeringResult {
    kb::math::Vec3 desiredVelocity{};
    bool arrived = false;
};

// Snapshot supplied by the owner of a navigation crowd query. Ordering is
// significant: callers must provide a deterministic entity-id order, so the
// accumulated correction is replayable across machines.
struct NavAvoidanceNeighbor {
    kb::math::Vec3 position{};
    float radius = 0.5F;
    bool enabled = true;
};

// Local horizontal avoidance is deliberately only a velocity recommendation.
// It neither queries nor writes physics state: CharacterMove/physics remains
// responsible for collision, grounding, gravity and the final movement.
[[nodiscard]] inline kb::math::Vec3 ComputeNavAvoidance(
    const NavAgent& agent,
    const kb::math::Vec3& position,
    const kb::math::Vec3& desiredVelocity,
    std::span<const NavAvoidanceNeighbor> neighbours) noexcept {
    float correctionX = 0.0F;
    float correctionZ = 0.0F;
    std::size_t neighbourIndex = 0U;
    for (const NavAvoidanceNeighbor& neighbour : neighbours) {
        if (!neighbour.enabled || !(neighbour.radius > 0.0F)) continue;
        const float dx = position.x - neighbour.position.x;
        const float dz = position.z - neighbour.position.z;
        const float distanceSquared = dx * dx + dz * dz;
        const float separation = agent.radius + neighbour.radius;
        if (!(separation > 0.0F) || distanceSquared >= separation * separation) continue;

        float normalX = 0.0F;
        float normalZ = 0.0F;
        float weight = 1.0F;
        if (distanceSquared > 0.0F) {
            const float distance = std::sqrt(distanceSquared);
            normalX = dx / distance;
            normalZ = dz / distance;
            weight = (separation - distance) / separation;
        } else {
            const float desiredMagnitudeSquared =
                desiredVelocity.x * desiredVelocity.x + desiredVelocity.z * desiredVelocity.z;
            if (desiredMagnitudeSquared > 0.0F) {
                const float inverseMagnitude = 1.0F / std::sqrt(desiredMagnitudeSquared);
                normalX = -desiredVelocity.x * inverseMagnitude;
                normalZ = -desiredVelocity.z * inverseMagnitude;
            } else if ((neighbourIndex & 1U) == 0U) {
                normalX = 1.0F;
            } else {
                normalZ = 1.0F;
            }
        }
        correctionX += normalX * agent.maxSpeed * weight;
        correctionZ += normalZ * agent.maxSpeed * weight;
        ++neighbourIndex;
    }
    float velocityX = desiredVelocity.x + correctionX;
    float velocityZ = desiredVelocity.z + correctionZ;
    const float magnitudeSquared = velocityX * velocityX + velocityZ * velocityZ;
    const float maxSpeedSquared = agent.maxSpeed * agent.maxSpeed;
    if (magnitudeSquared > maxSpeedSquared && magnitudeSquared > 0.0F) {
        const float scale = agent.maxSpeed / std::sqrt(magnitudeSquared);
        velocityX *= scale;
        velocityZ *= scale;
    }
    return { velocityX, 0.0F, velocityZ };
}

// Produces only horizontal input for PhysicsBackend::CharacterMove. It never
// writes Transform, Rigidbody or CharacterController state: the live physics
// backend remains the exclusive collision/grounding authority.
[[nodiscard]] inline NavSteeringResult ComputeNavSteering(
    const NavAgent& agent, const kb::math::Vec3& position, const kb::math::Vec3& currentVelocity, float deltaSeconds) noexcept {
    const float dx = agent.destination.x - position.x;
    const float dz = agent.destination.z - position.z;
    const float distance = std::sqrt(dx * dx + dz * dz);
    if (!agent.enabled || distance <= agent.stoppingDistance) return NavSteeringResult{ .arrived = true };
    const float invDistance = 1.0F / distance;
    const float desiredSpeed = agent.maxSpeed * std::min(1.0F, (distance - agent.stoppingDistance) / std::max(agent.stoppingDistance, 0.001F));
    const float targetX = dx * invDistance * desiredSpeed;
    const float targetZ = dz * invDistance * desiredSpeed;
    const float maxDelta = std::max(0.0F, agent.acceleration * std::max(0.0F, deltaSeconds));
    const float changeX = std::clamp(targetX - currentVelocity.x, -maxDelta, maxDelta);
    const float changeZ = std::clamp(targetZ - currentVelocity.z, -maxDelta, maxDelta);
    return NavSteeringResult{ .desiredVelocity = { currentVelocity.x + changeX, 0.0F, currentVelocity.z + changeZ } };
}

// Immutable-by-convention value passed to a navigation query. Excluded areas
// win over included ones, and non-positive/non-finite costs are rejected by
// SetAreaCost rather than silently creating invalid A* edge weights.
class NavQueryFilter final {
public:
    NavQueryFilter() noexcept { areaCosts_.fill(1.0F); }

    [[nodiscard]] NavAreaMask IncludedAreas() const noexcept { return includedAreas_; }
    [[nodiscard]] NavAreaMask ExcludedAreas() const noexcept { return excludedAreas_; }
    void SetIncludedAreas(NavAreaMask areas) noexcept { includedAreas_ = areas; }
    void SetExcludedAreas(NavAreaMask areas) noexcept { excludedAreas_ = areas; }
    [[nodiscard]] bool Allows(NavAreaId area) const noexcept {
        const NavAreaMask bit = NavAreaBit(area);
        return bit != 0U && (includedAreas_ & bit) != 0U && (excludedAreas_ & bit) == 0U;
    }
    [[nodiscard]] float AreaCost(NavAreaId area) const noexcept {
        return IsValidNavArea(area) ? areaCosts_[area] : 0.0F;
    }
    [[nodiscard]] bool SetAreaCost(NavAreaId area, float cost) noexcept {
        if (!IsValidNavArea(area) || !(cost > 0.0F) || cost != cost || cost == std::numeric_limits<float>::infinity()) return false;
        areaCosts_[area] = cost;
        return true;
    }

private:
    NavAreaMask includedAreas_ = kAllNavAreas;
    NavAreaMask excludedAreas_ = 0U;
    std::array<float, kNavAreaCount> areaCosts_{};
};

// LIB-184 synchronous graph query. Start/goal are node indices rather than
// arbitrary world positions because projection onto authored polygons belongs
// to the NavMesh baking/import layer. Dijkstra (rather than an unchecked
// heuristic) makes area-cost routing deterministic and exact.
[[nodiscard]] inline NavPath FindNavPath(const NavMesh& mesh, std::uint32_t start, std::uint32_t goal, const NavQueryFilter& filter) {
    if (start >= mesh.nodes.size() || goal >= mesh.nodes.size() || !filter.Allows(mesh.nodes[start].area) || !filter.Allows(mesh.nodes[goal].area)) return {};
    const std::size_t count = mesh.nodes.size();
    const float infinity = std::numeric_limits<float>::infinity();
    std::vector<float> distances(count, infinity);
    std::vector<std::uint32_t> previous(count, std::numeric_limits<std::uint32_t>::max());
    using QueueItem = std::pair<float, std::uint32_t>;
    std::priority_queue<QueueItem, std::vector<QueueItem>, std::greater<QueueItem>> open;
    distances[start] = 0.0F;
    open.emplace(0.0F, start);
    while (!open.empty()) {
        const auto [cost, current] = open.top(); open.pop();
        if (cost != distances[current]) continue;
        if (current == goal) break;
        for (const std::uint32_t next : mesh.nodes[current].neighbours) {
            if (next >= count || !filter.Allows(mesh.nodes[next].area)) continue;
            const float edge = kb::math::Distance(mesh.nodes[current].position, mesh.nodes[next].position) * filter.AreaCost(mesh.nodes[next].area);
            const float candidate = cost + edge;
            if (candidate < distances[next] || (candidate == distances[next] && current < previous[next])) {
                distances[next] = candidate; previous[next] = current; open.emplace(candidate, next);
            }
        }
    }
    if (distances[goal] == infinity) return NavPath{ .status = NavPathStatus::Failed };
    NavPath result{ .status = NavPathStatus::Complete, .totalCost = distances[goal], .meshRevision = mesh.revision };
    for (std::uint32_t node = goal;; node = previous[node]) { result.corners.push_back(mesh.nodes[node].position); if (node == start) break; }
    std::reverse(result.corners.begin(), result.corners.end());
    return result;
}

// Owns one worker request. The worker receives value snapshots, never a Scene
// reference; Poll is the only owner-thread observation point. Cancellation is
// cooperative at publication time: an already-running graph walk may finish,
// but its result is discarded and can never revive a cancelled request.
class NavPathAsyncRequest final {
public:
    NavPathAsyncRequest() = default;
    NavPathAsyncRequest(const NavPathAsyncRequest&) = delete;
    NavPathAsyncRequest& operator=(const NavPathAsyncRequest&) = delete;
    ~NavPathAsyncRequest() { static_cast<void>(Cancel()); Join(); }

    [[nodiscard]] bool Start(NavMesh mesh, std::uint32_t start, std::uint32_t goal, NavQueryFilter filter) {
        if (status_.load(std::memory_order_acquire) == NavPathStatus::Pending) return false;
        Join();
        { std::scoped_lock lock(resultMutex_); result_ = {}; }
        status_.store(NavPathStatus::Pending, std::memory_order_release);
        worker_ = std::thread([this, mesh = std::move(mesh), start, goal, filter = std::move(filter)]() mutable {
            NavPath result = FindNavPath(mesh, start, goal, filter);
            { std::scoped_lock lock(resultMutex_); result_ = std::move(result); }
            NavPathStatus expected = NavPathStatus::Pending;
            static_cast<void>(status_.compare_exchange_strong(expected, result_.status, std::memory_order_release, std::memory_order_acquire));
        });
        return true;
    }
    [[nodiscard]] NavPathStatus Status() const noexcept { return status_.load(std::memory_order_acquire); }
    [[nodiscard]] bool Cancel() noexcept {
        if (Status() != NavPathStatus::Pending) return false;
        status_.store(NavPathStatus::Cancelled, std::memory_order_release);
        return true;
    }
    [[nodiscard]] NavPath Poll() {
        if (Status() == NavPathStatus::Pending) return NavPath{ .status = NavPathStatus::Pending };
        Join();
        std::scoped_lock lock(resultMutex_);
        return Status() == NavPathStatus::Cancelled ? NavPath{ .status = NavPathStatus::Cancelled } : result_;
    }

private:
    void Join() { if (worker_.joinable()) worker_.join(); }
    std::atomic<NavPathStatus> status_{ NavPathStatus::Invalid };
    std::mutex resultMutex_;
    NavPath result_{};
    std::thread worker_;
};

} // namespace kb::scene
