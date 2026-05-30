#pragma once

#include "engine/scene/ScenePrefab.hpp"

#include <string>

namespace kb::scene {

class ScenePrefabNameResolver {
public:
    ScenePrefabNameResolver() = delete;

    [[nodiscard]] static std::string Resolve(const ScenePrefabNodeDesc& node, const ScenePrefabInstantiationSettings& settings);
};

} // namespace kb::scene
