#pragma once

#include "engine/scene/SceneObject.hpp"
#include "engine/scene/ScenePrefabCaptureSettings.hpp"

#include <cstddef>

namespace kb::scene {

class ScenePrefabHierarchyCounter {
public:
    [[nodiscard]] static std::size_t Count(SceneObject root, const ScenePrefabCaptureSettings& settings);
};

} // namespace kb::scene
