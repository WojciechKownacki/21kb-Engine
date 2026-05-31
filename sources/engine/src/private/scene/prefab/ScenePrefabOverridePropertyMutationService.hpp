#pragma once

#include "engine/scene/ScenePrefabInstanceHandle.hpp"

#include <cstdint>
#include <string_view>

namespace kb::scene {

class Scene;

class ScenePrefabOverridePropertyMutationService {
public:
    ScenePrefabOverridePropertyMutationService() = delete;

    [[nodiscard]] static bool Revert(Scene& scene, ScenePrefabInstanceHandle handle, std::uint32_t nodeIndex, std::string_view propertyPath);
    [[nodiscard]] static bool Apply(Scene& scene, ScenePrefabInstanceHandle handle, std::uint32_t nodeIndex, std::string_view propertyPath);
};

} // namespace kb::scene
