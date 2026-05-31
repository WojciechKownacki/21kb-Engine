#pragma once

#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/ScenePrefabCaptureSettings.hpp"
#include "engine/scene/ScenePrefabHandle.hpp"
#include "engine/scene/ScenePrefabInstanceHandle.hpp"
#include "engine/scene/ScenePrefabOverrides.hpp"

#include <cstddef>
#include <filesystem>
#include <string>

namespace kb::scene {

class Scene;
class SceneObject;

class ScenePrefabs {
public:
    explicit ScenePrefabs(Scene& scene) noexcept;

    [[nodiscard]] ScenePrefab Capture(SceneObject root) const;
    [[nodiscard]] ScenePrefab Capture(SceneObject root, const ScenePrefabCaptureSettings& settings) const;
    [[nodiscard]] ScenePrefabInstance Instantiate(const ScenePrefab& prefab);
    [[nodiscard]] ScenePrefabInstance Instantiate(const ScenePrefab& prefab, const ScenePrefabInstantiationSettings& settings);
    [[nodiscard]] ScenePrefabHandle Register(std::string name, ScenePrefab prefab);
    [[nodiscard]] ScenePrefabHandle CaptureRegistered(SceneObject root, std::string name);
    [[nodiscard]] ScenePrefabHandle CaptureRegistered(SceneObject root, const ScenePrefabCaptureSettings& settings, std::string name);
    [[nodiscard]] bool Contains(ScenePrefabHandle handle) const noexcept;
    [[nodiscard]] std::size_t RegisteredCount() const noexcept;
    [[nodiscard]] ScenePrefabInstance Instantiate(ScenePrefabHandle handle);
    [[nodiscard]] ScenePrefabInstance Instantiate(ScenePrefabHandle handle, const ScenePrefabInstantiationSettings& settings);
    [[nodiscard]] bool Save(ScenePrefabHandle handle, const std::filesystem::path& path) const;
    [[nodiscard]] ScenePrefabHandle Load(const std::filesystem::path& path);
    [[nodiscard]] bool IsInstance(ScenePrefabInstanceHandle handle) const noexcept;
    [[nodiscard]] ScenePrefabOverrideReport Overrides(ScenePrefabInstanceHandle handle) const;
    [[nodiscard]] bool RevertOverrides(ScenePrefabInstanceHandle handle);
    [[nodiscard]] bool ApplyOverrides(ScenePrefabInstanceHandle handle);

private:
    Scene& scene_;
};

} // namespace kb::scene
