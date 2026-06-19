#include "scene/prefab/ScenePrefabRegisteredInstantiationService.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabBulkInstantiationService.hpp"
#include "scene/prefab/ScenePrefabInstantiationService.hpp"
#include "scene/prefab/ScenePrefabNestedResolver.hpp"
#include "scene/prefab/ScenePrefabRecord.hpp"

#include <chrono>
#include <cstdint>
#include <utility>
#include <span>
#include <vector>

namespace kb::scene {
namespace {

using PrefabStatsClock = std::chrono::steady_clock;

[[nodiscard]] std::uint64_t ElapsedNanoseconds(PrefabStatsClock::time_point start, PrefabStatsClock::time_point end) noexcept {
    const std::uint64_t nanoseconds = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
    return nanoseconds == 0U ? 1U : nanoseconds;
}

[[nodiscard]] bool HasNestedPrefabNodes(const ScenePrefab& prefab) noexcept {
    for (const ScenePrefabNodeDesc& node : prefab.Nodes()) {
        if (!node.nestedPrefabGuid.empty()) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool HasValidBakedCache(const ScenePrefabRecord& record) noexcept {
    return record.bakedContentHash != 0U &&
        record.bakedContentHash == record.contentHash &&
        record.bakedPrefab.NodeCount() == record.prefab.NodeCount() &&
        !HasNestedPrefabNodes(record.prefab);
}

} // namespace

ScenePrefabInstance ScenePrefabRegisteredInstantiationService::Instantiate(Scene& scene, ScenePrefabHandle handle, const ScenePrefabInstantiationSettings& settings) {
    std::vector<ScenePrefabInstance> instances = InstantiateMany(scene, handle, 1, settings);
    if (instances.empty()) {
        return {};
    }
    return std::move(instances.front());
}

std::vector<ScenePrefabInstance> ScenePrefabRegisteredInstantiationService::InstantiateMany(Scene& scene, ScenePrefabHandle handle, std::size_t count, const ScenePrefabInstantiationSettings& settings) {
    SceneState& state = SceneAccess::State(scene);
    state.lastPrefabInstantiationStats = ScenePrefabInstantiationStats{
        .requestedInstances = count,
    };
    const auto registryStart = PrefabStatsClock::now();
    const ScenePrefabRecord* record = state.prefabs.FindRecord(handle);
    if (record == nullptr || count == 0) {
        state.lastPrefabInstantiationStats.registryResolveNanoseconds = ElapsedNanoseconds(registryStart, PrefabStatsClock::now());
        return {};
    }

    const bool useBakedCache = HasValidBakedCache(*record);
    ScenePrefab resolvedPrefab;
    const ScenePrefab* spawnPrefab = &record->prefab;
    if (!useBakedCache) {
        resolvedPrefab = ScenePrefabNestedResolver::Resolve(state.prefabs, record->prefab);
        spawnPrefab = &resolvedPrefab;
    }
    const std::uint64_t registryResolveNanoseconds = ElapsedNanoseconds(registryStart, PrefabStatsClock::now());
    std::vector<ScenePrefabInstance> instances = useBakedCache
        ? ScenePrefabBulkInstantiationService::InstantiateBaked(scene, *spawnPrefab, record->bakedPrefab, count, settings)
        : ScenePrefabInstantiationService::InstantiateMany(scene, *spawnPrefab, count, settings);
    const ScenePrefabInstantiationStats spawnStats = state.lastPrefabInstantiationStats;

    const auto historyStart = PrefabStatsClock::now();
    const std::size_t registeredCount = spawnStats.hasGeneratedEntityIndexRange
        ? state.prefabInstances.RegisterManyCreatedDenseInstancesInPlace(
            handle,
            record->guid,
            settings.parent,
            std::span<ScenePrefabInstance>{ instances },
            *spawnPrefab,
            spawnStats.maxGeneratedEntityIndex,
            true,
            spawnStats.hasContiguousGeneratedEntityRuns,
            true)
        : state.prefabInstances.RegisterManyInstancesInPlace(
            handle,
            record->guid,
            settings.parent,
            std::span<ScenePrefabInstance>{ instances },
            *spawnPrefab,
            true);
    const std::uint64_t historyRecordNanoseconds = ElapsedNanoseconds(historyStart, PrefabStatsClock::now());
    if (registeredCount != instances.size()) {
        state.lastPrefabInstantiationStats.registryResolveNanoseconds = registryResolveNanoseconds;
        state.lastPrefabInstantiationStats.historyRecordNanoseconds = historyRecordNanoseconds;
        return {};
    }
    state.lastPrefabInstantiationStats.registeredInstanceCount = registeredCount;
    state.lastPrefabInstantiationStats.registryResolveNanoseconds = registryResolveNanoseconds;
    state.lastPrefabInstantiationStats.historyRecordNanoseconds = historyRecordNanoseconds;
    return instances;
}

ScenePrefabInstantiationStats ScenePrefabRegisteredInstantiationService::InstantiateBatch(Scene& scene, ScenePrefabHandle handle, std::size_t count, const ScenePrefabInstantiationSettings& settings) {
    SceneState& state = SceneAccess::State(scene);
    state.lastPrefabInstantiationStats = ScenePrefabInstantiationStats{
        .requestedInstances = count,
    };
    const auto registryStart = PrefabStatsClock::now();
    const ScenePrefabRecord* record = state.prefabs.FindRecord(handle);
    if (record == nullptr || count == 0) {
        state.lastPrefabInstantiationStats.registryResolveNanoseconds = ElapsedNanoseconds(registryStart, PrefabStatsClock::now());
        return state.lastPrefabInstantiationStats;
    }

    const bool useBakedCache = HasValidBakedCache(*record);
    ScenePrefab resolvedPrefab;
    const ScenePrefab* spawnPrefab = &record->prefab;
    if (!useBakedCache) {
        resolvedPrefab = ScenePrefabNestedResolver::Resolve(state.prefabs, record->prefab);
        spawnPrefab = &resolvedPrefab;
    }
    const std::uint64_t registryResolveNanoseconds = ElapsedNanoseconds(registryStart, PrefabStatsClock::now());
    ScenePrefabInstantiationStats stats = useBakedCache
        ? ScenePrefabBulkInstantiationService::InstantiateBatchBaked(scene, *spawnPrefab, record->bakedPrefab, count, settings)
        : ScenePrefabInstantiationService::InstantiateBatch(scene, *spawnPrefab, count, settings);
    stats.registryResolveNanoseconds = registryResolveNanoseconds;
    state.lastPrefabInstantiationStats.registryResolveNanoseconds = registryResolveNanoseconds;
    return stats;
}

} // namespace kb::scene
