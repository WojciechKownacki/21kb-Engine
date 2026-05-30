#pragma once

#include "engine/scene/SceneObject.hpp"
#include "engine/scene/ScenePrefabCaptureSettings.hpp"

#include <vector>

namespace kb::scene {

class ScenePrefabChildrenReader {
public:
    [[nodiscard]] static std::vector<SceneObject> Read(SceneObject object, const ScenePrefabCaptureSettings& settings);
};

} // namespace kb::scene
