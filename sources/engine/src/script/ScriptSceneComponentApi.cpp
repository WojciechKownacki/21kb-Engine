#include "engine/script/ScriptSceneComponentApi.hpp"

#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/CharacterControllerComponent.hpp"
#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/JointComponent.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/RigidbodyComponent.hpp"
#include "engine/scene/SceneBehaviourComponents.hpp"
#include "engine/scene/SceneCameraComponents.hpp"
#include "engine/scene/SceneCharacterControllerComponents.hpp"
#include "engine/scene/SceneColliderComponents.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneJointComponents.hpp"
#include "engine/scene/SceneLightComponents.hpp"
#include "engine/scene/SceneMeshRendererComponents.hpp"
#include "engine/scene/SceneRigidbodyComponents.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/SceneVisibilityComponents.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "engine/scene/VisibilityComponent.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace kb::script {
namespace {

// LIB-082: every KB_* macro below asserts, at the point the field is
// registered, that the field's own declared type is not a raw pointer —
// this is what actually flows into the ScriptValue the read lambda
// constructs. ScriptValue::Storage itself already static_asserts (LIB-032)
// that none of its variant ALTERNATIVES is a pointer type, which forecloses
// storing one directly, but it cannot see what an individual field's C++
// type is before a macro reads it — this assert closes that gap at the one
// place a pointer-typed field would first be touched, so accidentally
// wiring up a pointer/handle-as-address field is a compile error here, not
// a silent runtime leak to Lua/VisualGraph.
#define KB_ASSERT_NOT_POINTER(expr) \
    static_assert(!std::is_pointer_v<decltype(expr)>, "kb::script field accessor must not expose a raw pointer to Lua/VisualGraph (LIB-082)")

// LIB-077: each FieldBinding carries a REAL, per-field accessor function
// pair — not an offsetof + reinterpret_cast<Field*> pointer walk (the
// pre-LIB-077 mechanism this replaces). `read`/`write` are non-capturing
// lambdas generated one per field by the KB_* macros below, each of which
// names the field directly (`static_cast<const Component*>(component)->
// field`) so the compiler checks the field actually exists and has the
// type the macro claims — a copy-paste that pairs the wrong macro with a
// field now fails to compile (wrong member access) instead of silently
// reinterpreting the wrong bytes at runtime, which is what a
// mismatched-but-still-offsetof-computable {kind, offset} pair could do
// before. The `void*` here is still untyped — required so one FieldBinding
// table works across the generic ComponentAccess/GetProperty/SetProperty
// machinery below for any of the 6 registered components — but the cast
// back to the concrete Component type happens ONCE, inside the generated
// lambda that also performs the real field access, not in a separate,
// disconnected helper trusted to receive the right type.
struct FieldBinding {
    std::string_view name;
    ScriptValue (*read)(const void* component) noexcept = nullptr;
    bool (*write)(void* component, const ScriptValue& value) noexcept = nullptr;
};

struct ComponentAccess {
    const void* immutable = nullptr;
    void* mutableComponent = nullptr;
    std::span<const FieldBinding> fields{};
    void (*markModified)(kb::scene::Scene&, kb::scene::SceneEntity) noexcept = nullptr;
};

// clang-format off
#define KB_BOOL(Component, field) \
    FieldBinding{ #field, \
        [](const void* component) noexcept -> ScriptValue { \
            KB_ASSERT_NOT_POINTER(static_cast<const Component*>(component)->field); \
            return ScriptValue{ static_cast<const Component*>(component)->field }; }, \
        [](void* component, const ScriptValue& value) noexcept -> bool { \
            if (value.Type() != ScriptValueType::Bool) { return false; } \
            static_cast<Component*>(component)->field = value.AsBool(); \
            return true; \
        } }

#define KB_INT(Component, field) \
    FieldBinding{ #field, \
        [](const void* component) noexcept -> ScriptValue { \
            KB_ASSERT_NOT_POINTER(static_cast<const Component*>(component)->field); \
            return ScriptValue{ static_cast<const Component*>(component)->field }; }, \
        [](void* component, const ScriptValue& value) noexcept -> bool { \
            if (value.Type() != ScriptValueType::Int) { return false; } \
            static_cast<Component*>(component)->field = value.AsInt(); \
            return true; \
        } }

#define KB_UINT32(Component, field) \
    FieldBinding{ #field, \
        [](const void* component) noexcept -> ScriptValue { \
            KB_ASSERT_NOT_POINTER(static_cast<const Component*>(component)->field); \
            return ScriptValue{ static_cast<int>(static_cast<const Component*>(component)->field) }; }, \
        [](void* component, const ScriptValue& value) noexcept -> bool { \
            if (value.Type() != ScriptValueType::Int || value.AsInt() < 0) { return false; } \
            static_cast<Component*>(component)->field = static_cast<std::uint32_t>(value.AsInt()); \
            return true; \
        } }

#define KB_FLOAT(Component, field) \
    FieldBinding{ #field, \
        [](const void* component) noexcept -> ScriptValue { \
            KB_ASSERT_NOT_POINTER(static_cast<const Component*>(component)->field); \
            return ScriptValue{ static_cast<const Component*>(component)->field }; }, \
        [](void* component, const ScriptValue& value) noexcept -> bool { \
            if (value.Type() == ScriptValueType::Float) { static_cast<Component*>(component)->field = value.AsFloat(); return true; } \
            if (value.Type() == ScriptValueType::Int) { static_cast<Component*>(component)->field = static_cast<float>(value.AsInt()); return true; } \
            return false; \
        } }

#define KB_NESTED_FLOAT(Component, parent, field) \
    FieldBinding{ #parent "." #field, \
        [](const void* component) noexcept -> ScriptValue { \
            KB_ASSERT_NOT_POINTER(static_cast<const Component*>(component)->parent.field); \
            return ScriptValue{ static_cast<const Component*>(component)->parent.field }; }, \
        [](void* component, const ScriptValue& value) noexcept -> bool { \
            if (value.Type() == ScriptValueType::Float) { static_cast<Component*>(component)->parent.field = value.AsFloat(); return true; } \
            if (value.Type() == ScriptValueType::Int) { static_cast<Component*>(component)->parent.field = static_cast<float>(value.AsInt()); return true; } \
            return false; \
        } }

#define KB_TICKGROUP(Component, field) \
    FieldBinding{ #field, \
        [](const void* component) noexcept -> ScriptValue { \
            KB_ASSERT_NOT_POINTER(static_cast<const Component*>(component)->field); \
            return ScriptValue{ static_cast<int>(static_cast<const Component*>(component)->field) }; }, \
        [](void* component, const ScriptValue& value) noexcept -> bool { \
            if (value.Type() != ScriptValueType::Int || value.AsInt() < 0 || value.AsInt() > static_cast<int>(kb::scene::BehaviourTickGroup::Presentation)) { return false; } \
            static_cast<Component*>(component)->field = static_cast<kb::scene::BehaviourTickGroup>(value.AsInt()); \
            return true; \
        } }

#define KB_CAMERA_PROJECTION(Component, field) \
    FieldBinding{ #field, \
        [](const void* component) noexcept -> ScriptValue { \
            KB_ASSERT_NOT_POINTER(static_cast<const Component*>(component)->field); \
            return ScriptValue{ static_cast<int>(static_cast<const Component*>(component)->field) }; }, \
        [](void* component, const ScriptValue& value) noexcept -> bool { \
            if (value.Type() != ScriptValueType::Int) { return false; } \
            static_cast<Component*>(component)->field = static_cast<kb::scene::CameraProjection>(value.AsInt()); \
            return true; \
        } }

#define KB_LIGHT_KIND(Component, field) \
    FieldBinding{ #field, \
        [](const void* component) noexcept -> ScriptValue { \
            KB_ASSERT_NOT_POINTER(static_cast<const Component*>(component)->field); \
            return ScriptValue{ static_cast<int>(static_cast<const Component*>(component)->field) }; }, \
        [](void* component, const ScriptValue& value) noexcept -> bool { \
            if (value.Type() != ScriptValueType::Int) { return false; } \
            static_cast<Component*>(component)->field = static_cast<kb::scene::LightKind>(value.AsInt()); \
            return true; \
        } }

#define KB_RIGIDBODY_BODY_TYPE(Component, field) \
    FieldBinding{ #field, \
        [](const void* component) noexcept -> ScriptValue { \
            KB_ASSERT_NOT_POINTER(static_cast<const Component*>(component)->field); \
            return ScriptValue{ static_cast<int>(static_cast<const Component*>(component)->field) }; }, \
        [](void* component, const ScriptValue& value) noexcept -> bool { \
            if (value.Type() != ScriptValueType::Int || value.AsInt() < 0 || value.AsInt() > static_cast<int>(kb::scene::RigidbodyBodyType::Kinematic)) { return false; } \
            static_cast<Component*>(component)->field = static_cast<kb::scene::RigidbodyBodyType>(value.AsInt()); \
            return true; \
        } }

#define KB_COLLIDER_SHAPE(Component, field) \
    FieldBinding{ #field, \
        [](const void* component) noexcept -> ScriptValue { \
            KB_ASSERT_NOT_POINTER(static_cast<const Component*>(component)->field); \
            return ScriptValue{ static_cast<int>(static_cast<const Component*>(component)->field) }; }, \
        [](void* component, const ScriptValue& value) noexcept -> bool { \
            if (value.Type() != ScriptValueType::Int || value.AsInt() < 0 || value.AsInt() > static_cast<int>(kb::scene::ColliderShape::Capsule)) { return false; } \
            static_cast<Component*>(component)->field = static_cast<kb::scene::ColliderShape>(value.AsInt()); \
            return true; \
        } }

#define KB_JOINT_TYPE(Component, field) \
    FieldBinding{ #field, \
        [](const void* component) noexcept -> ScriptValue { \
            KB_ASSERT_NOT_POINTER(static_cast<const Component*>(component)->field); \
            return ScriptValue{ static_cast<int>(static_cast<const Component*>(component)->field) }; }, \
        [](void* component, const ScriptValue& value) noexcept -> bool { \
            if (value.Type() != ScriptValueType::Int || value.AsInt() < 0 || value.AsInt() > static_cast<int>(kb::scene::JointType::Point)) { return false; } \
            static_cast<Component*>(component)->field = static_cast<kb::scene::JointType>(value.AsInt()); \
            return true; \
        } }

// clang-format on

constexpr std::array<std::string_view, 10> kComponentNames{
    "Transform",
    "Visibility",
    "Camera",
    "Light",
    "MeshRenderer",
    "Behaviour",
    "Rigidbody",
    "Collider",
    "CharacterController",
    "Joint",
};

constexpr std::array<ScriptSceneComponentPropertyDesc, 13> kTransformPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "localPosition.x", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "localPosition.y", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "localPosition.z", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "localRotation.x", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "localRotation.y", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "localRotation.z", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "localRotation.w", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "localScale.x", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "localScale.y", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "localScale.z", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "worldPosition.x", ScriptValueType::Float, false },
    ScriptSceneComponentPropertyDesc{ "worldPosition.y", ScriptValueType::Float, false },
    ScriptSceneComponentPropertyDesc{ "worldPosition.z", ScriptValueType::Float, false },
};

