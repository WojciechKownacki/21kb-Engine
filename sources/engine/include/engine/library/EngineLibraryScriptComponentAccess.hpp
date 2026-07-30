#pragma once

#include "engine/library/EngineLibraryEntityHandle.hpp"
#include "engine/library/EngineLibraryError.hpp"
#include "engine/library/EngineLibraryResult.hpp"
#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/CharacterControllerComponent.hpp"
#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/JointComponent.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/Navigation.hpp"
#include "engine/scene/RigidbodyComponent.hpp"
#include "engine/scene/RegionShapeComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneBehaviourComponents.hpp"
#include "engine/scene/SceneCameraComponents.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneLightComponents.hpp"
#include "engine/scene/SceneMeshRendererComponents.hpp"
#include "engine/scene/SceneNavigationComponents.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/SceneVisibilityComponents.hpp"
#include "engine/scene/TagsComponent.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "engine/scene/VisibilityComponent.hpp"

#include <type_traits>

namespace kb::library {

namespace detail {
// LIB-075: the "always false, but dependent on Component so it only fires
// when the primary ScriptComponentAccess template is actually
// instantiated" trick — a plain `static_assert(false, ...)` in the
// primary template would fire at PARSE time for every translation unit
// that includes this header, whether or not anyone ever names an
// unregistered Component.
template <typename Component>
struct ScriptComponentAccessUnregistered : std::false_type {};
} // namespace detail

// LIB-075: the closed, hand-maintained set of component types registered
// for scripts — deliberately the EXACT SAME six names
// ScriptSceneComponentApi.cpp's kComponentNames already gates Lua/Visual
// Graph property access behind (Transform, Visibility, Camera, Light,
// MeshRenderer, Behaviour), so native EntityHandle::Has<T>/etc. cannot
// reach a component the other two frontends can't already name. This is a
// PROPORTIONATE stand-in for LIB-076's not-yet-built full component
// registry (schema/version/serialization/thread-policy) — to be
// superseded by it later, not blocked on it now (same "small flat
// allowlist now, real registry later" call LIB-068 made for
// SceneState::inactiveEntities).
//
// The primary (unspecialized) template is intentionally never given a
// working body: instantiating EntityHandle::Has<SomeUnregisteredType>
// (or TryGet/GetRequired/Add/Remove) fails to COMPILE, not a runtime
// check — the strongest possible enforcement of "only registered
// components," available specifically because this is native C++ code
// (Lua/Visual Graph cannot express templates in the first place, so they
// never reach this path at all).
template <typename Component>
struct ScriptComponentAccess {
    static_assert(detail::ScriptComponentAccessUnregistered<Component>::value,
        "kb::library::EntityHandle::Has/TryGet/GetRequired/Add/Remove: this component type is not registered for scripts (LIB-075) — "
        "only Transform/Visibility/Camera/Light/MeshRenderer/Behaviour/Rigidbody/Collider/CharacterController/Joint (ScriptComponentAccess specializations in "
        "EngineLibraryScriptComponentAccess.hpp) are exposed to native script code, matching ScriptSceneComponentApi.cpp's "
        "kComponentNames used by Lua/Visual Graph");
};

// Transform and Visibility are on every entity from creation
// (kb::scene::SceneEntities::CreateEntity/CreateObject always sets both)
// and have no Remove() on their facades at all — there is no ECS-level
// concept of "an entity without a Transform" in this engine. Add<T> still
// succeeds for these two (Set semantics: overwrite the existing value),
// but Remove<T> honestly reports false rather than silently no-op'ing or
// calling a facade method that does not exist.
template <>
struct ScriptComponentAccess<kb::scene::TransformComponent> {
    [[nodiscard]] static const kb::scene::TransformComponent* TryGet(const kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
        return scene.Transforms().TryGet(entity);
    }
    [[nodiscard]] static kb::scene::TransformComponent* TryGet(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
        return scene.Transforms().TryGet(entity);
    }
    static void Set(kb::scene::Scene& scene, kb::scene::SceneEntity entity, const kb::scene::TransformComponent& value) {
        scene.Transforms().Set(entity, value);
    }
    [[nodiscard]] static bool Remove(kb::scene::Scene&, kb::scene::SceneEntity) noexcept {
        return false;
    }
};

template <>
struct ScriptComponentAccess<kb::scene::VisibilityComponent> {
    [[nodiscard]] static const kb::scene::VisibilityComponent* TryGet(const kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
        return scene.Components().Visibility().TryGet(entity);
    }
    [[nodiscard]] static kb::scene::VisibilityComponent* TryGet(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
        return scene.Components().Visibility().TryGet(entity);
    }
    static void Set(kb::scene::Scene& scene, kb::scene::SceneEntity entity, const kb::scene::VisibilityComponent& value) {
        scene.Components().Visibility().Set(entity, value);
    }
    [[nodiscard]] static bool Remove(kb::scene::Scene&, kb::scene::SceneEntity) noexcept {
        return false;
    }
};

template <>
struct ScriptComponentAccess<kb::scene::CameraComponent> {
    [[nodiscard]] static const kb::scene::CameraComponent* TryGet(const kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
        return scene.Components().Cameras().TryGet(entity);
    }
    [[nodiscard]] static kb::scene::CameraComponent* TryGet(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
        return scene.Components().Cameras().TryGet(entity);
    }
    static void Set(kb::scene::Scene& scene, kb::scene::SceneEntity entity, const kb::scene::CameraComponent& value) {
        scene.Components().Cameras().Set(entity, value);
    }
    [[nodiscard]] static bool Remove(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
        if (!scene.Components().Cameras().Has(entity)) {
            return false;
        }
        scene.Components().Cameras().Remove(entity);
        return true;
    }
};

template <>
struct ScriptComponentAccess<kb::scene::LightComponent> {
    [[nodiscard]] static const kb::scene::LightComponent* TryGet(const kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
        return scene.Components().Lights().TryGet(entity);
    }
    [[nodiscard]] static kb::scene::LightComponent* TryGet(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
        return scene.Components().Lights().TryGet(entity);
    }
    static void Set(kb::scene::Scene& scene, kb::scene::SceneEntity entity, const kb::scene::LightComponent& value) {
        scene.Components().Lights().Set(entity, value);
    }
    [[nodiscard]] static bool Remove(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
        if (!scene.Components().Lights().Has(entity)) {
            return false;
        }
        scene.Components().Lights().Remove(entity);
        return true;
    }
};

template <>
struct ScriptComponentAccess<kb::scene::MeshRendererComponent> {
    [[nodiscard]] static const kb::scene::MeshRendererComponent* TryGet(const kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
        return scene.Components().MeshRenderers().TryGet(entity);
    }
    [[nodiscard]] static kb::scene::MeshRendererComponent* TryGet(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
        return scene.Components().MeshRenderers().TryGet(entity);
    }
    static void Set(kb::scene::Scene& scene, kb::scene::SceneEntity entity, const kb::scene::MeshRendererComponent& value) {
        scene.Components().MeshRenderers().Set(entity, value);
    }
    [[nodiscard]] static bool Remove(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
        if (!scene.Components().MeshRenderers().Has(entity)) {
            return false;
        }
        scene.Components().MeshRenderers().Remove(entity);
        return true;
    }
};

template <>
struct ScriptComponentAccess<kb::scene::BehaviourComponent> {
    [[nodiscard]] static const kb::scene::BehaviourComponent* TryGet(const kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
        return scene.Components().Behaviours().TryGet(entity);
    }
    [[nodiscard]] static kb::scene::BehaviourComponent* TryGet(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
        return scene.Components().Behaviours().TryGet(entity);
    }
    static void Set(kb::scene::Scene& scene, kb::scene::SceneEntity entity, const kb::scene::BehaviourComponent& value) {
        scene.Components().Behaviours().Set(entity, value);
    }
    [[nodiscard]] static bool Remove(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
        if (!scene.Components().Behaviours().Has(entity)) {
            return false;
        }
        scene.Components().Behaviours().Remove(entity);
        return true;
    }
};

template <>
struct ScriptComponentAccess<kb::scene::RigidbodyComponent> {
    [[nodiscard]] static const kb::scene::RigidbodyComponent* TryGet(const kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
        return scene.Components().Rigidbodies().TryGet(entity);
    }
    [[nodiscard]] static kb::scene::RigidbodyComponent* TryGet(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
        return scene.Components().Rigidbodies().TryGet(entity);
    }
    static void Set(kb::scene::Scene& scene, kb::scene::SceneEntity entity, const kb::scene::RigidbodyComponent& value) {
        scene.Components().Rigidbodies().Set(entity, value);
    }
    [[nodiscard]] static bool Remove(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
        if (!scene.Components().Rigidbodies().Has(entity)) {
            return false;
        }
        scene.Components().Rigidbodies().Remove(entity);
        return true;
    }
};

template <>
struct ScriptComponentAccess<kb::scene::ColliderComponent> {
    [[nodiscard]] static const kb::scene::ColliderComponent* TryGet(const kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
        return scene.Components().Colliders().TryGet(entity);
    }
    [[nodiscard]] static kb::scene::ColliderComponent* TryGet(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
        return scene.Components().Colliders().TryGet(entity);
    }
    static void Set(kb::scene::Scene& scene, kb::scene::SceneEntity entity, const kb::scene::ColliderComponent& value) {
        scene.Components().Colliders().Set(entity, value);
    }
    [[nodiscard]] static bool Remove(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
        if (!scene.Components().Colliders().Has(entity)) {
            return false;
        }
        scene.Components().Colliders().Remove(entity);
        return true;
    }
};

template <>
struct ScriptComponentAccess<kb::scene::CharacterControllerComponent> {
    [[nodiscard]] static const kb::scene::CharacterControllerComponent* TryGet(const kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
        return scene.Components().CharacterControllers().TryGet(entity);
    }
    [[nodiscard]] static kb::scene::CharacterControllerComponent* TryGet(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
        return scene.Components().CharacterControllers().TryGet(entity);
    }
    static void Set(kb::scene::Scene& scene, kb::scene::SceneEntity entity, const kb::scene::CharacterControllerComponent& value) {
        scene.Components().CharacterControllers().Set(entity, value);
    }
    [[nodiscard]] static bool Remove(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
        if (!scene.Components().CharacterControllers().Has(entity)) {
            return false;
        }
        scene.Components().CharacterControllers().Remove(entity);
        return true;
    }
};

template <>
struct ScriptComponentAccess<kb::scene::JointComponent> {
    [[nodiscard]] static const kb::scene::JointComponent* TryGet(const kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
        return scene.Components().Joints().TryGet(entity);
    }
    [[nodiscard]] static kb::scene::JointComponent* TryGet(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
        return scene.Components().Joints().TryGet(entity);
    }
    static void Set(kb::scene::Scene& scene, kb::scene::SceneEntity entity, const kb::scene::JointComponent& value) {
        scene.Components().Joints().Set(entity, value);
    }
    [[nodiscard]] static bool Remove(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
        if (!scene.Components().Joints().Has(entity)) {
            return false;
        }
        scene.Components().Joints().Remove(entity);
        return true;
    }
};

template <>
struct ScriptComponentAccess<kb::scene::NavAgent> {
    [[nodiscard]] static const kb::scene::NavAgent* TryGet(const kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { return scene.Components().NavAgents().TryGet(entity); }
    [[nodiscard]] static kb::scene::NavAgent* TryGet(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { return scene.Components().NavAgents().TryGet(entity); }
    static void Set(kb::scene::Scene& scene, kb::scene::SceneEntity entity, const kb::scene::NavAgent& value) { scene.Components().NavAgents().Set(entity, value); }
    [[nodiscard]] static bool Remove(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
        if (!scene.Components().NavAgents().Has(entity)) return false;
        scene.Components().NavAgents().Remove(entity);
        return true;
    }
};

template <>
struct ScriptComponentAccess<kb::scene::NavObstacle> {
    [[nodiscard]] static const kb::scene::NavObstacle* TryGet(const kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { return scene.Components().NavObstacles().TryGet(entity); }
    [[nodiscard]] static kb::scene::NavObstacle* TryGet(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { return scene.Components().NavObstacles().TryGet(entity); }
    static void Set(kb::scene::Scene& scene, kb::scene::SceneEntity entity, const kb::scene::NavObstacle& value) { scene.Components().NavObstacles().Set(entity, value); }
    [[nodiscard]] static bool Remove(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
        if (!scene.Components().NavObstacles().Has(entity)) return false;
        scene.Components().NavObstacles().Remove(entity);
        return true;
    }
};

template <>
struct ScriptComponentAccess<kb::scene::TagsComponent> {
    [[nodiscard]] static const kb::scene::TagsComponent* TryGet(const kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { return scene.Components().Tags().TryGet(entity); }
    [[nodiscard]] static kb::scene::TagsComponent* TryGet(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { return scene.Components().Tags().TryGet(entity); }
    static void Set(kb::scene::Scene& scene, kb::scene::SceneEntity entity, const kb::scene::TagsComponent& value) { scene.Components().Tags().Set(entity, value); }
    [[nodiscard]] static bool Remove(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
        if (!scene.Components().Tags().Has(entity)) return false;
        scene.Components().Tags().Remove(entity);
        return true;
    }
};

template <>
struct ScriptComponentAccess<kb::scene::RegionShapeComponent> {
    [[nodiscard]] static const kb::scene::RegionShapeComponent* TryGet(const kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { return scene.Components().RegionShapes().TryGet(entity); }
    [[nodiscard]] static kb::scene::RegionShapeComponent* TryGet(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { return scene.Components().RegionShapes().TryGet(entity); }
    static void Set(kb::scene::Scene& scene, kb::scene::SceneEntity entity, const kb::scene::RegionShapeComponent& value) { scene.Components().RegionShapes().Set(entity, value); }
    [[nodiscard]] static bool Remove(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
        if (!scene.Components().RegionShapes().Has(entity)) return false;
        scene.Components().RegionShapes().Remove(entity);
        return true;
    }
};

template <typename Component>
bool EntityHandle::Has(const kb::scene::Scene& scene) const noexcept {
    return TryGet<Component>(scene) != nullptr;
}

template <typename Component>
const Component* EntityHandle::TryGet(const kb::scene::Scene& scene) const noexcept {
    if (!IsAlive(scene)) {
        return nullptr;
    }
    return ScriptComponentAccess<Component>::TryGet(scene, Entity());
}

template <typename Component>
Component* EntityHandle::TryGet(kb::scene::Scene& scene) const noexcept {
    if (!IsAlive(scene)) {
        return nullptr;
    }
    return ScriptComponentAccess<Component>::TryGet(scene, Entity());
}

template <typename Component>
Result<Component> EntityHandle::GetRequired(const kb::scene::Scene& scene) const {
    const Component* component = TryGet<Component>(scene);
    if (component == nullptr) {
        // LibraryErrorCode has no dedicated "component missing" category
        // (only InvalidHandle/InactiveWorld/UnavailableCapability/
        // Permission/InvalidArgument/Timeout) — InvalidArgument is the
        // closest honest fit: the request (asking for a component this
        // entity does not carry) is what's invalid, not the handle itself
        // (a dead/wrong-scene handle is a DIFFERENT, InvalidHandle,
        // failure — see IsAlive()/CheckError() above).
        return Result<Component>::Fail(ScriptError{
            .code = LibraryErrorCode::InvalidArgument,
            .operation = "EntityHandle.GetRequired",
            .message = "entity does not have the required component",
        });
    }
    return Result<Component>::Ok(*component);
}

template <typename Component>
bool EntityHandle::Add(kb::scene::Scene& scene, const Component& value) const {
    if (!IsAlive(scene)) {
        return false;
    }
    ScriptComponentAccess<Component>::Set(scene, Entity(), value);
    return true;
}

template <typename Component>
bool EntityHandle::Remove(kb::scene::Scene& scene) const noexcept {
    if (!IsAlive(scene)) {
        return false;
    }
    return ScriptComponentAccess<Component>::Remove(scene, Entity());
}

} // namespace kb::library
