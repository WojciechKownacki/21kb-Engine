#pragma once

#include <cstdint>

namespace kb::scene {

class Scene;

// LIB-139: read-only half of SceneMaterialInstances, mirroring
// SceneMeshRendererComponentQueries/SceneMeshRendererComponents' const/mutable
// split - obtained from a `const Scene&`, so it is safe to call from contexts
// that only ever see the scene read-only (e.g. kb::render's
// EcsRenderSceneSynchronizer, which never mutates kb::scene state).
class SceneMaterialInstanceQueries {
public:
    explicit SceneMaterialInstanceQueries(const Scene& scene) noexcept;

    [[nodiscard]] bool Exists(std::uint64_t id) const noexcept;
    // Returns 0 (never a valid asset id) if `id` names no currently live
    // instance.
    [[nodiscard]] std::uint64_t Parent(std::uint64_t id) const noexcept;

private:
    const Scene& scene_;
};

// LIB-139: MaterialInstance.Create/Release's engine-side facade. A runtime
// MaterialInstance is a scene-owned, explicit-lifetime indirection to a
// parent material asset - it exists so a script can hold a handle distinct
// from the shared parent asset (LIB-140 adds per-parameter overrides on top;
// LIB-139 alone already makes assigning a "private" reference to a
// MeshRenderer meaningful, since Release()ing it is observably different
// from never having created one, and a MeshRenderer referencing a released
// instance honestly falls back to no material rather than silently keeping
// the parent - see EcsRenderSceneSynchronizer::SyncMesh).
//
// `id` is a monotonically increasing per-scene std::uint64_t (SceneState::
// nextMaterialInstanceId), never reused within a scene's lifetime - the same
// convention as SceneTimers' TimerHandle, deliberately NOT a
// generation-checked handle registry for the same reason documented on
// SceneTimers.hpp: ids are never reused, so a stale id can never collide
// with a live one.
class SceneMaterialInstances {
public:
    explicit SceneMaterialInstances(Scene& scene) noexcept;

    // Returns 0 (never a valid id) if parentMaterialAssetId==0, or if the
    // scene already holds kMaxLiveMaterialInstances live instances (LIB-139's
    // "limit wariantów" - see SceneMaterialInstanceService.cpp).
    [[nodiscard]] std::uint64_t Create(std::uint64_t parentMaterialAssetId) noexcept;
    // Idempotent - false if `id` names no currently live instance (already
    // released, or never existed).
    [[nodiscard]] bool Release(std::uint64_t id) noexcept;
    [[nodiscard]] bool Exists(std::uint64_t id) const noexcept;
    [[nodiscard]] std::uint64_t Parent(std::uint64_t id) const noexcept;

private:
    Scene& scene_;
};

} // namespace kb::scene