constexpr std::array<ScriptSceneComponentPropertyDesc, 1> kVisibilityPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "visible", ScriptValueType::Bool },
};

constexpr std::array<ScriptSceneComponentPropertyDesc, 6> kCameraPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "projection", ScriptValueType::Int },
    ScriptSceneComponentPropertyDesc{ "verticalFovDegrees", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "orthographicHeight", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "nearClip", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "farClip", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "primary", ScriptValueType::Bool },
};

constexpr std::array<ScriptSceneComponentPropertyDesc, 11> kLightPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "kind", ScriptValueType::Int },
    ScriptSceneComponentPropertyDesc{ "color.x", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "color.y", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "color.z", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "intensity", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "range", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "innerConeDegrees", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "outerConeDegrees", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "contactShadowLength", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "volumetricScattering", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "castsShadow", ScriptValueType::Bool },
};

constexpr std::array<ScriptSceneComponentPropertyDesc, 3> kMeshRendererPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "materialSlotOverrideCount", ScriptValueType::Int },
    ScriptSceneComponentPropertyDesc{ "castsShadow", ScriptValueType::Bool },
    ScriptSceneComponentPropertyDesc{ "receivesShadow", ScriptValueType::Bool },
};

constexpr std::array<ScriptSceneComponentPropertyDesc, 3> kBehaviourPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "enabled", ScriptValueType::Bool },
    ScriptSceneComponentPropertyDesc{ "tickGroup", ScriptValueType::Int },
    ScriptSceneComponentPropertyDesc{ "executionOrder", ScriptValueType::Int },
};

