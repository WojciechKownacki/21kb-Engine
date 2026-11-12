#include "inspection/InspectorComponentDefaults.hpp"
#include "engine/scene/AudioSourceComponent.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/ParticleEffectComponent.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/SceneUIComponentSet.hpp"
#include "engine/ui/UIComponentPropertyCatalog.hpp"
#include "inspection/InspectorGenericComponentModel.hpp"
#include "inspection/ui/InspectorUIComponentModel.hpp"
#include "inspection/InspectorPhysicsModel.hpp"
#include "engine/scene/SceneComponentAuthoring.hpp"

#include <type_traits>
#include <variant>

namespace kb::editor {
namespace {

using P = InspectorPropertyId;

template <typename T>
    requires(std::is_arithmetic_v<T> || std::is_enum_v<T>)
[[nodiscard]] bool Same(const T& lhs, const T& rhs) noexcept {
    return lhs == rhs;
}

[[nodiscard]] bool Same(const kb::scene::Vec3& lhs, const kb::scene::Vec3& rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

[[nodiscard]] bool Same(const kb::scene::Quat& lhs, const kb::scene::Quat& rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z && lhs.w == rhs.w;
}

// One value of a component: its Inspector property, how to tell it differs from the default and how to put it back.
template <typename Component>
struct Field {
    InspectorPropertyId property;
    bool (*differs)(const Component&, const Component&);
    void (*reset)(Component&, const Component&);
};

#define KB_FIELD(Component, property, member)                                                                  \
    Field<Component> {                                                                                         \
        property, [](const Component& now, const Component& fresh) { return !Same(now.member, fresh.member); }, \
            [](Component& now, const Component& fresh) { now.member = fresh.member; }                          \
    }

// The components as Add Component creates them. A fresh camera is the primary one (the scene's first camera is
// what plays); the test suite checks that every component added by the editor has no changed field.
[[nodiscard]] kb::scene::CameraComponent FreshCamera() noexcept { return kb::scene::CameraComponent{ .primary = true }; }

const Field<kb::scene::CameraComponent> kCameraFields[] = {
    KB_FIELD(kb::scene::CameraComponent, P::CameraProjection, projection),
    KB_FIELD(kb::scene::CameraComponent, P::CameraVerticalFov, verticalFovDegrees),
    KB_FIELD(kb::scene::CameraComponent, P::CameraOrthographicHeight, orthographicHeight),
    KB_FIELD(kb::scene::CameraComponent, P::CameraNearClip, nearClip),
    KB_FIELD(kb::scene::CameraComponent, P::CameraFarClip, farClip),
    KB_FIELD(kb::scene::CameraComponent, P::CameraPrimary, primary),
    KB_FIELD(kb::scene::CameraComponent, P::CameraViewportId, viewportId),
    KB_FIELD(kb::scene::CameraComponent, P::CameraPriority, priority),
    KB_FIELD(kb::scene::CameraComponent, P::CameraCullingMask, cullingMask),
    KB_FIELD(kb::scene::CameraComponent, P::CameraClearMode, clearMode),
    KB_FIELD(kb::scene::CameraComponent, P::CameraClearColor, clearColor),
};

const Field<kb::scene::LightComponent> kLightFields[] = {
    KB_FIELD(kb::scene::LightComponent, P::LightKind, kind),
    KB_FIELD(kb::scene::LightComponent, P::LightColor, color),
    KB_FIELD(kb::scene::LightComponent, P::LightIntensity, intensity),
    KB_FIELD(kb::scene::LightComponent, P::LightRange, range),
    KB_FIELD(kb::scene::LightComponent, P::LightInnerCone, innerConeDegrees),
    KB_FIELD(kb::scene::LightComponent, P::LightOuterCone, outerConeDegrees),
    KB_FIELD(kb::scene::LightComponent, P::LightAreaWidth, areaWidth),
    KB_FIELD(kb::scene::LightComponent, P::LightAreaHeight, areaHeight),
    KB_FIELD(kb::scene::LightComponent, P::LightContactShadowLength, contactShadowLength),
    KB_FIELD(kb::scene::LightComponent, P::LightVolumetricScattering, volumetricScattering),
    KB_FIELD(kb::scene::LightComponent, P::LightCastsShadow, castsShadow),
    KB_FIELD(kb::scene::LightComponent, P::LightUseColorTemperature, useColorTemperature),
    KB_FIELD(kb::scene::LightComponent, P::LightColorTemperatureKelvin, colorTemperatureKelvin),
    KB_FIELD(kb::scene::LightComponent, P::LightLayerMask, layerMask),
    KB_FIELD(kb::scene::LightComponent, P::LightMobility, mobility),
    KB_FIELD(kb::scene::LightComponent, P::LightSourceRadius, sourceRadius),
    KB_FIELD(kb::scene::LightComponent, P::LightShadowStrength, shadowStrength),
    KB_FIELD(kb::scene::LightComponent, P::LightShadowDistance, shadowDistance),
    KB_FIELD(kb::scene::LightComponent, P::LightShadowBias, shadowBias),
    KB_FIELD(kb::scene::LightComponent, P::LightShadowNormalBias, shadowNormalBias),
    KB_FIELD(kb::scene::LightComponent, P::LightShadowFilter, shadowFilter),
    KB_FIELD(kb::scene::LightComponent, P::LightShadowQuality, shadowQuality),
    KB_FIELD(kb::scene::LightComponent, P::LightShadowCascadeCount, shadowCascadeCount),
    KB_FIELD(kb::scene::LightComponent, P::LightShadowCascadeDistribution, shadowCascadeDistribution),
};

// The mesh and the materials are choices the author made, not settings with a default.
const Field<kb::scene::MeshRendererComponent> kMeshRendererFields[] = {
    KB_FIELD(kb::scene::MeshRendererComponent, P::MeshRendererCastsShadow, castsShadow),
    KB_FIELD(kb::scene::MeshRendererComponent, P::MeshRendererReceivesShadow, receivesShadow),
    KB_FIELD(kb::scene::MeshRendererComponent, P::MeshRendererLodEnabled, lodEnabled),
    KB_FIELD(kb::scene::MeshRendererComponent, P::MeshRendererLodBias, lodBias),
    KB_FIELD(kb::scene::MeshRendererComponent, P::MeshRendererMobility, mobility),
};

// The clip and the output bus are choices; "Enabled" of a Particle Effect follows the choice of an effect (a new
// component stays off until it has one), so it is not marked.
const Field<kb::scene::AudioSourceComponent> kAudioSourceFields[] = {
    KB_FIELD(kb::scene::AudioSourceComponent, P::AudioSourceVolume, volume),
    KB_FIELD(kb::scene::AudioSourceComponent, P::AudioSourcePitch, pitch),
    KB_FIELD(kb::scene::AudioSourceComponent, P::AudioSourceEnabled, enabled),
    KB_FIELD(kb::scene::AudioSourceComponent, P::AudioSourceAutoplay, autoplay),
    KB_FIELD(kb::scene::AudioSourceComponent, P::AudioSourceLoop, loop),
    KB_FIELD(kb::scene::AudioSourceComponent, P::AudioSourceMute, mute),
    KB_FIELD(kb::scene::AudioSourceComponent, P::AudioSourceSpatial, spatial),
    KB_FIELD(kb::scene::AudioSourceComponent, P::AudioSourcePan, pan),
    KB_FIELD(kb::scene::AudioSourceComponent, P::AudioSourceSpatialBlend, spatialBlend),
    KB_FIELD(kb::scene::AudioSourceComponent, P::AudioSourceAttenuation, attenuationModel),
    KB_FIELD(kb::scene::AudioSourceComponent, P::AudioSourceMinDistance, minDistance),
    KB_FIELD(kb::scene::AudioSourceComponent, P::AudioSourceMaxDistance, maxDistance),
    KB_FIELD(kb::scene::AudioSourceComponent, P::AudioSourceRolloff, rolloff),
    KB_FIELD(kb::scene::AudioSourceComponent, P::AudioSourceDopplerFactor, dopplerFactor),
};

const Field<kb::scene::ParticleEffectComponent> kParticleEffectFields[] = {
    KB_FIELD(kb::scene::ParticleEffectComponent, P::ParticleEffectAutoPlay, autoPlay),
};

#undef KB_FIELD

template <typename Component, std::size_t N>
void CollectChanged(std::vector<InspectorFieldKey>& out, InspectorSectionId section, const Component& now,
    const Component& fresh, const Field<Component> (&fields)[N]) {
    for (const Field<Component>& field : fields) {
        if (field.differs(now, fresh)) {
            out.push_back(InspectorFieldKey{ .section = section, .property = field.property });
        }
    }
}

void CollectRows(std::vector<InspectorFieldKey>& out, InspectorSectionId section, InspectorPropertyId property, const std::vector<int>& rows) {
    for (const int row : rows) {
        out.push_back(InspectorFieldKey{ .section = section, .property = property, .index = row });
    }
}

template <typename Component, std::size_t N>
[[nodiscard]] bool ResetByProperty(Component& component, const Component& fresh, const Field<Component> (&fields)[N], InspectorPropertyId property) noexcept {
    for (const Field<Component>& field : fields) {
        if (field.property == property) {
            field.reset(component, fresh);
            return true;
        }
    }
    return false;
}

// The settings of a component shown from its property table (Sprite 2D and the other generic sections).
struct GenericSnapshot {
    std::string component;
    InspectorGenericComponentModel::Values values;
};

// The settings of a UI component as plain values, by property name.
using UIValues = std::vector<std::pair<std::string, kb::scene::UIComponentPropertyValue>>;

struct UISnapshot {
    kb::scene::UIComponentType component{};
    UIValues values;
};

// A UI component's settings. Links to other objects never travel between objects; with `keepReferences` assets stay
// out too, so that a reset keeps the picture or the font that was chosen.
[[nodiscard]] UIValues UIValuesOf(const kb::scene::UIComponentSet& set, kb::scene::UIComponentType component, bool keepReferences) {
    UIValues values;
    for (const kb::scene::UIComponentPropertyDescriptor& descriptor : kb::scene::UIComponentPropertyCatalog(component)) {
        if (!descriptor.writable || descriptor.type == kb::scene::UIComponentPropertyType::Entity ||
            (keepReferences && descriptor.type == kb::scene::UIComponentPropertyType::Asset)) {
            continue;
        }
        kb::scene::UIComponentPropertyValue value;
        if (kb::scene::ReadUIComponentProperty(set, component, descriptor.name, value)) {
            values.emplace_back(std::string{ descriptor.name }, std::move(value));
        }
    }
    return values;
}

[[nodiscard]] bool WriteUIValues(kb::scene::Scene& scene, kb::scene::SceneEntity entity, kb::scene::UIComponentType component, const UIValues& values) {
    kb::scene::UIComponentSet candidate = kb::scene::CaptureSceneUIComponents(scene.Components().UI(), entity);
    if (!kb::scene::HasUIComponent(candidate, component)) {
        return false;
    }
    bool any = false;
    for (const auto& [name, value] : values) {
        any = kb::scene::WriteUIComponentProperty(candidate, component, name, value) == kb::scene::UIComponentPropertyWriteResult::Succeeded || any;
    }
    if (!any) {
        return false;
    }
    kb::scene::SynchronizeSceneUIComponents(scene.Components().UI(), entity, candidate);
    return true;
}

// What Copy Settings keeps until the next one.
using Clipboard = std::variant<std::monostate, kb::scene::CameraComponent, kb::scene::LightComponent,
    kb::scene::MeshRendererComponent, kb::scene::RigidbodyComponent, kb::scene::ColliderComponent,
    kb::scene::CharacterControllerComponent, kb::scene::JointComponent, kb::scene::AudioSourceComponent,
    kb::scene::ParticleEffectComponent, GenericSnapshot, UISnapshot>;

Clipboard& Held() noexcept {
    static Clipboard clipboard;
    return clipboard;
}

template <typename Component>
[[nodiscard]] bool Holds(InspectorSectionId) noexcept {
    return std::holds_alternative<Component>(Held());
}

} // namespace

bool InspectorComponentDefaults::Supports(InspectorSectionId section) noexcept {
    switch (section) {
    case InspectorSectionId::Camera:
    case InspectorSectionId::Light:
    case InspectorSectionId::MeshRenderer:
    case InspectorSectionId::Rigidbody:
    case InspectorSectionId::Collider:
    case InspectorSectionId::CharacterController:
    case InspectorSectionId::Joint:
    case InspectorSectionId::AudioSource:
    case InspectorSectionId::ParticleEffect:
    case InspectorSectionId::Transform:
        return true;
    default:
        return InspectorGenericComponentModel::Find(section) != nullptr || InspectorUIComponentModel::Component(section).has_value();
    }
}

std::vector<InspectorFieldKey> InspectorComponentDefaults::ChangedFields(const kb::scene::Scene& scene, kb::scene::SceneEntity entity) {
    std::vector<InspectorFieldKey> changed;
    const auto components = scene.Components();
    if (const kb::scene::CameraComponent* camera = components.Cameras().TryGet(entity)) {
        CollectChanged(changed, InspectorSectionId::Camera, *camera, FreshCamera(), kCameraFields);
    }
    if (const kb::scene::LightComponent* light = components.Lights().TryGet(entity)) {
        CollectChanged(changed, InspectorSectionId::Light, *light, kb::scene::LightComponent{}, kLightFields);
    }
    if (const kb::scene::MeshRendererComponent* renderer = components.MeshRenderers().TryGet(entity)) {
        CollectChanged(changed, InspectorSectionId::MeshRenderer, *renderer, kb::scene::MeshRendererComponent{}, kMeshRendererFields);
    }
    if (const kb::scene::RigidbodyComponent* body = components.Rigidbodies().TryGet(entity)) {
        CollectRows(changed, InspectorSectionId::Rigidbody, P::RigidbodyField, InspectorPhysicsModel::ChangedRows(*body));
    }
    if (const kb::scene::ColliderComponent* collider = components.Colliders().TryGet(entity)) {
        CollectRows(changed, InspectorSectionId::Collider, P::ColliderField, InspectorPhysicsModel::ChangedRows(*collider));
    }
    if (const kb::scene::CharacterControllerComponent* character = components.CharacterControllers().TryGet(entity)) {
        CollectRows(changed, InspectorSectionId::CharacterController, P::CharacterControllerField, InspectorPhysicsModel::ChangedRows(*character));
    }
    if (const kb::scene::JointComponent* joint = components.Joints().TryGet(entity)) {
        CollectRows(changed, InspectorSectionId::Joint, P::JointField, InspectorPhysicsModel::ChangedRows(*joint));
    }
    if (const kb::scene::AudioSourceComponent* source = components.AudioSources().TryGet(entity)) {
        CollectChanged(changed, InspectorSectionId::AudioSource, *source, kb::scene::AudioSourceComponent{}, kAudioSourceFields);
    }
    if (const kb::scene::ParticleEffectComponent* effect = components.ParticleEffects().TryGet(entity)) {
        CollectChanged(changed, InspectorSectionId::ParticleEffect, *effect, kb::scene::ParticleEffectComponent{}, kParticleEffectFields);
    }
    for (const InspectorGenericComponentModel::Section& generic : InspectorGenericComponentModel::Sections()) {
        if (kb::scene::SceneComponentAuthoring::Has(scene, entity, generic.component)) {
            CollectRows(changed, generic.id, P::GenericComponentField, InspectorGenericComponentModel::ChangedRows(scene, entity, generic));
        }
    }
    // One mark per vector row, carried by the row's first property.
    if (const kb::scene::TransformComponent* transform = scene.Transforms().TryGet(entity)) {
        const kb::scene::TransformComponent fresh{};
        if (!Same(transform->localPosition, fresh.localPosition)) {
            changed.push_back(InspectorFieldKey{ .section = InspectorSectionId::Transform, .property = P::PositionX });
        }
        if (!Same(transform->localRotation, fresh.localRotation)) {
            changed.push_back(InspectorFieldKey{ .section = InspectorSectionId::Transform, .property = P::RotationX });
        }
        if (!Same(transform->localScale, fresh.localScale)) {
            changed.push_back(InspectorFieldKey{ .section = InspectorSectionId::Transform, .property = P::ScaleX });
        }
    }
    return changed;
}

bool InspectorComponentDefaults::ResetField(kb::scene::Scene& scene, kb::scene::SceneEntity entity, const InspectorFieldKey& field) {
    auto components = scene.Components();
    switch (field.section) {
    case InspectorSectionId::Camera: {
        kb::scene::CameraComponent* camera = components.Cameras().TryGet(entity);
        if (camera == nullptr || !ResetByProperty(*camera, FreshCamera(), kCameraFields, field.property)) {
            return false;
        }
        components.Cameras().MarkModified(entity);
        return true;
    }
    case InspectorSectionId::Light: {
        kb::scene::LightComponent* light = components.Lights().TryGet(entity);
        if (light == nullptr || !ResetByProperty(*light, kb::scene::LightComponent{}, kLightFields, field.property)) {
            return false;
        }
        components.Lights().MarkModified(entity);
        return true;
    }
    case InspectorSectionId::MeshRenderer: {
        kb::scene::MeshRendererComponent* renderer = components.MeshRenderers().TryGet(entity);
        if (renderer == nullptr || !ResetByProperty(*renderer, kb::scene::MeshRendererComponent{}, kMeshRendererFields, field.property)) {
            return false;
        }
        components.MeshRenderers().MarkModified(entity);
        return true;
    }
    case InspectorSectionId::Rigidbody: {
        kb::scene::RigidbodyComponent* body = components.Rigidbodies().TryGet(entity);
        if (body == nullptr || !InspectorPhysicsModel::ResetRow(*body, field.index)) {
            return false;
        }
        components.Rigidbodies().MarkModified(entity);
        return true;
    }
    case InspectorSectionId::Collider: {
        kb::scene::ColliderComponent* collider = components.Colliders().TryGet(entity);
        if (collider == nullptr || !InspectorPhysicsModel::ResetRow(*collider, field.index)) {
            return false;
        }
        components.Colliders().MarkModified(entity);
        return true;
    }
    case InspectorSectionId::CharacterController: {
        kb::scene::CharacterControllerComponent* character = components.CharacterControllers().TryGet(entity);
        if (character == nullptr || !InspectorPhysicsModel::ResetRow(*character, field.index)) {
            return false;
        }
        components.CharacterControllers().MarkModified(entity);
        return true;
    }
    case InspectorSectionId::Joint: {
        kb::scene::JointComponent* joint = components.Joints().TryGet(entity);
        if (joint == nullptr || !InspectorPhysicsModel::ResetRow(*joint, field.index)) {
            return false;
        }
        components.Joints().MarkModified(entity);
        return true;
    }
    case InspectorSectionId::AudioSource: {
        kb::scene::AudioSourceComponent* source = components.AudioSources().TryGet(entity);
        if (source == nullptr || !ResetByProperty(*source, kb::scene::AudioSourceComponent{}, kAudioSourceFields, field.property)) {
            return false;
        }
        components.AudioSources().MarkModified(entity);
        return true;
    }
    case InspectorSectionId::ParticleEffect: {
        kb::scene::ParticleEffectComponent* effect = components.ParticleEffects().TryGet(entity);
        if (effect == nullptr || !ResetByProperty(*effect, kb::scene::ParticleEffectComponent{}, kParticleEffectFields, field.property)) {
            return false;
        }
        components.ParticleEffects().MarkModified(entity);
        return true;
    }
    case InspectorSectionId::Transform:
        switch (field.property) {
        case P::PositionX: return ResetTransformPart(scene, entity, InspectorComponentMenuItem::ResetPosition);
        case P::RotationX: return ResetTransformPart(scene, entity, InspectorComponentMenuItem::ResetRotation);
        case P::ScaleX: return ResetTransformPart(scene, entity, InspectorComponentMenuItem::ResetScale);
        default: return false;
        }
    default:
        return InspectorGenericComponentModel::Find(field.section) != nullptr && InspectorGenericComponentModel::ResetRow(scene, entity, field.index);
    }
}

bool InspectorComponentDefaults::ResetTransformPart(kb::scene::Scene& scene, kb::scene::SceneEntity entity, InspectorComponentMenuItem part) {
    if (!scene.Entities().IsAlive(entity) || scene.Transforms().TryGet(entity) == nullptr) {
        return false;
    }
    kb::scene::TransformComponent transform = scene.Transforms().Get(entity);
    const kb::scene::TransformComponent fresh{};
    switch (part) {
    case InspectorComponentMenuItem::ResetPosition: transform.localPosition = fresh.localPosition; break;
    case InspectorComponentMenuItem::ResetRotation: transform.localRotation = fresh.localRotation; break;
    case InspectorComponentMenuItem::ResetScale: transform.localScale = fresh.localScale; break;
    default: return false;
    }
    scene.Transforms().Set(entity, transform);
    return true;
}

bool InspectorComponentDefaults::ResetComponent(kb::scene::Scene& scene, kb::scene::SceneEntity entity, InspectorSectionId section) {
    if (!Supports(section)) {
        return false;
    }
    // A UI component has no marks (a widget made from a preset starts away from the plain component), so a reset
    // writes the plain component's values.
    if (const std::optional<kb::scene::UIComponentType> ui = InspectorUIComponentModel::Component(section)) {
        kb::scene::UIComponentSet plain;
        return kb::scene::AddUIComponent(plain, *ui) && WriteUIValues(scene, entity, *ui, UIValuesOf(plain, *ui, true));
    }
    // Each changed field is put back on its own: a joint's own type decides which rows exist.
    for (const InspectorFieldKey& field : ChangedFields(scene, entity)) {
        if (field.section == section && !ResetField(scene, entity, field)) {
            return false;
        }
    }
    return scene.Entities().IsAlive(entity);
}

bool InspectorComponentDefaults::CopyComponent(const kb::scene::Scene& scene, kb::scene::SceneEntity entity, InspectorSectionId section) {
    const auto components = scene.Components();
    switch (section) {
    case InspectorSectionId::Camera:
        if (const auto* camera = components.Cameras().TryGet(entity)) { Held() = *camera; return true; }
        break;
    case InspectorSectionId::Light:
        if (const auto* light = components.Lights().TryGet(entity)) { Held() = *light; return true; }
        break;
    case InspectorSectionId::MeshRenderer:
        if (const auto* renderer = components.MeshRenderers().TryGet(entity)) { Held() = *renderer; return true; }
        break;
    case InspectorSectionId::Rigidbody:
        if (const auto* body = components.Rigidbodies().TryGet(entity)) { Held() = *body; return true; }
        break;
    case InspectorSectionId::Collider:
        if (const auto* collider = components.Colliders().TryGet(entity)) { Held() = *collider; return true; }
        break;
    case InspectorSectionId::CharacterController:
        if (const auto* character = components.CharacterControllers().TryGet(entity)) { Held() = *character; return true; }
        break;
    case InspectorSectionId::Joint:
        if (const auto* joint = components.Joints().TryGet(entity)) { Held() = *joint; return true; }
        break;
    case InspectorSectionId::AudioSource:
        if (const auto* source = components.AudioSources().TryGet(entity)) { Held() = *source; return true; }
        break;
    case InspectorSectionId::ParticleEffect:
        if (const auto* effect = components.ParticleEffects().TryGet(entity)) { Held() = *effect; return true; }
        break;
    default:
        if (const InspectorGenericComponentModel::Section* generic = InspectorGenericComponentModel::Find(section)) {
            GenericSnapshot snapshot{ .component = std::string{ generic->component } };
            if (InspectorGenericComponentModel::Capture(scene, entity, *generic, snapshot.values)) { Held() = std::move(snapshot); return true; }
        }
        if (const std::optional<kb::scene::UIComponentType> ui = InspectorUIComponentModel::Component(section)) {
            const kb::scene::UIComponentSet set = kb::scene::CaptureSceneUIComponents(scene.Components().UI(), entity);
            if (kb::scene::HasUIComponent(set, *ui)) { Held() = UISnapshot{ .component = *ui, .values = UIValuesOf(set, *ui, false) }; return true; }
        }
        break;
    }
    return false;
}

bool InspectorComponentDefaults::CanPaste(InspectorSectionId section) noexcept {
    switch (section) {
    case InspectorSectionId::Camera: return Holds<kb::scene::CameraComponent>(section);
    case InspectorSectionId::Light: return Holds<kb::scene::LightComponent>(section);
    case InspectorSectionId::MeshRenderer: return Holds<kb::scene::MeshRendererComponent>(section);
    case InspectorSectionId::Rigidbody: return Holds<kb::scene::RigidbodyComponent>(section);
    case InspectorSectionId::Collider: return Holds<kb::scene::ColliderComponent>(section);
    case InspectorSectionId::CharacterController: return Holds<kb::scene::CharacterControllerComponent>(section);
    case InspectorSectionId::Joint: return Holds<kb::scene::JointComponent>(section);
    case InspectorSectionId::AudioSource: return Holds<kb::scene::AudioSourceComponent>(section);
    case InspectorSectionId::ParticleEffect: return Holds<kb::scene::ParticleEffectComponent>(section);
    default: {
        const InspectorGenericComponentModel::Section* generic = InspectorGenericComponentModel::Find(section);
        const GenericSnapshot* held = std::get_if<GenericSnapshot>(&Held());
        if (generic != nullptr) {
            return held != nullptr && held->component == generic->component;
        }
        const std::optional<kb::scene::UIComponentType> ui = InspectorUIComponentModel::Component(section);
        const UISnapshot* heldUI = std::get_if<UISnapshot>(&Held());
        return ui.has_value() && heldUI != nullptr && heldUI->component == *ui;
    }
    }
}

bool InspectorComponentDefaults::PasteComponent(kb::scene::Scene& scene, kb::scene::SceneEntity entity, InspectorSectionId section) {
    if (!CanPaste(section)) {
        return false;
    }
    auto components = scene.Components();
    switch (section) {
    case InspectorSectionId::Camera:
        if (components.Cameras().Has(entity)) { components.Cameras().Set(entity, std::get<kb::scene::CameraComponent>(Held())); return true; }
        break;
    case InspectorSectionId::Light:
        if (auto* light = components.Lights().TryGet(entity)) { *light = std::get<kb::scene::LightComponent>(Held()); components.Lights().MarkModified(entity); return true; }
        break;
    case InspectorSectionId::MeshRenderer:
        if (auto* renderer = components.MeshRenderers().TryGet(entity)) {
            const std::uint64_t instance = renderer->materialInstanceHandle;
            *renderer = std::get<kb::scene::MeshRendererComponent>(Held());
            renderer->materialInstanceHandle = instance;
            components.MeshRenderers().MarkModified(entity);
            return true;
        }
        break;
    case InspectorSectionId::Rigidbody:
        if (auto* body = components.Rigidbodies().TryGet(entity)) { *body = std::get<kb::scene::RigidbodyComponent>(Held()); components.Rigidbodies().MarkModified(entity); return true; }
        break;
    case InspectorSectionId::Collider:
        if (auto* collider = components.Colliders().TryGet(entity)) { *collider = std::get<kb::scene::ColliderComponent>(Held()); components.Colliders().MarkModified(entity); return true; }
        break;
    case InspectorSectionId::CharacterController:
        if (auto* character = components.CharacterControllers().TryGet(entity)) { *character = std::get<kb::scene::CharacterControllerComponent>(Held()); components.CharacterControllers().MarkModified(entity); return true; }
        break;
    case InspectorSectionId::Joint:
        if (auto* joint = components.Joints().TryGet(entity)) {
            const kb::scene::SceneEntity connected = joint->connectedEntity;
            *joint = std::get<kb::scene::JointComponent>(Held());
            joint->connectedEntity = connected;
            components.Joints().MarkModified(entity);
            return true;
        }
        break;
    case InspectorSectionId::AudioSource:
        if (auto* source = components.AudioSources().TryGet(entity)) { *source = std::get<kb::scene::AudioSourceComponent>(Held()); components.AudioSources().MarkModified(entity); return true; }
        break;
    case InspectorSectionId::ParticleEffect:
        if (auto* effect = components.ParticleEffects().TryGet(entity)) { *effect = std::get<kb::scene::ParticleEffectComponent>(Held()); components.ParticleEffects().MarkModified(entity); return true; }
        break;
    default:
        if (const InspectorGenericComponentModel::Section* generic = InspectorGenericComponentModel::Find(section);
            generic != nullptr && kb::scene::SceneComponentAuthoring::Has(scene, entity, generic->component)) {
            return InspectorGenericComponentModel::Apply(scene, entity, *generic, std::get<GenericSnapshot>(Held()).values);
        }
        if (const std::optional<kb::scene::UIComponentType> ui = InspectorUIComponentModel::Component(section)) {
            return WriteUIValues(scene, entity, *ui, std::get<UISnapshot>(Held()).values);
        }
        break;
    }
    return false;
}

} // namespace kb::editor
