#include "scene/SceneMeshVisibilityIndexBuilder.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

namespace kb::render {
namespace {

using Index = SceneMeshVisibilityIndex;
using Entry = Index::Entry;
using Cluster = Index::Cluster;
constexpr std::uint32_t kCapacity = Index::kClusterCapacity;

// Clusters are made with this many members, which leaves room for instances that join them later.
constexpr std::size_t kBuildFill = 12U;

// A model smaller than this, or a bounds override of a smaller radius, may have bounds that vanish in the arithmetic of a
// scan: the cluster that holds it is never ruled out.
constexpr float kMinimumScale = 1.0e-12F;
constexpr float kMinimumOverrideRadius = 1.0e-6F;

// Instances are indexed again once their clusters have grown to this multiple of the size they were made with.
constexpr float kDriftLimit = 2.0F;
constexpr float kDriftRootShare = 0.01F;

[[nodiscard]] Entry MakeEntry(const SceneRenderMeshInstance& instance, std::uint32_t position) noexcept {
    const std::array<float, 16>& m = instance.model;
    Entry entry;
    entry.origin = { m[12], m[13], m[14] };
    entry.scale = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2] + m[4] * m[4] + m[5] * m[5] + m[6] * m[6] +
        m[8] * m[8] + m[9] * m[9] + m[10] * m[10]);
    entry.instance = position;
    entry.layer = instance.layer;
    entry.kind = static_cast<std::uint8_t>((instance.castsShadow ? 1U : 0U) | (instance.shadowOnly ? 2U : 0U));
    bool unbounded = !(entry.scale >= kMinimumScale);
    if (instance.boundsOverride.IsValid()) {
        const std::array<float, 3>& c = instance.boundsOverride.center;
        entry.overrideReach = entry.scale * (std::sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]) + instance.boundsOverride.radius);
        unbounded = unbounded || !(instance.boundsOverride.radius >= kMinimumOverrideRadius);
    }
    entry.unbounded = unbounded || !std::isfinite(entry.origin[0] + entry.origin[1] + entry.origin[2] + entry.scale + entry.overrideReach);
    return entry;
}

// The bounds of `cluster` from its members, which are the first `cluster.count` of `entries`.
void Recompute(Cluster& cluster, const Entry* entries) noexcept {
    constexpr float kInfinity = std::numeric_limits<float>::infinity();
    std::array<float, 3> low{ kInfinity, kInfinity, kInfinity };
    std::array<float, 3> high{ -kInfinity, -kInfinity, -kInfinity };
    cluster.maximumScale = 0.0F;
    cluster.overrideReach = 0.0F;
    cluster.layersAny = 0U;
    cluster.layersAll = 0xFFFFFFFFU;
    cluster.kindCount = {};
    cluster.unbounded = false;
    for (std::uint32_t member = 0U; member < cluster.count; ++member) {
        const Entry& entry = entries[member];
        cluster.layersAny |= entry.layer;
        cluster.layersAll &= entry.layer;
        ++cluster.kindCount[entry.kind];
        if (entry.unbounded) {
            cluster.unbounded = true;
            continue;
        }
        for (std::size_t axis = 0U; axis < 3U; ++axis) {
            low[axis] = std::min(low[axis], entry.origin[axis]);
            high[axis] = std::max(high[axis], entry.origin[axis]);
        }
        cluster.maximumScale = std::max(cluster.maximumScale, entry.scale);
        cluster.overrideReach = std::max(cluster.overrideReach, entry.overrideReach);
    }
    cluster.size = 0.0F;
    for (std::size_t axis = 0U; axis < 3U; ++axis) {
        const bool any = low[axis] <= high[axis];
        cluster.center[axis] = any ? (low[axis] + high[axis]) * 0.5F : 0.0F;
        cluster.halfExtent[axis] = any ? (high[axis] - low[axis]) * 0.5F : 0.0F;
        cluster.size += 2.0F * cluster.halfExtent[axis];
    }
    cluster.stale = 0U;
}