constexpr std::array<ScriptSceneComponentPropertyDesc, 11> kRigidbodyPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "bodyType", ScriptValueType::Int },
    ScriptSceneComponentPropertyDesc{ "mass", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "linearVelocity.x", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "linearVelocity.y", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "linearVelocity.z", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "angularVelocity.x", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "angularVelocity.y", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "angularVelocity.z", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "gravityScale", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "useGravity", ScriptValueType::Bool },
    ScriptSceneComponentPropertyDesc{ "lockRotation", ScriptValueType::Bool },
};

constexpr std::array<ScriptSceneComponentPropertyDesc, 13> kColliderPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "shape", ScriptValueType::Int },
    ScriptSceneComponentPropertyDesc{ "center.x", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "center.y", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "center.z", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "boxSize.x", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "boxSize.y", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "boxSize.z", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "radius", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "height", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "trigger", ScriptValueType::Bool },
    ScriptSceneComponentPropertyDesc{ "friction", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "restitution", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "layer", ScriptValueType::Int },
};

constexpr std::array<ScriptSceneComponentPropertyDesc, 5> kCharacterControllerPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "center.x", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "center.y", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "center.z", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "radius", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "height", ScriptValueType::Float },
};

// LIB-082: connectedEntity is deliberately NOT in this table - Script
// SceneComponentApi's generic FieldBinding property mechanism is audited
// (RunScriptSceneComponentPropertiesNeverExposeRawPointerTest) to expose
// ONLY Bool/Int/Float, since a wider ScriptValueType (Entity included) is
// capable of carrying a full 64-bit value that could, in principle, encode
// a raw pointer's bit pattern. connectedEntity is still fully real and
// script-addressable through native kb::library::EntityHandle::Add<
// JointComponent>/TryGet<JointComponent> (the whole struct, field access is
// plain C++, not this string-keyed mechanism) - see
// RunEntityHandlePhysicsComponentAccessTest.
constexpr std::array<ScriptSceneComponentPropertyDesc, 13> kJointPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "type", ScriptValueType::Int },
    ScriptSceneComponentPropertyDesc{ "anchor.x", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "anchor.y", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "anchor.z", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "connectedAnchor.x", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "connectedAnchor.y", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "connectedAnchor.z", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "axis.x", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "axis.y", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "axis.z", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "minLimit", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "maxLimit", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "enableLimit", ScriptValueType::Bool },
};

