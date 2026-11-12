#include "navigation/NavMeshHorde.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>

#if defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
#define KB_HORDE_NEON 1
#elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define KB_HORDE_SSE2 1
#endif

namespace kb::navigation {

using kb::math::Vec3;
using kb::scene::NavPathStatus;

namespace {

constexpr std::uint32_t kNone = NavPolygonGraph::kNone;
const Vec3 kSearchExtents{ 2.0F, 4.0F, 2.0F };
// Agents per piece of a parallel pass; fewer run on the calling thread.
constexpr std::size_t kAgentsPerPiece = 2048U;
// An agent moves to a farther level of detail only this far beyond its edge.
constexpr float kDetailHysteresis = 1.1F;
// Steps between updates of near, mid and far agents.
constexpr std::uint32_t kTickInterval[3] = { 1U, 2U, 4U };
// Nodes a field expands beyond the last agent it serves, so agents stepping on do not wait.
constexpr std::size_t kFieldLookahead = 64U;
// Steps through polygon edges one move may take.
constexpr int kMaxCrossings = 8;
constexpr std::uint32_t kMaxGridCells = 1U << 22U;

template <typename Body>
void ForEachPiece(kb::ecs::WorkerPool* pool, std::size_t count, Body&& body) {
    const std::size_t pieces = pool != nullptr ? pool->WorkerCount() * 4U : 1U;
    const std::size_t size = std::max(kAgentsPerPiece, (count + pieces - 1U) / pieces);
    if (pool == nullptr || count <= size) {
        if (count > 0U) body(std::size_t{ 0U }, count);
        return;
    }
    pool->ParallelForChunks(count, size, [&](kb::ecs::WorkerContext, const kb::ecs::WorkerPoolChunk& piece) {
        body(piece.begin, piece.begin + piece.count);
    });
}

[[nodiscard]] float Cross2(float ax, float az, float bx, float bz) noexcept {
    return ax * bz - az * bx;
}

[[nodiscard]] float HorizontalLength(Vec3 value) noexcept {
    return std::sqrt(value.x * value.x + value.z * value.z);
}

[[nodiscard]] bool SameBits(Vec3 a, Vec3 b) noexcept {
    return std::memcmp(&a, &b, sizeof(Vec3)) == 0;
}

[[nodiscard]] Vec3 Lerp(Vec3 a, Vec3 b, float t) noexcept {
    return Vec3{ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
}

// The polygon's winding over XZ: +1 counter-clockwise, -1 clockwise.
[[nodiscard]] float Winding(const Vec3* v, std::uint32_t count) noexcept {
    float area = 0.0F;
    for (std::uint32_t i = 0U; i < count; ++i) {
        const Vec3 a = v[i];
        const Vec3 b = v[(i + 1U) % count];
        area += Cross2(a.x, a.z, b.x, b.z);
    }
    return area >= 0.0F ? 1.0F : -1.0F;
}

// Height of the polygon under (x, z): its fan of triangles from the first corner.
[[nodiscard]] float PolygonHeight(const Vec3* v, std::uint32_t count, float x, float z) noexcept {
    float best = v[0].y;
    float bestOutside = std::numeric_limits<float>::max();
    for (std::uint32_t i = 1U; i + 1U < count; ++i) {
        const Vec3 a = v[0];
        const Vec3 b = v[i];
        const Vec3 c = v[i + 1U];
        const float den = Cross2(b.x - a.x, b.z - a.z, c.x - a.x, c.z - a.z);
        if (std::fabs(den) < 1.0e-9F) continue;
        const float u = Cross2(x - a.x, z - a.z, c.x - a.x, c.z - a.z) / den;
        const float w = Cross2(b.x - a.x, b.z - a.z, x - a.x, z - a.z) / den;
        const float height = a.y + (b.y - a.y) * u + (c.y - a.y) * w;
        const float outside = std::max({ -u, -w, u + w - 1.0F, 0.0F });
        if (outside < bestOutside) {
            bestOutside = outside;
            best = height;
        }
        if (outside <= 0.0F) break;
    }
    return best;
}

// Closest point of segment ab to p, over XZ.
[[nodiscard]] Vec3 ClosestOnSegment(Vec3 p, Vec3 a, Vec3 b) noexcept {
    const float dx = b.x - a.x;
    const float dz = b.z - a.z;
    const float length = dx * dx + dz * dz;
    const float t = length > 1.0e-12F ? std::clamp(((p.x - a.x) * dx + (p.z - a.z) * dz) / length, 0.0F, 1.0F) : 0.0F;
    return Lerp(a, b, t);
}

// Separation of one agent from the agents of a run of the grid: every neighbour inside the summed
// radii plus `buffer` pushes along the centre line with a squared falloff. Agents more than
// `heightLimit` above or below (another storey) are ignored, and so is the agent itself (`self`).
// Agents standing on the same spot push apart along X, the lower index toward -X.
void AccumulateSeparation(const std::uint32_t* ids, const float* xs, const float* ys, const float* zs, const float* rs, std::size_t count,
    std::uint32_t self, float x, float y, float z, float radius, float buffer, float heightLimit, float& pushX, float& pushZ) noexcept {
    constexpr float kTie = 0.01F;
    constexpr float kTiny = 1.0e-8F;
    std::size_t at = 0U;
    const float inverseBuffer = 1.0F / buffer;
#if defined(KB_HORDE_SSE2)
    __m128 sumX = _mm_setzero_ps();
    __m128 sumZ = _mm_setzero_ps();
    const __m128 px = _mm_set1_ps(x), py = _mm_set1_ps(y), pz = _mm_set1_ps(z);
    const __m128 reachBase = _mm_set1_ps(radius + buffer);
    const __m128 limit = _mm_set1_ps(heightLimit);
    const __m128 tiny = _mm_set1_ps(kTiny);
    const __m128 one = _mm_set1_ps(1.0F);
    const __m128 zero = _mm_setzero_ps();
    const __m128 inverse = _mm_set1_ps(inverseBuffer);
    const __m128 tieBelow = _mm_set1_ps(-kTie), tieAbove = _mm_set1_ps(kTie), tieSquared = _mm_set1_ps(kTie * kTie);
    const __m128 absMask = _mm_castsi128_ps(_mm_set1_epi32(0x7FFFFFFF));
    const __m128i selfIndex = _mm_set1_epi32(static_cast<int>(self));
    for (; at + 4U <= count; at += 4U) {
        const __m128i other = _mm_loadu_si128(reinterpret_cast<const __m128i*>(ids + at));
        const __m128 isSelf = _mm_castsi128_ps(_mm_cmpeq_epi32(other, selfIndex));
        const __m128 below = _mm_castsi128_ps(_mm_cmplt_epi32(selfIndex, other));
        __m128 dx = _mm_sub_ps(px, _mm_loadu_ps(xs + at));
        const __m128 dz0 = _mm_sub_ps(pz, _mm_loadu_ps(zs + at));
        const __m128 dy = _mm_and_ps(_mm_sub_ps(py, _mm_loadu_ps(ys + at)), absMask);
        const __m128 reach = _mm_add_ps(reachBase, _mm_loadu_ps(rs + at));
        __m128 d2 = _mm_add_ps(_mm_mul_ps(dx, dx), _mm_mul_ps(dz0, dz0));
        const __m128 tie = _mm_cmple_ps(d2, tiny);
        const __m128 tieX = _mm_or_ps(_mm_and_ps(below, tieBelow), _mm_andnot_ps(below, tieAbove));
        dx = _mm_or_ps(_mm_and_ps(tie, tieX), _mm_andnot_ps(tie, dx));
        const __m128 dz = _mm_andnot_ps(tie, dz0);
        d2 = _mm_or_ps(_mm_and_ps(tie, tieSquared), _mm_andnot_ps(tie, d2));
        const __m128 inside = _mm_andnot_ps(isSelf, _mm_and_ps(_mm_cmplt_ps(d2, _mm_mul_ps(reach, reach)), _mm_cmplt_ps(dy, limit)));
        const __m128 d = _mm_sqrt_ps(d2);
        __m128 weight = _mm_min_ps(_mm_max_ps(_mm_mul_ps(_mm_sub_ps(reach, d), inverse), zero), one);
        weight = _mm_and_ps(_mm_div_ps(_mm_mul_ps(weight, weight), d), inside);
        sumX = _mm_add_ps(sumX, _mm_mul_ps(dx, weight));
        sumZ = _mm_add_ps(sumZ, _mm_mul_ps(dz, weight));
    }
    alignas(16) float lanesX[4];
    alignas(16) float lanesZ[4];
    _mm_store_ps(lanesX, sumX);
    _mm_store_ps(lanesZ, sumZ);
    pushX += (lanesX[0] + lanesX[1]) + (lanesX[2] + lanesX[3]);
    pushZ += (lanesZ[0] + lanesZ[1]) + (lanesZ[2] + lanesZ[3]);
#elif defined(KB_HORDE_NEON)
    float32x4_t sumX = vdupq_n_f32(0.0F);
    float32x4_t sumZ = vdupq_n_f32(0.0F);
    const float32x4_t px = vdupq_n_f32(x), py = vdupq_n_f32(y), pz = vdupq_n_f32(z);
    const float32x4_t reachBase = vdupq_n_f32(radius + buffer);
    const float32x4_t limit = vdupq_n_f32(heightLimit);
    const float32x4_t tiny = vdupq_n_f32(kTiny);
    const float32x4_t one = vdupq_n_f32(1.0F);
    const float32x4_t zero = vdupq_n_f32(0.0F);
    const float32x4_t inverse = vdupq_n_f32(inverseBuffer);
    const float32x4_t tieBelow = vdupq_n_f32(-kTie), tieAbove = vdupq_n_f32(kTie), tieSquared = vdupq_n_f32(kTie * kTie);
    const uint32x4_t selfIndex = vdupq_n_u32(self);
    for (; at + 4U <= count; at += 4U) {
        const uint32x4_t other = vld1q_u32(ids + at);
        const uint32x4_t notSelf = vmvnq_u32(vceqq_u32(other, selfIndex));
        const uint32x4_t below = vcltq_u32(selfIndex, other);
        const float32x4_t dx0 = vsubq_f32(px, vld1q_f32(xs + at));
        const float32x4_t dz0 = vsubq_f32(pz, vld1q_f32(zs + at));
        const float32x4_t dy = vabsq_f32(vsubq_f32(py, vld1q_f32(ys + at)));
        const float32x4_t reach = vaddq_f32(reachBase, vld1q_f32(rs + at));
        const float32x4_t d20 = vaddq_f32(vmulq_f32(dx0, dx0), vmulq_f32(dz0, dz0));
        const uint32x4_t tie = vcleq_f32(d20, tiny);
        const float32x4_t dx = vbslq_f32(tie, vbslq_f32(below, tieBelow, tieAbove), dx0);
        const float32x4_t dz = vbslq_f32(tie, zero, dz0);
        const float32x4_t d2 = vbslq_f32(tie, tieSquared, d20);
        const uint32x4_t inside = vandq_u32(notSelf, vandq_u32(vcltq_f32(d2, vmulq_f32(reach, reach)), vcltq_f32(dy, limit)));
        const float32x4_t d = vsqrtq_f32(d2);
        float32x4_t weight = vminq_f32(vmaxq_f32(vmulq_f32(vsubq_f32(reach, d), inverse), zero), one);
        weight = vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(vdivq_f32(vmulq_f32(weight, weight), d)), inside));
        sumX = vaddq_f32(sumX, vmulq_f32(dx, weight));
        sumZ = vaddq_f32(sumZ, vmulq_f32(dz, weight));
    }
    pushX += (vgetq_lane_f32(sumX, 0) + vgetq_lane_f32(sumX, 1)) + (vgetq_lane_f32(sumX, 2) + vgetq_lane_f32(sumX, 3));
    pushZ += (vgetq_lane_f32(sumZ, 0) + vgetq_lane_f32(sumZ, 1)) + (vgetq_lane_f32(sumZ, 2) + vgetq_lane_f32(sumZ, 3));
#endif
    for (; at < count; ++at) {
        if (ids[at] == self) continue;
        float dx = x - xs[at];
        float dz = z - zs[at];
        const float reach = radius + buffer + rs[at];
        float d2 = dx * dx + dz * dz;
        if (d2 <= kTiny) {
            dx = self < ids[at] ? -kTie : kTie;
            dz = 0.0F;
            d2 = kTie * kTie;
        }
        if (!(d2 < reach * reach) || !(std::fabs(y - ys[at]) < heightLimit)) continue;
        const float d = std::sqrt(d2);
        float weight = std::min(std::max((reach - d) * inverseBuffer, 0.0F), 1.0F);
        weight = weight * weight / d;
        pushX += dx * weight;
        pushZ += dz * weight;
    }
}

} // namespace

