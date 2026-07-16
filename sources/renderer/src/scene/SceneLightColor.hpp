#pragma once

#include "engine/scene/LightComponent.hpp"

#include <array>

namespace kb::render {

// LIB-141: derives the effective RGB color the renderer should use for a light, resolving
// LightComponent::useColorTemperature/colorTemperatureKelvin. Lives on the renderer side
// (mirrors SceneTransformMatrices deriving render matrices from TransformComponent) since
// kb::scene must stay renderer-agnostic and this is purely a render-time interpretation of
// authored data, not a stored/queryable scene fact.
class SceneLightColor {
public:
    SceneLightColor() = delete;

    [[nodiscard]] static std::array<float, 3> Resolve(const kb::scene::LightComponent& light) noexcept;
};

} // namespace kb::render
