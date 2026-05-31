#pragma once

#include "engine/scene/ScenePrefabNode.hpp"
#include "scene/prefab/io/ScenePrefabAssetFieldParser.hpp"

namespace kb::scene {

class ScenePrefabAssetNestedOverrideParser {
public:
    ScenePrefabAssetNestedOverrideParser() = delete;

    [[nodiscard]] static bool Parse(const ScenePrefabAssetFieldMap& fields, ScenePrefabNodeDesc& node);
};

} // namespace kb::scene
