#pragma once

#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/ScenePrefabHandle.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace kb::scene {

struct ScenePrefabRecord {
    std::string name;
    ScenePrefab prefab;
    std::uint64_t contentHash = 0;
};

class ScenePrefabRegistry {
public:
    [[nodiscard]] ScenePrefabHandle Register(std::string name, ScenePrefab prefab);
    [[nodiscard]] bool Contains(ScenePrefabHandle handle) const noexcept;
    [[nodiscard]] const ScenePrefabRecord* FindRecord(ScenePrefabHandle handle) const noexcept;
    [[nodiscard]] const ScenePrefab* Find(ScenePrefabHandle handle) const noexcept;
    [[nodiscard]] ScenePrefab* FindMutable(ScenePrefabHandle handle) noexcept;
    void RefreshContentHash(ScenePrefabHandle handle) noexcept;
    [[nodiscard]] std::size_t Count() const noexcept;
    void Clear() noexcept;

private:
    std::uint64_t nextId_ = 1;
    std::unordered_map<std::uint64_t, ScenePrefabRecord> records_;
};

} // namespace kb::scene
