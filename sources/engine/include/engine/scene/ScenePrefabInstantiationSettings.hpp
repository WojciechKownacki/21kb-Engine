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
    // The objects take the stable ids the nodes hold, as when a scene file loads or an undo brings an object back.
    // Otherwise each object gets an id of its own when it is first saved, so a copy never shares one with its source.
    bool keepStableIds = false;
};

} // namespace kb::scene