void NavPolygonGraph::Build(const dtNavMesh& mesh, std::uint64_t meshRevision, std::uint64_t meshGeneration) {
    revision = meshRevision;
    generation = meshGeneration;
    firstVertex.clear();
    vertexCount.clear();
    firstPortal.clear();
    portalCount.clear();
    centre.clear();
    area.clear();
    linkRadius.clear();
    reference.clear();
    vertices.clear();
    portals.clear();
    portalOwner.clear();
    firstIncoming.clear();
    incoming.clear();
    const int maxTiles = mesh.getMaxTiles();
    tileBase.assign(static_cast<std::size_t>(maxTiles), kNone);
    tileSalt.assign(static_cast<std::size_t>(maxTiles), 0U);
    std::uint32_t nodes = 0U;
    for (int index = 0; index < maxTiles; ++index) {
        const dtMeshTile* tile = mesh.getTile(index);
        if (tile == nullptr || tile->header == nullptr) continue;
        tileBase[static_cast<std::size_t>(index)] = nodes;
        tileSalt[static_cast<std::size_t>(index)] = tile->salt;
        nodes += static_cast<std::uint32_t>(tile->header->polyCount);
    }
    firstVertex.resize(nodes);
    vertexCount.resize(nodes);
    firstPortal.resize(nodes);
    portalCount.resize(nodes);
    centre.resize(nodes);
    area.resize(nodes);
    linkRadius.resize(nodes);
    reference.resize(nodes);
    // Every node's reference first, so portals to polygons not visited yet resolve.
    for (int index = 0; index < maxTiles; ++index) {
        const dtMeshTile* tile = mesh.getTile(index);
        if (tile == nullptr || tile->header == nullptr) continue;
        const dtPolyRef base = mesh.getPolyRefBase(tile);
        for (int polygon = 0; polygon < tile->header->polyCount; ++polygon) {
            reference[tileBase[static_cast<std::size_t>(index)] + static_cast<std::uint32_t>(polygon)] = base | static_cast<dtPolyRef>(polygon);
        }
    }
    for (int index = 0; index < maxTiles; ++index) {
        const dtMeshTile* tile = mesh.getTile(index);
        if (tile == nullptr || tile->header == nullptr) continue;
        const dtPolyRef base = mesh.getPolyRefBase(tile);
        for (int polygon = 0; polygon < tile->header->polyCount; ++polygon) {
            const std::uint32_t node = tileBase[static_cast<std::size_t>(index)] + static_cast<std::uint32_t>(polygon);
            const dtPoly& poly = tile->polys[polygon];
            reference[node] = base | static_cast<dtPolyRef>(polygon);
            const unsigned char code = poly.getArea();
            area[node] = code == 0U || code > kb::scene::kNavAreaCount ? 0xFFU : static_cast<std::uint8_t>(code - 1U);
            firstVertex[node] = static_cast<std::uint32_t>(vertices.size());
            vertexCount[node] = poly.vertCount;
            Vec3 sum{};
            for (int corner = 0; corner < poly.vertCount; ++corner) {
                const float* v = &tile->verts[static_cast<std::size_t>(poly.verts[corner]) * 3U];
                vertices.push_back(Vec3{ v[0], v[1], v[2] });
                sum = sum + vertices.back();
            }
            centre[node] = sum * (1.0F / static_cast<float>(std::max<int>(poly.vertCount, 1)));
            linkRadius[node] = 0.0F;
            if (poly.getType() == DT_POLYTYPE_OFFMESH_CONNECTION) {
                const int connection = polygon - tile->header->offMeshBase;
                const float radius = connection >= 0 && connection < tile->header->offMeshConCount ? tile->offMeshCons[connection].rad : 0.5F;
                linkRadius[node] = std::max(radius, 0.05F);
            }
            firstPortal[node] = static_cast<std::uint32_t>(portals.size());
            for (unsigned int link = poly.firstLink; link != DT_NULL_LINK; link = tile->links[link].next) {
                const dtLink& entry = tile->links[link];
                const std::uint32_t target = NodeOf(mesh, entry.ref);
                if (target == kNone) continue;
                Portal portal{ .node = target, .edge = entry.edge };
                if (entry.side != 0xFFU && (entry.bmin != 0U || entry.bmax != 255U)) {
                    portal.from = static_cast<float>(entry.bmin) / 255.0F;
                    portal.to = static_cast<float>(entry.bmax) / 255.0F;
                }
                portals.push_back(portal);
                portalOwner.push_back(node);
            }
            portalCount[node] = static_cast<std::uint8_t>(std::min<std::size_t>(portals.size() - firstPortal[node], 255U));
            portals.resize(firstPortal[node] + portalCount[node]);
            portalOwner.resize(portals.size());
        }
    }
    // Portals into every node, grouped by the node they lead to (counting sort keeps their order).
    firstIncoming.assign(static_cast<std::size_t>(nodes) + 1U, 0U);
    for (const Portal& portal : portals) ++firstIncoming[portal.node + 1U];
    for (std::uint32_t node = 0U; node < nodes; ++node) firstIncoming[node + 1U] += firstIncoming[node];
    incoming.resize(portals.size());
    std::vector<std::uint32_t> fill(firstIncoming.begin(), firstIncoming.end() - 1);
    for (std::uint32_t index = 0U; index < portals.size(); ++index) incoming[fill[portals[index].node]++] = index;
}