[[nodiscard]] SceneMeshVisibilityBounds Aggregate(const std::vector<Cluster>& clusters) noexcept {
    constexpr float kInfinity = std::numeric_limits<float>::infinity();
    SceneMeshVisibilityBounds root;
    std::array<float, 3> low{ kInfinity, kInfinity, kInfinity };
    std::array<float, 3> high{ -kInfinity, -kInfinity, -kInfinity };
    for (const Cluster& cluster : clusters) {
        root.count += cluster.count;
        root.layersAny |= cluster.layersAny;
        root.layersAll &= cluster.layersAll;
        for (std::size_t kind = 0U; kind < root.kindCount.size(); ++kind) root.kindCount[kind] += cluster.kindCount[kind];
        root.unbounded = root.unbounded || cluster.unbounded;
        root.maximumScale = std::max(root.maximumScale, cluster.maximumScale);
        root.overrideReach = std::max(root.overrideReach, cluster.overrideReach);
        for (std::size_t axis = 0U; axis < 3U; ++axis) {
            low[axis] = std::min(low[axis], cluster.center[axis] - cluster.halfExtent[axis]);
            high[axis] = std::max(high[axis], cluster.center[axis] + cluster.halfExtent[axis]);
        }
    }
    for (std::size_t axis = 0U; axis < 3U; ++axis) {
        const bool any = low[axis] <= high[axis];
        root.center[axis] = any ? (low[axis] + high[axis]) * 0.5F : 0.0F;
        root.halfExtent[axis] = any ? (high[axis] - low[axis]) * 0.5F : 0.0F;
    }
    return root;
}

[[nodiscard]] float SplitKey(const Entry& entry, std::size_t axis) noexcept {
    const float value = entry.origin[axis];
    return value == value ? value : 0.0F;
}

// Divides the entries `order[first, last)` into `clusterCount` clusters of nearby ones, by cutting along the longest side of
// their box in two parts that hold their share of the clusters.
void Split(const std::vector<Entry>& all, std::vector<std::uint32_t>& order, std::size_t first, std::size_t last,
    std::size_t clusterCount, Index& index) {
    const std::size_t size = last - first;
    clusterCount = std::min(clusterCount, size);
    if (clusterCount <= 1U && size <= kCapacity) {
        const std::size_t cluster = index.clusters.size();
        index.clusters.emplace_back();
        index.entries.resize(index.entries.size() + kCapacity);
        Entry* destination = index.entries.data() + cluster * kCapacity;
        for (std::size_t member = 0U; member < size; ++member) {
            const Entry& entry = all[order[first + member]];
            destination[member] = entry;
            index.slots[entry.instance] = static_cast<std::uint32_t>(cluster * kCapacity + member);
        }
        index.clusters[cluster].count = static_cast<std::uint32_t>(size);
        return;
    }
    clusterCount = std::max<std::size_t>(clusterCount, 2U);
    constexpr float kInfinity = std::numeric_limits<float>::infinity();
    std::array<float, 3> low{ kInfinity, kInfinity, kInfinity };
    std::array<float, 3> high{ -kInfinity, -kInfinity, -kInfinity };
    for (std::size_t position = first; position < last; ++position) {
        const std::array<float, 3>& origin = all[order[position]].origin;
        for (std::size_t axis = 0U; axis < 3U; ++axis) {
            if (std::isfinite(origin[axis])) {
                low[axis] = std::min(low[axis], origin[axis]);
                high[axis] = std::max(high[axis], origin[axis]);
            }
        }
    }
    std::size_t axis = 0U;
    float longest = -kInfinity;
    for (std::size_t candidate = 0U; candidate < 3U; ++candidate) {
        const float extent = high[candidate] - low[candidate];
        if (extent > longest) {
            longest = extent;
            axis = candidate;
        }
    }
    const std::size_t leftClusters = clusterCount / 2U;
    const std::size_t middle = first + size * leftClusters / clusterCount;
    std::nth_element(order.begin() + static_cast<std::ptrdiff_t>(first), order.begin() + static_cast<std::ptrdiff_t>(middle),
        order.begin() + static_cast<std::ptrdiff_t>(last), [&all, axis](std::uint32_t left, std::uint32_t right) {
            return SplitKey(all[left], axis) < SplitKey(all[right], axis);
        });
    Split(all, order, first, middle, leftClusters, index);
    Split(all, order, middle, last, clusterCount - leftClusters, index);
}

