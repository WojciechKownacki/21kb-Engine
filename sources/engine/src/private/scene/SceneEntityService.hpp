#pragma once

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

class SceneEntityService {
public:
    SceneEntityService() = delete;

    [[nodiscard]] static SceneObject CreateObject(Scene& scene);
    [[nodiscard]] static SceneObject CreateObject(Scene& scene, SceneObjectDesc desc);
    [[nodiscard]] static std::vector<SceneObject> CreateObjects(Scene& scene, std::span<const SceneObjectDesc> descs);
    [[nodiscard]] static SceneEntity CreateEntity(Scene& scene);
    [[nodiscard]] static SceneEntity CreateEntity(Scene& scene, SceneObjectDesc desc);
    [[nodiscard]] static SceneObject DuplicateObject(Scene& scene, SceneObject object);
    [[nodiscard]] static std::vector<SceneObject> DuplicateObjects(Scene& scene, std::span<const SceneObject> objects);
    static void DestroyObject(Scene& scene, SceneObject object) noexcept;
    static void DestroyEntity(Scene& scene, SceneEntity entity) noexcept;
    static void DestroyObjects(Scene& scene, std::span<const SceneObject> objects) noexcept;
    // LIB-067: enqueue an entity for destruction at the next frame playback
    // point instead of destroying it now (World.Destroy(deferred=true)).
    // De-duplicated by handle; a non-alive/never-existed entity is not queued.
    static void QueueDeferredDestroy(Scene& scene, SceneEntity entity) noexcept;
    // LIB-067: destroy every still-alive queued entity and clear the queue,
    // returning how many were actually destroyed. Idempotent and generation-
    // safe: a queued handle that is no longer alive (already destroyed, or its
    // id recycled to a newer generation) is skipped, never mis-destroyed.
    static std::size_t DrainDeferredDestroys(Scene& scene) noexcept;
    [[nodiscard]] static bool SetParent(Scene& scene, std::span<const SceneObject> objects, SceneObject parent) noexcept;
    [[nodiscard]] static bool IsAlive(const Scene& scene, SceneObject object) noexcept;
    [[nodiscard]] static bool IsAlive(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static SceneObject Object(Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static std::string Name(const Scene& scene, SceneObject object);
    [[nodiscard]] static std::string Name(const Scene& scene, SceneEntity entity);
    static void SetName(Scene& scene, SceneObject object, std::string_view name);
    static void SetName(Scene& scene, SceneEntity entity, std::string_view name);
    // LIB-068: an entity not currently alive is reported inactive (false)
    // rather than throwing/asserting — same "dead means not queryable as
    // active" contract IsAlive already establishes for every other status
    // check in this class.
    [[nodiscard]] static bool IsActive(const Scene& scene, SceneObject object) noexcept;
    [[nodiscard]] static bool IsActive(const Scene& scene, SceneEntity entity) noexcept;
    static void SetActive(Scene& scene, SceneObject object, bool active) noexcept;
    static void SetActive(Scene& scene, SceneEntity entity, bool active) noexcept;
    // LIB-072: same "dead means not queryable" contract as IsActive — a
    // destroyed/never-existed entity reports false rather than throwing.
    [[nodiscard]] static bool IsPersistent(const Scene& scene, SceneObject object) noexcept;
    [[nodiscard]] static bool IsPersistent(const Scene& scene, SceneEntity entity) noexcept;
    static void SetPersistent(Scene& scene, SceneObject object, bool persistent) noexcept;
    static void SetPersistent(Scene& scene, SceneEntity entity, bool persistent) noexcept;
    [[nodiscard]] static std::size_t Count(const Scene& scene);
};

} // namespace kb::scene