constexpr std::array<FieldBinding, 13> kTransformFields{
    KB_NESTED_FLOAT(kb::scene::TransformComponent, localPosition, x),
    KB_NESTED_FLOAT(kb::scene::TransformComponent, localPosition, y),
    KB_NESTED_FLOAT(kb::scene::TransformComponent, localPosition, z),
    KB_NESTED_FLOAT(kb::scene::TransformComponent, localRotation, x),
    KB_NESTED_FLOAT(kb::scene::TransformComponent, localRotation, y),
    KB_NESTED_FLOAT(kb::scene::TransformComponent, localRotation, z),
    KB_NESTED_FLOAT(kb::scene::TransformComponent, localRotation, w),
    KB_NESTED_FLOAT(kb::scene::TransformComponent, localScale, x),
    KB_NESTED_FLOAT(kb::scene::TransformComponent, localScale, y),
    KB_NESTED_FLOAT(kb::scene::TransformComponent, localScale, z),
    KB_NESTED_FLOAT(kb::scene::TransformComponent, worldPosition, x),
    KB_NESTED_FLOAT(kb::scene::TransformComponent, worldPosition, y),
    KB_NESTED_FLOAT(kb::scene::TransformComponent, worldPosition, z),
};

constexpr std::array<FieldBinding, 1> kVisibilityFields{
    KB_BOOL(kb::scene::VisibilityComponent, visible),
};

constexpr std::array<FieldBinding, 6> kCameraFields{
    KB_CAMERA_PROJECTION(kb::scene::CameraComponent, projection),
    KB_FLOAT(kb::scene::CameraComponent, verticalFovDegrees),
    KB_FLOAT(kb::scene::CameraComponent, orthographicHeight),
    KB_FLOAT(kb::scene::CameraComponent, nearClip),
    KB_FLOAT(kb::scene::CameraComponent, farClip),
    KB_BOOL(kb::scene::CameraComponent, primary),
};

