#pragma once

#include "engine/scene/ScenePrefabInstanceHandle.hpp"
#include "engine/scene/ScenePrefabOverrides.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace kb::scene {

class Scene;

class ScenePrefabOverrideFacade {
public:
    ScenePrefabOverrideFacade() = delete;

    [[nodiscard]] static bool IsInstance(Scene& scene, ScenePrefabInstanceHandle handle) noexcept;
    [[nodiscard]] static ScenePrefabOverrideReport Overrides(Scene& scene, ScenePrefabInstanceHandle handle);
    [[nodiscard]] static bool Revert(Scene& scene, ScenePrefabInstanceHandle handle);
    [[nodiscard]] static bool Apply(Scene& scene, ScenePrefabInstanceHandle handle);
    [[nodiscard]] static bool ApplyAndSave(Scene& scene, ScenePrefabInstanceHandle handle, const std::filesystem::path& assetPath);
    [[nodiscard]] static bool RevertProperty(Scene& scene, ScenePrefabInstanceHandle handle, std::uint32_t nodeIndex, std::string propertyPath);
    [[nodiscard]] static bool ApplyProperty(Scene& scene, ScenePrefabInstanceHandle handle, std::uint32_t nodeIndex, std::string propertyPath);
    [[nodiscard]] static bool ApplyPropertyAndSave(Scene& scene, ScenePrefabInstanceHandle handle, std::uint32_t nodeIndex, std::string propertyPath, const std::filesystem::path& assetPath);
};

} // namespace kb::scene
