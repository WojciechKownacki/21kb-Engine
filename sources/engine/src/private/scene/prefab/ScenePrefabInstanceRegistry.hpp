#pragma once

#include "engine/scene/ScenePrefabHandle.hpp"
#include "engine/scene/ScenePrefabInstanceHandle.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/ScenePrefab.hpp"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace kb::scene {

struct ScenePrefabInstanceRecord {
    ScenePrefabHandle prefab;
    SceneObject rootParent{};
    std::vector<SceneObject> objects;
    ScenePrefab resolvedPrefab;
};

class ScenePrefabInstanceRegistry {
public:
    [[nodiscard]] ScenePrefabInstanceHandle Register(ScenePrefabHandle prefab, SceneObject rootParent, std::vector<SceneObject> objects, ScenePrefab resolvedPrefab);
    [[nodiscard]] bool Contains(ScenePrefabInstanceHandle handle) const noexcept;
    [[nodiscard]] const ScenePrefabInstanceRecord* Find(ScenePrefabInstanceHandle handle) const noexcept;
    [[nodiscard]] ScenePrefabInstanceRecord* FindMutable(ScenePrefabInstanceHandle handle) noexcept;
    [[nodiscard]] ScenePrefabInstanceHandle FindRootInstance(SceneObject object) const noexcept;
    [[nodiscard]] ScenePrefabInstanceHandle FindContainingInstance(SceneObject object, std::uint32_t& nodeIndex) const noexcept;
    [[nodiscard]] std::size_t Count() const noexcept;
    void Clear() noexcept;

private:
    std::uint64_t nextId_ = 1;
    std::unordered_map<std::uint64_t, ScenePrefabInstanceRecord> records_;
};

} // namespace kb::scene