std::uint32_t NavPolygonGraph::NodeOf(const dtNavMesh& mesh, dtPolyRef polygon) const noexcept {
    if (polygon == 0U) return kNone;
    const unsigned int tile = mesh.decodePolyIdTile(polygon);
    const unsigned int index = mesh.decodePolyIdPoly(polygon);
    if (tile >= tileBase.size() || tileBase[tile] == kNone || mesh.decodePolyIdSalt(polygon) != tileSalt[tile]) return kNone;
    const std::uint32_t node = tileBase[tile] + index;
    return node < reference.size() && reference[node] == polygon ? node : kNone;
}

bool NavMeshHorde::Goal::Same(const Goal& other) const noexcept {
    return profile == other.profile && areas == other.areas && SameBits(destination, other.destination);
}

std::uint64_t NavMeshHorde::Goal::Hash() const noexcept {
    std::uint32_t bits[3];
    std::memcpy(bits, &destination, sizeof(bits));
    std::uint64_t hash = 1469598103934665603ULL;
    for (const std::uint32_t value : { bits[0], bits[1], bits[2], areas, profile }) hash = (hash ^ value) * 1099511628211ULL;
    return hash;
}

void NavMeshHorde::Clear() {
    graphs_.clear();
    fields_.clear();
    records_.clear();
    goals_.clear();
    goalTable_.clear();
}

