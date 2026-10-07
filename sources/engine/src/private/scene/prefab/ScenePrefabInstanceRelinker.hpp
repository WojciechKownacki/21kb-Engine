#pragma once

#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/ScenePrefabInstance.hpp"
#include "engine/scene/SceneObject.hpp"

namespace kb::scene {

class Scene;

// A captured prefab keeps an instance's link only as the guid on the instance root node.
// Instantiating such a prefab registers every guid-named subtree as an instance of that prefab
// again, matching its objects to the prefab's nodes by hierarchy and name.
class ScenePrefabInstanceRelinker {
public:
    ScenePrefabInstanceRelinker() = delete;

    static void Relink(Scene& scene, const ScenePrefab& prefab, SceneObject parent, const ScenePrefabInstance& instance);
};

} // namespace kb::scene