void RemoveCluster(Index& index, std::size_t cluster) noexcept {
    const std::size_t last = index.clusters.size() - 1U;
    if (cluster != last) {
        const std::uint32_t count = index.clusters[last].count;
        for (std::uint32_t member = 0U; member < count; ++member) {
            const std::size_t slot = cluster * kCapacity + member;
            index.entries[slot] = index.entries[last * kCapacity + member];
            index.slots[index.entries[slot].instance] = static_cast<std::uint32_t>(slot);
        }
        index.clusters[cluster] = index.clusters[last];
    }
    index.clusters.pop_back();
    index.entries.resize(index.clusters.size() * kCapacity);
}

} // namespace

void SceneMeshVisibilityIndexBuilder::Build(std::span<const SceneRenderMeshInstance> instances, SceneMeshVisibilityIndex& index) {
    index.root = {};
    index.clusters.clear();
    index.entries.clear();
    index.slots.clear();
    index.builtSize = 0.0F;
    index.currentSize = 0.0F;
    index.stale = 0U;
    if (instances.size() < kMinimumInstances) {
        return;
    }
    std::vector<Entry> all(instances.size());
    for (std::size_t position = 0U; position < instances.size(); ++position) {
        all[position] = MakeEntry(instances[position], static_cast<std::uint32_t>(position));
    }
    std::vector<std::uint32_t> order(instances.size());
    std::iota(order.begin(), order.end(), 0U);
    index.slots.resize(instances.size());
    const std::size_t clusterCount = (instances.size() + kBuildFill - 1U) / kBuildFill;
    index.clusters.reserve(clusterCount);
    index.entries.reserve(clusterCount * kCapacity);
    Split(all, order, 0U, instances.size(), clusterCount, index);
    for (std::size_t cluster = 0U; cluster < index.clusters.size(); ++cluster) {
        Recompute(index.clusters[cluster], index.entries.data() + cluster * kCapacity);
        index.currentSize += index.clusters[cluster].size;
    }
    index.root = Aggregate(index.clusters);
    index.builtSize = index.currentSize;
}

void SceneMeshVisibilityIndexBuilder::Append(std::span<const SceneRenderMeshInstance> instances, SceneMeshVisibilityIndex& index) {
    if (index.Empty() || index.slots.size() + 1U != instances.size()) {
        Build(instances, index);
        return;
    }
    const std::uint32_t position = static_cast<std::uint32_t>(instances.size() - 1U);
    const Entry entry = MakeEntry(instances[position], position);
    // The cluster with room that the new origin makes grow the least.
    std::size_t best = index.clusters.size();
    float bestCost = std::numeric_limits<float>::infinity();
    for (std::size_t cluster = 0U; cluster < index.clusters.size(); ++cluster) {
        const Cluster& candidate = index.clusters[cluster];
        if (candidate.count >= kCapacity) {
            continue;
        }
        float cost = 0.0F;
        if (!entry.unbounded) {
            for (std::size_t axis = 0U; axis < 3U; ++axis) {
                const float low = candidate.center[axis] - candidate.halfExtent[axis];
                const float high = candidate.center[axis] + candidate.halfExtent[axis];
                cost += std::max(high, entry.origin[axis]) - std::min(low, entry.origin[axis]) - (high - low);
            }
        }
        if (best == index.clusters.size() || cost < bestCost ||
            (cost == bestCost && candidate.count < index.clusters[best].count)) {
            best = cluster;
            bestCost = cost;
        }
    }
    if (best == index.clusters.size()) {
        index.clusters.emplace_back();
        index.entries.resize(index.entries.size() + kCapacity);
    }
    Cluster& cluster = index.clusters[best];
    const std::size_t slot = best * kCapacity + cluster.count;
    index.entries[slot] = entry;
    ++cluster.count;
    cluster.stale = 1U;
    index.slots.push_back(static_cast<std::uint32_t>(slot));
    index.stale = 1U;
}

