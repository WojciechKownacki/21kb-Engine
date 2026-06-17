#pragma once

#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/ScenePrefabHandle.hpp"
#include "engine/scene/ScenePrefabInstanceHandle.hpp"
#include "engine/scene/SceneObject.hpp"

#include <string>
#include <vector>

namespace kb::scene {

struct SceneHistoryPrefabInstanceSnapshot {
    ScenePrefabInstanceHandle handle;
    ScenePrefabHandle prefab;
    std::string prefabGuid;
    SceneObject rootParent{};
    ScenePrefab resolvedPrefab;
    ScenePrefab currentState;
};

struct SceneHistoryEntry {
    std::string label;
    std::vector<ScenePrefab> roots;
    std::vector<SceneHistoryPrefabInstanceSnapshot> prefabInstances;
};

class SceneHistoryStack {
public:
    [[nodiscard]] bool Empty() const noexcept;
    [[nodiscard]] std::size_t Size() const noexcept;
    void Push(SceneHistoryEntry entry);
    [[nodiscard]] SceneHistoryEntry Pop();
    void Clear() noexcept;

private:
    std::vector<SceneHistoryEntry> entries_;
};

} // namespace kb::scene
