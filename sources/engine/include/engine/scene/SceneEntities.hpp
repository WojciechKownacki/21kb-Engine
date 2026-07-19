#pragma once

#include "engine/scene/BehaviourVariableOverride.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/SceneObjectDesc.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::scene {

class Scene;

class SceneEntityQueries {
public:
    explicit SceneEntityQueries(const Scene& scene) noexcept;

    [[nodiscard]] bool IsAlive(SceneObject object) const noexcept;
    [[nodiscard]] bool IsAlive(SceneEntity entity) const noexcept;
    [[nodiscard]] std::string Name(SceneObject object) const;
    [[nodiscard]] std::string Name(SceneEntity entity) const;
    [[nodiscard]] bool IsActive(SceneObject object) const noexcept;
    [[nodiscard]] bool IsActive(SceneEntity entity) const noexcept;
    // LIB-072: whether this entity is exempt from ClearSceneRoots (the
    // non-additive Scene.Load wipe) — see SceneState::persistentEntities.
    [[nodiscard]] bool IsPersistent(SceneObject object) const noexcept;
    [[nodiscard]] bool IsPersistent(SceneEntity entity) const noexcept;
    [[nodiscard]] std::size_t Count() const;

private:
    const Scene& scene_;
};

class SceneEntities {
public:
    explicit SceneEntities(Scene& scene) noexcept;

    [[nodiscard]] SceneObject CreateObject();
    [[nodiscard]] SceneObject CreateObject(SceneObjectDesc desc);
    // Bulk spawn: creates one object per descriptor in a single call. Structural
    // changes batch through the world's version-based query-plan invalidation, so
    // the query cache rebuilds lazily once rather than per spawned object.
    [[nodiscard]] std::vector<SceneObject> CreateObjects(std::span<const SceneObjectDesc> descs);
    [[nodiscard]] SceneEntity CreateEntity();
    [[nodiscard]] SceneEntity CreateEntity(SceneObjectDesc desc);
    [[nodiscard]] SceneObject Duplicate(SceneObject object);
    [[nodiscard]] std::vector<SceneObject> Duplicate(std::span<const SceneObject> objects);
    void Destroy(SceneObject object) noexcept;
    void Destroy(SceneEntity entity) noexcept;
    void Destroy(std::span<const SceneObject> objects) noexcept;
    // LIB-067: queue an entity for destruction at the next frame playback
    // point (World.Destroy(deferred=true)); drained by DrainDeferredDestroys.
    void QueueDeferredDestroy(SceneEntity entity) noexcept;
    // LIB-067: destroy every still-alive queued entity, clear the queue, and
    // return the count destroyed. Called once per frame by the scene system.
    [[nodiscard]] std::size_t DrainDeferredDestroys() noexcept;

    // Per-instance overrides of a behaviour's exposed ("@expose") script
    // variables, authored in the Inspector. Delta-over-default (the Unity/
    // Unreal/Godot/O3DE model): only variables whose value differs from the
    // script's declared default are stored; setting one back to its default
    // drops the override. Read returns the stored deltas (empty span if none).
    [[nodiscard]] std::span<const BehaviourVariableOverride> BehaviourVariableOverrides(SceneEntity entity) const noexcept;
    void SetBehaviourVariableOverride(SceneEntity entity, std::string name, kb::script::ScriptValue value);
    bool RemoveBehaviourVariableOverride(SceneEntity entity, std::string_view name) noexcept;
    void ReplaceBehaviourVariableOverrides(SceneEntity entity, std::vector<BehaviourVariableOverride> overrides);

    [[nodiscard]] bool SetParent(std::span<const SceneObject> objects, SceneObject parent) noexcept;

    [[nodiscard]] bool IsAlive(SceneObject object) const noexcept;
    [[nodiscard]] bool IsAlive(SceneEntity entity) const noexcept;
    [[nodiscard]] SceneObject Object(SceneEntity entity) const noexcept;
    [[nodiscard]] std::string Name(SceneObject object) const;
    [[nodiscard]] std::string Name(SceneEntity entity) const;
    void SetName(SceneObject object, std::string_view name);
    void SetName(SceneEntity entity, std::string_view name);
    [[nodiscard]] bool IsActive(SceneObject object) const noexcept;
    [[nodiscard]] bool IsActive(SceneEntity entity) const noexcept;
    void SetActive(SceneObject object, bool active) noexcept;
    void SetActive(SceneEntity entity, bool active) noexcept;
    // LIB-072: marks a ROOT entity as exempt from ClearSceneRoots (the
    // non-additive Scene.Load wipe) — its whole hierarchy survives with it,
    // since Destroy cascades to children from whatever root is (not)
    // destroyed. Marking a non-root entity persistent has no protective
    // effect if its own ancestor root is not also persistent — see
    // SceneState::persistentEntities' comment for why.
    [[nodiscard]] bool IsPersistent(SceneObject object) const noexcept;
    [[nodiscard]] bool IsPersistent(SceneEntity entity) const noexcept;
    void SetPersistent(SceneObject object, bool persistent) noexcept;
    void SetPersistent(SceneEntity entity, bool persistent) noexcept;
    [[nodiscard]] std::size_t Count() const;

private:
    Scene& scene_;
};

} // namespace kb::scene
