#include "navigation/NavMeshCrowd.hpp"

#include <DetourCommon.h>
#include <DetourCrowd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>

namespace kb::navigation {

using kb::math::Vec3;
using kb::scene::NavPathStatus;

namespace {

constexpr int kInitialCapacity = 64;
// The crowd's proximity grid addresses members with 16 bits.
constexpr int kMaxCapacity = 0xFFFF;
constexpr float kEpsilon = 1.0e-5F;
// Box searched for the polygon an agent or a destination stands on.
const Vec3 kSearchExtents{ 2.0F, 4.0F, 2.0F };
constexpr unsigned kNearFlags = DT_CROWD_ANTICIPATE_TURNS | DT_CROWD_OPTIMIZE_VIS | DT_CROWD_OPTIMIZE_TOPO;

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

[[nodiscard]] Vec3 Lerp(Vec3 a, Vec3 b, float t) noexcept {
    return a + (b - a) * t;
}

[[nodiscard]] bool SameBits(Vec3 a, Vec3 b) noexcept {
    return std::memcmp(&a, &b, sizeof(Vec3)) == 0;
}

[[nodiscard]] dtCrowdAgentParams AgentParams(const kb::scene::NavAgent& agent, int filter, unsigned char flags,
    const kb::scene::NavigationCrowdSettings& settings) noexcept {
    dtCrowdAgentParams params{};
    params.radius = std::max(agent.radius, 0.0F);
    params.height = std::max(agent.height, 0.01F);
    params.maxAcceleration = std::max(agent.acceleration, 0.0F);
    params.maxSpeed = std::max(agent.maxSpeed, 0.0F);
    params.collisionQueryRange = std::max(params.radius * settings.neighbourRangeRadii, 0.01F);
    params.pathOptimizationRange = std::max(params.radius * 30.0F, 0.01F);
    params.separationWeight = settings.separationWeight;
    params.updateFlags = flags;
    params.obstacleAvoidanceType = 0U;
    params.queryFilterType = static_cast<unsigned char>(filter);
    params.userData = nullptr;
    return params;
}

[[nodiscard]] dtCrowdAgentParams ObstacleParams(const NavMeshCrowdObstacle& obstacle) noexcept {
    dtCrowdAgentParams params{};
    params.radius = std::max(obstacle.radius, 0.0F);
    params.height = std::max(obstacle.height, 0.01F);
    params.collisionQueryRange = std::max(params.radius, 0.01F);
    params.pathOptimizationRange = 0.01F;
    params.updateFlags = 0U;
    return params;
}

[[nodiscard]] bool SameParams(const dtCrowdAgentParams& a, const dtCrowdAgentParams& b) noexcept {
    return a.radius == b.radius && a.height == b.height && a.maxAcceleration == b.maxAcceleration && a.maxSpeed == b.maxSpeed &&
        a.collisionQueryRange == b.collisionQueryRange && a.pathOptimizationRange == b.pathOptimizationRange &&
        a.separationWeight == b.separationWeight && a.updateFlags == b.updateFlags && a.obstacleAvoidanceType == b.obstacleAvoidanceType &&
        a.queryFilterType == b.queryFilterType;
}

// Places an obstacle in a crowd as a member that never moves: the crowd neither steers nor
// displaces members that are not walking, yet every walking member sees it as a neighbour,
// avoids it and is pushed out of it.
void PlaceObstacle(dtCrowd& crowd, int index, const NavMeshCrowdObstacle& obstacle) {
    dtCrowdAgent* member = crowd.getEditableAgent(index);
    const Vec3 bottom{ obstacle.center.x, obstacle.center.y - obstacle.height * 0.5F, obstacle.center.z };
    float point[3];
    ToArray(bottom, point);
    member->corridor.reset(0U, point);
    dtVcopy(member->npos, point);
    dtVset(member->vel, 0.0F, 0.0F, 0.0F);
    dtVset(member->dvel, 0.0F, 0.0F, 0.0F);
    dtVset(member->nvel, 0.0F, 0.0F, 0.0F);
    member->state = DT_CROWDAGENT_STATE_INVALID;
    member->nneis = 0;
}

// Where an agent crossing a link is shown: the crowd moves it straight from the link's start to its
// end; a jump arcs above that line, a ladder is climbed at its low end before stepping across.
[[nodiscard]] Vec3 LinkPosition(kb::scene::NavLinkKind kind, Vec3 start, Vec3 end, Vec3 position) noexcept {
    const Vec3 span = end - start;
    const float lengthSquared = kb::math::Dot(span, span);
    if (lengthSquared <= kEpsilon) return position;
    const float along = kb::math::Dot(position - start, span) / lengthSquared;
    const Vec3 onLine = start + span * along;
    if (along <= 0.0F || kb::math::Length(position - onLine) > 1.0e-3F + 1.0e-4F * std::sqrt(lengthSquared)) {
        // Still stepping onto the link's start.
        return position;
    }
    const float w = std::clamp(along, 0.0F, 1.0F);
    const float across = HorizontalDistance(start, end);
    const float climb = end.y - start.y;
    switch (kind) {
    case kb::scene::NavLinkKind::Jump: {
        Vec3 result = Lerp(start, end, w);
        result.y += (0.5F + 0.15F * across) * 4.0F * w * (1.0F - w);
        return result;
    }
    case kb::scene::NavLinkKind::Ladder: {
        const float traverse = std::max(across + std::fabs(climb) * 2.0F, kEpsilon);
        const float verticalShare = std::fabs(climb) * 2.0F / traverse;
        if (climb >= 0.0F) {
            if (w < verticalShare) return Vec3{ start.x, start.y + climb * (w / verticalShare), start.z };
            return Lerp(Vec3{ start.x, end.y, start.z }, end, (w - verticalShare) / std::max(1.0F - verticalShare, kEpsilon));
        }
        const float acrossShare = 1.0F - verticalShare;
        const Vec3 edge{ end.x, start.y, end.z };
        if (w < acrossShare) return Lerp(start, edge, w / std::max(acrossShare, kEpsilon));
        return Lerp(edge, end, (w - acrossShare) / std::max(verticalShare, kEpsilon));
    }
    case kb::scene::NavLinkKind::Walk:
        break;
    }
    return position;
}

} // namespace

void NavMeshCrowd::CrowdDeleter::operator()(dtCrowd* crowd) const noexcept {
    dtFreeCrowd(crowd);
}

NavMeshCrowd::NavMeshCrowd() = default;
NavMeshCrowd::~NavMeshCrowd() = default;

void NavMeshCrowd::Clear() {
    crowds_.clear();
    agents_.clear();
    obstacles_.clear();
    stats_ = {};
}

bool NavMeshCrowd::OnLink(std::uint64_t id) const noexcept {
    const auto found = agents_.find(id);
    return found != agents_.end() && found->second.onLink;
}

std::vector<Vec3> NavMeshCrowd::Corners(std::uint64_t id) const {
    std::vector<Vec3> corners;
    const auto found = agents_.find(id);
    if (found == agents_.end() || found->second.index < 0 || found->second.arrived || found->second.onLink ||
        found->second.profile >= crowds_.size() || !crowds_[found->second.profile].crowd) {
        return corners;
    }
    const dtCrowdAgent* member = crowds_[found->second.profile].crowd->getAgent(found->second.index);
    if (member == nullptr || member->state != DT_CROWDAGENT_STATE_WALKING) return corners;
    for (int corner = 0; corner < member->ncorners; ++corner) {
        corners.push_back(FromArray(&member->cornerVerts[static_cast<std::size_t>(corner) * 3U]));
    }
    return corners;
}

void NavMeshCrowd::ResetProfiles(std::size_t count) {
    crowds_.clear();
    crowds_.resize(count);
    for (auto& [id, record] : agents_) {
        record.index = -1;
        record.requested = false;
        record.onLink = false;
        record.nextLink = 0U;
    }
    for (auto& [id, record] : obstacles_) record.indices.assign(count, -1);
}

void NavMeshCrowd::Detach(AgentRecord& record) {
    if (record.index >= 0 && record.profile < crowds_.size() && crowds_[record.profile].crowd) {
        crowds_[record.profile].crowd->removeAgent(record.index);
    }
    record.index = -1;
    record.requested = false;
    record.onLink = false;
    record.nextLink = 0U;
}

void NavMeshCrowd::Step(NavMeshRuntime& runtime, std::span<const NavMeshCrowdAgentInput> agents, std::span<const NavMeshCrowdObstacle> obstacles,
    std::span<const Vec3> focuses, const kb::scene::NavigationCrowdSettings& settings, float deltaSeconds,
    std::vector<NavMeshCrowdAgentOutput>& outputs) {
    const auto started = std::chrono::steady_clock::now();
    stats_ = {};
    ++step_;
    outputs.assign(agents.size(), NavMeshCrowdAgentOutput{});
    const std::uint32_t profiles = runtime.ProfileCount();
    const std::uint64_t revision = runtime.Revision();

    // 1. Forget agents and obstacles that are gone.
    for (auto record = agents_.begin(); record != agents_.end();) {
        if (std::ranges::binary_search(agents, record->first, {}, &NavMeshCrowdAgentInput::id)) {
            ++record;
            continue;
        }
        Detach(record->second);
        record = agents_.erase(record);
    }
    for (auto record = obstacles_.begin(); record != obstacles_.end();) {
        if (std::ranges::binary_search(obstacles, record->first, {}, &NavMeshCrowdObstacle::id)) {
            ++record;
            continue;
        }
        for (std::size_t profile = 0U; profile < record->second.indices.size() && profile < crowds_.size(); ++profile) {
            if (record->second.indices[profile] >= 0 && crowds_[profile].crowd) crowds_[profile].crowd->removeAgent(record->second.indices[profile]);
        }
        record = obstacles_.erase(record);
    }

    // 2. Which crowd every agent belongs to, and the filter of its areas.
    std::vector<kb::scene::NavAreaMask> masks;
    for (const NavMeshCrowdAgentInput& input : agents) {
        if (input.agent.enabled) masks.push_back(input.agent.areaMask);
    }
    std::ranges::sort(masks);
    masks.erase(std::unique(masks.begin(), masks.end()), masks.end());
    runtime.KeepFilterSlots(masks);
    std::vector<AgentRecord*> records(agents.size(), nullptr);
    std::vector<std::size_t> members(profiles, obstacles.size());
    for (std::size_t index = 0U; index < agents.size(); ++index) {
        const NavMeshCrowdAgentInput& input = agents[index];
        AgentRecord& record = agents_[input.id];
        records[index] = &record;
        const std::uint32_t profile = runtime.ProfileForRadius(input.agent.radius);
        const int filter = input.agent.enabled ? runtime.FilterSlot(input.agent.areaMask) : -1;
        if (!input.agent.enabled || filter < 0 || profile >= profiles || profile != record.profile || filter != record.filter) {
            Detach(record);
        }
        record.profile = profile;
        record.filter = std::max(filter, 0);
        record.areas = input.agent.areaMask;
        if (input.agent.enabled && filter >= 0 && profile < profiles) ++members[profile];
    }

    // 3. One crowd per profile, over the profile's current Detour mesh, large enough for its members.
    if (crowds_.size() != profiles) ResetProfiles(profiles);
    for (std::uint32_t profile = 0U; profile < profiles; ++profile) {
        ProfileCrowd& slot = crowds_[profile];
        dtNavMesh* mesh = runtime.Mesh(profile);
        const std::uint64_t generation = runtime.MeshGeneration(profile);
        const int needed = static_cast<int>(std::min<std::size_t>(members[profile], kMaxCapacity));
        if (slot.crowd && slot.meshGeneration == generation && needed <= slot.capacity) continue;
        int capacity = std::max(kInitialCapacity, slot.capacity);
        while (capacity < needed) capacity = std::min(capacity * 2, kMaxCapacity);
        for (auto& [id, record] : agents_) {
            if (record.profile == profile) {
                record.index = -1;
                record.requested = false;
                record.onLink = false;
                record.nextLink = 0U;
            }
        }
        for (auto& [id, record] : obstacles_) {
            if (profile < record.indices.size()) record.indices[profile] = -1;
        }
        slot = ProfileCrowd{};
        if (mesh == nullptr) continue;
        std::unique_ptr<dtCrowd, CrowdDeleter> crowd{ dtAllocCrowd() };
        const float radius = runtime.Layout() != nullptr ? runtime.Layout()->profiles[profile].radius : 0.5F;
        if (!crowd || !crowd->init(capacity, std::max(radius, 0.1F), mesh)) continue;
        dtObstacleAvoidanceParams avoidance{};
        avoidance.velBias = 0.4F;
        avoidance.weightDesVel = 2.0F;
        avoidance.weightCurVel = 0.75F;
        avoidance.weightSide = 0.75F;
        avoidance.weightToi = 2.5F;
        avoidance.horizTime = 2.5F;
        avoidance.gridSize = 33;
        avoidance.adaptiveDivs = 7;
        avoidance.adaptiveRings = 2;
        avoidance.adaptiveDepth = 3;
        crowd->setObstacleAvoidanceParams(0, &avoidance);
        slot.crowd = std::move(crowd);
        slot.capacity = capacity;
        slot.meshGeneration = generation;
    }
    for (std::uint32_t profile = 0U; profile < profiles; ++profile) {
        if (!crowds_[profile].crowd) continue;
        for (int filter = 0; filter < NavMeshRuntime::kCrowdFilterCount; ++filter) {
            runtime.ConfigureCrowdFilter(*crowds_[profile].crowd->getEditableFilter(filter), filter);
        }
    }

    // 4. Obstacles stand in every crowd as members that never move.
    for (const NavMeshCrowdObstacle& obstacle : obstacles) {
        ObstacleRecord& record = obstacles_[obstacle.id];
        const bool changed = !(SameBits(record.obstacle.center, obstacle.center) && record.obstacle.radius == obstacle.radius &&
            record.obstacle.height == obstacle.height);
        record.obstacle = obstacle;
        record.indices.resize(profiles, -1);
        for (std::uint32_t profile = 0U; profile < profiles; ++profile) {
            dtCrowd* crowd = crowds_[profile].crowd.get();
            if (crowd == nullptr) continue;
            const dtCrowdAgentParams params = ObstacleParams(obstacle);
            if (record.indices[profile] < 0) {
                float point[3];
                ToArray(obstacle.center, point);
                record.indices[profile] = crowd->addAgent(point, &params);
                if (record.indices[profile] >= 0) PlaceObstacle(*crowd, record.indices[profile], obstacle);
            } else if (changed) {
                crowd->updateAgentParameters(record.indices[profile], &params);
                PlaceObstacle(*crowd, record.indices[profile], obstacle);
            }
        }
    }

    // 5. Members follow their agents: join, follow moves made outside the crowd, take parameters.
    NavAreaFilter filter;
    std::vector<std::size_t> waiting;
    for (std::size_t index = 0U; index < agents.size(); ++index) {
        const NavMeshCrowdAgentInput& input = agents[index];
        AgentRecord& record = *records[index];
        NavMeshCrowdAgentOutput& output = outputs[index];
        output.position = input.position;
        output.status = record.status;
        output.remainingDistance = record.remaining;
        if (!input.agent.enabled) continue;
        dtCrowd* crowd = record.profile < crowds_.size() ? crowds_[record.profile].crowd.get() : nullptr;
        if (crowd == nullptr || runtime.FilterSlot(input.agent.areaMask) < 0) {
            record.status = NavPathStatus::Failed;
            output.status = record.status;
            continue;
        }
        runtime.ConfigureFilter(filter, input.agent.areaMask);
        const bool near = focuses.empty() || std::ranges::any_of(focuses, [&](Vec3 focus) {
            return HorizontalDistance(focus, input.position) <= settings.nearDistance;
        });
        unsigned flags = kNearFlags;
        if (settings.separationWeight > 0.0F) flags |= DT_CROWD_SEPARATION;
        if (settings.avoidance) flags |= DT_CROWD_OBSTACLE_AVOIDANCE;
        if (!near) {
            // Far agents follow their corridors without avoidance or separation, and shorten them toward
            // corners that came into view only every few steps.
            const std::uint32_t interval = std::max(1U, settings.farPathOptimizationInterval);
            flags = DT_CROWD_ANTICIPATE_TURNS | ((step_ + input.id) % interval == 0U ? unsigned{ DT_CROWD_OPTIMIZE_VIS } : 0U);
        }
        ++(near ? stats_.nearAgents : stats_.farAgents);
        const dtCrowdAgentParams params = AgentParams(input.agent, record.filter, static_cast<unsigned char>(flags), settings);
        const auto attach = [&](Vec3 position) {
            Detach(record);
            const auto nearest = runtime.Nearest(record.profile, position, kSearchExtents, filter);
            if (!nearest) {
                record.status = NavPathStatus::Failed;
                return false;
            }
            float point[3];
            ToArray(nearest->second, point);
            record.index = crowd->addAgent(point, &params);
            if (record.index < 0) {
                record.status = NavPathStatus::Failed;
                return false;
            }
            record.position = nearest->second;
            record.meshRevision = revision;
            record.arrived = false;
            return true;
        };
        if (record.index < 0) {
            if (!attach(input.position)) {
                output.status = record.status;
                continue;
            }
        } else if (!record.onLink) {
            const float moved = HorizontalDistance(input.position, record.position);
            if (moved > 1.0e-3F || std::fabs(input.position.y - record.position.y) > input.agent.height) {
                dtCrowdAgent* member = crowd->getEditableAgent(record.index);
                if (member->state == DT_CROWDAGENT_STATE_WALKING && moved <= std::max(input.agent.radius * 4.0F, 2.0F)) {
                    // Moved a little by something else (a character's physics): slide the corridor along.
                    float point[3];
                    ToArray(input.position, point);
                    static_cast<void>(member->corridor.movePosition(point, runtime.Query(record.profile), crowd->getFilter(record.filter)));
                    dtVcopy(member->npos, member->corridor.getPos());
                    record.position = FromArray(member->npos);
                } else if (!attach(input.position)) {
                    output.status = record.status;
                    continue;
                }
            }
        }
        dtCrowdAgentParams current = crowd->getAgent(record.index)->params;
        if (!SameParams(current, params)) crowd->updateAgentParameters(record.index, &params);
        ++stats_.agents;

        // A new destination, new areas, or tiles that changed under a path that did not reach.
        bool replan = !record.requested || !SameBits(record.destination, input.agent.destination) || record.areas != input.agent.areaMask;
        if (record.meshRevision != revision) {
            record.meshRevision = revision;
            replan = replan || record.destinationMissing || record.status == NavPathStatus::Partial || record.status == NavPathStatus::Failed;
        }
        if (record.arrived && !replan && HorizontalDistance(record.position, record.arrivedAt) > input.agent.stoppingDistance + input.agent.radius) {
            // Pushed away from where it stopped.
            replan = true;
        }
        if (replan && !record.onLink) {
            if (record.requested || record.requestOrder == 0U || !SameBits(record.destination, input.agent.destination)) {
                record.requestOrder = nextRequest_++;
            }
            record.requested = false;
            record.destination = input.agent.destination;
            record.status = NavPathStatus::Pending;
            waiting.push_back(index);
        }
    }

    // 6. Hand the oldest waiting destinations to the crowd, as many as the step allows.
    std::ranges::sort(waiting, [&](std::size_t left, std::size_t right) {
        return records[left]->requestOrder != records[right]->requestOrder ? records[left]->requestOrder < records[right]->requestOrder
                                                                            : agents[left].id < agents[right].id;
    });
    for (const std::size_t index : waiting) {
        AgentRecord& record = *records[index];
        if (stats_.pathRequests >= settings.maxPathRequestsPerStep) {
            ++stats_.waitingForPath;
            continue;
        }
        ++stats_.pathRequests;
        dtCrowd& crowd = *crowds_[record.profile].crowd;
        runtime.ConfigureFilter(filter, agents[index].agent.areaMask);
        const auto target = runtime.Nearest(record.profile, agents[index].agent.destination, kSearchExtents, filter);
        record.requested = true;
        record.arrived = false;
        if (!target) {
            static_cast<void>(crowd.resetMoveTarget(record.index));
            record.destinationMissing = true;
            record.status = NavPathStatus::Failed;
            continue;
        }
        float point[3];
        ToArray(target->second, point);
        record.destinationMissing = !crowd.requestMoveTarget(record.index, target->first, point);
        record.status = record.destinationMissing ? NavPathStatus::Failed : NavPathStatus::Pending;
    }

    // 7. Step every crowd; note which link each member heads for, to know the link it starts crossing.
    for (std::size_t index = 0U; index < agents.size(); ++index) {
        AgentRecord& record = *records[index];
        record.nextLink = 0U;
        if (record.index < 0 || record.onLink) continue;
        const dtCrowdAgent* member = crowds_[record.profile].crowd->getAgent(record.index);
        if (member->state == DT_CROWDAGENT_STATE_WALKING && member->ncorners > 0 &&
            (member->cornerFlags[member->ncorners - 1] & DT_STRAIGHTPATH_OFFMESH_CONNECTION) != 0U) {
            record.nextLink = member->cornerPolys[member->ncorners - 1];
        }
    }
    for (ProfileCrowd& slot : crowds_) {
        if (!slot.crowd) continue;
        slot.crowd->update(deltaSeconds, nullptr);
        stats_.velocitySamples += static_cast<std::size_t>(slot.crowd->getVelocitySampleCount());
    }

    // 8. Read the members back.
    for (std::size_t index = 0U; index < agents.size(); ++index) {
        const NavMeshCrowdAgentInput& input = agents[index];
        AgentRecord& record = *records[index];
        NavMeshCrowdAgentOutput& output = outputs[index];
        if (!input.agent.enabled || record.index < 0) continue;
        dtCrowd& crowd = *crowds_[record.profile].crowd;
        dtCrowdAgent* member = crowd.getEditableAgent(record.index);
        if (member->state == DT_CROWDAGENT_STATE_INVALID) {
            // The polygons under the agent went away; it joins again where it stands next step.
            Detach(record);
            record.status = NavPathStatus::Failed;
            output.status = record.status;
            continue;
        }
        const Vec3 position = FromArray(member->npos);
        if (member->state == DT_CROWDAGENT_STATE_OFFMESH) {
            if (!record.onLink) {
                record.onLink = true;
                record.linkKind = kb::scene::NavLinkKind::Walk;
                record.linkStart = record.position;
                record.linkEnd = position;
                const dtNavMesh* mesh = runtime.Mesh(record.profile);
                const dtMeshTile* tile = nullptr;
                const dtPoly* poly = nullptr;
                if (mesh != nullptr && record.nextLink != 0U && dtStatusSucceed(mesh->getTileAndPolyByRef(record.nextLink, &tile, &poly)) &&
                    poly->getType() == DT_POLYTYPE_OFFMESH_CONNECTION) {
                    // The link's ends as placed on the polygons, which the crowd moves the agent between.
                    const Vec3 a = FromArray(&tile->verts[static_cast<std::size_t>(poly->verts[0]) * 3U]);
                    const Vec3 b = FromArray(&tile->verts[static_cast<std::size_t>(poly->verts[1]) * 3U]);
                    // The end the agent came from is the start of its crossing.
                    const bool forward = HorizontalDistance(record.position, a) <= HorizontalDistance(record.position, b);
                    record.linkStart = forward ? a : b;
                    record.linkEnd = forward ? b : a;
                    if (const NavRuntimeLink* link = runtime.LinkForPolygon(record.profile, record.nextLink); link != nullptr) {
                        record.linkKind = link->kind;
                    }
                }
            }
            ++stats_.onLinks;
            record.position = position;
            output.position = LinkPosition(record.linkKind, record.linkStart, record.linkEnd, position);
            output.velocity = {};
            output.onLink = true;
            output.status = record.status;
            output.remainingDistance = record.remaining;
            continue;
        }
        record.onLink = false;
        record.position = position;

        if (!record.requested) {
            record.status = NavPathStatus::Pending;
            record.remaining = HorizontalDistance(position, input.agent.destination);
        } else if (!record.arrived && !record.destinationMissing) {
            switch (member->targetState) {
            case DT_CROWDAGENT_TARGET_VALID:
                record.status = member->corridor.getLastPoly() == member->targetRef && !member->partial ? NavPathStatus::Complete : NavPathStatus::Partial;
                break;
            case DT_CROWDAGENT_TARGET_FAILED:
                record.status = NavPathStatus::Failed;
                break;
            case DT_CROWDAGENT_TARGET_NONE:
                // The crowd dropped a destination whose polygons went away; it is planned again when the tiles change.
                record.destinationMissing = true;
                record.status = NavPathStatus::Failed;
                break;
            default:
                record.status = NavPathStatus::Pending;
                ++stats_.waitingForPath;
                break;
            }
            float remaining = 0.0F;
            bool endInSight = member->ncorners == 0;
            if (member->ncorners > 0) {
                remaining = HorizontalDistance(position, FromArray(&member->cornerVerts[0]));
                for (int corner = 1; corner < member->ncorners; ++corner) {
                    remaining += HorizontalDistance(FromArray(&member->cornerVerts[static_cast<std::size_t>(corner - 1) * 3U]),
                        FromArray(&member->cornerVerts[static_cast<std::size_t>(corner) * 3U]));
                }
                const int last = member->ncorners - 1;
                endInSight = (member->cornerFlags[last] & DT_STRAIGHTPATH_END) != 0U;
                if (!endInSight) {
                    remaining += HorizontalDistance(FromArray(&member->cornerVerts[static_cast<std::size_t>(last) * 3U]),
                        FromArray(member->corridor.getTarget()));
                }
            } else {
                remaining = HorizontalDistance(position, FromArray(member->corridor.getTarget()));
            }
            record.remaining = remaining;
            const bool settled = record.status == NavPathStatus::Complete || record.status == NavPathStatus::Partial;
            if (settled && endInSight && remaining <= std::max(input.agent.stoppingDistance, 1.0e-3F)) {
                // Within the stopping distance of the end: the crowd stops steering it.
                record.arrived = true;
                record.arrivedAt = FromArray(member->corridor.getTarget());
                static_cast<void>(crowd.resetMoveTarget(record.index));
                dtVset(member->vel, 0.0F, 0.0F, 0.0F);
                dtVset(member->nvel, 0.0F, 0.0F, 0.0F);
            }
        }
        if (record.arrived) {
            record.remaining = HorizontalDistance(position, record.arrivedAt);
        }
        if (record.destinationMissing) {
            record.remaining = HorizontalDistance(position, input.agent.destination);
        }
        output.position = position;
        output.velocity = record.arrived || record.destinationMissing ? Vec3{} : Vec3{ member->vel[0], 0.0F, member->vel[2] };
        output.remainingDistance = record.remaining;
        output.status = record.status;
    }
    stats_.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
}

} // namespace kb::navigation
