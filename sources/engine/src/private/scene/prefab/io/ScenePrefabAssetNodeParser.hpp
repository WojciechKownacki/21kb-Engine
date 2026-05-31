#pragma once

#include "engine/scene/ScenePrefabNode.hpp"
#include "scene/prefab/io/ScenePrefabAssetFieldParser.hpp"

namespace kb::scene {

class ScenePrefabAssetNodeParser {
public:
    ScenePrefabAssetNodeParser() = delete;

    [[nodiscard]] static bool Parse(const ScenePrefabAssetFieldMap& fields, ScenePrefabNodeDesc& node);
};

} // namespace kb::scene
