#pragma once

#include "engine/scene/ScenePrefabNode.hpp"
#include "scene/prefab/io/ScenePrefabAssetFieldParser.hpp"

namespace kb::scene {

class ScenePrefabAssetComponentParser {
public:
    ScenePrefabAssetComponentParser() = delete;

    [[nodiscard]] static bool Parse(const ScenePrefabAssetFieldMap& fields, ScenePrefabNodeComponents& components);
};

} // namespace kb::scene
