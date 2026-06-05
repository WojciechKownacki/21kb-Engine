#pragma once

#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/ScenePrefabCaptureSettings.hpp"
#include "engine/scene/ScenePrefabHandle.hpp"
#include "engine/scene/ScenePrefabInstanceHandle.hpp"
#include "engine/scene/ScenePrefabOverrides.hpp"
#include "engine/scene/SceneObject.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

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
    [[nodiscard]] ScenePrefabHandle RegisterVariant(std::string name, ScenePrefabHandle basePrefab, std::vector<ScenePrefabPropertyOverride> overrides);
    [[nodiscard]] ScenePrefabHandle CaptureRegistered(SceneObject root, std::string name);
    [[nodiscard]] ScenePrefabHandle CaptureRegistered(SceneObject root, const ScenePrefabCaptureSettings& settings, std::string name);
    [[nodiscard]] ScenePrefabHandle CreateAsset(SceneObject root, std::string name, const std::filesystem::path& path);
    [[nodiscard]] ScenePrefabHandle CreateAsset(SceneObject root, const ScenePrefabCaptureSettings& settings, std::string name, const std::filesystem::path& path);
    [[nodiscard]] bool Contains(ScenePrefabHandle handle) const noexcept;
    [[nodiscard]] std::string Guid(ScenePrefabHandle handle) const;
    [[nodiscard]] std::size_t RegisteredCount() const noexcept;
    [[nodiscard]] ScenePrefab Get(ScenePrefabHandle handle) const;
    [[nodiscard]] ScenePrefabInstance Instantiate(ScenePrefabHandle handle);
    [[nodiscard]] ScenePrefabInstance Instantiate(ScenePrefabHandle handle, const ScenePrefabInstantiationSettings& settings);
    [[nodiscard]] bool Save(ScenePrefabHandle handle, const std::filesystem::path& path) const;
    [[nodiscard]] ScenePrefabHandle Load(const std::filesystem::path& path);
    [[nodiscard]] bool IsInstance(ScenePrefabInstanceHandle handle) const noexcept;
    [[nodiscard]] ScenePrefabInstanceHandle RootInstance(SceneObject object) const noexcept;
    [[nodiscard]] ScenePrefabInstanceHandle RootInstance(SceneEntity entity) const noexcept;
    [[nodiscard]] ScenePrefabInstanceHandle ContainingInstance(SceneObject object, std::uint32_t& nodeIndex) const noexcept;
    [[nodiscard]] ScenePrefabInstanceHandle ContainingInstance(SceneEntity entity, std::uint32_t& nodeIndex) const noexcept;
    [[nodiscard]] ScenePrefabOverrideReport Overrides(ScenePrefabInstanceHandle handle) const;
    [[nodiscard]] bool RevertOverrides(ScenePrefabInstanceHandle handle);
    [[nodiscard]] bool ApplyOverrides(ScenePrefabInstanceHandle handle);
    [[nodiscard]] bool ApplyOverrides(ScenePrefabInstanceHandle handle, const std::filesystem::path& assetPath);
    [[nodiscard]] bool RevertOverride(ScenePrefabInstanceHandle handle, std::uint32_t nodeIndex, std::string propertyPath);
    [[nodiscard]] bool ApplyOverride(ScenePrefabInstanceHandle handle, std::uint32_t nodeIndex, std::string propertyPath);
    [[nodiscard]] bool ApplyOverride(ScenePrefabInstanceHandle handle, std::uint32_t nodeIndex, std::string propertyPath, const std::filesystem::path& assetPath);

private:
    Scene& scene_;
};

} // namespace kb::scene
