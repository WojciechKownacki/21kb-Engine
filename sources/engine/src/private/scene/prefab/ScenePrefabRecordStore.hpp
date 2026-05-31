#pragma once

#include "engine/scene/ScenePrefabHandle.hpp"
#include "scene/prefab/ScenePrefabRecord.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace kb::scene {

class ScenePrefabRecordStore {
public:
    [[nodiscard]] std::uint64_t NextId() const noexcept;
    [[nodiscard]] ScenePrefabHandle Insert(ScenePrefabRecord record);
    [[nodiscard]] bool Contains(ScenePrefabHandle handle) const noexcept;
    [[nodiscard]] const ScenePrefabRecord* Find(ScenePrefabHandle handle) const noexcept;
    [[nodiscard]] ScenePrefabRecord* FindMutable(ScenePrefabHandle handle) noexcept;
    [[nodiscard]] ScenePrefabHandle FindByGuid(std::string_view guid) const noexcept;
    [[nodiscard]] std::vector<ScenePrefabHandle> VariantChildrenOf(ScenePrefabHandle baseHandle) const;
    [[nodiscard]] std::size_t Count() const noexcept;
    void Clear() noexcept;

private:
    std::uint64_t nextId_ = 1;
    std::unordered_map<std::uint64_t, ScenePrefabRecord> records_;
};

} // namespace kb::scene