void SceneMeshVisibilityIndexBuilder::Remove(SceneMeshVisibilityIndex& index, std::uint32_t position) noexcept {
    if (index.Empty() || position >= index.slots.size()) {
        return;
    }
    const std::uint32_t last = static_cast<std::uint32_t>(index.slots.size() - 1U);
    const std::size_t slot = index.slots[position];
    const std::size_t clusterIndex = slot / kCapacity;
    Cluster& cluster = index.clusters[clusterIndex];
    const std::size_t lastSlot = clusterIndex * kCapacity + cluster.count - 1U;
    if (slot != lastSlot) {
        index.entries[slot] = index.entries[lastSlot];
        index.slots[index.entries[slot].instance] = static_cast<std::uint32_t>(slot);
    }
    --cluster.count;
    if (cluster.count == 0U) {
        RemoveCluster(index, clusterIndex);
    } else {
        cluster.stale = 1U;
    }
    // The last instance takes the place of the removed one.
    if (position != last) {
        index.slots[position] = index.slots[last];
        index.entries[index.slots[position]].instance = position;
    }
    index.slots.pop_back();
    index.stale = 1U;
    if (index.slots.size() < kMinimumInstances / 2U) {
        index = {};
    }
}

void SceneMeshVisibilityIndexBuilder::Update(SceneMeshVisibilityIndex& index, const SceneRenderMeshInstance& instance,
    std::uint32_t position) noexcept {
    if (position >= index.slots.size()) {
        return;
    }
    const std::uint32_t slot = index.slots[position];
    Entry& entry = index.entries[slot];
    const Entry previous = entry;
    entry = MakeEntry(instance, position);
    // An instance that stays within the bounds of its cluster, and changes nothing else they say, leaves them as they are.
    Cluster& cluster = index.clusters[slot / kCapacity];
    const bool withinBounds = !entry.unbounded && !previous.unbounded && entry.kind == previous.kind && entry.layer == previous.layer &&
        entry.scale <= cluster.maximumScale && entry.overrideReach <= cluster.overrideReach &&
        std::abs(entry.origin[0] - cluster.center[0]) <= cluster.halfExtent[0] &&
        std::abs(entry.origin[1] - cluster.center[1]) <= cluster.halfExtent[1] &&
        std::abs(entry.origin[2] - cluster.center[2]) <= cluster.halfExtent[2];
    if (withinBounds) {
        return;
    }
    std::atomic_ref<std::uint8_t>(cluster.stale).store(1U, std::memory_order_relaxed);
    std::atomic_ref<std::uint8_t>(index.stale).store(1U, std::memory_order_relaxed);
}

void SceneMeshVisibilityIndexBuilder::Refresh(std::span<const SceneRenderMeshInstance> instances, SceneMeshVisibilityIndex& index) {
    if (index.stale == 0U) {
        return;
    }
    index.stale = 0U;
    if (!index.Empty() && index.slots.size() != instances.size()) {
        Build(instances, index);
        return;
    }
    float total = 0.0F;
    for (std::size_t cluster = 0U; cluster < index.clusters.size(); ++cluster) {
        Cluster& stale = index.clusters[cluster];
        if (stale.stale != 0U) {
            Recompute(stale, index.entries.data() + cluster * kCapacity);
        }
        total += stale.size;
    }
    index.root = Aggregate(index.clusters);
    index.currentSize = total;
    const float rootSize = 2.0F * (index.root.halfExtent[0] + index.root.halfExtent[1] + index.root.halfExtent[2]);
    if (total > index.builtSize * kDriftLimit + rootSize * kDriftRootShare) {
        Build(instances, index);
    }
}

} // namespace kb::render
