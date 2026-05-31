#pragma once

#include "engine/scene/LightComponent.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/ScenePrefabOverrides.hpp"

#include <cstdint>
#include <optional>

namespace kb::scene {

class ScenePrefabLightOverrideReporter {
public:
    ScenePrefabLightOverrideReporter() = delete;

    static void Append(SceneComponents components, SceneEntity entity, const std::optional<LightComponent>& expected, ScenePrefabOverrideReport& report, std::uint32_t nodeIndex, SceneObject object);
};

} // namespace kb::scene
