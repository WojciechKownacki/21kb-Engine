#pragma once

#include "engine/scene/ScenePrefabInstanceHandle.hpp"

#include <cstdint>
#include <string_view>

namespace kb::scene {

class Scene;

class ScenePrefabOverrideMutationService {
public:
    ScenePrefabOverrideMutationService() = delete;

    [[nodiscard]] static bool Revert(Scene& scene, ScenePrefabInstanceHandle handle);
    [[nodiscard]] static bool Apply(Scene& scene, ScenePrefabInstanceHandle handle);
    [[nodiscard]] static bool RevertProperty(Scene& scene, ScenePrefabInstanceHandle handle, std::uint32_t nodeIndex, std::string_view propertyPath);
    [[nodiscard]] static bool ApplyProperty(Scene& scene, ScenePrefabInstanceHandle handle, std::uint32_t nodeIndex, std::string_view propertyPath);
};

} // namespace kb::scene
