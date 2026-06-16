#pragma once

#include "engine/scene/ScenePrefabHandle.hpp"
#include "scene/prefab/ScenePrefabRecord.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
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
    [[nodiscard]] bool Remove(ScenePrefabHandle handle) noexcept;
    void Clear() noexcept;

private:
    void IndexRecord(ScenePrefabHandle handle, const ScenePrefabRecord& record);
    void UnindexRecord(ScenePrefabHandle handle, const ScenePrefabRecord& record) noexcept;

    std::uint64_t nextId_ = 1;
    std::unordered_map<std::uint64_t, ScenePrefabRecord> records_;
    std::unordered_map<std::string, ScenePrefabHandle> guidIndex_;
    std::unordered_map<std::uint64_t, std::vector<ScenePrefabHandle>> variantChildrenIndex_;
};

} // namespace kb::scene
