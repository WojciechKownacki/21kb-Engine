#pragma once

#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/ScenePrefabOverrides.hpp"

#include <cstdint>
#include <optional>

namespace kb::scene {

class ScenePrefabCameraOverrideReporter {
public:
    ScenePrefabCameraOverrideReporter() = delete;

    static void Append(SceneComponents components, SceneEntity entity, const std::optional<CameraComponent>& expected, ScenePrefabOverrideReport& report, std::uint32_t nodeIndex, SceneObject object);
};

} // namespace kb::scene