std::uint32_t NavMeshHorde::FindGoal(const Goal& key) const noexcept {
    if (goalTable_.empty()) return kNone;
    const std::size_t mask = goalTable_.size() - 1U;
    for (std::size_t slot = static_cast<std::size_t>(key.Hash()) & mask;; slot = (slot + 1U) & mask) {
        const std::uint32_t goal = goalTable_[slot];
        if (goal == kNone || goals_[goal].Same(key)) return goal;
    }
}

void NavMeshHorde::IndexGoals(std::uint32_t added) {
    // An open-addressing table at most half full; it is rebuilt when it grows or goals are forgotten.
    if (added == kNone || goals_.size() * 2U > goalTable_.size()) {
        std::size_t size = 64U;
        while (size < goals_.size() * 4U) size *= 2U;
        goalTable_.assign(size, kNone);
        for (std::uint32_t goal = 0U; goal < goals_.size(); ++goal) IndexGoals(goal);
        return;
    }
    const std::size_t mask = goalTable_.size() - 1U;
    std::size_t slot = static_cast<std::size_t>(goals_[added].Hash()) & mask;
    while (goalTable_[slot] != kNone) slot = (slot + 1U) & mask;
    goalTable_[slot] = added;
}

bool NavMeshHorde::OnLink(std::uint64_t id) const noexcept {
    const auto found = std::ranges::lower_bound(records_, id, {}, &AgentRecord::id);
    return found != records_.end() && found->id == id && found->linkNode != kNone;
}

void NavMeshHorde::AddStats(kb::scene::NavigationCrowdStats& stats) const noexcept {
    stats.hordeAgents = agents_;
    stats.hordeUpdated = ticked_;
    stats.hordeAsleep = asleep_;
    stats.hordeWaiting = waiting_;
    stats.hordeOnLinks = onLinks_;
    stats.hordeFields = 0U;
    for (const NavHordeField& field : fields_) stats.hordeFields += field.goal != kNone ? 1U : 0U;
    stats.hordeFieldNodes = expanded_;
    stats.hordeMilliseconds = milliseconds_;
    stats.hordeFieldMilliseconds = fieldMilliseconds_;
}

void NavMeshHorde::SyncGraphs(NavMeshRuntime& runtime) {
    const std::uint32_t profiles = runtime.ProfileCount();
    graphs_.resize(profiles);
    for (std::uint32_t profile = 0U; profile < profiles; ++profile) {
        NavPolygonGraph& graph = graphs_[profile];
        const dtNavMesh* mesh = runtime.Mesh(profile);
        const std::uint64_t revision = runtime.Revision(profile);
        const std::uint64_t generation = runtime.MeshGeneration(profile);
        if (mesh == nullptr || (graph.revision == revision && graph.generation == generation)) continue;
        graph.Build(*mesh, revision, generation);
        // Agents keep their polygon when its tile stayed; fields of the old graph are dropped.
        for (AgentRecord& record : records_) {
            if (record.profile != profile) continue;
            record.node = graph.NodeOf(*mesh, record.polygon);
            record.field = kNone;
            record.linkNode = kNone;
        }
        for (NavHordeField& field : fields_) {
            if (field.profile == profile && field.goal != kNone) field.goal = kNone;
        }
        for (Goal& goal : goals_) {
            if (goal.profile == profile) goal.revision = 0U;
        }
    }
}

std::uint32_t NavMeshHorde::GoalNode(NavMeshRuntime& runtime, std::uint32_t profile, Vec3 destination, kb::scene::NavAreaMask areas) {
    NavAreaFilter filter;
    runtime.ConfigureFilter(filter, areas);
    const auto nearest = runtime.Nearest(profile, destination, kSearchExtents, filter);
    const dtNavMesh* mesh = runtime.Mesh(profile);
    if (!nearest || mesh == nullptr || profile >= graphs_.size()) return kNone;
    return graphs_[profile].NodeOf(*mesh, nearest->first);
}

std::uint32_t NavMeshHorde::FindField(std::uint32_t profile, std::uint32_t goal, kb::scene::NavAreaMask areas, std::size_t cacheSize) {
    std::uint32_t free = kNone;
    std::uint32_t oldest = kNone;
    for (std::uint32_t index = 0U; index < fields_.size(); ++index) {
        NavHordeField& field = fields_[index];
        if (field.goal == kNone) {
            if (free == kNone) free = index;
            continue;
        }
        if (field.profile == profile && field.goal == goal && field.areas == areas) {
            field.lastUsed = step_;
            return index;
        }
        // Fields in use this step or the last (agents still follow them) are kept.
        if (field.lastUsed + 1U < step_ && (oldest == kNone || field.lastUsed < fields_[oldest].lastUsed)) oldest = index;
    }
    std::uint32_t slot = free;
    if (slot == kNone) {
        if (fields_.size() < cacheSize || oldest == kNone) {
            slot = static_cast<std::uint32_t>(fields_.size());
            fields_.emplace_back();
        } else {
            slot = oldest;
        }
    }
    const NavPolygonGraph& graph = graphs_[profile];
    NavHordeField& field = fields_[slot];
    const std::size_t nodes = graph.NodeCount();
    field.profile = profile;
    field.goal = goal;
    field.areas = areas;
    field.graphRevision = graph.revision;
    field.cost.assign(nodes, std::numeric_limits<float>::max());
    field.way.assign(nodes, kNone);
    field.settled.assign(nodes, 0U);
    field.needed.assign(nodes, 0U);
    field.open.clear();
    field.settledCount = 0U;
    field.unmet = 0U;
    field.lookahead = kFieldLookahead;
    field.lastUsed = step_;
    field.stamp = ++fieldStamp_;
    field.cost[goal] = 0.0F;
    field.open.emplace_back(0.0F, goal);
    return slot;
}

std::size_t NavMeshHorde::Expand(const NavMeshRuntime& runtime, NavHordeField& field, std::size_t budget) {
    const NavPolygonGraph& graph = graphs_[field.profile];
    NavAreaFilter filter;
    runtime.ConfigureFilter(filter, field.areas);
    const auto passable = [&](std::uint32_t node) {
        const std::uint8_t area = graph.area[node];
        return area != 0xFFU && (filter.Areas() & kb::scene::NavAreaBit(area)) != 0U && runtime.AreaCost(area) > 0.0F;
    };
    const auto later = [](const std::pair<float, std::uint32_t>& a, const std::pair<float, std::uint32_t>& b) {
        return a.first != b.first ? a.first > b.first : a.second > b.second;
    };
    std::size_t used = 0U;
    while (used < budget && !field.open.empty() && (field.unmet > 0U || field.lookahead > 0U)) {
        std::ranges::pop_heap(field.open, later);
        const auto [cost, node] = field.open.back();
        field.open.pop_back();
        ++used;
        if (field.settled[node] != 0U) continue;
        field.settled[node] = 1U;
        ++field.settledCount;
        if (field.needed[node] != 0U) {
            field.needed[node] = 0U;
            --field.unmet;
        } else if (field.unmet == 0U && field.lookahead > 0U) {
            --field.lookahead;
        }
        // Every node with a portal into this one can step here.
        for (std::uint32_t at = graph.firstIncoming[node]; at < graph.firstIncoming[node + 1U]; ++at) {
            const std::uint32_t portal = graph.incoming[at];
            const std::uint32_t from = graph.portalOwner[portal];
            if (field.settled[from] != 0U || !passable(from)) continue;
            const Vec3 delta = graph.centre[node] - graph.centre[from];
            const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
            const float next = cost + length * runtime.AreaCost(graph.area[from]);
            if (next < field.cost[from]) {
                field.cost[from] = next;
                field.way[from] = portal;
                field.open.emplace_back(next, from);
                std::ranges::push_heap(field.open, later);
            }
        }
    }
    return used;
}

