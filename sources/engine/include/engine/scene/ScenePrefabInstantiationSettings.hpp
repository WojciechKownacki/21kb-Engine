#pragma once

#include "engine/scene/SceneObject.hpp"

#include <string>

namespace kb::scene {

struct ScenePrefabInstantiationSettings {
    SceneObject parent{};
    std::string namePrefix;
    bool assignNames = true;
    bool syncWorldHierarchy = false;
    // Register nodes that name a prefab guid (captured instance roots) as instances of that prefab
    // again. Scene document loads and editor undo/redo/duplicate set it; runtime spawns do not.
    bool linkPrefabInstances = false;
};

} // namespace kb::scene