constexpr std::array<FieldBinding, 11> kLightFields{
    KB_LIGHT_KIND(kb::scene::LightComponent, kind),
    KB_NESTED_FLOAT(kb::scene::LightComponent, color, x),
    KB_NESTED_FLOAT(kb::scene::LightComponent, color, y),
    KB_NESTED_FLOAT(kb::scene::LightComponent, color, z),
    KB_FLOAT(kb::scene::LightComponent, intensity),
    KB_FLOAT(kb::scene::LightComponent, range),
    KB_FLOAT(kb::scene::LightComponent, innerConeDegrees),
    KB_FLOAT(kb::scene::LightComponent, outerConeDegrees),
    KB_FLOAT(kb::scene::LightComponent, contactShadowLength),
    KB_FLOAT(kb::scene::LightComponent, volumetricScattering),
    KB_BOOL(kb::scene::LightComponent, castsShadow),
};

constexpr std::array<FieldBinding, 3> kMeshRendererFields{
    KB_UINT32(kb::scene::MeshRendererComponent, materialSlotOverrideCount),
    KB_BOOL(kb::scene::MeshRendererComponent, castsShadow),
    KB_BOOL(kb::scene::MeshRendererComponent, receivesShadow),
};

constexpr std::array<FieldBinding, 3> kBehaviourFields{
    KB_BOOL(kb::scene::BehaviourComponent, enabled),
    KB_TICKGROUP(kb::scene::BehaviourComponent, tickGroup),
    KB_INT(kb::scene::BehaviourComponent, executionOrder),
};

constexpr std::array<FieldBinding, 11> kRigidbodyFields{
    KB_RIGIDBODY_BODY_TYPE(kb::scene::RigidbodyComponent, bodyType),
    KB_FLOAT(kb::scene::RigidbodyComponent, mass),
    KB_NESTED_FLOAT(kb::scene::RigidbodyComponent, linearVelocity, x),
    KB_NESTED_FLOAT(kb::scene::RigidbodyComponent, linearVelocity, y),
    KB_NESTED_FLOAT(kb::scene::RigidbodyComponent, linearVelocity, z),
    KB_NESTED_FLOAT(kb::scene::RigidbodyComponent, angularVelocity, x),
    KB_NESTED_FLOAT(kb::scene::RigidbodyComponent, angularVelocity, y),
    KB_NESTED_FLOAT(kb::scene::RigidbodyComponent, angularVelocity, z),
    KB_FLOAT(kb::scene::RigidbodyComponent, gravityScale),
    KB_BOOL(kb::scene::RigidbodyComponent, useGravity),
    KB_BOOL(kb::scene::RigidbodyComponent, lockRotation),
};

constexpr std::array<FieldBinding, 13> kColliderFields{
    KB_COLLIDER_SHAPE(kb::scene::ColliderComponent, shape),
    KB_NESTED_FLOAT(kb::scene::ColliderComponent, center, x),
    KB_NESTED_FLOAT(kb::scene::ColliderComponent, center, y),
    KB_NESTED_FLOAT(kb::scene::ColliderComponent, center, z),
    KB_NESTED_FLOAT(kb::scene::ColliderComponent, boxSize, x),
    KB_NESTED_FLOAT(kb::scene::ColliderComponent, boxSize, y),
    KB_NESTED_FLOAT(kb::scene::ColliderComponent, boxSize, z),
    KB_FLOAT(kb::scene::ColliderComponent, radius),
    KB_FLOAT(kb::scene::ColliderComponent, height),
    KB_BOOL(kb::scene::ColliderComponent, trigger),
    KB_FLOAT(kb::scene::ColliderComponent, friction),
    KB_FLOAT(kb::scene::ColliderComponent, restitution),
    KB_UINT32(kb::scene::ColliderComponent, layer),
};

constexpr std::array<FieldBinding, 5> kCharacterControllerFields{
    KB_NESTED_FLOAT(kb::scene::CharacterControllerComponent, center, x),
    KB_NESTED_FLOAT(kb::scene::CharacterControllerComponent, center, y),
    KB_NESTED_FLOAT(kb::scene::CharacterControllerComponent, center, z),
    KB_FLOAT(kb::scene::CharacterControllerComponent, radius),
    KB_FLOAT(kb::scene::CharacterControllerComponent, height),
};