void NavMeshHorde::ExpandFields(const NavMeshRuntime& runtime, std::size_t budget) {
    // Fields agents wait for come first, in slot order starting after last step's first, so a
    // shared budget reaches every field.
    const std::size_t count = fields_.size();
    if (count == 0U) return;
    for (int pass = 0; pass < 2 && budget > 0U; ++pass) {
        for (std::size_t offset = 0U; offset < count && budget > 0U; ++offset) {
            NavHordeField& field = fields_[(fieldCursor_ + offset) % count];
            if (field.goal == kNone || field.open.empty() || field.lastUsed != step_) continue;
            if (pass == 0 && field.unmet == 0U) continue;
            const std::size_t used = Expand(runtime, field, budget);
            budget -= std::min(used, budget);
            expanded_ += used;
        }
    }
    fieldCursor_ = (fieldCursor_ + 1U) % count;
}

void NavMeshHorde::BuildGrid(float cellSize) {
    const std::size_t count = records_.size();
    float minX = std::numeric_limits<float>::max();
    float minZ = minX;
    float maxX = std::numeric_limits<float>::lowest();
    float maxZ = maxX;
    for (const AgentRecord& record : records_) {
        if (record.node == kNone) continue;
        minX = std::min(minX, record.position.x);
        minZ = std::min(minZ, record.position.z);
        maxX = std::max(maxX, record.position.x);
        maxZ = std::max(maxZ, record.position.z);
    }
    if (minX > maxX) {
        gridWidth_ = gridHeight_ = 0U;
        cellStart_.assign(1U, 0U);
        sorted_.clear();
        return;
    }
    gridCell_ = cellSize;
    for (;;) {
        gridWidth_ = static_cast<std::uint32_t>((maxX - minX) / gridCell_) + 1U;
        gridHeight_ = static_cast<std::uint32_t>((maxZ - minZ) / gridCell_) + 1U;
        if (static_cast<std::uint64_t>(gridWidth_) * gridHeight_ <= kMaxGridCells) break;
        gridCell_ *= 2.0F;
    }
    gridMinX_ = minX;
    gridMinZ_ = minZ;
    const std::size_t cells = static_cast<std::size_t>(gridWidth_) * gridHeight_;
    cellStart_.assign(cells + 1U, 0U);
    cellOf_.resize(count);
    for (std::size_t index = 0U; index < count; ++index) {
        const AgentRecord& record = records_[index];
        if (record.node == kNone) {
            cellOf_[index] = kNone;
            continue;
        }
        const auto cx = std::min(static_cast<std::uint32_t>((record.position.x - minX) / gridCell_), gridWidth_ - 1U);
        const auto cz = std::min(static_cast<std::uint32_t>((record.position.z - minZ) / gridCell_), gridHeight_ - 1U);
        cellOf_[index] = cz * gridWidth_ + cx;
        ++cellStart_[cellOf_[index] + 1U];
    }
    for (std::size_t cell = 0U; cell < cells; ++cell) cellStart_[cell + 1U] += cellStart_[cell];
    const std::size_t placed = cellStart_[cells];
    sorted_.resize(placed);
    sortedX_.resize(placed);
    sortedY_.resize(placed);
    sortedZ_.resize(placed);
    sortedRadius_.resize(placed);
    fill_.assign(cellStart_.begin(), cellStart_.end() - 1);
    for (std::size_t index = 0U; index < count; ++index) {
        if (cellOf_[index] == kNone) continue;
        const std::uint32_t at = fill_[cellOf_[index]]++;
        sorted_[at] = static_cast<std::uint32_t>(index);
        sortedX_[at] = records_[index].position.x;
        sortedY_[at] = records_[index].position.y;
        sortedZ_[at] = records_[index].position.z;
        sortedRadius_[at] = radii_[index];
    }
}

