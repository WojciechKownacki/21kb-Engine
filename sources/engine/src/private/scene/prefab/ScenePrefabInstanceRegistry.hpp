#pragma once

#include "engine/scene/ScenePrefabHandle.hpp"
#include "engine/scene/ScenePrefabInstanceHandle.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/ScenePrefab.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
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
    [[nodiscard]] std::vector<ScenePrefabInstanceHandle> Handles() const;
    [[nodiscard]] std::vector<ScenePrefabInstanceHandle> HandlesForPrefab(ScenePrefabHandle prefab) const;
    [[nodiscard]] std::size_t Count() const noexcept;
    void ReindexObjects(ScenePrefabInstanceHandle handle, std::span<const SceneObject> oldObjects) noexcept;
    [[nodiscard]] bool Remove(ScenePrefabInstanceHandle handle) noexcept;
    void Clear() noexcept;

private:
    struct ObjectIndexEntry {
        ScenePrefabInstanceHandle instance;
        std::uint32_t nodeIndex = 0;
    };

    void IndexRecord(ScenePrefabInstanceHandle handle, const ScenePrefabInstanceRecord& record);
    void IndexObjects(ScenePrefabInstanceHandle handle, std::span<const SceneObject> objects);
    void UnindexObjects(ScenePrefabInstanceHandle handle, std::span<const SceneObject> objects) noexcept;
    void RemoveFromPrefabIndex(ScenePrefabHandle prefab, ScenePrefabInstanceHandle handle) noexcept;

    std::uint64_t nextId_ = 1;
    std::unordered_map<std::uint64_t, ScenePrefabInstanceRecord> records_;
    std::map<ScenePrefabHandle, std::vector<ScenePrefabInstanceHandle>> prefabIndex_;
    std::unordered_map<SceneEntity::IdType, ScenePrefabInstanceHandle> rootIndex_;
    std::unordered_map<SceneEntity::IdType, ObjectIndexEntry> objectIndex_;
};

} // namespace kb::scene
