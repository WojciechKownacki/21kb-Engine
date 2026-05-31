#pragma once

#include "engine/scene/SceneObject.hpp"
#include "engine/scene/ScenePrefabOverrides.hpp"

#include <cstdint>
#include <string_view>

namespace kb::scene {

class Scene;

class ScenePrefabAppliedPropertyBuilder {
public:
    ScenePrefabAppliedPropertyBuilder() = delete;

    [[nodiscard]] static bool Build(Scene& scene, std::uint32_t nodeIndex, SceneObject object, std::string_view propertyPath, ScenePrefabPropertyOverride& property);
};

} // namespace kb::scene
