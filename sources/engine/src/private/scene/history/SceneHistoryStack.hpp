#pragma once

#include "engine/scene/SceneAudioOcclusionAccess.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneHistory.hpp"
#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/ScenePrefabHandle.hpp"
#include "engine/scene/ScenePrefabInstanceHandle.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace kb::scene {

using SceneHistoryObjectPath = std::vector<std::uint32_t>;

struct SceneHistoryPrefabInstanceSnapshot {
    ScenePrefabInstanceHandle handle;
    ScenePrefabHandle prefab;
    std::string prefabGuid;
    SceneHistoryObjectPath rootParentPath;
    std::vector<SceneHistoryObjectPath> objectPaths;
    std::vector<std::uint64_t> nodeIds;
    ScenePrefab resolvedPrefab;
};

struct SceneHistoryEntry {
    std::string label;
    std::uint64_t audioMixerAssetId = 0U;
    std::string audioMixerSnapshot;
    AudioOcclusionSettings audioOcclusionSettings;
    std::vector<ScenePrefab> roots;
    std::vector<SceneHistoryPrefabInstanceSnapshot> prefabInstances;
    // The entity each captured node came from, in capture order across the roots.
    std::vector<SceneEntity> entities;
};

class SceneHistoryStack {
public:
    [[nodiscard]] bool Empty() const noexcept;
    [[nodiscard]] std::size_t Size() const noexcept;
    void Push(SceneHistoryEntry entry);
    [[nodiscard]] SceneHistoryEntry Pop();
    void Clear() noexcept;
    // `remap` is sorted by the id of the entity it replaces.
    void RemapEntities(std::span<const SceneEntityRemap> remap) noexcept;

private:
    std::vector<SceneHistoryEntry> entries_;
};

} // namespace kb::scene
