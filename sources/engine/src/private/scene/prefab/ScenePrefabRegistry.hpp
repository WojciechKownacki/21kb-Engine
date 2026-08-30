#pragma once

#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/ScenePrefabHandle.hpp"
#include "engine/scene/ScenePrefabOverrides.hpp"
#include "scene/prefab/ScenePrefabRecord.hpp"
#include "scene/prefab/ScenePrefabRecordStore.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace kb::scene {

class ScenePrefabRegistry {
public:
    [[nodiscard]] ScenePrefabHandle Register(std::string name, ScenePrefab prefab);
    [[nodiscard]] ScenePrefabHandle RegisterLoaded(std::string guid, std::string name, ScenePrefab prefab, std::string sourcePath);
    [[nodiscard]] ScenePrefabHandle RegisterVariant(std::string name, ScenePrefabHandle basePrefab, std::vector<ScenePrefabPropertyOverride> overrides);
    [[nodiscard]] ScenePrefabHandle RegisterLoadedVariant(std::string guid, std::string name, std::string basePrefabGuid, std::vector<ScenePrefabPropertyOverride> overrides, std::vector<ScenePrefabVariantAddedSubtree> addedChildren = {});
    [[nodiscard]] bool Contains(ScenePrefabHandle handle) const noexcept;
    [[nodiscard]] const ScenePrefabRecord* FindRecord(ScenePrefabHandle handle) const noexcept;
    [[nodiscard]] ScenePrefabRecord* FindMutableRecord(ScenePrefabHandle handle) noexcept;
    [[nodiscard]] const ScenePrefab* Find(ScenePrefabHandle handle) const noexcept;
    [[nodiscard]] ScenePrefab* FindMutable(ScenePrefabHandle handle) noexcept;
    [[nodiscard]] ScenePrefabHandle FindByGuid(std::string_view guid) const noexcept;
    [[nodiscard]] std::vector<ScenePrefabHandle> VariantChildrenOf(ScenePrefabHandle baseHandle) const;
    [[nodiscard]] bool UpsertVariantOverride(ScenePrefabHandle handle, ScenePrefabPropertyOverride property);
    // LIB-092: replaces a variant's stored added-child subtrees wholesale and
    // re-materializes it, so the variant prefab structurally contains the
    // added children (mirrors UpsertVariantOverride's re-materialize path).
    [[nodiscard]] bool SetVariantAddedChildren(ScenePrefabHandle handle, std::vector<ScenePrefabVariantAddedSubtree> addedChildren);
    void RefreshContentHash(ScenePrefabHandle handle) noexcept;
    void RefreshDerivedPrefabs(ScenePrefabHandle baseHandle);
    [[nodiscard]] std::size_t Count() const noexcept;
    [[nodiscard]] bool Remove(ScenePrefabHandle handle) noexcept;
    void Clear() noexcept;

private:
    ScenePrefabRecordStore records_;
};

} // namespace kb::scene
