#pragma once

#include "engine/scene/SceneObject.hpp"

#include <string>

namespace kb::scene {

struct ScenePrefabInstantiationSettings {
    SceneObject parent{};
    std::string namePrefix;
    bool assignNames = true;
    bool syncWorldHierarchy = false;
};

} // namespace kb::scene
