#pragma once

#include "engine/scene/ScenePrefab.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace kb::scene {

class ScenePrefabRegistry;

class ScenePrefabNestedResolver {
public:
    ScenePrefabNestedResolver() = delete;

    [[nodiscard]] static ScenePrefab Resolve(const ScenePrefabRegistry& registry, const ScenePrefab& prefab);
    [[nodiscard]] static ScenePrefab Resolve(const ScenePrefabRegistry& registry, const ScenePrefab& prefab, std::vector<std::string>& stack);
};

} // namespace kb::scene
