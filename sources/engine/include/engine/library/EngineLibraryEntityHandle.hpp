#pragma once

#include "engine/library/EngineLibraryError.hpp"
#include "engine/library/EngineLibraryExecutionOrder.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <compare>
#include <cstdint>
#include <optional>
#include <string_view>

namespace kb::library {

// A stable, value-type reference to an entity within a specific Scene.
// EntityHandle never stores a pointer or reference to the Scene/World it
// was created from — only the entity id (EntityId, LIB-005:
// generation-protected by flecs) and the originating
// kb::scene::Scene::Id(). Checking whether the handle still points at
// something real always means passing the Scene back in
// (IsAlive(scene)/Validate(scene, ...)); the handle never resolves itself.
// This mirrors the existing kb::scene::ScenePrefabHandle /
// ScenePrefabInstanceHandle convention rather than kb::assets::AssetHandle
// (which carries its own payload) — there is one owner (Scene/World) here,
// so the handle only needs to name it, not carry it.
class EntityHandle final {
public:
    constexpr EntityHandle() noexcept = default;
    constexpr EntityHandle(kb::scene::SceneEntity entity, std::uint64_t sceneId) noexcept
        : entity_(entity)
        , sceneId_(sceneId) {}

    [[nodiscard]] constexpr kb::scene::SceneEntity Entity() const noexcept { return entity_; }
    [[nodiscard]] constexpr EntityId Id() const noexcept { return entity_.Id(); }
    [[nodiscard]] constexpr std::uint64_t SceneId() const noexcept { return sceneId_; }

    // Structural validity only (non-zero id): does not check the entity is
    // still alive, or that it belongs to any particular Scene. Use IsAlive()
    // for the real liveness check.
    [[nodiscard]] constexpr bool IsValid() const noexcept { return entity_.IsValid(); }

    [[nodiscard]] constexpr bool operator==(const EntityHandle&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const EntityHandle&) const noexcept = default;

    // True only if the handle is structurally valid, belongs to `scene`
    // (SceneId() == scene.Id()), and the entity is still alive in it
    // (kb::scene::SceneEntities::IsAlive — generation-checked, so a
    // destroyed entity whose index was recycled by a new entity still
    // reports false). Never throws.
    [[nodiscard]] bool IsAlive(const kb::scene::Scene& scene) const noexcept;

    // Same checks as IsAlive(), but throws a diagnostic naming which check
    // failed instead of silently returning false, for callers that must
    // fail loudly on a stale handle rather than no-op. `operation` is
    // folded into the message so the failure names the call that used the
    // handle (e.g. "Transform.SetWorldPose").
    void Validate(const kb::scene::Scene& scene, std::string_view operation) const;

    // LIB-035: the non-throwing counterpart of Validate() for callers that
    // report failures through ScriptError/Result<T> instead of exceptions.
    // Returns std::nullopt when the handle is valid; otherwise a ScriptError
    // with code == LibraryErrorCode::InvalidHandle (every EntityHandle
    // failure mode — structurally invalid, wrong scene, stale — is a kind
    // of invalid handle) and the same message text Validate() would throw.
    [[nodiscard]] std::optional<ScriptError> CheckError(const kb::scene::Scene& scene, std::string_view operation) const;

private:
    kb::scene::SceneEntity entity_{};
    std::uint64_t sceneId_ = 0U;
};

} // namespace kb::library
