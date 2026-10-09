#include "navigation/NavCrowd.hpp"

#include <DetourCommon.h>
#include <DetourLocalBoundary.h>
#include <DetourObstacleAvoidance.h>
#include <DetourPathCorridor.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <unordered_map>

namespace kb::navigation {

using kb::math::Vec3;
using kb::scene::NavPathStatus;

namespace {

constexpr int kMaxCorridor = 256;
constexpr int kMaxCorners = 4;
constexpr int kMaxAvoidanceCircles = 12;
constexpr int kMaxAvoidanceSegments = 12;
constexpr float kTopologyOptimizationSeconds = 0.5F;
constexpr float kEpsilon = 1.0e-5F;
const Vec3 kSearchExtents{ 2.0F, 4.0F, 2.0F };

[[nodiscard]] float HorizontalLength(Vec3 value) noexcept {
    return std::sqrt(value.x * value.x + value.z * value.z);
}

[[nodiscard]] float HorizontalDistance(Vec3 a, Vec3 b) noexcept {
    return HorizontalLength(b - a);
}

[[nodiscard]] Vec3 FromArray(const float* value) noexcept {
    return Vec3{ value[0], value[1], value[2] };
}

void ToArray(Vec3 value, float* out) noexcept {
    out[0] = value.x;
    out[1] = value.y;
    out[2] = value.z;
}

[[nodiscard]] Vec3 LimitAcceleration(Vec3 current, Vec3 desired, float acceleration, float deltaSeconds, float maxSpeed) noexcept {
    Vec3 change{ desired.x - current.x, 0.0F, desired.z - current.z };
    const float maxChange = std::max(acceleration, 0.0F) * deltaSeconds;
    const float changeLength = HorizontalLength(change);
    if (changeLength > maxChange && changeLength > 0.0F) change = change * (maxChange / changeLength);
    Vec3 result{ current.x + change.x, 0.0F, current.z + change.z };
    const float speed = HorizontalLength(result);
    if (speed > maxSpeed && speed > 0.0F) result = result * (maxSpeed / speed);
    return result;
}

[[nodiscard]] Vec3 Lerp(Vec3 a, Vec3 b, float t) noexcept {
    return a + (b - a) * t;
}

[[nodiscard]] std::uint64_t CellKey(std::int64_t x, std::int64_t z) noexcept {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32U) | static_cast<std::uint32_t>(z);
}

} // namespace

struct NavCrowdAgentState {
    enum class Mode : std::uint8_t { Lost, Waiting, Walking, Link };

    dtPathCorridor corridor;
    dtLocalBoundary boundary;
    NavAreaFilter filter;
    Mode mode = Mode::Lost;
    std::uint32_t profile = std::numeric_limits<std::uint32_t>::max();
    Vec3 position{};
    Vec3 velocity{};
    Vec3 desired{};
    Vec3 chosen{};
    float desiredSpeed = 0.0F;
    Vec3 plannedDestination{};
    kb::scene::NavAreaMask plannedAreas = kb::scene::kAllNavAreas;
    bool planned = false;
    NavPathStatus status = NavPathStatus::Invalid;
    std::uint64_t request = 0U;
    std::uint64_t meshRevision = 0U;
    float pendingSeconds = 0.0F;
    float stepSeconds = 0.0F;
    float topologySeconds = 0.0F;
    float remaining = 0.0F;
    bool updating = false;
    bool near = true;
    bool endInSight = false;
    std::array<float, kMaxCorners * 3> corners{};
    std::array<unsigned char, kMaxCorners> cornerFlags{};
    std::array<dtPolyRef, kMaxCorners> cornerPolygons{};
    int cornerCount = 0;
    std::vector<std::uint32_t> neighbours;
    Vec3 displacement{};
    Vec3 linkFrom{};
    Vec3 linkStart{};
    Vec3 linkEnd{};
    float linkSeconds = 0.0F;
    float linkDuration = 0.0F;
    kb::scene::NavLinkKind linkKind = kb::scene::NavLinkKind::Walk;
};

void NavCrowd::AvoidanceDeleter::operator()(dtObstacleAvoidanceQuery* query) const noexcept {
    dtFreeObstacleAvoidanceQuery(query);
}

NavCrowd::NavCrowd() = default;
NavCrowd::~NavCrowd() = default;

void NavCrowd::Clear() {
    agents_.clear();
    stats_ = {};
}