constexpr std::array<FieldBinding, 13> kJointFields{
    KB_JOINT_TYPE(kb::scene::JointComponent, type),
    KB_NESTED_FLOAT(kb::scene::JointComponent, anchor, x),
    KB_NESTED_FLOAT(kb::scene::JointComponent, anchor, y),
    KB_NESTED_FLOAT(kb::scene::JointComponent, anchor, z),
    KB_NESTED_FLOAT(kb::scene::JointComponent, connectedAnchor, x),
    KB_NESTED_FLOAT(kb::scene::JointComponent, connectedAnchor, y),
    KB_NESTED_FLOAT(kb::scene::JointComponent, connectedAnchor, z),
    KB_NESTED_FLOAT(kb::scene::JointComponent, axis, x),
    KB_NESTED_FLOAT(kb::scene::JointComponent, axis, y),
    KB_NESTED_FLOAT(kb::scene::JointComponent, axis, z),
    KB_FLOAT(kb::scene::JointComponent, minLimit),
    KB_FLOAT(kb::scene::JointComponent, maxLimit),
    KB_BOOL(kb::scene::JointComponent, enableLimit),
};

#undef KB_BOOL
#undef KB_INT
#undef KB_UINT32
#undef KB_FLOAT
#undef KB_NESTED_FLOAT
#undef KB_TICKGROUP
#undef KB_CAMERA_PROJECTION
#undef KB_LIGHT_KIND
#undef KB_RIGIDBODY_BODY_TYPE
#undef KB_COLLIDER_SHAPE
#undef KB_JOINT_TYPE
#undef KB_ASSERT_NOT_POINTER

[[nodiscard]] const FieldBinding* FindField(std::span<const FieldBinding> fields, std::string_view name) noexcept {
    for (const FieldBinding& field : fields) {
        if (field.name == name) {
            return &field;
        }
    }
    return nullptr;
}

[[nodiscard]] const ScriptSceneComponentPropertyDesc* FindPropertyDesc(std::span<const ScriptSceneComponentPropertyDesc> properties, std::string_view name) noexcept {
    for (const ScriptSceneComponentPropertyDesc& property : properties) {
        if (property.name == name) {
            return &property;
        }
    }
    return nullptr;
}

void MarkTransformModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
    scene.Transforms().MarkModified(entity);
}

void MarkVisibilityModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
    scene.Components().Visibility().MarkModified(entity);
}

void MarkCameraModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
    scene.Components().Cameras().MarkModified(entity);
}

void MarkLightModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
    scene.Components().Lights().MarkModified(entity);
}

void MarkMeshRendererModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
    scene.Components().MeshRenderers().MarkModified(entity);
}

void MarkBehaviourModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
    scene.Components().Behaviours().MarkModified(entity);
}

void MarkRigidbodyModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
    scene.Components().Rigidbodies().MarkModified(entity);
}

void MarkColliderModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
    scene.Components().Colliders().MarkModified(entity);
}

void MarkCharacterControllerModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
    scene.Components().CharacterControllers().MarkModified(entity);
}

void MarkJointModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept {
    scene.Components().Joints().MarkModified(entity);
}

[[nodiscard]] ComponentAccess AccessComponent(kb::scene::Scene& scene, kb::scene::SceneEntity entity, std::string_view componentName) noexcept {
    if (!entity.IsValid() || !scene.Entities().IsAlive(entity)) {
        return {};
    }
    if (componentName == "Transform") {
        kb::scene::TransformComponent* component = scene.Transforms().TryGet(entity);
        return ComponentAccess{ component, component, kTransformFields, &MarkTransformModified };
    }
    if (componentName == "Visibility") {
        kb::scene::VisibilityComponent* component = scene.Components().Visibility().TryGet(entity);
        return ComponentAccess{ component, component, kVisibilityFields, &MarkVisibilityModified };
    }
    if (componentName == "Camera") {
        kb::scene::CameraComponent* component = scene.Components().Cameras().TryGet(entity);
        return ComponentAccess{ component, component, kCameraFields, &MarkCameraModified };
    }
    if (componentName == "Light") {
        kb::scene::LightComponent* component = scene.Components().Lights().TryGet(entity);
        return ComponentAccess{ component, component, kLightFields, &MarkLightModified };
    }
    if (componentName == "MeshRenderer") {
        kb::scene::MeshRendererComponent* component = scene.Components().MeshRenderers().TryGet(entity);
        return ComponentAccess{ component, component, kMeshRendererFields, &MarkMeshRendererModified };
    }
    if (componentName == "Behaviour") {
        kb::scene::BehaviourComponent* component = scene.Components().Behaviours().TryGet(entity);
        return ComponentAccess{ component, component, kBehaviourFields, &MarkBehaviourModified };
    }
    if (componentName == "Rigidbody") {
        kb::scene::RigidbodyComponent* component = scene.Components().Rigidbodies().TryGet(entity);
        return ComponentAccess{ component, component, kRigidbodyFields, &MarkRigidbodyModified };
    }
    if (componentName == "Collider") {
        kb::scene::ColliderComponent* component = scene.Components().Colliders().TryGet(entity);
        return ComponentAccess{ component, component, kColliderFields, &MarkColliderModified };
    }
    if (componentName == "CharacterController") {
        kb::scene::CharacterControllerComponent* component = scene.Components().CharacterControllers().TryGet(entity);
        return ComponentAccess{ component, component, kCharacterControllerFields, &MarkCharacterControllerModified };
    }
    if (componentName == "Joint") {
        kb::scene::JointComponent* component = scene.Components().Joints().TryGet(entity);
        return ComponentAccess{ component, component, kJointFields, &MarkJointModified };
    }
    return {};
}

} // namespace

