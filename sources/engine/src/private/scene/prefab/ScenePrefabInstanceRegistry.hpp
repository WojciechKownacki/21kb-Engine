#pragma once

#include "engine/scene/ScenePrefabHandle.hpp"
#include "engine/scene/ScenePrefabInstanceHandle.hpp"
#include "engine/scene/SceneObject.hpp"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace kb::scene {

struct ScenePrefabInstanceRecord {
    ScenePrefabHandle prefab;
    SceneObject rootParent{};
    std::vector<SceneObject> objects;
};

class ScenePrefabInstanceRegistry {
public:
    [[nodiscard]] ScenePrefabInstanceHandle Register(ScenePrefabHandle prefab, SceneObject rootParent, std::vector<SceneObject> objects);
    [[nodiscard]] bool Contains(ScenePrefabInstanceHandle handle) const noexcept;
    [[nodiscard]] const ScenePrefabInstanceRecord* Find(ScenePrefabInstanceHandle handle) const noexcept;
    [[nodiscard]] ScenePrefabInstanceRecord* FindMutable(ScenePrefabInstanceHandle handle) noexcept;
    [[nodiscard]] std::size_t Count() const noexcept;
    void Clear() noexcept;

private:
    std::uint64_t nextId_ = 1;
    std::unordered_map<std::uint64_t, ScenePrefabInstanceRecord> records_;
};

} // namespace kb::scene
