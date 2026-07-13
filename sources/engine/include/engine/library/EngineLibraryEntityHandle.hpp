#pragma once

#include "engine/library/EngineLibraryError.hpp"
#include "engine/library/EngineLibraryExecutionOrder.hpp"
#include "engine/library/EngineLibraryResult.hpp"
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

    // LIB-075: Entity.Has<T>/TryGet<T>/GetRequired<T>/Add<T>/Remove<T> —
    // ONLY for the closed, hand-maintained set of component types
    // registered for scripts (see EngineLibraryScriptComponentAccess.hpp's
    // ScriptComponentAccess<T> specializations: Transform, Visibility,
    // Camera, Light, MeshRenderer, Behaviour — the exact same six names
    // ScriptSceneComponentApi.cpp's kComponentNames already gates Lua/
    // Visual Graph property access behind). Instantiating any of these
    // for an unregistered Component fails to compile (static_assert in
    // the primary ScriptComponentAccess template), not a runtime check —
    // this is native C++ template code, unreachable from Lua/Visual Graph.
    // Declared here (no component-type headers needed to declare a
    // template), DEFINED in EngineLibraryScriptComponentAccess.hpp — a
    // caller must include that header too to actually instantiate one of
    // these for a real component type, keeping this lightweight header's
    // own dependents from pulling in every component type's header.
    //
    // A dead/wrong-scene handle never crashes: Has()/TryGet() return
    // false/nullptr, Add()/Remove() return false — same "never crash on a
    // stale handle" contract every other kb::scene status query already
    // follows. GetRequired() is the one exception, matching its name: it
    // returns a failed Result (not a crash) naming why.
    template <typename Component>
    [[nodiscard]] bool Has(const kb::scene::Scene& scene) const noexcept;
    template <typename Component>
    [[nodiscard]] const Component* TryGet(const kb::scene::Scene& scene) const noexcept;
    template <typename Component>
    [[nodiscard]] Component* TryGet(kb::scene::Scene& scene) const noexcept;
    // Returns a copy (not a reference) on success — mirrors
    // kb::scene::SceneTransforms::Get()'s existing "TryGet returns a
    // pointer, Get returns a copy" split; a Result<T> cannot hold a
    // reference anyway (LIB-032's ScriptValue-adjacent value-type-only
    // convention).
    template <typename Component>
    [[nodiscard]] Result<Component> GetRequired(const kb::scene::Scene& scene) const;
    // Ensures the entity has Component with this value (Set semantics,
    // not "fails if already present") — the natural meaning of "Add" for
    // a component that may already implicitly exist (Transform/
    // Visibility are on every entity from creation and can never be
    // removed; Add<TransformComponent>/Add<VisibilityComponent> still
    // succeed, they just overwrite). Returns false only for a dead/wrong-
    // scene handle.
    template <typename Component>
    bool Add(kb::scene::Scene& scene, const Component& value) const;
    // False (not a crash) both for a dead/wrong-scene handle AND for a
    // mandatory component that cannot be removed (Transform, Visibility —
    // see ScriptComponentAccess<T>::Remove's per-type comment) — the
    // caller cannot distinguish the two from the return value alone,
    // matching every other kb::scene status query's flat bool contract in
    // this class.
    template <typename Component>
    [[nodiscard]] bool Remove(kb::scene::Scene& scene) const noexcept;

private:
    kb::scene::SceneEntity entity_{};
    std::uint64_t sceneId_ = 0U;
};

} // namespace kb::library
