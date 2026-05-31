#pragma once

#include "engine/scene/ScenePrefab.hpp"
#include "scene/prefab/ScenePrefabNodeTreeIndex.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace kb::scene {

class ScenePrefabRegistry;
struct ScenePrefabRecord;

class ScenePrefabNestedAppendContext {
public:
    ScenePrefabNestedAppendContext(const ScenePrefabRegistry& registry, const ScenePrefab& source, ScenePrefab& output, std::vector<std::string>& stack);

    void Append(std::uint32_t sourceIndex, std::uint32_t outputParent);

private:
    void AppendNested(std::uint32_t sourceIndex, std::uint32_t outputParent, const ScenePrefabRecord& nestedRecord);

    const ScenePrefabRegistry& registry_;
    const ScenePrefab& source_;
    ScenePrefab& output_;
    std::vector<std::string>& stack_;
    ScenePrefabNodeTreeIndex tree_;
};

} // namespace kb::scene
