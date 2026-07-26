#pragma once

#include "engine/scene/SceneObject.hpp"
#include "engine/scene/ScenePrefabCaptureSettings.hpp"
#include "engine/scene/ScenePrefabNode.hpp"

#include <cstdint>
#include <vector>

namespace kb::scene {

class Scene;
class ScenePrefab;

class ScenePrefabCaptureTraversal {
public:
    static void Append(Scene& scene, SceneObject object, const ScenePrefabCaptureSettings& settings, ScenePrefab& prefab, std::uint32_t parentNode, std::vector<SceneEntity>& capturedEntities);
};

} // namespace kb::scene
