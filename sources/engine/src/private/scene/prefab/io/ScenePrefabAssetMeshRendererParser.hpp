#pragma once

#include "engine/scene/ScenePrefabNode.hpp"
#include "scene/prefab/io/ScenePrefabAssetFieldParser.hpp"

namespace kb::scene {

class ScenePrefabAssetMeshRendererParser {
public:
    ScenePrefabAssetMeshRendererParser() = delete;

    [[nodiscard]] static bool Parse(const ScenePrefabAssetFieldMap& fields, ScenePrefabNodeComponents& components);
};

} // namespace kb::scene
