#pragma once

#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/ScenePrefabCaptureSettings.hpp"
#include "engine/scene/ScenePrefabHandle.hpp"
#include "engine/scene/ScenePrefabInstanceHandle.hpp"
#include "engine/scene/ScenePrefabOverrides.hpp"
#include "engine/scene/SceneObject.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace kb::scene {

class Scene;
class SceneObject;
class ScenePrefabPrivateScene;

enum class ScenePrefabAssetType {
    None,
    Template,
    Variant,
    Missing,
};

enum class ScenePrefabInstanceStatus {
    NotInstance,
    Connected,
    MissingAsset,
};

enum class ScenePrefabUnpackMode {
    RootOnly,
    Complete,
};

class ScenePrefabs {
public:
    explicit ScenePrefabs(Scene& scene) noexcept;

    [[nodiscard]] ScenePrefab Capture(SceneObject root) const;
    [[nodiscard]] ScenePrefab Capture(SceneObject root, const ScenePrefabCaptureSettings& settings) const;
    [[nodiscard]] ScenePrefabInstance Instantiate(const ScenePrefab& prefab);
    [[nodiscard]] ScenePrefabInstance Instantiate(const ScenePrefab& prefab, const ScenePrefabInstantiationSettings& settings);
    [[nodiscard]] std::vector<ScenePrefabInstance> InstantiateMany(const ScenePrefab& prefab, std::size_t count);
    [[nodiscard]] std::vector<ScenePrefabInstance> InstantiateMany(const ScenePrefab& prefab, std::size_t count, const ScenePrefabInstantiationSettings& settings);
    [[nodiscard]] ScenePrefabHandle Register(std::string name, ScenePrefab prefab);
    [[nodiscard]] ScenePrefabHandle RegisterVariant(std::string name, ScenePrefabHandle basePrefab, std::vector<ScenePrefabPropertyOverride> overrides);
    [[nodiscard]] ScenePrefabHandle CaptureRegistered(SceneObject root, std::string name);
    [[nodiscard]] ScenePrefabHandle CaptureRegistered(SceneObject root, const ScenePrefabCaptureSettings& settings, std::string name);
    [[nodiscard]] ScenePrefabHandle CreateAsset(SceneObject root, std::string name, const std::filesystem::path& path);
    [[nodiscard]] ScenePrefabHandle CreateAsset(SceneObject root, const ScenePrefabCaptureSettings& settings, std::string name, const std::filesystem::path& path);
    [[nodiscard]] bool Contains(ScenePrefabHandle handle) const noexcept;
    [[nodiscard]] std::string Guid(ScenePrefabHandle handle) const;
    [[nodiscard]] ScenePrefabAssetType AssetType(ScenePrefabHandle handle) const noexcept;
    [[nodiscard]] std::size_t RegisteredCount() const noexcept;
    void Clear() noexcept;
    [[nodiscard]] ScenePrefab Get(ScenePrefabHandle handle) const;
    [[nodiscard]] ScenePrefabInstance Instantiate(ScenePrefabHandle handle);
    [[nodiscard]] ScenePrefabInstance Instantiate(ScenePrefabHandle handle, const ScenePrefabInstantiationSettings& settings);
    [[nodiscard]] std::vector<ScenePrefabInstance> InstantiateMany(ScenePrefabHandle handle, std::size_t count);
    [[nodiscard]] std::vector<ScenePrefabInstance> InstantiateMany(ScenePrefabHandle handle, std::size_t count, const ScenePrefabInstantiationSettings& settings);
    [[nodiscard]] bool Save(ScenePrefabHandle handle, const std::filesystem::path& path) const;
    [[nodiscard]] ScenePrefabHandle Load(const std::filesystem::path& path);
    [[nodiscard]] bool Unload(ScenePrefabHandle handle) noexcept;
    [[nodiscard]] bool IsInstance(ScenePrefabInstanceHandle handle) const noexcept;
    [[nodiscard]] ScenePrefabInstanceStatus InstanceStatus(ScenePrefabInstanceHandle handle) const noexcept;
    [[nodiscard]] ScenePrefabHandle SourcePrefab(ScenePrefabInstanceHandle handle) const noexcept;
    [[nodiscard]] ScenePrefabHandle SourcePrefab(SceneObject object) const noexcept;
    [[nodiscard]] ScenePrefabHandle SourcePrefab(SceneEntity entity) const noexcept;
    [[nodiscard]] ScenePrefabHandle OriginalSourcePrefab(ScenePrefabHandle handle) const noexcept;
    [[nodiscard]] ScenePrefabHandle OriginalSourcePrefab(ScenePrefabInstanceHandle handle) const noexcept;
    [[nodiscard]] ScenePrefabHandle OriginalSourcePrefab(SceneObject object) const noexcept;
    [[nodiscard]] ScenePrefabHandle OriginalSourcePrefab(SceneEntity entity) const noexcept;
    [[nodiscard]] ScenePrefabPrivateScene OpenPrivateScene(ScenePrefabHandle handle);
    [[nodiscard]] ScenePrefabInstanceHandle RootInstance(SceneObject object) const noexcept;
    [[nodiscard]] ScenePrefabInstanceHandle RootInstance(SceneEntity entity) const noexcept;
    [[nodiscard]] ScenePrefabInstanceHandle ContainingInstance(SceneObject object, std::uint32_t& nodeIndex) const noexcept;
    [[nodiscard]] ScenePrefabInstanceHandle ContainingInstance(SceneEntity entity, std::uint32_t& nodeIndex) const noexcept;
    [[nodiscard]] ScenePrefabInstanceHandle ContainingInstance(SceneObject object, std::uint32_t& nodeIndex, std::uint64_t& nodeId) const noexcept;
    [[nodiscard]] ScenePrefabInstanceHandle ContainingInstance(SceneEntity entity, std::uint32_t& nodeIndex, std::uint64_t& nodeId) const noexcept;
    [[nodiscard]] std::size_t RefreshInstances(ScenePrefabHandle handle);
    [[nodiscard]] bool Reconnect(ScenePrefabInstanceHandle handle, ScenePrefabHandle sourcePrefab);
    [[nodiscard]] bool Unpack(ScenePrefabInstanceHandle handle, ScenePrefabUnpackMode mode = ScenePrefabUnpackMode::RootOnly);
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
