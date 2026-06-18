#include "scene/prefab/ScenePrefabRegisteredInstantiationService.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
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

    ScenePrefab resolvedPrefab = ScenePrefabNestedResolver::Resolve(state.prefabs, record->prefab);
    const std::uint64_t registryResolveNanoseconds = ElapsedNanoseconds(registryStart, PrefabStatsClock::now());
    std::vector<ScenePrefabInstance> instances = ScenePrefabInstantiationService::InstantiateMany(scene, resolvedPrefab, count, settings);

    const auto historyStart = PrefabStatsClock::now();
    const std::vector<ScenePrefabInstanceHandle> handles = state.prefabInstances.RegisterManyInstances(
        handle,
        record->guid,
        settings.parent,
        std::span<const ScenePrefabInstance>{ instances },
        resolvedPrefab,
        true);
    const std::uint64_t historyRecordNanoseconds = ElapsedNanoseconds(historyStart, PrefabStatsClock::now());
    if (handles.size() != instances.size()) {
        state.lastPrefabInstantiationStats.registryResolveNanoseconds = registryResolveNanoseconds;
        state.lastPrefabInstantiationStats.historyRecordNanoseconds = historyRecordNanoseconds;
        return {};
    }
    state.lastPrefabInstantiationStats.registeredInstanceCount = handles.size();
    state.lastPrefabInstantiationStats.registryResolveNanoseconds = registryResolveNanoseconds;
    state.lastPrefabInstantiationStats.historyRecordNanoseconds = historyRecordNanoseconds;

    for (std::size_t index = 0; index < instances.size(); ++index) {
        instances[index].AssignHandle(handles[index]);
    }
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

    ScenePrefab resolvedPrefab = ScenePrefabNestedResolver::Resolve(state.prefabs, record->prefab);
    const std::uint64_t registryResolveNanoseconds = ElapsedNanoseconds(registryStart, PrefabStatsClock::now());
    ScenePrefabInstantiationStats stats = ScenePrefabInstantiationService::InstantiateBatch(scene, resolvedPrefab, count, settings);
    stats.registryResolveNanoseconds = registryResolveNanoseconds;
    state.lastPrefabInstantiationStats.registryResolveNanoseconds = registryResolveNanoseconds;
    return stats;
}

} // namespace kb::scene
