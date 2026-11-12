#include "navigation/NavMeshHorde.hpp"

#include <algorithm>
#include <atomic>
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
// Passes over fewer agents than this run on the calling thread; larger ones are cut into blocks of
// kAgentsPerBlock that the workers take one after another.
constexpr std::size_t kMinAgentsForWorkers = 2048U;
constexpr std::size_t kAgentsPerBlock = 128U;
// An agent moves to a farther level of detail only this far beyond its edge.
constexpr float kDetailHysteresis = 1.1F;
// Steps between updates of near, mid and far agents.
constexpr std::uint32_t kTickInterval[3] = { 1U, 2U, 4U };
// Clusters a route expands beyond the last agent it serves, so agents stepping on do not wait.
constexpr std::size_t kRouteLookahead = 8U;
// Steps a field or route may go unused before its slot can be taken by another.
constexpr std::uint64_t kFieldIdle = 48U;
// Steps between the refreshes of the use stamps of the exit fields routes keep.
constexpr std::uint64_t kExitRefresh = 16U;
constexpr std::uint8_t kNoWay = 0xFFU;
// Steps through polygon edges one move may take.
constexpr int kMaxCrossings = 8;
constexpr std::uint32_t kMaxGridCells = 1U << 22U;

// Runs `body(begin, end)` over all `count` agents. On the workers, each one takes the next block of
// agents from a shared counter until none is left, so a worker on a slower core ends up with fewer.
template <typename Body>
void ForEachPiece(kb::ecs::WorkerPool* pool, std::size_t count, Body&& body) {
    if (pool == nullptr || count <= kMinAgentsForWorkers || count < kMinAgentsOnWorkers) {
        if (count > 0U) body(std::size_t{ 0U }, count);
        return;
    }
    std::atomic<std::size_t> next{ 0U };
    pool->ParallelForChunks(pool->WorkerCount(), 1U, [&](kb::ecs::WorkerContext, const kb::ecs::WorkerPoolChunk&) {
        for (std::size_t begin = next.fetch_add(kAgentsPerBlock, std::memory_order_relaxed); begin < count;
             begin = next.fetch_add(kAgentsPerBlock, std::memory_order_relaxed)) {
            body(begin, std::min(begin + kAgentsPerBlock, count));
        }
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

[[nodiscard]] std::uint64_t Mix(std::uint64_t hash, std::uint64_t value) noexcept {
    return (hash ^ value) * 1099511628211ULL;
}

[[nodiscard]] std::uint64_t FloatBits(float value) noexcept {
    std::uint32_t bits = 0U;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

// Whether `field` is the one over `source` leaving by `exitWay` for `areas` made with `costs`.
[[nodiscard]] std::uint64_t FieldKey(std::uint32_t profile, std::uint32_t source, std::uint8_t exitWay, kb::scene::NavAreaMask areas,
    std::uint64_t costs) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const std::uint64_t value : { std::uint64_t{ profile }, std::uint64_t{ source }, std::uint64_t{ exitWay }, std::uint64_t{ areas }, costs }) {
        hash = Mix(hash, value);
    }
    return hash;
}

[[nodiscard]] bool SameField(const NavHordeField& field, std::uint32_t profile, std::uint32_t source, std::uint8_t exitWay,
    kb::scene::NavAreaMask areas, std::uint64_t costs) noexcept {
    return field.source == source && field.profile == profile && field.exitWay == exitWay && field.areas == areas && field.costs == costs;
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
    std::vector<std::uint32_t> slotOf(nodes);
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
            slotOf[node] = static_cast<std::uint32_t>(index);
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

    // Clusters: the polygons of one tile slot that join through portals; a link stands alone.
    std::vector<std::uint32_t> parent(nodes);
    for (std::uint32_t node = 0U; node < nodes; ++node) parent[node] = node;
    const auto root = [&](std::uint32_t node) {
        while (parent[node] != node) {
            parent[node] = parent[parent[node]];
            node = parent[node];
        }
        return node;
    };
    for (std::uint32_t index = 0U; index < portals.size(); ++index) {
        const std::uint32_t a = portalOwner[index];
        const std::uint32_t b = portals[index].node;
        if (slotOf[a] != slotOf[b] || linkRadius[a] > 0.0F || linkRadius[b] > 0.0F) continue;
        const std::uint32_t ra = root(a);
        const std::uint32_t rb = root(b);
        if (ra != rb) parent[std::max(ra, rb)] = std::min(ra, rb);
    }
    cluster.assign(nodes, kNone);
    local.assign(nodes, 0U);
    std::vector<std::uint32_t> idOfRoot(nodes, kNone);
    std::uint32_t clusters = 0U;
    for (std::uint32_t node = 0U; node < nodes; ++node) {
        const std::uint32_t top = root(node);
        if (idOfRoot[top] == kNone) idOfRoot[top] = clusters++;
        cluster[node] = idOfRoot[top];
    }
    clusterFirst.assign(static_cast<std::size_t>(clusters) + 1U, 0U);
    for (std::uint32_t node = 0U; node < nodes; ++node) ++clusterFirst[cluster[node] + 1U];
    for (std::uint32_t c = 0U; c < clusters; ++c) clusterFirst[c + 1U] += clusterFirst[c];
    clusterNodes.resize(nodes);
    {
        std::vector<std::uint32_t> cursor(clusterFirst.begin(), clusterFirst.end() - 1);
        for (std::uint32_t node = 0U; node < nodes; ++node) {
            const std::uint32_t c = cluster[node];
            local[node] = cursor[c] - clusterFirst[c];
            clusterNodes[cursor[c]++] = node;
        }
    }
    clusterCentre.assign(clusters, Vec3{});
    signature.assign(clusters, 0U);
    for (std::uint32_t c = 0U; c < clusters; ++c) {
        Vec3 sum{};
        std::uint64_t hash = 1469598103934665603ULL;
        for (std::uint32_t at = clusterFirst[c]; at < clusterFirst[c + 1U]; ++at) {
            const std::uint32_t node = clusterNodes[at];
            sum = sum + centre[node];
            hash = Mix(hash, reference[node]);
            hash = Mix(hash, (static_cast<std::uint64_t>(area[node]) << 8U) | vertexCount[node]);
            hash = Mix(hash, FloatBits(centre[node].x));
            hash = Mix(hash, FloatBits(centre[node].y));
            hash = Mix(hash, FloatBits(centre[node].z));
            for (std::uint32_t portal = firstPortal[node]; portal < firstPortal[node] + portalCount[node]; ++portal) {
                hash = Mix(hash, reference[portals[portal].node]);
                hash = Mix(hash, (static_cast<std::uint64_t>(portals[portal].edge) << 32U) | FloatBits(portals[portal].from));
                hash = Mix(hash, FloatBits(portals[portal].to));
            }
        }
        clusterCentre[c] = sum * (1.0F / static_cast<float>(clusterFirst[c + 1U] - clusterFirst[c]));
        signature[c] = hash;
    }
    // Gateways: for every ordered pair of neighbouring clusters the widest opening between them.
    struct Opening {
        std::uint32_t portal = 0U;
        float width = 0.0F;
    };
    std::vector<Opening> openings;
    for (std::uint32_t index = 0U; index < portals.size(); ++index) {
        const std::uint32_t a = portalOwner[index];
        const std::uint32_t b = portals[index].node;
        if (cluster[a] == cluster[b]) continue;
        float width = 1.0F;
        if (linkRadius[a] <= 0.0F && linkRadius[b] <= 0.0F) {
            const Vec3 p = vertices[firstVertex[a] + portals[index].edge];
            const Vec3 q = vertices[firstVertex[a] + (portals[index].edge + 1U) % vertexCount[a]];
            width = HorizontalLength(q - p) * (portals[index].to - portals[index].from);
        }
        openings.push_back(Opening{ index, width });
    }
    std::ranges::sort(openings, [&](const Opening& x, const Opening& y) {
        const std::uint32_t fx = cluster[portalOwner[x.portal]], fy = cluster[portalOwner[y.portal]];
        if (fx != fy) return fx < fy;
        const std::uint32_t tx = cluster[portals[x.portal].node], ty = cluster[portals[y.portal].node];
        if (tx != ty) return tx < ty;
        return x.width != y.width ? x.width > y.width : x.portal < y.portal;
    });
    gateways.clear();
    gatewayFirst.assign(static_cast<std::size_t>(clusters) + 1U, 0U);
    for (const Opening& opening : openings) {
        const std::uint32_t from = cluster[portalOwner[opening.portal]];
        const std::uint32_t to = cluster[portals[opening.portal].node];
        if (!gateways.empty() && gateways.back().from == from && gateways.back().to == to) continue;
        const Vec3 delta = clusterCentre[to] - clusterCentre[from];
        gateways.push_back(Gateway{ from, to, portalOwner[opening.portal], opening.portal,
            std::max(std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z), 0.01F) });
        ++gatewayFirst[from + 1U];
    }
    for (std::uint32_t c = 0U; c < clusters; ++c) gatewayFirst[c + 1U] += gatewayFirst[c];
    firstArriving.assign(static_cast<std::size_t>(clusters) + 1U, 0U);
    for (const Gateway& gateway : gateways) ++firstArriving[gateway.to + 1U];
    for (std::uint32_t c = 0U; c < clusters; ++c) firstArriving[c + 1U] += firstArriving[c];
    arriving.resize(gateways.size());
    std::vector<std::uint32_t> arrivals(firstArriving.begin(), firstArriving.end() - 1);
    for (std::uint32_t index = 0U; index < gateways.size(); ++index) arriving[arrivals[gateways[index].to]++] = index;
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
    fieldIndex_.clear();
    freeFields_.clear();
    routes_.clear();
    costSignature_ = 0U;
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
    for (const NavHordeField& field : fields_) stats.hordeFields += field.source != kNone ? 1U : 0U;
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
        // A finished field survives where its cluster is unchanged (the same polygons, areas and
        // portals), so only the tiles that were rebuilt, and their neighbours, make new ones.
        for (NavHordeField& field : fields_) {
            if (field.profile != profile || field.source == kNone) continue;
            const std::uint32_t anchor = graph.NodeOf(*mesh, field.anchor);
            const std::uint32_t source = graph.NodeOf(*mesh, field.sourceReference);
            const bool kept = field.open.empty() && anchor != kNone && source != kNone && graph.cluster[anchor] == graph.cluster[source] &&
                graph.clusterNodes[graph.clusterFirst[graph.cluster[anchor]]] == anchor && graph.signature[graph.cluster[anchor]] == field.signature;
            field.source = kept ? source : kNone;
            field.cluster = kept ? graph.cluster[anchor] : kNone;
        }
        freeFields_.clear();
        for (std::uint32_t slot = 0U; slot < fields_.size(); ++slot) {
            if (fields_[slot].source == kNone) freeFields_.push_back(slot);
        }
        RebuildFieldIndex();
        for (NavHordeRoute& route : routes_) {
            if (route.profile == profile) route.goalCluster = kNone;
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

std::uint32_t NavMeshHorde::FindField(std::uint32_t profile, std::uint32_t goal, std::uint8_t exitWay, kb::scene::NavAreaMask areas) {
    const std::uint64_t key = FieldKey(profile, goal, exitWay, areas, costSignature_);
    for (auto [at, end] = fieldIndex_.equal_range(key); at != end; ++at) {
        NavHordeField& field = fields_[at->second];
        if (SameField(field, profile, goal, exitWay, areas, costSignature_)) {
            field.lastUsed = step_;
            return at->second;
        }
    }
    std::uint32_t slot = kNone;
    if (!freeFields_.empty()) {
        slot = freeFields_.back();
        freeFields_.pop_back();
    } else {
        slot = static_cast<std::uint32_t>(fields_.size());
        fields_.emplace_back();
    }
    fieldIndex_.emplace(key, slot);
    const NavPolygonGraph& graph = graphs_[profile];
    NavHordeField& field = fields_[slot];
    const std::uint32_t cluster = graph.cluster[goal];
    const std::size_t nodes = graph.clusterFirst[cluster + 1U] - graph.clusterFirst[cluster];
    field.profile = profile;
    field.cluster = cluster;
    field.source = goal;
    field.exitWay = exitWay;
    field.areas = areas;
    field.costs = costSignature_;
    field.anchor = graph.reference[graph.clusterNodes[graph.clusterFirst[cluster]]];
    field.sourceReference = graph.reference[goal];
    field.signature = graph.signature[cluster];
    field.cost.assign(nodes, std::numeric_limits<float>::max());
    field.way.assign(nodes, kNoWay);
    field.settled.assign(nodes, 0U);
    field.open.clear();
    field.wanted = true;
    field.lastUsed = step_;
    field.stamp = ++fieldStamp_;
    field.cost[graph.local[goal]] = 0.0F;
    field.way[graph.local[goal]] = exitWay;
    field.open.emplace_back(0.0F, goal);
    return slot;
}

void NavMeshHorde::RebuildFieldIndex() {
    fieldIndex_.clear();
    for (std::uint32_t slot = 0U; slot < fields_.size(); ++slot) {
        const NavHordeField& field = fields_[slot];
        if (field.source != kNone) fieldIndex_.emplace(FieldKey(field.profile, field.source, field.exitWay, field.areas, field.costs), slot);
    }
}

void NavMeshHorde::SweepFields(std::size_t cacheSize) {
    std::size_t live = fields_.size() - freeFields_.size();
    if (live <= cacheSize) return;
    std::vector<std::uint32_t> idle;
    for (std::uint32_t slot = 0U; slot < fields_.size(); ++slot) {
        if (fields_[slot].source != kNone && fields_[slot].lastUsed + kFieldIdle < step_) idle.push_back(slot);
    }
    std::ranges::sort(idle, [&](std::uint32_t a, std::uint32_t b) {
        return fields_[a].lastUsed != fields_[b].lastUsed ? fields_[a].lastUsed < fields_[b].lastUsed : a < b;
    });
    for (std::size_t at = 0U; at < idle.size() && live > cacheSize; ++at, --live) {
        fields_[idle[at]].source = kNone;
        freeFields_.push_back(idle[at]);
    }
    RebuildFieldIndex();
}

std::uint32_t NavMeshHorde::FindRoute(std::uint32_t profile, std::uint32_t goalCluster, std::size_t cacheSize) {
    const NavPolygonGraph& graph = graphs_[profile];
    std::uint32_t free = kNone;
    std::uint32_t oldest = kNone;
    for (std::uint32_t index = 0U; index < routes_.size(); ++index) {
        NavHordeRoute& route = routes_[index];
        if (route.goalCluster == kNone) {
            if (free == kNone) free = index;
            continue;
        }
        if (route.profile == profile && route.goalCluster == goalCluster && route.graphRevision == graph.revision) {
            route.lastUsed = step_;
            return index;
        }
        if (route.lastUsed + kFieldIdle < step_ && (oldest == kNone || route.lastUsed < routes_[oldest].lastUsed)) oldest = index;
    }
    std::uint32_t slot = free;
    if (slot == kNone) {
        if (routes_.size() < cacheSize || oldest == kNone) {
            slot = static_cast<std::uint32_t>(routes_.size());
            routes_.emplace_back();
        } else {
            slot = oldest;
        }
    }
    NavHordeRoute& route = routes_[slot];
    const std::size_t clusters = graph.ClusterCount();
    route.profile = profile;
    route.goalCluster = goalCluster;
    route.graphRevision = graph.revision;
    route.cost.assign(clusters, std::numeric_limits<float>::max());
    route.gateway.assign(clusters, kNone);
    route.settled.assign(clusters, 0U);
    route.needed.assign(clusters, 0U);
    route.exitField.assign(clusters, kNone);
    route.exitStamp.assign(clusters, 0U);
    route.exitClusters.clear();
    route.unmet = 0U;
    route.lookahead = kRouteLookahead;
    route.open.clear();
    route.lastUsed = step_;
    route.stamp = ++fieldStamp_;
    route.cost[goalCluster] = 0.0F;
    route.open.emplace_back(0.0F, goalCluster);
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
    while (used < budget && !field.open.empty()) {
        std::ranges::pop_heap(field.open, later);
        const auto [cost, node] = field.open.back();
        field.open.pop_back();
        ++used;
        if (field.settled[graph.local[node]] != 0U) continue;
        if (node == field.source && !passable(node)) {
            // A source the agents may not enter leads nowhere.
            field.open.clear();
            break;
        }
        field.settled[graph.local[node]] = 1U;
        // Every node of the cluster with a portal into this one can step here.
        for (std::uint32_t at = graph.firstIncoming[node]; at < graph.firstIncoming[node + 1U]; ++at) {
            const std::uint32_t portal = graph.incoming[at];
            const std::uint32_t from = graph.portalOwner[portal];
            if (graph.cluster[from] != field.cluster || field.settled[graph.local[from]] != 0U || !passable(from)) continue;
            const Vec3 delta = graph.centre[node] - graph.centre[from];
            const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
            const float next = cost + length * runtime.AreaCost(graph.area[from]);
            if (next < field.cost[graph.local[from]]) {
                field.cost[graph.local[from]] = next;
                field.way[graph.local[from]] = static_cast<std::uint8_t>(portal - graph.firstPortal[from]);
                field.open.emplace_back(next, from);
                std::ranges::push_heap(field.open, later);
            }
        }
    }
    return used;
}

std::size_t NavMeshHorde::ExpandRoute(NavHordeRoute& route, std::size_t budget) {
    const NavPolygonGraph& graph = graphs_[route.profile];
    const auto later = [](const std::pair<float, std::uint32_t>& a, const std::pair<float, std::uint32_t>& b) {
        return a.first != b.first ? a.first > b.first : a.second > b.second;
    };
    std::size_t used = 0U;
    while (used < budget && !route.open.empty() && (route.unmet > 0U || route.lookahead > 0U)) {
        std::ranges::pop_heap(route.open, later);
        const auto [cost, cluster] = route.open.back();
        route.open.pop_back();
        ++used;
        if (route.settled[cluster] != 0U) continue;
        route.settled[cluster] = 1U;
        if (route.needed[cluster] != 0U) {
            route.needed[cluster] = 0U;
            --route.unmet;
        } else if (route.unmet == 0U && route.lookahead > 0U) {
            --route.lookahead;
        }
        // Every cluster with a gateway into this one can step here.
        for (std::uint32_t at = graph.firstArriving[cluster]; at < graph.firstArriving[cluster + 1U]; ++at) {
            const std::uint32_t gateway = graph.arriving[at];
            const std::uint32_t from = graph.gateways[gateway].from;
            if (route.settled[from] != 0U) continue;
            const float next = cost + graph.gateways[gateway].cost;
            if (next < route.cost[from]) {
                route.cost[from] = next;
                route.gateway[from] = gateway;
                route.open.emplace_back(next, from);
                std::ranges::push_heap(route.open, later);
            } else if (route.gateway[from] != kNone && next <= route.cost[from] + 1.0e-3F) {
                // Equally cheap ways: the one that goes on toward the goal, so routes run straight rather than along a lattice.
                const Vec3 goalCentre = graph.clusterCentre[route.goalCluster];
                const Vec3 viaThis = graph.clusterCentre[graph.gateways[gateway].to] - goalCentre;
                const Vec3 viaOther = graph.clusterCentre[graph.gateways[route.gateway[from]].to] - goalCentre;
                if (viaThis.x * viaThis.x + viaThis.z * viaThis.z < viaOther.x * viaOther.x + viaOther.z * viaOther.z) route.gateway[from] = gateway;
            }
        }
    }
    return used;
}

void NavMeshHorde::ExpandFields(const NavMeshRuntime& runtime, std::size_t budget) {
    // Routes first (one cluster step unblocks many agents), then the fields agents wait for, then
    // the others, each in slot order starting after last step's first so the shared budget reaches all.
    for (NavHordeRoute& route : routes_) {
        if (route.goalCluster == kNone || route.open.empty() || route.lastUsed != step_ || budget == 0U) continue;
        const std::size_t used = ExpandRoute(route, budget);
        budget -= std::min(used, budget);
        expanded_ += used;
    }
    const std::size_t count = fields_.size();
    if (count == 0U) return;
    for (int pass = 0; pass < 2 && budget > 0U; ++pass) {
        for (std::size_t offset = 0U; offset < count && budget > 0U; ++offset) {
            NavHordeField& field = fields_[(fieldCursor_ + offset) % count];
            if (field.source == kNone || field.open.empty() || field.lastUsed + kExitRefresh * 2U < step_) continue;
            if (pass == 0 && !field.wanted) continue;
            const std::size_t used = Expand(runtime, field, budget);
            budget -= std::min(used, budget);
            expanded_ += used;
            if (field.open.empty()) field.wanted = false;
        }
    }
    fieldCursor_ = (fieldCursor_ + 1U) % count;
}

void NavMeshHorde::RegisterDemand(std::uint32_t goalIndex, std::uint32_t node) {
    const Goal& goal = goals_[goalIndex];
    const NavPolygonGraph& graph = graphs_[goal.profile];
    const std::uint32_t here = graph.cluster[node];
    if (here == goal.cluster) {
        fields_[goal.field].wanted = true;
        return;
    }
    NavHordeRoute& route = routes_[goal.route];
    if (route.settled[here] == 0U) {
        if (route.needed[here] == 0U) {
            route.needed[here] = 1U;
            ++route.unmet;
            route.lookahead = kRouteLookahead;
        }
        return;
    }
    // The exit field of a cluster's gateway, made once per route.
    const auto exitOf = [&](std::uint32_t cluster) {
        const std::uint32_t slot = route.exitField[cluster];
        if (slot != kNone && fields_[slot].source != kNone && fields_[slot].stamp == route.exitStamp[cluster] && fields_[slot].costs == costSignature_) return slot;
        const NavPolygonGraph::Gateway& gateway = graph.gateways[route.gateway[cluster]];
        const std::uint32_t made = FindField(goal.profile, gateway.node, static_cast<std::uint8_t>(gateway.portal - graph.firstPortal[gateway.node]),
            goal.areas);
        if (route.exitField[cluster] == kNone) route.exitClusters.push_back(cluster);
        route.exitField[cluster] = made;
        route.exitStamp[cluster] = fields_[made].stamp;
        return made;
    };
    fields_[exitOf(here)].wanted = true;
    // The two clusters after it: their fields are made ahead, so agents stepping over do not wait for them.
    std::uint32_t next = here;
    for (int ahead = 0; ahead < 2; ++ahead) {
        next = graph.gateways[route.gateway[next]].to;
        while (next != goal.cluster && graph.IsLinkCluster(next) && route.settled[next] != 0U && route.gateway[next] != kNone) {
            next = graph.gateways[route.gateway[next]].to;
        }
        if (next == goal.cluster) {
            fields_[goal.field].wanted = true;
            break;
        }
        if (route.settled[next] == 0U || route.gateway[next] == kNone || graph.IsLinkCluster(next)) break;
        fields_[exitOf(next)].wanted = true;
    }
}

void NavMeshHorde::BuildGrid(float cellSize, std::span<const NavMeshBody> bodies) {
    // The agents come first, then the bodies of other kinds (crowd agents, obstacles) that push them.
    const std::size_t agents = records_.size();
    const std::size_t count = agents + bodies.size();
    const auto positionAt = [&](std::size_t index) { return index < agents ? records_[index].position : bodies[index - agents].position; };
    const auto onGrid = [&](std::size_t index) { return index >= agents || records_[index].node != kNone; };
    float minX = std::numeric_limits<float>::max();
    float minZ = minX;
    float maxX = std::numeric_limits<float>::lowest();
    float maxZ = maxX;
    for (std::size_t index = 0U; index < count; ++index) {
        if (!onGrid(index)) continue;
        const Vec3 position = positionAt(index);
        minX = std::min(minX, position.x);
        minZ = std::min(minZ, position.z);
        maxX = std::max(maxX, position.x);
        maxZ = std::max(maxZ, position.z);
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
        if (!onGrid(index)) {
            cellOf_[index] = kNone;
            continue;
        }
        const Vec3 position = positionAt(index);
        const auto cx = std::min(static_cast<std::uint32_t>((position.x - minX) / gridCell_), gridWidth_ - 1U);
        const auto cz = std::min(static_cast<std::uint32_t>((position.z - minZ) / gridCell_), gridHeight_ - 1U);
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
        const Vec3 position = positionAt(index);
        sorted_[at] = static_cast<std::uint32_t>(index);
        sortedX_[at] = position.x;
        sortedY_[at] = position.y;
        sortedZ_[at] = position.z;
        sortedRadius_[at] = index < agents ? radii_[index] : bodies[index - agents].radius;
    }
}

void NavMeshHorde::Step(NavMeshRuntime& runtime, std::span<const NavMeshCrowdAgentInput> agents, std::span<const NavMeshBody> bodies,
    std::span<const Vec3> focuses, const kb::scene::NavigationCrowdSettings& settings, float deltaSeconds, kb::ecs::WorkerPool* workers,
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
    const std::size_t cacheSize = std::max<std::size_t>(settings.hordeFieldCacheSize, 1U);
    // Changed area costs or blocked areas make new fields; the old ones keep guiding till those reach the agents.
    {
        std::uint64_t costs = 1469598103934665603ULL;
        for (std::uint32_t area = 0U; area < kb::scene::kNavAreaCount; ++area) costs = Mix(costs, FloatBits(runtime.AreaCost(static_cast<kb::scene::NavAreaId>(area))));
        NavAreaFilter every;
        runtime.ConfigureFilter(every, kb::scene::kAllNavAreas);
        costSignature_ = Mix(costs, every.Areas());
    }

    // 2. Every agent's goal and the field shared by the agents of that goal; agents join the mesh.
    for (Goal& goal : goals_) goal.used = false;
    wanted_.assign(agents.size(), kNone);
    demand_.assign(agents.size(), 0U);
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
            target.cluster = target.node != kNone ? graph.cluster[target.node] : kNone;
            target.revision = graph.revision;
            target.field = kNone;
            target.route = kNone;
        }
        if (target.node == kNone) continue;
        if (target.field == kNone || !SameField(fields_[target.field], profile, target.node, kNoWay, target.areas, costSignature_)) {
            target.field = FindField(profile, target.node, kNoWay, target.areas);
        }
        if (target.route == kNone || routes_[target.route].goalCluster != target.cluster || routes_[target.route].profile != profile ||
            routes_[target.route].graphRevision != graph.revision) {
            target.route = FindRoute(profile, target.cluster, cacheSize);
        }
        fields_[target.field].lastUsed = step_;
        routes_[target.route].lastUsed = step_;
        wanted_[index] = goal;
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
    for (const NavMeshBody& body : bodies) widest = std::max(widest, body.radius);
    const float buffer = std::max(settings.hordeSeparationDistance, 0.01F);
    BuildGrid(widest * 2.0F + buffer, bodies);

    // 5. Every agent steers and moves, writing only itself.
    const float separationWeight = settings.separationWeight;
    std::atomic<std::size_t> waiting{ 0U };
    std::atomic<std::size_t> asleep{ 0U };
    std::atomic<std::size_t> onLinks{ 0U };
    std::atomic<std::size_t> ticked{ 0U };
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
            const std::uint32_t here = graph.cluster[record.node];
            const NavHordeRoute& route = routes_[goal.route];
            // The guidance of this step: the goal's field in the goal's cluster, else the exit field of
            // the cluster's gateway on the route.
            std::uint32_t next = kNone;
            if (here == goal.cluster) {
                next = goal.field;
            } else {
                const std::uint32_t slot = route.exitField[here];
                if (slot != kNone && fields_[slot].source != kNone && fields_[slot].stamp == route.exitStamp[here] && fields_[slot].costs == costSignature_) next = slot;
            }
            // The agent takes it once it reaches the agent, and keeps the old field (unless its slot went
            // to another field) till then.
            std::uint32_t use = record.field;
            if (use != kNone && (fields_[use].stamp != record.fieldStamp || fields_[use].source == kNone)) use = kNone;
            const bool guided = next != kNone && fields_[next].Reaches(graph, record.node);
            if (guided) use = next;
            if (use != kNone && !fields_[use].Reaches(graph, record.node) && record.linkNode == kNone) use = kNone;
            if (!guided) demand_[index] = 1U;
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
            // What is left of the way: the field's cost, and the route's from the gateway's cluster on.
            float tail = 0.0F;
            if (use != kNone && here != goal.cluster && route.gateway[here] != kNone) tail = route.cost[graph.gateways[route.gateway[here]].to];
            const auto left = [&](std::uint32_t node) {
                return use != kNone && fields_[use].Reaches(graph, node) ? fields_[use].cost[graph.local[node]] + tail : toGoal;
            };
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
                // The guidance has not reached this agent yet: it stands this step. A field or route that
                // grew as far as it can without reaching it leaves it no way to the goal.
                record.velocity = Vec3{};
                output.position = record.position;
                const bool exhausted = next != kNone ? fields_[next].open.empty()
                    : here == goal.cluster ? fields_[goal.field].open.empty() : route.open.empty() && route.settled[here] == 0U;
                output.status = exhausted ? NavPathStatus::Failed : NavPathStatus::Pending;
                record.waiting = !exhausted;
                if (exhausted) demand_[index] = 0U;
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
                output.remainingDistance = left(record.node);
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
                output.remainingDistance = left(record.node);
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
                const std::uint32_t way = field.Way(graph, record.node);
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
                            } else if (field.Way(graph, ahead) != kNone && !graph.IsLink(graph.portals[field.Way(graph, ahead)].node)) {
                                aim = portalPoint(ahead, field.Way(graph, ahead), aim);
                            } else {
                                aim = graph.centre[ahead];
                            }
                        }
                    }
                }
            }
            const std::uint32_t exitGateway = linkAhead != kNone ? route.gateway[graph.cluster[linkAhead]] : kNone;
            if (linkAhead != kNone && HorizontalLength(aim - record.position) <= std::max(graph.linkRadius[linkAhead], input.agent.radius * 2.25F) &&
                exitGateway != kNone) {
                // At the link: cross it toward the end the route continues from.
                record.linkNode = linkAhead;
                record.linkEnd = graph.portals[graph.gateways[exitGateway].portal].edge;
                record.linkLanding = graph.portals[graph.gateways[exitGateway].portal].node;
                record.linkTravelled = 0.0F;
                record.velocity = Vec3{};
                output.position = record.position;
                output.onLink = true;
                output.status = NavPathStatus::Complete;
                output.remainingDistance = left(record.node);
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
            output.remainingDistance = node == goal.node ? HorizontalLength(goal.destination - from) : left(node);
        }
        // The block's share of the step's figures.
        std::size_t blockWaiting = 0U;
        std::size_t blockAsleep = 0U;
        std::size_t blockOnLinks = 0U;
        std::size_t blockTicked = 0U;
        for (std::size_t index = begin; index < end; ++index) {
            const AgentRecord& record = records_[index];
            if (!agents[index].agent.enabled || record.node == kNone) continue;
            blockWaiting += record.waiting ? 1U : 0U;
            blockAsleep += record.asleep ? 1U : 0U;
            blockOnLinks += record.linkNode != kNone ? 1U : 0U;
            blockTicked += record.idle == 0U && !record.asleep && !record.waiting ? 1U : 0U;
        }
        waiting.fetch_add(blockWaiting, std::memory_order_relaxed);
        asleep.fetch_add(blockAsleep, std::memory_order_relaxed);
        onLinks.fetch_add(blockOnLinks, std::memory_order_relaxed);
        ticked.fetch_add(blockTicked, std::memory_order_relaxed);
    });
    waiting_ = waiting.load(std::memory_order_relaxed);
    asleep_ = asleep.load(std::memory_order_relaxed);
    onLinks_ = onLinks.load(std::memory_order_relaxed);
    ticked_ = ticked.load(std::memory_order_relaxed);
    // 6. Agents waiting for guidance tell the fields and routes (in agent order, so any number of workers gives the same result).
    for (std::size_t index = 0U; index < agents.size(); ++index) {
        if (demand_[index] != 0U) RegisterDemand(wanted_[index], records_[index].node);
    }
    if (step_ % kExitRefresh == 0U) {
        // The exit fields of routes in use count as used, so they stay while the route does.
        for (const NavHordeRoute& route : routes_) {
            if (route.goalCluster == kNone || route.lastUsed + kExitRefresh < step_) continue;
            for (const std::uint32_t cluster : route.exitClusters) {
                const std::uint32_t slot = route.exitField[cluster];
                if (slot != kNone && fields_[slot].source != kNone && fields_[slot].stamp == route.exitStamp[cluster]) fields_[slot].lastUsed = step_;
            }
        }
        SweepFields(cacheSize);
    }
    milliseconds_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
}

} // namespace kb::navigation