void NavMeshHorde::Step(NavMeshRuntime& runtime, std::span<const NavMeshCrowdAgentInput> agents, std::span<const Vec3> focuses,
    const kb::scene::NavigationCrowdSettings& settings, float deltaSeconds, kb::ecs::WorkerPool* workers,
    std::vector<NavMeshCrowdAgentOutput>& outputs) {
    const auto started = std::chrono::steady_clock::now();
    ++step_;
    expanded_ = waiting_ = asleep_ = ticked_ = onLinks_ = 0U;
    agents_ = agents.size();
    outputs.assign(agents.size(), NavMeshCrowdAgentOutput{});

    // 1. Records follow the agents (both in id order).
    bool sameAgents = records_.size() == agents.size();
    for (std::size_t index = 0U; sameAgents && index < agents.size(); ++index) sameAgents = records_[index].id == agents[index].id;
    if (!sameAgents) {
        scratch_.clear();
        scratch_.reserve(agents.size());
        auto known = records_.begin();
        for (const NavMeshCrowdAgentInput& input : agents) {
            while (known != records_.end() && known->id < input.id) ++known;
            if (known != records_.end() && known->id == input.id) {
                scratch_.push_back(*known);
                ++known;
            } else {
                scratch_.push_back(AgentRecord{ .id = input.id, .position = input.position });
            }
        }
        records_.swap(scratch_);
    }
    SyncGraphs(runtime);
    const std::uint32_t profiles = runtime.ProfileCount();

    // 2. Every agent's goal and the field shared by the agents of that goal; agents join the mesh.
    for (Goal& goal : goals_) goal.used = false;
    wanted_.assign(agents.size(), kNone);
    radii_.resize(agents.size());
    std::uint32_t lastGoal = kNone;
    float lastRadius = -1.0F;
    std::uint32_t lastProfile = kNone;
    for (std::size_t index = 0U; index < agents.size(); ++index) {
        const NavMeshCrowdAgentInput& input = agents[index];
        AgentRecord& record = records_[index];
        radii_[index] = input.agent.radius;
        if (input.agent.radius != lastRadius) {
            lastRadius = input.agent.radius;
            lastProfile = runtime.ProfileForRadius(input.agent.radius);
            if (lastProfile >= profiles || runtime.Mesh(lastProfile) == nullptr) lastProfile = kNone;
        }
        const std::uint32_t profile = lastProfile;
        if (!input.agent.enabled || profile == kNone) {
            record.node = kNone;
            continue;
        }
        if (record.profile != profile) {
            record.node = kNone;
            record.field = kNone;
        }
        record.profile = profile;
        const NavPolygonGraph& graph = graphs_[profile];
        const float moved = HorizontalLength(input.position - record.position);
        if (record.node == kNone || (record.linkNode == kNone && (moved > std::max(input.agent.radius * 4.0F, 2.0F) ||
                                         std::fabs(input.position.y - record.position.y) > input.agent.height))) {
            // Joining, or moved far by something else: find the polygon again.
            NavAreaFilter filter;
            runtime.ConfigureFilter(filter, input.agent.areaMask);
            const auto nearest = runtime.Nearest(profile, input.position, kSearchExtents, filter);
            record.node = nearest ? graph.NodeOf(*runtime.Mesh(profile), nearest->first) : kNone;
            record.polygon = nearest ? nearest->first : 0U;
            record.position = nearest ? nearest->second : input.position;
            record.velocity = Vec3{};
            record.linkNode = kNone;
            record.asleep = false;
            if (record.node == kNone) continue;
        } else if (record.linkNode == kNone && moved > 1.0e-4F) {
            // Moved a little by something else (a character's physics): follow it over the polygons.
            record.position = input.position;
        }
        // The goal: agents with the same destination, areas and profile share one.
        Goal key{ .profile = profile, .destination = input.agent.destination, .areas = input.agent.areaMask };
        std::uint32_t goal = kNone;
        if (lastGoal != kNone && goals_[lastGoal].Same(key)) {
            goal = lastGoal;
        } else {
            goal = FindGoal(key);
            if (goal == kNone) {
                goal = static_cast<std::uint32_t>(goals_.size());
                goals_.push_back(key);
                IndexGoals(goal);
            }
        }
        lastGoal = goal;
        Goal& target = goals_[goal];
        target.used = true;
        if (target.revision != graph.revision) {
            target.node = GoalNode(runtime, profile, target.destination, target.areas);
            target.revision = graph.revision;
            target.field = kNone;
        }
        if (target.node == kNone) continue;
        if (target.field == kNone || fields_[target.field].goal != target.node || fields_[target.field].areas != target.areas ||
            fields_[target.field].profile != profile) {
            target.field = FindField(profile, target.node, target.areas, std::max<std::size_t>(settings.hordeFieldCacheSize, 1U));
        }
        fields_[target.field].lastUsed = step_;
        wanted_[index] = goal;
        NavHordeField& field = fields_[target.field];
        if (field.settled[record.node] == 0U && field.needed[record.node] == 0U) {
            field.needed[record.node] = 1U;
            ++field.unmet;
            field.lookahead = kFieldLookahead;
        }
    }
    // Goals no agent is after any more are forgotten (their fields stay cached).
    if (std::ranges::any_of(goals_, [](const Goal& goal) { return !goal.used; })) {
        std::size_t kept = 0U;
        remap_.assign(goals_.size(), kNone);
        for (std::size_t index = 0U; index < goals_.size(); ++index) {
            if (!goals_[index].used) continue;
            remap_[index] = static_cast<std::uint32_t>(kept);
            goals_[kept++] = goals_[index];
        }
        goals_.resize(kept);
        IndexGoals(kNone);
        for (std::uint32_t& goal : wanted_) goal = goal == kNone ? kNone : remap_[goal];
    }

    // 3. Fields grow toward the agents waiting for them, within the step's budget.
    const auto fieldsStarted = std::chrono::steady_clock::now();
    ExpandFields(runtime, std::max<std::size_t>(settings.hordeFieldNodesPerStep, 1U));
    fieldMilliseconds_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - fieldsStarted).count();

    // 4. One spatial grid over where the agents stand at the start of the step.
    float widest = 0.1F;
    for (std::size_t index = 0U; index < agents.size(); ++index) widest = std::max(widest, radii_[index]);
    const float buffer = std::max(settings.hordeSeparationDistance, 0.01F);
    BuildGrid(widest * 2.0F + buffer);

    // 5. Every agent steers and moves, writing only itself.
    const float separationWeight = settings.separationWeight;
    ForEachPiece(workers, agents.size(), [&](std::size_t begin, std::size_t end) {
        for (std::size_t index = begin; index < end; ++index) {
            const NavMeshCrowdAgentInput& input = agents[index];
            AgentRecord& record = records_[index];
            NavMeshCrowdAgentOutput& output = outputs[index];
            output.position = input.position;
            if (!input.agent.enabled) continue;
            if (record.node == kNone || wanted_[index] == kNone) {
                output.status = NavPathStatus::Failed;
                continue;
            }
            const NavPolygonGraph& graph = graphs_[record.profile];
            const Goal& goal = goals_[wanted_[index]];
            const NavHordeField& next = fields_[goal.field];
            // The agent takes the field of its goal once it reaches it, and keeps the old one (unless its
            // slot went to another field) till then.
            std::uint32_t use = record.field;
            if (use != kNone && (fields_[use].stamp != record.fieldStamp || fields_[use].goal == kNone)) use = kNone;
            if (next.settled[record.node] != 0U) use = goal.field;
            if (use != kNone && fields_[use].settled[record.node] == 0U && record.linkNode == kNone) use = kNone;
            record.field = use;
            record.fieldStamp = use != kNone ? fields_[use].stamp : 0U;

            // Level of detail and update interval.
            float focusDistance = 0.0F;
            if (!focuses.empty()) {
                focusDistance = std::numeric_limits<float>::max();
                for (const Vec3 focus : focuses) focusDistance = std::min(focusDistance, HorizontalLength(focus - record.position));
            }
            const auto detailAt = [&](float scale) -> std::uint8_t {
                return focusDistance <= settings.nearDistance * scale ? 0U : focusDistance <= settings.midDistance * scale ? 1U : 2U;
            };
            std::uint8_t detail = detailAt(1.0F);
            if (detail > record.detail) detail = std::max(record.detail, detailAt(kDetailHysteresis));
            record.detail = detail;
            const float stop = std::max(input.agent.stoppingDistance, 1.0e-3F);
            const bool inGoal = record.node == goal.node && record.linkNode == kNone;
            const float toGoal = HorizontalLength(goal.destination - record.position);
            if (record.asleep) {
                // Asleep where it stopped: it wakes when its goal moves away.
                if (!inGoal || HorizontalLength(goal.destination - record.sleptFor) > stop + input.agent.radius) {
                    record.asleep = false;
                } else {
                    output.position = record.position;
                    output.status = NavPathStatus::Complete;
                    output.remainingDistance = toGoal;
                    continue;
                }
            }
            if (use == kNone && !inGoal) {
                // The field has not reached this agent yet: it stands this step. A field that grew as
                // far as it can without reaching it leaves it no way to the goal.
                record.velocity = Vec3{};
                output.position = record.position;
                output.status = next.open.empty() ? NavPathStatus::Failed : NavPathStatus::Pending;
                record.waiting = !next.open.empty();
                continue;
            }
            record.waiting = false;
            const std::uint32_t interval = kTickInterval[detail];
            ++record.idle;
            if (record.idle < interval && (step_ + record.id) % interval != 0U) {
                // Not this agent's step: it keeps going as it was.
                output.position = record.position;
                output.velocity = record.velocity;
                output.status = NavPathStatus::Complete;
                output.remainingDistance = use != kNone ? fields_[use].cost[record.node] : toGoal;
                output.onLink = record.linkNode != kNone;
                continue;
            }
            const float dt = deltaSeconds * static_cast<float>(record.idle);
            record.idle = 0U;
            const float speed = input.agent.maxSpeed;

            if (record.linkNode != kNone) {
                // Crossing an off-mesh link straight to its landing end.
                const std::uint32_t first = graph.firstVertex[record.linkNode];
                const Vec3 from = graph.vertices[first + (1U - record.linkEnd)];
                const Vec3 to = graph.vertices[first + record.linkEnd];
                const Vec3 delta = to - from;
                const float length = std::max(std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z), 1.0e-3F);
                record.linkTravelled += speed * dt;
                const float t = std::min(record.linkTravelled / length, 1.0F);
                record.position = Lerp(from, to, t);
                record.velocity = delta * (speed / length);
                if (t >= 1.0F) {
                    // An end point can touch several polygons (a bridge and the ground under it): the
                    // agent lands on the one the field led it to.
                    record.linkNode = kNone;
                    record.node = record.linkLanding;
                }
                record.polygon = graph.reference[record.node];
                output.position = record.position;
                output.velocity = record.velocity;
                output.onLink = record.linkNode != kNone;
                output.status = NavPathStatus::Complete;
                output.remainingDistance = use != kNone ? fields_[use].cost[record.node] : toGoal;
                continue;
            }

            // Where to head: the destination in the goal's polygon, else the portal toward the goal.
            Vec3 aim = goal.destination;
            std::uint32_t linkAhead = kNone;
            std::uint8_t linkEntry = 0U;
            const auto portalPoint = [&](std::uint32_t node, std::uint32_t portalIndex, Vec3 from) {
                const NavPolygonGraph::Portal& portal = graph.portals[portalIndex];
                const std::uint32_t first = graph.firstVertex[node];
                const std::uint32_t count = graph.vertexCount[node];
                const Vec3 a = graph.vertices[first + portal.edge];
                const Vec3 b = graph.vertices[first + (portal.edge + 1U) % count];
                Vec3 left = Lerp(a, b, portal.from);
                Vec3 right = Lerp(a, b, portal.to);
                const float width = HorizontalLength(right - left);
                const float margin = std::min(input.agent.radius, width * 0.5F);
                if (width > 1.0e-4F) {
                    const Vec3 along = (right - left) * (margin / width);
                    left = left + along;
                    right = right - along;
                }
                return ClosestOnSegment(from, left, right);
            };
            if (!inGoal && use != kNone) {
                const NavHordeField& field = fields_[use];
                const std::uint32_t way = field.way[record.node];
                if (way != kNone) {
                    const std::uint32_t ahead = graph.portals[way].node;
                    if (graph.IsLink(ahead)) {
                        // Head for the link's end point on this side.
                        for (std::uint32_t at = graph.firstPortal[ahead]; at < graph.firstPortal[ahead] + graph.portalCount[ahead]; ++at) {
                            if (graph.portals[at].node == record.node) linkEntry = graph.portals[at].edge;
                        }
                        aim = graph.vertices[graph.firstVertex[ahead] + linkEntry];
                        linkAhead = ahead;
                    } else {
                        aim = portalPoint(record.node, way, record.position);
                        // Close to the portal: look one polygon further so the agent does not stall on the edge.
                        if (HorizontalLength(aim - record.position) < input.agent.radius + 0.25F) {
                            if (ahead == goal.node) {
                                aim = goal.destination;
                            } else if (field.way[ahead] != kNone && !graph.IsLink(graph.portals[field.way[ahead]].node)) {
                                aim = portalPoint(ahead, field.way[ahead], aim);
                            } else {
                                aim = graph.centre[ahead];
                            }
                        }
                    }
                }
            }
            if (linkAhead != kNone && HorizontalLength(aim - record.position) <= std::max(graph.linkRadius[linkAhead], input.agent.radius * 2.25F) &&
                fields_[use].way[linkAhead] != kNone) {
                // At the link: cross it toward the end the field continues from.
                record.linkNode = linkAhead;
                record.linkEnd = graph.portals[fields_[use].way[linkAhead]].edge;
                record.linkLanding = graph.portals[fields_[use].way[linkAhead]].node;
                record.linkTravelled = 0.0F;
                record.velocity = Vec3{};
                output.position = record.position;
                output.onLink = true;
                output.status = NavPathStatus::Complete;
                output.remainingDistance = fields_[use].cost[record.node];
                continue;
            }
            Vec3 desired{};
            const Vec3 toAim{ aim.x - record.position.x, 0.0F, aim.z - record.position.z };
            const float aimDistance = HorizontalLength(toAim);
            const bool arrived = inGoal && toGoal <= stop;
            if (!arrived && aimDistance > 1.0e-4F) {
                // Slow down over the last metres before the destination.
                const float slow = inGoal ? std::min(1.0F, toGoal / std::max(input.agent.radius * 2.0F, 0.5F)) : 1.0F;
                desired = toAim * (speed * slow / aimDistance);
            }
            // Separation from the neighbours of the grid (near and mid agents).
            float pushX = 0.0F;
            float pushZ = 0.0F;
            if (detail < 2U && separationWeight > 0.0F && gridWidth_ > 0U) {
                const float reach = input.agent.radius + widest + buffer;
                const auto axis = [&](float value, float origin, std::uint32_t count) {
                    const float cell = std::floor((value - origin) / gridCell_);
                    return cell <= 0.0F ? 0U : cell >= static_cast<float>(count) ? count - 1U : static_cast<std::uint32_t>(cell);
                };
                const std::uint32_t lowX = axis(record.position.x - reach, gridMinX_, gridWidth_);
                const std::uint32_t highX = axis(record.position.x + reach, gridMinX_, gridWidth_);
                const std::uint32_t lowZ = axis(record.position.z - reach, gridMinZ_, gridHeight_);
                const std::uint32_t highZ = axis(record.position.z + reach, gridMinZ_, gridHeight_);
                for (std::uint32_t cz = lowZ; cz <= highZ; ++cz) {
                    const std::size_t row = static_cast<std::size_t>(cz) * gridWidth_;
                    const std::uint32_t first = cellStart_[row + lowX];
                    const std::uint32_t last = cellStart_[row + highX + 1U];
                    AccumulateSeparation(sorted_.data() + first, sortedX_.data() + first, sortedY_.data() + first, sortedZ_.data() + first, sortedRadius_.data() + first,
                        last - first, static_cast<std::uint32_t>(index), record.position.x, record.position.y, record.position.z, input.agent.radius, buffer,
                        input.agent.height, pushX, pushZ);
                }
                const float scale = separationWeight * 0.5F * speed;
                desired.x += pushX * scale;
                desired.z += pushZ * scale;
            }
            const float desiredSpeed = HorizontalLength(desired);
            if (desiredSpeed > speed) desired = desired * (speed / desiredSpeed);
            if (arrived && pushX * pushX + pushZ * pushZ < 1.0e-6F) {
                record.asleep = true;
                record.sleptFor = goal.destination;
                record.velocity = Vec3{};
                output.position = record.position;
                output.status = NavPathStatus::Complete;
                output.remainingDistance = toGoal;
                continue;
            }
            // Accelerate toward the desired velocity.
            Vec3 change = desired - record.velocity;
            change.y = 0.0F;
            const float changeLength = HorizontalLength(change);
            const float maxChange = input.agent.acceleration * dt;
            if (changeLength > maxChange && changeLength > 0.0F) change = change * (maxChange / changeLength);
            record.velocity = Vec3{ record.velocity.x + change.x, 0.0F, record.velocity.z + change.z };

            // Move over the polygons: through portals into neighbours the agent may enter, sliding along walls.
            Vec3 from = record.position;
            Vec3 to{ from.x + record.velocity.x * dt, from.y, from.z + record.velocity.z * dt };
            std::uint32_t node = record.node;
            const kb::scene::NavAreaMask areas = goal.areas;
            for (int crossing = 0; crossing < kMaxCrossings; ++crossing) {
                const std::uint32_t first = graph.firstVertex[node];
                const std::uint32_t count = graph.vertexCount[node];
                const Vec3* v = &graph.vertices[first];
                const float winding = Winding(v, count);
                float bestT = 2.0F;
                std::uint32_t bestEdge = kNone;
                float bestTo = 0.0F;
                float bestLength = 1.0F;
                for (std::uint32_t edge = 0U; edge < count; ++edge) {
                    const Vec3 a = v[edge];
                    const Vec3 b = v[(edge + 1U) % count];
                    const float sideTo = winding * Cross2(b.x - a.x, b.z - a.z, to.x - a.x, to.z - a.z);
                    if (sideTo >= 0.0F) continue;
                    const float sideFrom = std::max(winding * Cross2(b.x - a.x, b.z - a.z, from.x - a.x, from.z - a.z), 0.0F);
                    const float t = sideFrom / (sideFrom - sideTo);
                    if (t < bestT) {
                        bestT = t;
                        bestEdge = edge;
                        bestTo = sideTo;
                        bestLength = std::max(HorizontalLength(b - a), 1.0e-6F);
                    }
                }
                if (bestEdge == kNone) {
                    from = to;
                    break;
                }
                const Vec3 a = v[bestEdge];
                const Vec3 b = v[(bestEdge + 1U) % count];
                const Vec3 cross = Lerp(from, to, bestT);
                const float along = std::clamp(((cross.x - a.x) * (b.x - a.x) + (cross.z - a.z) * (b.z - a.z)) / (bestLength * bestLength), 0.0F, 1.0F);
                std::uint32_t beyond = kNone;
                for (std::uint32_t at = graph.firstPortal[node]; at < graph.firstPortal[node] + graph.portalCount[node]; ++at) {
                    const NavPolygonGraph::Portal& portal = graph.portals[at];
                    if (portal.edge != bestEdge || graph.IsLink(portal.node) || along < portal.from - 1.0e-3F || along > portal.to + 1.0e-3F) continue;
                    const std::uint8_t area = graph.area[portal.node];
                    if (area != 0xFFU && (areas & kb::scene::NavAreaBit(area)) != 0U) beyond = portal.node;
                }
                if (beyond != kNone) {
                    node = beyond;
                    from = cross;
                    continue;
                }
                // A wall: keep the part of the move along it, just inside.
                const float nx = -winding * (b.z - a.z) / bestLength;
                const float nz = winding * (b.x - a.x) / bestLength;
                const float depth = bestTo / bestLength;
                to = Vec3{ to.x - nx * depth + nx * 1.0e-3F, to.y, to.z - nz * depth + nz * 1.0e-3F };
                from = Vec3{ cross.x + nx * 1.0e-3F, cross.y, cross.z + nz * 1.0e-3F };
            }
            from.y = PolygonHeight(&graph.vertices[graph.firstVertex[node]], graph.vertexCount[node], from.x, from.z);
            record.position = from;
            record.node = node;
            record.polygon = graph.reference[node];
            output.position = record.position;
            output.velocity = record.velocity;
            output.status = NavPathStatus::Complete;
            output.remainingDistance = node == goal.node ? HorizontalLength(goal.destination - from)
                : use != kNone && fields_[use].settled[node] != 0U ? fields_[use].cost[node] : toGoal;
        }
    });
    for (std::size_t index = 0U; index < agents.size(); ++index) {
        const AgentRecord& record = records_[index];
        if (!agents[index].agent.enabled || record.node == kNone) continue;
        waiting_ += record.waiting ? 1U : 0U;
        asleep_ += record.asleep ? 1U : 0U;
        onLinks_ += record.linkNode != kNone ? 1U : 0U;
        ticked_ += record.idle == 0U && !record.asleep && !record.waiting ? 1U : 0U;
    }
    milliseconds_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
}

} // namespace kb::navigation
