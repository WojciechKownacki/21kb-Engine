#pragma once

#include "engine/scene/SceneObject.hpp"

#include <string>

namespace kb::scene {

struct ScenePrefabInstantiationSettings {
    SceneObject parent{};
    std::string namePrefix;
};

} // namespace kb::scene