std::span<const std::string_view> ScriptSceneComponentApi::ComponentNames() noexcept {
    return kComponentNames;
}

std::span<const ScriptSceneComponentPropertyDesc> ScriptSceneComponentApi::ComponentProperties(std::string_view componentName) noexcept {
    if (componentName == "Transform") {
        return kTransformPropertyDescs;
    }
    if (componentName == "Visibility") {
        return kVisibilityPropertyDescs;
    }
    if (componentName == "Camera") {
        return kCameraPropertyDescs;
    }
    if (componentName == "Light") {
        return kLightPropertyDescs;
    }
    if (componentName == "MeshRenderer") {
        return kMeshRendererPropertyDescs;
    }
    if (componentName == "Behaviour") {
        return kBehaviourPropertyDescs;
    }
    if (componentName == "Rigidbody") {
        return kRigidbodyPropertyDescs;
    }
    if (componentName == "Collider") {
        return kColliderPropertyDescs;
    }
    if (componentName == "CharacterController") {
        return kCharacterControllerPropertyDescs;
    }
    if (componentName == "Joint") {
        return kJointPropertyDescs;
    }
    return {};
}

bool ScriptSceneComponentApi::HasComponent(kb::scene::Scene& scene, kb::scene::SceneEntity entity, std::string_view componentName) noexcept {
    return AccessComponent(scene, entity, componentName).immutable != nullptr;
}

ScriptSceneComponentPropertyResult ScriptSceneComponentApi::GetProperty(
    kb::scene::Scene& scene,
    kb::scene::SceneEntity entity,
    std::string_view componentName,
    std::string_view propertyName) {
    const ComponentAccess component = AccessComponent(scene, entity, componentName);
    if (component.immutable == nullptr) {
        return ScriptSceneComponentPropertyResult{ .error = "component is not present on entity" };
    }

    const FieldBinding* field = FindField(component.fields, propertyName);
    if (field == nullptr) {
        return ScriptSceneComponentPropertyResult{ .error = "component property is not registered for scripts" };
    }

    return ScriptSceneComponentPropertyResult{
        .succeeded = true,
        .value = field->read(component.immutable),
        .error = {},
    };
}

ScriptSceneComponentMutationResult ScriptSceneComponentApi::SetProperty(
    kb::scene::Scene& scene,
    kb::scene::SceneEntity entity,
    std::string_view componentName,
    std::string_view propertyName,
    const ScriptValue& value) {
    const ComponentAccess component = AccessComponent(scene, entity, componentName);
    if (component.mutableComponent == nullptr) {
        return ScriptSceneComponentMutationResult{ .error = "component is not present on entity" };
    }

    const FieldBinding* field = FindField(component.fields, propertyName);
    if (field == nullptr) {
        return ScriptSceneComponentMutationResult{ .error = "component property is not registered for scripts" };
    }
    const ScriptSceneComponentPropertyDesc* property = FindPropertyDesc(ComponentProperties(componentName), propertyName);
    if (property != nullptr && !property->writable) {
        return ScriptSceneComponentMutationResult{ .error = "component property is read-only for scripts" };
    }

    if (!field->write(component.mutableComponent, value)) {
        return ScriptSceneComponentMutationResult{ .error = "script value type does not match component property" };
    }

    if (component.markModified != nullptr) {
        component.markModified(scene, entity);
    }
    return ScriptSceneComponentMutationResult{ .succeeded = true };
}

} // namespace kb::script
