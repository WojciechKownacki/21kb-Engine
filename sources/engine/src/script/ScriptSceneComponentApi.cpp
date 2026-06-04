#include "engine/script/ScriptSceneComponentApi.hpp"

#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/SceneBehaviourComponents.hpp"
#include "engine/scene/SceneCameraComponents.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneLightComponents.hpp"
#include "engine/scene/SceneMeshRendererComponents.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/SceneVisibilityComponents.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "engine/scene/VisibilityComponent.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace kb::script {
namespace {

enum class FieldKind {
    Bool,
    Int,
    UInt32,
    Float,
    CameraProjection,
    LightKind,
};

struct FieldBinding {
    std::string_view name;
    FieldKind kind = FieldKind::Float;
    std::size_t offset = 0;
};

struct ComponentAccess {
    const void* immutable = nullptr;
    void* mutableComponent = nullptr;
    std::span<const FieldBinding> fields{};
    void (*markModified)(kb::scene::Scene&, kb::scene::SceneEntity) noexcept = nullptr;
};

template <typename Component, typename Field>
[[nodiscard]] const Field& ReadField(const void* component, std::size_t offset) noexcept {
    return *reinterpret_cast<const Field*>(static_cast<const std::byte*>(component) + offset);
}

template <typename Component, typename Field>
[[nodiscard]] Field& WriteField(void* component, std::size_t offset) noexcept {
    return *reinterpret_cast<Field*>(static_cast<std::byte*>(component) + offset);
}

template <typename Component>
[[nodiscard]] ScriptValue ReadValue(const void* component, const FieldBinding& field) {
    switch (field.kind) {
    case FieldKind::Bool:
        return ScriptValue{ ReadField<Component, bool>(component, field.offset) };
    case FieldKind::Int:
        return ScriptValue{ ReadField<Component, int>(component, field.offset) };
    case FieldKind::UInt32:
        return ScriptValue{ static_cast<int>(ReadField<Component, std::uint32_t>(component, field.offset)) };
    case FieldKind::Float:
        return ScriptValue{ ReadField<Component, float>(component, field.offset) };
    case FieldKind::CameraProjection:
        return ScriptValue{ static_cast<int>(ReadField<Component, kb::scene::CameraProjection>(component, field.offset)) };
    case FieldKind::LightKind:
        return ScriptValue{ static_cast<int>(ReadField<Component, kb::scene::LightKind>(component, field.offset)) };
    }
    return ScriptValue{};
}

template <typename Component>
[[nodiscard]] bool WriteValue(void* component, const FieldBinding& field, const ScriptValue& value) {
    switch (field.kind) {
    case FieldKind::Bool:
        if (value.Type() != ScriptValueType::Bool) {
            return false;
        }
        WriteField<Component, bool>(component, field.offset) = value.AsBool();
        return true;
    case FieldKind::Int:
        if (value.Type() != ScriptValueType::Int) {
            return false;
        }
        WriteField<Component, int>(component, field.offset) = value.AsInt();
        return true;
    case FieldKind::UInt32:
        if (value.Type() != ScriptValueType::Int || value.AsInt() < 0) {
            return false;
        }
        WriteField<Component, std::uint32_t>(component, field.offset) = static_cast<std::uint32_t>(value.AsInt());
        return true;
    case FieldKind::Float:
        if (value.Type() == ScriptValueType::Float) {
            WriteField<Component, float>(component, field.offset) = value.AsFloat();
            return true;
        }
        if (value.Type() == ScriptValueType::Int) {
            WriteField<Component, float>(component, field.offset) = static_cast<float>(value.AsInt());
            return true;
        }
        return false;
    case FieldKind::CameraProjection:
        if (value.Type() != ScriptValueType::Int) {
            return false;
        }
        WriteField<Component, kb::scene::CameraProjection>(component, field.offset) = static_cast<kb::scene::CameraProjection>(value.AsInt());
        return true;
    case FieldKind::LightKind:
        if (value.Type() != ScriptValueType::Int) {
            return false;
        }
        WriteField<Component, kb::scene::LightKind>(component, field.offset) = static_cast<kb::scene::LightKind>(value.AsInt());
        return true;
    }
    return false;
}

#define KB_FIELD(component, field, kind_value) FieldBinding{ #field, kind_value, offsetof(component, field) }
#define KB_NESTED_FIELD(component, parent, field, kind_value) FieldBinding{ #parent "." #field, kind_value, offsetof(component, parent) + offsetof(decltype(std::declval<component>().parent), field) }

constexpr std::array<std::string_view, 6> kComponentNames{
    "Transform",
    "Visibility",
    "Camera",
    "Light",
    "MeshRenderer",
    "Behaviour",
};

constexpr std::array<FieldBinding, 13> kTransformFields{
    KB_NESTED_FIELD(kb::scene::TransformComponent, localPosition, x, FieldKind::Float),
    KB_NESTED_FIELD(kb::scene::TransformComponent, localPosition, y, FieldKind::Float),
    KB_NESTED_FIELD(kb::scene::TransformComponent, localPosition, z, FieldKind::Float),
    KB_NESTED_FIELD(kb::scene::TransformComponent, localRotation, x, FieldKind::Float),
    KB_NESTED_FIELD(kb::scene::TransformComponent, localRotation, y, FieldKind::Float),
    KB_NESTED_FIELD(kb::scene::TransformComponent, localRotation, z, FieldKind::Float),
    KB_NESTED_FIELD(kb::scene::TransformComponent, localRotation, w, FieldKind::Float),
    KB_NESTED_FIELD(kb::scene::TransformComponent, localScale, x, FieldKind::Float),
    KB_NESTED_FIELD(kb::scene::TransformComponent, localScale, y, FieldKind::Float),
    KB_NESTED_FIELD(kb::scene::TransformComponent, localScale, z, FieldKind::Float),
    KB_NESTED_FIELD(kb::scene::TransformComponent, worldPosition, x, FieldKind::Float),
    KB_NESTED_FIELD(kb::scene::TransformComponent, worldPosition, y, FieldKind::Float),
    KB_NESTED_FIELD(kb::scene::TransformComponent, worldPosition, z, FieldKind::Float),
};

constexpr std::array<FieldBinding, 1> kVisibilityFields{
    KB_FIELD(kb::scene::VisibilityComponent, visible, FieldKind::Bool),
};

constexpr std::array<FieldBinding, 6> kCameraFields{
    KB_FIELD(kb::scene::CameraComponent, projection, FieldKind::CameraProjection),
    KB_FIELD(kb::scene::CameraComponent, verticalFovDegrees, FieldKind::Float),
    KB_FIELD(kb::scene::CameraComponent, orthographicHeight, FieldKind::Float),
    KB_FIELD(kb::scene::CameraComponent, nearClip, FieldKind::Float),
    KB_FIELD(kb::scene::CameraComponent, farClip, FieldKind::Float),
    KB_FIELD(kb::scene::CameraComponent, primary, FieldKind::Bool),
};

constexpr std::array<FieldBinding, 11> kLightFields{
    KB_FIELD(kb::scene::LightComponent, kind, FieldKind::LightKind),
    KB_NESTED_FIELD(kb::scene::LightComponent, color, x, FieldKind::Float),
    KB_NESTED_FIELD(kb::scene::LightComponent, color, y, FieldKind::Float),
    KB_NESTED_FIELD(kb::scene::LightComponent, color, z, FieldKind::Float),
    KB_FIELD(kb::scene::LightComponent, intensity, FieldKind::Float),
    KB_FIELD(kb::scene::LightComponent, range, FieldKind::Float),
    KB_FIELD(kb::scene::LightComponent, innerConeDegrees, FieldKind::Float),
    KB_FIELD(kb::scene::LightComponent, outerConeDegrees, FieldKind::Float),
    KB_FIELD(kb::scene::LightComponent, contactShadowLength, FieldKind::Float),
    KB_FIELD(kb::scene::LightComponent, volumetricScattering, FieldKind::Float),
    KB_FIELD(kb::scene::LightComponent, castsShadow, FieldKind::Bool),
};

constexpr std::array<FieldBinding, 3> kMeshRendererFields{
    KB_FIELD(kb::scene::MeshRendererComponent, materialSlotOverrideCount, FieldKind::UInt32),
    KB_FIELD(kb::scene::MeshRendererComponent, castsShadow, FieldKind::Bool),
    KB_FIELD(kb::scene::MeshRendererComponent, receivesShadow, FieldKind::Bool),
};

constexpr std::array<FieldBinding, 1> kBehaviourFields{
    KB_FIELD(kb::scene::BehaviourComponent, enabled, FieldKind::Bool),
};

#undef KB_NESTED_FIELD
#undef KB_FIELD

[[nodiscard]] const FieldBinding* FindField(std::span<const FieldBinding> fields, std::string_view name) noexcept {
    for (const FieldBinding& field : fields) {
        if (field.name == name) {
            return &field;
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
    return {};
}

[[nodiscard]] ScriptValue ReadByComponent(std::string_view componentName, const void* component, const FieldBinding& field) {
    if (componentName == "Transform") {
        return ReadValue<kb::scene::TransformComponent>(component, field);
    }
    if (componentName == "Visibility") {
        return ReadValue<kb::scene::VisibilityComponent>(component, field);
    }
    if (componentName == "Camera") {
        return ReadValue<kb::scene::CameraComponent>(component, field);
    }
    if (componentName == "Light") {
        return ReadValue<kb::scene::LightComponent>(component, field);
    }
    if (componentName == "MeshRenderer") {
        return ReadValue<kb::scene::MeshRendererComponent>(component, field);
    }
    if (componentName == "Behaviour") {
        return ReadValue<kb::scene::BehaviourComponent>(component, field);
    }
    return ScriptValue{};
}

[[nodiscard]] bool WriteByComponent(std::string_view componentName, void* component, const FieldBinding& field, const ScriptValue& value) {
    if (componentName == "Transform") {
        return WriteValue<kb::scene::TransformComponent>(component, field, value);
    }
    if (componentName == "Visibility") {
        return WriteValue<kb::scene::VisibilityComponent>(component, field, value);
    }
    if (componentName == "Camera") {
        return WriteValue<kb::scene::CameraComponent>(component, field, value);
    }
    if (componentName == "Light") {
        return WriteValue<kb::scene::LightComponent>(component, field, value);
    }
    if (componentName == "MeshRenderer") {
        return WriteValue<kb::scene::MeshRendererComponent>(component, field, value);
    }
    if (componentName == "Behaviour") {
        return WriteValue<kb::scene::BehaviourComponent>(component, field, value);
    }
    return false;
}

} // namespace

std::span<const std::string_view> ScriptSceneComponentApi::ComponentNames() noexcept {
    return kComponentNames;
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
        .value = ReadByComponent(componentName, component.immutable, *field),
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

    if (!WriteByComponent(componentName, component.mutableComponent, *field, value)) {
        return ScriptSceneComponentMutationResult{ .error = "script value type does not match component property" };
    }

    if (component.markModified != nullptr) {
        component.markModified(scene, entity);
    }
    return ScriptSceneComponentMutationResult{ .succeeded = true };
}

} // namespace kb::script