bool NavCrowd::OnLink(std::uint64_t id) const noexcept {
    const auto found = agents_.find(id);
    return found != agents_.end() && found->second->mode == NavCrowdAgentState::Mode::Link;
}

std::vector<Vec3> NavCrowd::Corners(std::uint64_t id) const {
    std::vector<Vec3> corners;
    const auto found = agents_.find(id);
    if (found == agents_.end() || found->second->mode != NavCrowdAgentState::Mode::Walking) {
        return corners;
    }
    for (int corner = 0; corner < found->second->cornerCount; ++corner) {
        corners.push_back(FromArray(&found->second->corners[static_cast<std::size_t>(corner) * 3U]));
    }
    return corners;
}

void NavCrowd::Step(NavMeshRuntime& runtime, std::span<const NavCrowdAgentInput> agents, std::span<const NavCrowdCircle> circles,
    std::span<const Vec3> focuses, const kb::scene::NavCrowdSettings& settings, float deltaSeconds, std::vector<NavCrowdAgentOutput>& outputs) {
    using Mode = NavCrowdAgentState::Mode;
    const auto started = std::chrono::steady_clock::now();
    stats_ = {};
    stats_.agents = agents.size();
    ++step_;
    outputs.assign(agents.size(), NavCrowdAgentOutput{});
    if (!avoidance_) {
        avoidance_.reset(dtAllocObstacleAvoidanceQuery());
        if (!avoidance_ || !avoidance_->init(kMaxAvoidanceCircles, kMaxAvoidanceSegments)) {
            avoidance_.reset();
        }
    }
    for (auto record = agents_.begin(); record != agents_.end();) {
        const bool present = std::ranges::binary_search(agents, record->first, {}, &NavCrowdAgentInput::id);
        record = present ? std::next(record) : agents_.erase(record);
    }

    const std::uint64_t revision = runtime.Revision();
    std::vector<NavCrowdAgentState*> states(agents.size(), nullptr);

    // 1. Attach every agent to the mesh, follow external moves and mesh changes, request paths.
    for (std::size_t index = 0U; index < agents.size(); ++index) {
        const NavCrowdAgentInput& input = agents[index];
        std::unique_ptr<NavCrowdAgentState>& slot = agents_[input.id];
        if (!slot) {
            slot = std::make_unique<NavCrowdAgentState>();
            static_cast<void>(slot->corridor.init(kMaxCorridor));
            slot->position = input.position;
        }
        NavCrowdAgentState& state = *slot;
        states[index] = &state;
        state.updating = false;
        if (!input.agent.enabled) {
            state.velocity = {};
            continue;
        }
        const std::uint32_t profile = runtime.ProfileForRadius(input.agent.radius);
        runtime.ConfigureFilter(state.filter, input.agent.areaMask);
        dtNavMeshQuery* query = runtime.Query(profile);
        if (query == nullptr) {
            state.mode = Mode::Lost;
            continue;
        }
        bool replan = !state.planned || state.plannedAreas != input.agent.areaMask ||
            std::memcmp(&state.plannedDestination, &input.agent.destination, sizeof(Vec3)) != 0;
        const auto attach = [&](Vec3 position) {
            const auto nearest = runtime.Nearest(profile, position, kSearchExtents, state.filter);
            if (!nearest) {
                state.mode = Mode::Lost;
                state.position = position;
                state.status = NavPathStatus::Failed;
                return false;
            }
            float point[3];
            ToArray(nearest->second, point);
            state.corridor.reset(nearest->first, point);
            state.boundary.reset();
            state.position = nearest->second;
            state.mode = Mode::Walking;
            state.meshRevision = revision;
            replan = true;
            return true;
        };
        if (state.mode == Mode::Lost || state.profile != profile) {
            state.profile = profile;
            if (!attach(input.position)) continue;
        } else if (state.mode != Mode::Link) {
            const float moved = HorizontalDistance(input.position, state.position);
            if (moved > 1.0e-3F || std::fabs(input.position.y - state.position.y) > input.agent.height) {
                if (moved <= std::max(input.agent.radius * 4.0F, 2.0F)) {
                    float point[3];
                    ToArray(input.position, point);
                    static_cast<void>(state.corridor.movePosition(point, query, &state.filter));
                    state.position = FromArray(state.corridor.getPos());
                } else if (!attach(input.position)) {
                    continue;
                }
            }
        }
        if (state.meshRevision != revision && state.mode != Mode::Link) {
            state.meshRevision = revision;
            state.boundary.reset();
            if (!state.corridor.isValid(std::max(1, state.corridor.getPathCount()), query, &state.filter)) {
                if (!attach(state.position)) continue;
            } else if (state.status == NavPathStatus::Partial || state.status == NavPathStatus::Failed) {
                // New tiles may now reach the destination.
                replan = true;
            }
        }
        if (replan && state.mode != Mode::Link) {
            state.mode = Mode::Waiting;
            state.request = nextRequest_++;
            state.status = NavPathStatus::Pending;
            state.planned = true;
            state.plannedAreas = input.agent.areaMask;
            state.plannedDestination = input.agent.destination;
        }
    }

    // 2. Full path searches, oldest request first.
    std::vector<std::size_t> waiting;
    for (std::size_t index = 0U; index < agents.size(); ++index) {
        if (agents[index].agent.enabled && states[index]->mode == Mode::Waiting) waiting.push_back(index);
    }
    std::ranges::sort(waiting, [&](std::size_t left, std::size_t right) {
        return states[left]->request != states[right]->request ? states[left]->request < states[right]->request : agents[left].id < agents[right].id;
    });
    for (const std::size_t index : waiting) {
        NavCrowdAgentState& state = *states[index];
        if (stats_.pathSearches >= settings.maxPathSearchesPerStep) {
            ++stats_.waitingForPath;
            continue;
        }
        ++stats_.pathSearches;
        dtNavMeshQuery* query = runtime.Query(state.profile);
        const auto target = runtime.Nearest(state.profile, agents[index].agent.destination, kSearchExtents, state.filter);
        state.mode = Mode::Walking;
        state.boundary.reset();
        if (!target) {
            float point[3];
            ToArray(state.position, point);
            state.corridor.reset(state.corridor.getFirstPoly(), point);
            state.status = NavPathStatus::Failed;
            continue;
        }
        std::array<dtPolyRef, kMaxCorridor> path{};
        int count = 0;
        float targetPoint[3];
        ToArray(target->second, targetPoint);
        const dtStatus found = query->findPath(state.corridor.getFirstPoly(), target->first, state.corridor.getPos(), targetPoint, &state.filter,
            path.data(), &count, kMaxCorridor);
        if (dtStatusFailed(found) || count == 0) {
            float point[3];
            ToArray(state.position, point);
            state.corridor.reset(state.corridor.getFirstPoly(), point);
            state.status = NavPathStatus::Failed;
            continue;
        }
        bool partial = path[static_cast<std::size_t>(count - 1)] != target->first;
        if (partial) {
            float closest[3];
            if (dtStatusSucceed(query->closestPointOnPoly(path[static_cast<std::size_t>(count - 1)], targetPoint, closest, nullptr))) {
                dtVcopy(targetPoint, closest);
            }
        }
        state.corridor.setCorridor(targetPoint, path.data(), count);
        state.status = partial ? NavPathStatus::Partial : NavPathStatus::Complete;
    }

    // 3. Level of detail: who updates this step, with how much time.
    for (std::size_t index = 0U; index < agents.size(); ++index) {
        NavCrowdAgentState& state = *states[index];
        if (!agents[index].agent.enabled || state.mode == Mode::Lost) continue;
        state.pendingSeconds += deltaSeconds;
        state.near = focuses.empty();
        for (const Vec3& focus : focuses) {
            if (HorizontalDistance(focus, state.position) <= settings.nearDistance) {
                state.near = true;
                break;
            }
        }
        const std::uint32_t interval = std::max(1U, settings.farUpdateInterval);
        state.updating = state.near || state.mode == Mode::Link || (step_ + index) % interval == 0U;
        if (state.updating) {
            state.stepSeconds = state.pendingSeconds;
            state.pendingSeconds = 0.0F;
            ++(state.near ? stats_.nearUpdates : stats_.farUpdates);
        }
    }

    // 4. Neighbours from a uniform grid over every agent on the mesh.
    float cellSize = 1.0F;
    for (std::size_t index = 0U; index < agents.size(); ++index) {
        cellSize = std::max(cellSize, agents[index].agent.radius * settings.neighbourRangeRadii);
    }
    std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> grid;
    const auto cellOf = [cellSize](float value) { return static_cast<std::int64_t>(std::floor(value / cellSize)); };
    for (std::size_t index = 0U; index < agents.size(); ++index) {
        const NavCrowdAgentState& state = *states[index];
        if (!agents[index].agent.enabled || state.mode == Mode::Lost) continue;
        grid[CellKey(cellOf(state.position.x), cellOf(state.position.z))].push_back(static_cast<std::uint32_t>(index));
    }
    std::vector<std::pair<float, std::uint32_t>> candidates;
    for (std::size_t index = 0U; index < agents.size(); ++index) {
        NavCrowdAgentState& state = *states[index];
        state.neighbours.clear();
        if (!state.updating || state.mode != Mode::Walking) continue;
        const kb::scene::NavAgent& agent = agents[index].agent;
        const float range = agent.radius * settings.neighbourRangeRadii;
        candidates.clear();
        const std::int64_t cellX = cellOf(state.position.x);
        const std::int64_t cellZ = cellOf(state.position.z);
        for (std::int64_t dz = -1; dz <= 1; ++dz) {
            for (std::int64_t dx = -1; dx <= 1; ++dx) {
                const auto found = grid.find(CellKey(cellX + dx, cellZ + dz));
                if (found == grid.end()) continue;
                for (const std::uint32_t other : found->second) {
                    if (other == index) continue;
                    const NavCrowdAgentState& neighbour = *states[other];
                    if (std::fabs(neighbour.position.y - state.position.y) > (agent.height + agents[other].agent.height) * 0.5F) continue;
                    const float dxz = HorizontalDistance(state.position, neighbour.position);
                    if (dxz <= range) candidates.emplace_back(dxz, other);
                }
            }
        }
        std::ranges::sort(candidates);
        for (std::size_t candidate = 0U; candidate < candidates.size() && candidate < settings.maxNeighbours; ++candidate) {
            state.neighbours.push_back(candidates[candidate].second);
        }
    }

    // 5. Corners, shortcuts, links and the velocity each agent wants.
    for (std::size_t index = 0U; index < agents.size(); ++index) {
        NavCrowdAgentState& state = *states[index];
        state.desired = {};
        state.chosen = {};
        state.desiredSpeed = 0.0F;
        if (!state.updating || state.mode != Mode::Walking) continue;
        const kb::scene::NavAgent& agent = agents[index].agent;
        dtNavMeshQuery* query = runtime.Query(state.profile);
        state.cornerCount = state.corridor.findCorners(state.corners.data(), state.cornerFlags.data(), state.cornerPolygons.data(), kMaxCorners,
            query, &state.filter);
        if (state.near && state.cornerCount > 0) {
            const float* next = &state.corners[static_cast<std::size_t>(std::min(1, state.cornerCount - 1)) * 3U];
            state.corridor.optimizePathVisibility(next, agent.radius * 30.0F, query, &state.filter);
            state.topologySeconds += state.stepSeconds;
            if (state.topologySeconds >= kTopologyOptimizationSeconds) {
                state.topologySeconds = 0.0F;
                if (state.corridor.optimizePathTopology(query, &state.filter)) {
                    state.cornerCount = state.corridor.findCorners(state.corners.data(), state.cornerFlags.data(), state.cornerPolygons.data(),
                        kMaxCorners, query, &state.filter);
                }
            }
        }
        if (state.cornerCount == 0) {
            state.remaining = 0.0F;
            state.endInSight = true;
            continue;
        }
        const std::size_t last = static_cast<std::size_t>(state.cornerCount - 1);
        const Vec3 lastCorner = FromArray(&state.corners[last * 3U]);
        if ((state.cornerFlags[last] & DT_STRAIGHTPATH_OFFMESH_CONNECTION) != 0U) {
            const float trigger = agent.radius * 2.25F;
            if (HorizontalDistance(state.position, lastCorner) < trigger) {
                std::array<dtPolyRef, 2> references{};
                float start[3];
                float end[3];
                const NavRuntimeLink* link = runtime.LinkForPolygon(state.profile, state.cornerPolygons[last]);
                if (state.corridor.moveOverOffmeshConnection(state.cornerPolygons[last], references.data(), start, end, query)) {
                    state.mode = Mode::Link;
                    state.linkFrom = state.position;
                    state.linkStart = FromArray(start);
                    state.linkEnd = FromArray(end);
                    state.linkKind = link != nullptr ? link->kind : kb::scene::NavLinkKind::Walk;
                    state.linkSeconds = 0.0F;
                    const float approach = HorizontalDistance(state.linkFrom, state.linkStart);
                    const float across = HorizontalDistance(state.linkStart, state.linkEnd);
                    const float climb = std::fabs(state.linkEnd.y - state.linkStart.y);
                    const float travel = approach + (state.linkKind == kb::scene::NavLinkKind::Ladder ? across + climb * 2.0F : std::max(across, climb));
                    state.linkDuration = std::max(0.1F, travel / std::max(agent.maxSpeed, 0.1F));
                    state.velocity = {};
                    state.cornerCount = 0;
                    continue;
                }
            }
        }
        state.endInSight = (state.cornerFlags[last] & DT_STRAIGHTPATH_END) != 0U;
        float remaining = HorizontalDistance(state.position, FromArray(&state.corners[0]));
        for (std::size_t corner = 1U; corner <= last; ++corner) {
            remaining += HorizontalDistance(FromArray(&state.corners[(corner - 1U) * 3U]), FromArray(&state.corners[corner * 3U]));
        }
        if (!state.endInSight) {
            remaining += HorizontalDistance(lastCorner, FromArray(state.corridor.getTarget()));
        }
        state.remaining = remaining;
        const float stopping = std::max(agent.stoppingDistance, 1.0e-3F);
        if (state.endInSight && remaining <= stopping) {
            continue;
        }
        // Steer to the first corner the agent is not standing on.
        std::size_t steer = 0U;
        while (steer < last && HorizontalDistance(state.position, FromArray(&state.corners[steer * 3U])) < 0.01F) ++steer;
        const Vec3 target = FromArray(&state.corners[steer * 3U]);
        const float distance = HorizontalDistance(state.position, target);
        if (distance <= kEpsilon) continue;
        const Vec3 direction = Vec3{ target.x - state.position.x, 0.0F, target.z - state.position.z } * (1.0F / distance);
        const float maxSpeed = std::max(agent.maxSpeed, 0.0F);
        const float braking = state.endInSight ? std::sqrt(2.0F * std::max(agent.acceleration, 0.0F) * (remaining - stopping)) : maxSpeed;
        state.desiredSpeed = std::min(maxSpeed, braking);
        Vec3 desired = direction * state.desiredSpeed;
        if (settings.separationWeight > 0.0F && !state.neighbours.empty()) {
            const float separation = agent.radius * settings.neighbourRangeRadii;
            Vec3 push{};
            float weightSum = 0.0F;
            for (const std::uint32_t other : state.neighbours) {
                const Vec3 offset{ state.position.x - states[other]->position.x, 0.0F, state.position.z - states[other]->position.z };
                const float distanceSquared = offset.x * offset.x + offset.z * offset.z;
                if (distanceSquared < 1.0e-5F || distanceSquared > separation * separation) continue;
                const float separationDistance = std::sqrt(distanceSquared);
                const float closeness = separationDistance / separation;
                const float weight = settings.separationWeight * (1.0F - closeness * closeness);
                push = push + offset * (weight / separationDistance);
                weightSum += 1.0F;
            }
            if (weightSum > 1.0e-4F) {
                desired = desired + push * (1.0F / weightSum);
                const float speed = HorizontalLength(desired);
                if (speed > state.desiredSpeed && speed > 0.0F) desired = desired * (state.desiredSpeed / speed);
            }
        }
        state.desired = desired;
        state.chosen = desired;
    }

    // 6. Velocity obstacles around neighbours, obstacle circles and walls, for near agents.
    dtObstacleAvoidanceParams params{};
    params.velBias = 0.4F;
    params.weightDesVel = 2.0F;
    params.weightCurVel = 0.75F;
    params.weightSide = 0.75F;
    params.weightToi = 2.5F;
    params.horizTime = 2.5F;
    params.gridSize = 33;
    params.adaptiveDivs = 7;
    params.adaptiveRings = 2;
    params.adaptiveDepth = 3;
    for (std::size_t index = 0U; index < agents.size(); ++index) {
        NavCrowdAgentState& state = *states[index];
        if (!state.updating || state.mode != Mode::Walking || !state.near || !settings.avoidance || !avoidance_ || state.desiredSpeed <= 0.0F) {
            continue;
        }
        const kb::scene::NavAgent& agent = agents[index].agent;
        const float range = agent.radius * settings.neighbourRangeRadii;
        dtNavMeshQuery* query = runtime.Query(state.profile);
        float position[3];
        ToArray(state.position, position);
        if (dtVdist2DSqr(position, state.boundary.getCenter()) > (range * 0.25F) * (range * 0.25F) || !state.boundary.isValid(query, &state.filter)) {
            state.boundary.update(state.corridor.getFirstPoly(), position, range, query, &state.filter);
        }
        avoidance_->reset();
        for (const std::uint32_t other : state.neighbours) {
            const NavCrowdAgentState& neighbour = *states[other];
            float center[3];
            float velocity[3];
            float desired[3];
            ToArray(neighbour.position, center);
            ToArray(neighbour.velocity, velocity);
            ToArray(neighbour.desired, desired);
            avoidance_->addCircle(center, agents[other].agent.radius, velocity, desired);
        }
        const float zero[3] = { 0.0F, 0.0F, 0.0F };
        for (const NavCrowdCircle& circle : circles) {
            if (HorizontalDistance(circle.center, state.position) > range + circle.radius) continue;
            if (circle.top < state.position.y || circle.bottom > state.position.y + agent.height) continue;
            float center[3];
            ToArray(circle.center, center);
            avoidance_->addCircle(center, circle.radius, zero, zero);
        }
        for (int segment = 0; segment < state.boundary.getSegmentCount(); ++segment) {
            const float* points = state.boundary.getSegment(segment);
            if (dtTriArea2D(position, points, points + 3) < 0.0F) continue;
            avoidance_->addSegment(points, points + 3);
        }
        float velocity[3];
        float desired[3];
        float chosen[3];
        ToArray(state.velocity, velocity);
        ToArray(state.desired, desired);
        static_cast<void>(avoidance_->sampleVelocityAdaptive(position, agent.radius, state.desiredSpeed, velocity, desired, chosen, &params, nullptr));
        state.chosen = Vec3{ chosen[0], 0.0F, chosen[2] };
    }

    // 7. Integrate.
    std::vector<Vec3> moved(agents.size());
    for (std::size_t index = 0U; index < agents.size(); ++index) {
        NavCrowdAgentState& state = *states[index];
        moved[index] = state.position;
        if (!state.updating || state.mode != Mode::Walking) continue;
        const kb::scene::NavAgent& agent = agents[index].agent;
        const float seconds = state.stepSeconds;
        Vec3 velocity = LimitAcceleration(state.velocity, state.chosen, agent.acceleration, seconds, std::max(agent.maxSpeed, 0.0F));
        if (state.endInSight && state.cornerCount > 0) {
            // Never step past the end of the path.
            const Vec3 end = FromArray(&state.corners[static_cast<std::size_t>(state.cornerCount - 1) * 3U]);
            const float distance = HorizontalDistance(state.position, end);
            const float speed = HorizontalLength(velocity);
            if (speed * seconds > distance && speed > 0.0F) velocity = velocity * (distance / (speed * seconds));
        } else if (state.cornerCount == 0) {
            velocity = {};
        }
        if (state.desiredSpeed <= 0.0F && HorizontalLength(state.desired) <= 0.0F && state.endInSight) {
            velocity = {};
        }
        state.velocity = velocity;
        moved[index] = state.position + velocity * seconds;
    }

    // 8. Push overlapping near agents apart, all from the same positions.
    for (int iteration = 0; iteration < 2; ++iteration) {
        for (std::size_t index = 0U; index < agents.size(); ++index) {
            NavCrowdAgentState& state = *states[index];
            state.displacement = {};
            if (!state.updating || !state.near || state.mode != Mode::Walking) continue;
            float weightSum = 0.0F;
            for (const std::uint32_t other : state.neighbours) {
                const Vec3 offset{ moved[index].x - moved[other].x, 0.0F, moved[index].z - moved[other].z };
                const float reach = agents[index].agent.radius + agents[other].agent.radius;
                const float distanceSquared = offset.x * offset.x + offset.z * offset.z;
                if (distanceSquared > reach * reach) continue;
                const float distance = std::sqrt(distanceSquared);
                Vec3 push{};
                if (distance < 1.0e-4F) {
                    push = agents[index].id > agents[other].id ? Vec3{ -state.desired.z, 0.0F, state.desired.x } : Vec3{ state.desired.z, 0.0F, -state.desired.x };
                    push = push * 0.01F;
                } else {
                    push = offset * ((reach - distance) * 0.5F * 0.7F / distance);
                }
                state.displacement = state.displacement + push;
                weightSum += 1.0F;
            }
            if (weightSum > 1.0e-4F) state.displacement = state.displacement * (1.0F / weightSum);
        }
        for (std::size_t index = 0U; index < agents.size(); ++index) {
            moved[index] = moved[index] + states[index]->displacement;
        }
    }

    // 9. Keep every moved agent on the mesh; advance agents crossing links.
    for (std::size_t index = 0U; index < agents.size(); ++index) {
        NavCrowdAgentState& state = *states[index];
        const kb::scene::NavAgent& agent = agents[index].agent;
        NavCrowdAgentOutput& output = outputs[index];
        if (!agent.enabled) {
            output.position = agents[index].position;
            output.status = state.status;
            output.remainingDistance = state.remaining;
            continue;
        }
        if (state.mode == Mode::Lost) {
            output.position = agents[index].position;
            output.status = NavPathStatus::Failed;
            continue;
        }
        if (state.mode == Mode::Walking && state.updating) {
            float point[3];
            ToArray(moved[index], point);
            static_cast<void>(state.corridor.movePosition(point, runtime.Query(state.profile), &state.filter));
            state.position = FromArray(state.corridor.getPos());
        } else if (state.mode == Mode::Link) {
            ++stats_.onLinks;
            state.linkSeconds += deltaSeconds;
            const float u = std::clamp(state.linkSeconds / state.linkDuration, 0.0F, 1.0F);
            const float approach = HorizontalDistance(state.linkFrom, state.linkStart);
            const float across = HorizontalDistance(state.linkStart, state.linkEnd);
            const float climb = state.linkEnd.y - state.linkStart.y;
            const float traverse = state.linkKind == kb::scene::NavLinkKind::Ladder ? across + std::fabs(climb) * 2.0F : std::max(across, std::fabs(climb));
            const float total = std::max(approach + traverse, kEpsilon);
            const float travelled = u * total;
            if (travelled < approach) {
                state.position = Lerp(state.linkFrom, state.linkStart, travelled / std::max(approach, kEpsilon));
            } else {
                const float w = std::clamp((travelled - approach) / std::max(traverse, kEpsilon), 0.0F, 1.0F);
                switch (state.linkKind) {
                case kb::scene::NavLinkKind::Jump: {
                    state.position = Lerp(state.linkStart, state.linkEnd, w);
                    const float arc = 0.5F + 0.15F * across;
                    state.position.y += arc * 4.0F * w * (1.0F - w);
                    break;
                }
                case kb::scene::NavLinkKind::Ladder: {
                    // Climb on the low side's end: up first when going up, across first when going down.
                    const float verticalShare = std::fabs(climb) * 2.0F / std::max(traverse, kEpsilon);
                    if (climb >= 0.0F) {
                        if (w < verticalShare) {
                            state.position = Vec3{ state.linkStart.x, state.linkStart.y + climb * (w / verticalShare), state.linkStart.z };
                        } else {
                            const Vec3 top{ state.linkStart.x, state.linkEnd.y, state.linkStart.z };
                            state.position = Lerp(top, state.linkEnd, (w - verticalShare) / std::max(1.0F - verticalShare, kEpsilon));
                        }
                    } else {
                        const float acrossShare = 1.0F - verticalShare;
                        const Vec3 edge{ state.linkEnd.x, state.linkStart.y, state.linkEnd.z };
                        if (w < acrossShare) {
                            state.position = Lerp(state.linkStart, edge, w / std::max(acrossShare, kEpsilon));
                        } else {
                            state.position = Lerp(edge, state.linkEnd, (w - acrossShare) / std::max(verticalShare, kEpsilon));
                        }
                    }
                    break;
                }
                case kb::scene::NavLinkKind::Walk:
                    state.position = Lerp(state.linkStart, state.linkEnd, w);
                    break;
                }
            }
            if (u >= 1.0F) {
                state.mode = Mode::Walking;
                state.position = FromArray(state.corridor.getPos());
                state.boundary.reset();
            }
        }
        output.position = state.position;
        output.velocity = state.mode == Mode::Link ? Vec3{} : state.velocity;
        output.remainingDistance = state.mode == Mode::Waiting ? HorizontalDistance(state.position, agent.destination) : state.remaining;
        output.status = state.status;
        output.onLink = state.mode == Mode::Link;
    }
    stats_.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
}

} // namespace kb::navigation
