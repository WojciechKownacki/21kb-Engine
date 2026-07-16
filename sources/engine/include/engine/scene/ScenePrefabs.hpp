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
#include <span>
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

// LIB-160: one queued prefab-instantiation completion notification, drained
// by kb::script::ScriptRuntimeSceneSystem into an entity-local
// "OnPrefabInstantiated" event on `caller` carrying `root` and `count`.
struct ScenePrefabInstantiatedEventRecord {
    SceneEntity caller;
    SceneEntity root;
    std::int32_t count = 0;
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

struct ScenePrefabInstantiationStats {
    std::size_t requestedInstances = 0;
    std::size_t instantiatedInstances = 0;
    std::size_t nodesPerInstance = 0;
    std::size_t entitiesCreated = 0;
    std::size_t prefabArchetypesTouched = 0;
    std::size_t bulkCreateCommands = 0;
    std::size_t componentSetCommands = 0;
    std::size_t parentCommands = 0;
    std::size_t componentBytesCopied = 0;
    std::size_t componentSourceBytesRead = 0;
    double componentCopyBytesPerSecond = 0.0;
    double componentSourceBytesPerSecond = 0.0;
    double entityCreateEntitiesPerSecond = 0.0;
    std::size_t chunksAllocatedDelta = 0;
    std::size_t chunksReusedDelta = 0;
    std::size_t registeredInstanceCount = 0;
    std::uint64_t entityCreateNanoseconds = 0;
    std::uint64_t prefabBakeNanoseconds = 0;
    std::uint64_t componentPayloadBuildNanoseconds = 0;
    std::uint64_t entityBulkCreateNanoseconds = 0;
    std::uint64_t entityPrefabOrderMapNanoseconds = 0;
    std::uint64_t instanceObjectSlabNanoseconds = 0;
    std::uint64_t commandBuildNanoseconds = 0;
    std::uint64_t commandPlaybackNanoseconds = 0;
    std::uint64_t commandPlaybackCreateNanoseconds = 0;
    std::uint64_t commandPlaybackApplyNanoseconds = 0;
    std::uint64_t commandPlaybackParentNanoseconds = 0;
    std::uint64_t commandPlaybackDestroyNanoseconds = 0;
    std::uint64_t hierarchyRecordNanoseconds = 0;
    std::uint64_t nameAssignmentNanoseconds = 0;
    std::uint64_t registryResolveNanoseconds = 0;
    std::uint64_t historyRecordNanoseconds = 0;
    bool hasGeneratedEntityIndexRange = false;
    bool hasContiguousGeneratedEntityRuns = false;
    std::uint32_t maxGeneratedEntityIndex = 0;
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
    [[nodiscard]] ScenePrefabInstantiationStats InstantiateBatch(const ScenePrefab& prefab, std::size_t count);
    [[nodiscard]] ScenePrefabInstantiationStats InstantiateBatch(const ScenePrefab& prefab, std::size_t count, const ScenePrefabInstantiationSettings& settings);
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
    [[nodiscard]] ScenePrefabInstantiationStats InstantiateBatch(ScenePrefabHandle handle, std::size_t count);
    [[nodiscard]] ScenePrefabInstantiationStats InstantiateBatch(ScenePrefabHandle handle, std::size_t count, const ScenePrefabInstantiationSettings& settings);
    [[nodiscard]] ScenePrefabInstantiationStats LastInstantiationStats() const noexcept;

    // LIB-160: queue a prefab-instantiation completion notification for
    // `caller` (a no-op if `caller` is invalid — the completion callback is
    // for the entity script that requested the spawn). Drained once per frame
    // by ScriptRuntimeSceneSystem into an "OnPrefabInstantiated" event.
    void QueueInstantiatedEvent(SceneEntity caller, SceneEntity root, std::size_t count);
    // Returns and clears the queued prefab-instantiation completion
    // notifications (mirrors SceneLoadedContent::DrainPendingLifecycleEvents).
    [[nodiscard]] std::vector<ScenePrefabInstantiatedEventRecord> DrainPendingInstantiatedEvents();
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
    [[nodiscard]] bool RevertOverrides(std::span<const ScenePrefabInstanceHandle> handles);
    [[nodiscard]] bool ApplyOverrides(ScenePrefabInstanceHandle handle);
    [[nodiscard]] bool ApplyOverrides(std::span<const ScenePrefabInstanceHandle> handles);
    [[nodiscard]] bool ApplyOverrides(ScenePrefabInstanceHandle handle, const std::filesystem::path& assetPath);
    [[nodiscard]] bool RevertOverride(ScenePrefabInstanceHandle handle, std::uint32_t nodeIndex, std::string propertyPath);
    [[nodiscard]] bool ApplyOverride(ScenePrefabInstanceHandle handle, std::uint32_t nodeIndex, std::string propertyPath);
    [[nodiscard]] bool ApplyOverride(ScenePrefabInstanceHandle handle, std::uint32_t nodeIndex, std::string propertyPath, const std::filesystem::path& assetPath);

private:
    Scene& scene_;
};

} // namespace kb::scene
