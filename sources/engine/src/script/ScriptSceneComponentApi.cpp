#include "engine/script/ScriptSceneComponentApi.hpp"

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
#include "engine/scene/GuideCurveComponent.hpp"
#include "engine/scene/ContentInstanceComponent.hpp"
#include "engine/scene/StreamFocusComponent.hpp"
#include "engine/scene/WorldBackdropComponent.hpp"
#include "engine/scene/AmbientRadianceComponent.hpp"
#include "engine/scene/DetailSwitchComponent.hpp"
#include "engine/scene/VisibilityBlockerComponent.hpp"
#include "engine/scene/VisibilityCellComponent.hpp"
#include "engine/scene/RegionPortalComponent.hpp"
#include "engine/scene/AuxFrameComponent.hpp"
#include "engine/scene/GeometrySwarmComponent.hpp"
#include "engine/scene/SceneBehaviourComponents.hpp"
#include "engine/scene/SceneCameraComponents.hpp"
#include "engine/scene/SceneCharacterControllerComponents.hpp"
#include "engine/scene/SceneColliderComponents.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneJointComponents.hpp"
#include "engine/scene/SceneLightComponents.hpp"
#include "engine/scene/SceneMeshRendererComponents.hpp"
#include "engine/scene/SceneNavigationComponents.hpp"
#include "engine/scene/SceneRigidbodyComponents.hpp"
#include "engine/scene/SceneRegionShapeComponents.hpp"
#include "engine/scene/SceneGuideCurveComponents.hpp"
#include "engine/scene/SceneContentInstanceComponents.hpp"
#include "engine/scene/SceneStreamFocusComponents.hpp"
#include "engine/scene/SceneWorldBackdropComponents.hpp"
#include "engine/scene/SceneAmbientRadianceComponents.hpp"
#include "engine/scene/SceneDetailSwitchComponents.hpp"
#include "engine/scene/SceneVisibilityBlockerComponents.hpp"
#include "engine/scene/SceneVisibilityCellComponents.hpp"
#include "engine/scene/SceneRegionPortalComponents.hpp"
#include "engine/scene/SceneAuxFrameComponents.hpp"
#include "engine/scene/SceneGeometrySwarmComponents.hpp"
#include "engine/scene/SceneTagsComponents.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/SceneVisibilityComponents.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "engine/scene/TagsComponent.hpp"
#include "engine/scene/VisibilityComponent.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <string>
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

#define KB_CAMERA_CLEAR_MODE(Component, field) \
    FieldBinding{ #field, \
        [](const void* component) noexcept -> ScriptValue { \
            KB_ASSERT_NOT_POINTER(static_cast<const Component*>(component)->field); \
            return ScriptValue{ static_cast<int>(static_cast<const Component*>(component)->field) }; }, \
        [](void* component, const ScriptValue& value) noexcept -> bool { \
            if (value.Type() != ScriptValueType::Int || value.AsInt() < 0 || value.AsInt() > static_cast<int>(kb::scene::CameraClearMode::DontClear)) { return false; } \
            static_cast<Component*>(component)->field = static_cast<kb::scene::CameraClearMode>(value.AsInt()); \
            return true; \
        } }

#define KB_LIGHT_KIND(Component, field) \
    FieldBinding{ #field, \
        [](const void* component) noexcept -> ScriptValue { \
            KB_ASSERT_NOT_POINTER(static_cast<const Component*>(component)->field); \
            return ScriptValue{ static_cast<int>(static_cast<const Component*>(component)->field) }; }, \
        [](void* component, const ScriptValue& value) noexcept -> bool { \
            if (value.Type() != ScriptValueType::Int) { return false; } \
            const auto kind = static_cast<kb::scene::LightKind>(value.AsInt()); \
            if (!kb::scene::IsLightKindValid(kind)) { return false; } \
            static_cast<Component*>(component)->field = kind; \
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

constexpr std::array<std::string_view, 25> kComponentNames{
    "Transform",
    "Visibility",
    "Camera",
    "3D Radiance Emitter",
    "MeshRenderer",
    "Behaviour",
    "Rigidbody",
    "Collider",
    "CharacterController",
    "Joint",
    "NavAgent",
    "NavObstacle",
    "Tags",
    "RegionShape",
    "GuideCurve",
    "ContentInstance",
    "StreamFocus",
    "WorldBackdrop",
    "Ambient Radiance",
    "Detail Switch",
    "Visibility Blocker",
    "Visibility Cell",
    "Region Portal",
    "Secondary Frame",
    "Geometry Swarm",
};

constexpr std::array<ScriptSceneComponentPropertyDesc, 10> kRegionShapePropertyDescs{
    ScriptSceneComponentPropertyDesc{ "kind", ScriptValueType::Int },
    ScriptSceneComponentPropertyDesc{ "center.x", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "center.y", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "center.z", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "size.x", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "size.y", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "size.z", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "radius", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "height", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "enabled", ScriptValueType::Bool },
};

constexpr std::array<ScriptSceneComponentPropertyDesc, 4> kGuideCurvePropertyDescs{
    ScriptSceneComponentPropertyDesc{ "controlPointCount", ScriptValueType::UInt32 },
    ScriptSceneComponentPropertyDesc{ "interpolation", ScriptValueType::Int },
    ScriptSceneComponentPropertyDesc{ "closed", ScriptValueType::Bool },
    ScriptSceneComponentPropertyDesc{ "enabled", ScriptValueType::Bool },
};

constexpr std::array<ScriptSceneComponentPropertyDesc, 4> kContentInstancePropertyDescs{
    ScriptSceneComponentPropertyDesc{ "assetId", ScriptValueType::Hash },
    ScriptSceneComponentPropertyDesc{ "kind", ScriptValueType::Int },
    ScriptSceneComponentPropertyDesc{ "lifetime", ScriptValueType::Int },
    ScriptSceneComponentPropertyDesc{ "active", ScriptValueType::Bool },
};

constexpr std::array<ScriptSceneComponentPropertyDesc, 5> kStreamFocusPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "innerRadius", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "outerRadius", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "priority", ScriptValueType::Int },
    ScriptSceneComponentPropertyDesc{ "loadMask", ScriptValueType::UInt32 },
    ScriptSceneComponentPropertyDesc{ "enabled", ScriptValueType::Bool },
};

constexpr std::array<ScriptSceneComponentPropertyDesc, 15> kWorldBackdropPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "mode", ScriptValueType::Int },
    ScriptSceneComponentPropertyDesc{ "color.x", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "color.y", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "color.z", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "horizonColor.x", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "horizonColor.y", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "horizonColor.z", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "zenithColor.x", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "zenithColor.y", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "zenithColor.z", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "environmentAssetId", ScriptValueType::Hash },
    ScriptSceneComponentPropertyDesc{ "horizonHeight", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "gradientExponent", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "priority", ScriptValueType::Int },
    ScriptSceneComponentPropertyDesc{ "enabled", ScriptValueType::Bool },
};

constexpr std::array<ScriptSceneComponentPropertyDesc, 16> kAmbientRadiancePropertyDescs{
    ScriptSceneComponentPropertyDesc{ "mode", ScriptValueType::Int },
    ScriptSceneComponentPropertyDesc{ "color.x", ScriptValueType::Float }, ScriptSceneComponentPropertyDesc{ "color.y", ScriptValueType::Float }, ScriptSceneComponentPropertyDesc{ "color.z", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "horizonColor.x", ScriptValueType::Float }, ScriptSceneComponentPropertyDesc{ "horizonColor.y", ScriptValueType::Float }, ScriptSceneComponentPropertyDesc{ "horizonColor.z", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "zenithColor.x", ScriptValueType::Float }, ScriptSceneComponentPropertyDesc{ "zenithColor.y", ScriptValueType::Float }, ScriptSceneComponentPropertyDesc{ "zenithColor.z", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "environmentAssetId", ScriptValueType::Hash }, ScriptSceneComponentPropertyDesc{ "intensity", ScriptValueType::Float }, ScriptSceneComponentPropertyDesc{ "diffuseIntensity", ScriptValueType::Float }, ScriptSceneComponentPropertyDesc{ "specularIntensity", ScriptValueType::Float }, ScriptSceneComponentPropertyDesc{ "priority", ScriptValueType::Int }, ScriptSceneComponentPropertyDesc{ "enabled", ScriptValueType::Bool },
};

constexpr std::array<ScriptSceneComponentPropertyDesc, 6> kDetailSwitchPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "groupId", ScriptValueType::Hash },
    ScriptSceneComponentPropertyDesc{ "minimumLod", ScriptValueType::UInt32 },
    ScriptSceneComponentPropertyDesc{ "maximumLod", ScriptValueType::UInt32 },
    ScriptSceneComponentPropertyDesc{ "promoteCoverage", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "demoteCoverage", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "enabled", ScriptValueType::Bool },
};

constexpr std::array<ScriptSceneComponentPropertyDesc, 7> kVisibilityBlockerPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "localCenter.x", ScriptValueType::Float }, ScriptSceneComponentPropertyDesc{ "localCenter.y", ScriptValueType::Float }, ScriptSceneComponentPropertyDesc{ "localCenter.z", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "size.x", ScriptValueType::Float }, ScriptSceneComponentPropertyDesc{ "size.y", ScriptValueType::Float }, ScriptSceneComponentPropertyDesc{ "size.z", ScriptValueType::Float }, ScriptSceneComponentPropertyDesc{ "enabled", ScriptValueType::Bool },
};

constexpr std::array<ScriptSceneComponentPropertyDesc, 4> kVisibilityCellPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "membershipMask", ScriptValueType::Int }, ScriptSceneComponentPropertyDesc{ "membership", ScriptValueType::Int },
    ScriptSceneComponentPropertyDesc{ "visibilityOverride", ScriptValueType::Int }, ScriptSceneComponentPropertyDesc{ "enabled", ScriptValueType::Bool },
};

constexpr std::array<ScriptSceneComponentPropertyDesc, 4> kRegionPortalPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "sourceCell", ScriptValueType::Entity }, ScriptSceneComponentPropertyDesc{ "targetCell", ScriptValueType::Entity },
    ScriptSceneComponentPropertyDesc{ "purposes", ScriptValueType::UInt32 }, ScriptSceneComponentPropertyDesc{ "enabled", ScriptValueType::Bool },
};

constexpr std::array<ScriptSceneComponentPropertyDesc, 9> kSecondaryFramePropertyDescs{
    ScriptSceneComponentPropertyDesc{ "mode", ScriptValueType::Int }, ScriptSceneComponentPropertyDesc{ "imageTargetId", ScriptValueType::Hash },
    ScriptSceneComponentPropertyDesc{ "width", ScriptValueType::UInt32 }, ScriptSceneComponentPropertyDesc{ "height", ScriptValueType::UInt32 },
    ScriptSceneComponentPropertyDesc{ "mirrorPlaneNormal.x", ScriptValueType::Float }, ScriptSceneComponentPropertyDesc{ "mirrorPlaneNormal.y", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "mirrorPlaneNormal.z", ScriptValueType::Float }, ScriptSceneComponentPropertyDesc{ "mirrorPlaneOffset", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "enabled", ScriptValueType::Bool },
};
constexpr std::array<ScriptSceneComponentPropertyDesc, 14> kGeometrySwarmPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "meshAssetId", ScriptValueType::Hash }, ScriptSceneComponentPropertyDesc{ "materialAssetId", ScriptValueType::Hash },
    ScriptSceneComponentPropertyDesc{ "instanceCount", ScriptValueType::UInt32 }, ScriptSceneComponentPropertyDesc{ "columns", ScriptValueType::UInt32 },
    ScriptSceneComponentPropertyDesc{ "rows", ScriptValueType::UInt32 }, ScriptSceneComponentPropertyDesc{ "layers", ScriptValueType::UInt32 },
    ScriptSceneComponentPropertyDesc{ "spacing.x", ScriptValueType::Float }, ScriptSceneComponentPropertyDesc{ "spacing.y", ScriptValueType::Float }, ScriptSceneComponentPropertyDesc{ "spacing.z", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "instanceScale", ScriptValueType::Float }, ScriptSceneComponentPropertyDesc{ "layer", ScriptValueType::UInt32 },
    ScriptSceneComponentPropertyDesc{ "castsShadow", ScriptValueType::Bool }, ScriptSceneComponentPropertyDesc{ "receivesShadow", ScriptValueType::Bool }, ScriptSceneComponentPropertyDesc{ "enabled", ScriptValueType::Bool },
};

constexpr std::array<ScriptSceneComponentPropertyDesc, 1> kTagsPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "text", ScriptValueType::String },
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

constexpr std::array<ScriptSceneComponentPropertyDesc, 3> kVisibilityPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "mode", ScriptValueType::Int },
    ScriptSceneComponentPropertyDesc{ "mask", ScriptValueType::UInt32 },
    ScriptSceneComponentPropertyDesc{ "visible", ScriptValueType::Bool },
};

constexpr std::array<ScriptSceneComponentPropertyDesc, 13> kCameraPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "projection", ScriptValueType::Int },
    ScriptSceneComponentPropertyDesc{ "verticalFovDegrees", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "orthographicHeight", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "nearClip", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "farClip", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "primary", ScriptValueType::Bool },
    ScriptSceneComponentPropertyDesc{ "viewportId", ScriptValueType::Int },
    ScriptSceneComponentPropertyDesc{ "priority", ScriptValueType::Int },
    ScriptSceneComponentPropertyDesc{ "cullingMask", ScriptValueType::Int },
    ScriptSceneComponentPropertyDesc{ "clearMode", ScriptValueType::Int },
    ScriptSceneComponentPropertyDesc{ "clearColor.x", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "clearColor.y", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "clearColor.z", ScriptValueType::Float },
};

constexpr std::array<ScriptSceneComponentPropertyDesc, 16> kLightPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "kind", ScriptValueType::Int },
    ScriptSceneComponentPropertyDesc{ "color.x", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "color.y", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "color.z", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "intensity", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "range", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "innerConeDegrees", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "outerConeDegrees", ScriptValueType::Float },
    // LIB-141: areaWidth/areaHeight already existed on LightComponent (AreaRect/AreaDisk/Tube
    // authoring) but were never in this reflection table - a pre-existing gap, closed here.
    ScriptSceneComponentPropertyDesc{ "areaWidth", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "areaHeight", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "contactShadowLength", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "volumetricScattering", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "castsShadow", ScriptValueType::Bool },
    ScriptSceneComponentPropertyDesc{ "useColorTemperature", ScriptValueType::Bool },
    ScriptSceneComponentPropertyDesc{ "colorTemperatureKelvin", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "layerMask", ScriptValueType::Int },
};

constexpr std::array<ScriptSceneComponentPropertyDesc, 4> kMeshRendererPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "materialSlotOverrideCount", ScriptValueType::Int },
    ScriptSceneComponentPropertyDesc{ "castsShadow", ScriptValueType::Bool },
    ScriptSceneComponentPropertyDesc{ "receivesShadow", ScriptValueType::Bool },
    ScriptSceneComponentPropertyDesc{ "layer", ScriptValueType::Int },
};

constexpr std::array<ScriptSceneComponentPropertyDesc, 3> kBehaviourPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "enabled", ScriptValueType::Bool },
    ScriptSceneComponentPropertyDesc{ "tickGroup", ScriptValueType::Int },
    ScriptSceneComponentPropertyDesc{ "executionOrder", ScriptValueType::Int },
};

constexpr std::array<ScriptSceneComponentPropertyDesc, 12> kRigidbodyPropertyDescs{
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
    ScriptSceneComponentPropertyDesc{ "useContinuousCollision", ScriptValueType::Bool },
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

constexpr std::array<ScriptSceneComponentPropertyDesc, 9> kCharacterControllerPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "center.x", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "center.y", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "center.z", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "radius", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "height", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "slopeLimitDegrees", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "stepOffset", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "gravityScale", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "useGravity", ScriptValueType::Bool },
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

constexpr std::array<ScriptSceneComponentPropertyDesc, 11> kNavAgentPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "radius", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "height", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "maxSpeed", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "acceleration", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "angularSpeedDegrees", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "stoppingDistance", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "areaMask", ScriptValueType::Int },
    ScriptSceneComponentPropertyDesc{ "destination.x", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "destination.y", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "destination.z", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "enabled", ScriptValueType::Bool },
};

constexpr std::array<ScriptSceneComponentPropertyDesc, 9> kNavObstaclePropertyDescs{
    ScriptSceneComponentPropertyDesc{ "center.x", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "center.y", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "center.z", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "size.x", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "size.y", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "size.z", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "radius", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "height", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "enabled", ScriptValueType::Bool },
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

constexpr std::array<FieldBinding, 3> kVisibilityFields{
    FieldBinding{ "mode",
        [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<int>(static_cast<const kb::scene::VisibilityComponent*>(component)->mode) }; },
        [](void* component, const ScriptValue& value) noexcept -> bool {
            if (value.Type() != ScriptValueType::Int || value.AsInt() < 0 || value.AsInt() > 2) return false;
            auto& visibility = *static_cast<kb::scene::VisibilityComponent*>(component);
            visibility.mode = static_cast<kb::scene::VisibilityMode>(value.AsInt());
            visibility.visible = visibility.mode != kb::scene::VisibilityMode::Hidden;
            return true;
        } },
    FieldBinding{ "mask",
        [](const void* component) noexcept -> ScriptValue {
            return ScriptValue{ static_cast<const kb::scene::VisibilityComponent*>(component)->mask };
        },
        [](void* component, const ScriptValue& value) noexcept -> bool {
            auto& visibility = *static_cast<kb::scene::VisibilityComponent*>(component);
            if (value.Type() == ScriptValueType::UInt32) {
                visibility.mask = value.AsUInt32();
                return true;
            }
            if (value.Type() == ScriptValueType::Int && value.AsInt() >= 0) {
                visibility.mask = static_cast<std::uint32_t>(value.AsInt());
                return true;
            }
            return false;
        } },
    FieldBinding{ "visible",
        [](const void* component) noexcept -> ScriptValue {
            const auto& visibility = *static_cast<const kb::scene::VisibilityComponent*>(component);
            return ScriptValue{ visibility.mode != kb::scene::VisibilityMode::Hidden && visibility.visible };
        },
        [](void* component, const ScriptValue& value) noexcept -> bool {
            if (value.Type() != ScriptValueType::Bool) return false;
            auto& visibility = *static_cast<kb::scene::VisibilityComponent*>(component);
            visibility.mode = value.AsBool() ? kb::scene::VisibilityMode::Visible : kb::scene::VisibilityMode::Hidden;
            visibility.visible = value.AsBool();
            return true;
        } },
};

constexpr std::array<FieldBinding, 13> kCameraFields{
    KB_CAMERA_PROJECTION(kb::scene::CameraComponent, projection),
    KB_FLOAT(kb::scene::CameraComponent, verticalFovDegrees),
    KB_FLOAT(kb::scene::CameraComponent, orthographicHeight),
    KB_FLOAT(kb::scene::CameraComponent, nearClip),
    KB_FLOAT(kb::scene::CameraComponent, farClip),
    KB_BOOL(kb::scene::CameraComponent, primary),
    KB_UINT32(kb::scene::CameraComponent, viewportId),
    KB_INT(kb::scene::CameraComponent, priority),
    KB_UINT32(kb::scene::CameraComponent, cullingMask),
    KB_CAMERA_CLEAR_MODE(kb::scene::CameraComponent, clearMode),
    KB_NESTED_FLOAT(kb::scene::CameraComponent, clearColor, x),
    KB_NESTED_FLOAT(kb::scene::CameraComponent, clearColor, y),
    KB_NESTED_FLOAT(kb::scene::CameraComponent, clearColor, z),
};

constexpr std::array<FieldBinding, 16> kLightFields{
    KB_LIGHT_KIND(kb::scene::LightComponent, kind),
    KB_NESTED_FLOAT(kb::scene::LightComponent, color, x),
    KB_NESTED_FLOAT(kb::scene::LightComponent, color, y),
    KB_NESTED_FLOAT(kb::scene::LightComponent, color, z),
    KB_FLOAT(kb::scene::LightComponent, intensity),
    KB_FLOAT(kb::scene::LightComponent, range),
    KB_FLOAT(kb::scene::LightComponent, innerConeDegrees),
    KB_FLOAT(kb::scene::LightComponent, outerConeDegrees),
    KB_FLOAT(kb::scene::LightComponent, areaWidth),
    KB_FLOAT(kb::scene::LightComponent, areaHeight),
    KB_FLOAT(kb::scene::LightComponent, contactShadowLength),
    KB_FLOAT(kb::scene::LightComponent, volumetricScattering),
    KB_BOOL(kb::scene::LightComponent, castsShadow),
    KB_BOOL(kb::scene::LightComponent, useColorTemperature),
    KB_FLOAT(kb::scene::LightComponent, colorTemperatureKelvin),
    KB_UINT32(kb::scene::LightComponent, layerMask),
};

constexpr std::array<FieldBinding, 4> kMeshRendererFields{
    KB_UINT32(kb::scene::MeshRendererComponent, materialSlotOverrideCount),
    KB_BOOL(kb::scene::MeshRendererComponent, castsShadow),
    KB_BOOL(kb::scene::MeshRendererComponent, receivesShadow),
    KB_UINT32(kb::scene::MeshRendererComponent, layer),
};

constexpr std::array<FieldBinding, 3> kBehaviourFields{
    KB_BOOL(kb::scene::BehaviourComponent, enabled),
    KB_TICKGROUP(kb::scene::BehaviourComponent, tickGroup),
    KB_INT(kb::scene::BehaviourComponent, executionOrder),
};

constexpr std::array<FieldBinding, 12> kRigidbodyFields{
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
    KB_BOOL(kb::scene::RigidbodyComponent, useContinuousCollision),
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

constexpr std::array<FieldBinding, 9> kCharacterControllerFields{
    KB_NESTED_FLOAT(kb::scene::CharacterControllerComponent, center, x),
    KB_NESTED_FLOAT(kb::scene::CharacterControllerComponent, center, y),
    KB_NESTED_FLOAT(kb::scene::CharacterControllerComponent, center, z),
    KB_FLOAT(kb::scene::CharacterControllerComponent, radius),
    KB_FLOAT(kb::scene::CharacterControllerComponent, height),
    KB_FLOAT(kb::scene::CharacterControllerComponent, slopeLimitDegrees),
    KB_FLOAT(kb::scene::CharacterControllerComponent, stepOffset),
    KB_FLOAT(kb::scene::CharacterControllerComponent, gravityScale),
    KB_BOOL(kb::scene::CharacterControllerComponent, useGravity),
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

constexpr std::array<FieldBinding, 11> kNavAgentFields{
    KB_FLOAT(kb::scene::NavAgent, radius),
    KB_FLOAT(kb::scene::NavAgent, height),
    KB_FLOAT(kb::scene::NavAgent, maxSpeed),
    KB_FLOAT(kb::scene::NavAgent, acceleration),
    KB_FLOAT(kb::scene::NavAgent, angularSpeedDegrees),
    KB_FLOAT(kb::scene::NavAgent, stoppingDistance),
    KB_UINT32(kb::scene::NavAgent, areaMask),
    KB_NESTED_FLOAT(kb::scene::NavAgent, destination, x),
    KB_NESTED_FLOAT(kb::scene::NavAgent, destination, y),
    KB_NESTED_FLOAT(kb::scene::NavAgent, destination, z),
    KB_BOOL(kb::scene::NavAgent, enabled),
};

constexpr std::array<FieldBinding, 9> kNavObstacleFields{
    KB_NESTED_FLOAT(kb::scene::NavObstacle, center, x),
    KB_NESTED_FLOAT(kb::scene::NavObstacle, center, y),
    KB_NESTED_FLOAT(kb::scene::NavObstacle, center, z),
    KB_NESTED_FLOAT(kb::scene::NavObstacle, size, x),
    KB_NESTED_FLOAT(kb::scene::NavObstacle, size, y),
    KB_NESTED_FLOAT(kb::scene::NavObstacle, size, z),
    KB_FLOAT(kb::scene::NavObstacle, radius),
    KB_FLOAT(kb::scene::NavObstacle, height),
    KB_BOOL(kb::scene::NavObstacle, enabled),
};

constexpr std::array<FieldBinding, 1> kTagsFields{
    FieldBinding{
        "text",
        [](const void* component) noexcept -> ScriptValue {
            return ScriptValue{ std::string{ kb::scene::TagsText(*static_cast<const kb::scene::TagsComponent*>(component)) } };
        },
        [](void* component, const ScriptValue& value) noexcept -> bool {
            if (value.Type() != ScriptValueType::String) {
                return false;
            }
            return kb::scene::TrySetTagsText(*static_cast<kb::scene::TagsComponent*>(component), value.AsString());
        } },
};

constexpr std::array<FieldBinding, 10> kRegionShapeFields{
    FieldBinding{ "kind", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<int>(static_cast<const kb::scene::RegionShapeComponent*>(component)->kind) }; },
        [](void* component, const ScriptValue& value) noexcept -> bool {
            if (value.Type() != ScriptValueType::Int || value.AsInt() < static_cast<int>(kb::scene::RegionShapeKind::Circle2D) || value.AsInt() > static_cast<int>(kb::scene::RegionShapeKind::Capsule)) return false;
            const auto kind = static_cast<kb::scene::RegionShapeKind>(value.AsInt());
            if (!kb::scene::IsRegionShapeKindValid(kind)) return false;
            static_cast<kb::scene::RegionShapeComponent*>(component)->kind = kind;
            return true;
        } },
    KB_NESTED_FLOAT(kb::scene::RegionShapeComponent, center, x),
    KB_NESTED_FLOAT(kb::scene::RegionShapeComponent, center, y),
    KB_NESTED_FLOAT(kb::scene::RegionShapeComponent, center, z),
    KB_NESTED_FLOAT(kb::scene::RegionShapeComponent, size, x),
    KB_NESTED_FLOAT(kb::scene::RegionShapeComponent, size, y),
    KB_NESTED_FLOAT(kb::scene::RegionShapeComponent, size, z),
    KB_FLOAT(kb::scene::RegionShapeComponent, radius),
    KB_FLOAT(kb::scene::RegionShapeComponent, height),
    KB_BOOL(kb::scene::RegionShapeComponent, enabled),
};

constexpr std::array<FieldBinding, 4> kGuideCurveFields{
    FieldBinding{ "controlPointCount", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<std::uint32_t>(static_cast<const kb::scene::GuideCurveComponent*>(component)->controlPointCount) }; },
        [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::UInt32 || !kb::scene::IsGuideCurveControlPointCountValid(value.AsUInt32())) return false; static_cast<kb::scene::GuideCurveComponent*>(component)->controlPointCount = value.AsUInt32(); return true; } },
    FieldBinding{ "interpolation", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<int>(static_cast<const kb::scene::GuideCurveComponent*>(component)->interpolation) }; },
        [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Int) return false; const auto interpolation=static_cast<kb::scene::GuideCurveInterpolation>(value.AsInt()); if (!kb::scene::IsGuideCurveInterpolationValid(interpolation)) return false; static_cast<kb::scene::GuideCurveComponent*>(component)->interpolation=interpolation; return true; } },
    KB_BOOL(kb::scene::GuideCurveComponent, closed),
    KB_BOOL(kb::scene::GuideCurveComponent, enabled),
};

constexpr std::array<FieldBinding, 4> kContentInstanceFields{
    FieldBinding{ "assetId", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::ContentInstanceComponent*>(component)->assetId, ScriptValueType::Hash }; },
        [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Hash) return false; static_cast<kb::scene::ContentInstanceComponent*>(component)->assetId = value.AsUInt64(); return true; } },
    FieldBinding{ "kind", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<int>(static_cast<const kb::scene::ContentInstanceComponent*>(component)->kind) }; },
        [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Int) return false; const auto kind = static_cast<kb::scene::ContentInstanceKind>(value.AsInt()); if (!kb::scene::IsContentInstanceKindValid(kind)) return false; static_cast<kb::scene::ContentInstanceComponent*>(component)->kind = kind; return true; } },
    FieldBinding{ "lifetime", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<int>(static_cast<const kb::scene::ContentInstanceComponent*>(component)->lifetime) }; },
        [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Int) return false; const auto lifetime = static_cast<kb::scene::ContentInstanceLifetime>(value.AsInt()); if (!kb::scene::IsContentInstanceLifetimeValid(lifetime)) return false; static_cast<kb::scene::ContentInstanceComponent*>(component)->lifetime = lifetime; return true; } },
    KB_BOOL(kb::scene::ContentInstanceComponent, active),
};

constexpr std::array<FieldBinding, 5> kStreamFocusFields{
    FieldBinding{ "innerRadius", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::StreamFocusComponent*>(component)->innerRadius }; },
        [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Float || !std::isfinite(value.AsFloat()) || value.AsFloat() < 0.0F) return false; auto& focus = *static_cast<kb::scene::StreamFocusComponent*>(component); if (value.AsFloat() > focus.outerRadius) return false; focus.innerRadius = value.AsFloat(); return true; } },
    FieldBinding{ "outerRadius", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::StreamFocusComponent*>(component)->outerRadius }; },
        [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Float || !std::isfinite(value.AsFloat())) return false; auto& focus = *static_cast<kb::scene::StreamFocusComponent*>(component); if (value.AsFloat() < focus.innerRadius) return false; focus.outerRadius = value.AsFloat(); return true; } },
    FieldBinding{ "priority", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<int>(static_cast<const kb::scene::StreamFocusComponent*>(component)->priority) }; },
        [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Int) return false; static_cast<kb::scene::StreamFocusComponent*>(component)->priority = value.AsInt(); return true; } },
    FieldBinding{ "loadMask", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<std::uint32_t>(static_cast<const kb::scene::StreamFocusComponent*>(component)->loadMask) }; },
        [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::UInt32) return false; const auto mask = static_cast<kb::scene::StreamLoadMask>(value.AsUInt32()); if (!kb::scene::IsStreamLoadMaskValid(mask)) return false; static_cast<kb::scene::StreamFocusComponent*>(component)->loadMask = mask; return true; } },
    KB_BOOL(kb::scene::StreamFocusComponent, enabled),
};

constexpr std::array<FieldBinding, 15> kWorldBackdropFields{
    FieldBinding{ "mode", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<int>(static_cast<const kb::scene::WorldBackdropComponent*>(component)->mode) }; },
        [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Int) return false; const auto mode = static_cast<kb::scene::WorldBackdropMode>(value.AsInt()); if (!kb::scene::IsWorldBackdropModeValid(mode)) return false; static_cast<kb::scene::WorldBackdropComponent*>(component)->mode = mode; return true; } },
    KB_NESTED_FLOAT(kb::scene::WorldBackdropComponent, color, x),
    KB_NESTED_FLOAT(kb::scene::WorldBackdropComponent, color, y),
    KB_NESTED_FLOAT(kb::scene::WorldBackdropComponent, color, z),
    KB_NESTED_FLOAT(kb::scene::WorldBackdropComponent, horizonColor, x),
    KB_NESTED_FLOAT(kb::scene::WorldBackdropComponent, horizonColor, y),
    KB_NESTED_FLOAT(kb::scene::WorldBackdropComponent, horizonColor, z),
    KB_NESTED_FLOAT(kb::scene::WorldBackdropComponent, zenithColor, x),
    KB_NESTED_FLOAT(kb::scene::WorldBackdropComponent, zenithColor, y),
    KB_NESTED_FLOAT(kb::scene::WorldBackdropComponent, zenithColor, z),
    FieldBinding{ "environmentAssetId", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::WorldBackdropComponent*>(component)->environmentAssetId, ScriptValueType::Hash }; },
        [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Hash) return false; static_cast<kb::scene::WorldBackdropComponent*>(component)->environmentAssetId = value.AsUInt64(); return true; } },
    FieldBinding{ "horizonHeight", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::WorldBackdropComponent*>(component)->horizonHeight }; },
        [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Float || !std::isfinite(value.AsFloat())) return false; static_cast<kb::scene::WorldBackdropComponent*>(component)->horizonHeight = value.AsFloat(); return true; } },
    FieldBinding{ "gradientExponent", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::WorldBackdropComponent*>(component)->gradientExponent }; },
        [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Float || !std::isfinite(value.AsFloat()) || value.AsFloat() <= 0.0F) return false; static_cast<kb::scene::WorldBackdropComponent*>(component)->gradientExponent = value.AsFloat(); return true; } },
    FieldBinding{ "priority", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<int>(static_cast<const kb::scene::WorldBackdropComponent*>(component)->priority) }; },
        [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Int) return false; static_cast<kb::scene::WorldBackdropComponent*>(component)->priority = value.AsInt(); return true; } },
    KB_BOOL(kb::scene::WorldBackdropComponent, enabled),
};

constexpr std::array<FieldBinding, 16> kAmbientRadianceFields{
    FieldBinding{ "mode", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<int>(static_cast<const kb::scene::AmbientRadianceComponent*>(component)->mode) }; }, [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Int) return false; const auto mode = static_cast<kb::scene::AmbientRadianceMode>(value.AsInt()); if (!kb::scene::IsAmbientRadianceModeValid(mode)) return false; static_cast<kb::scene::AmbientRadianceComponent*>(component)->mode = mode; return true; } },
    KB_NESTED_FLOAT(kb::scene::AmbientRadianceComponent, color, x), KB_NESTED_FLOAT(kb::scene::AmbientRadianceComponent, color, y), KB_NESTED_FLOAT(kb::scene::AmbientRadianceComponent, color, z),
    KB_NESTED_FLOAT(kb::scene::AmbientRadianceComponent, horizonColor, x), KB_NESTED_FLOAT(kb::scene::AmbientRadianceComponent, horizonColor, y), KB_NESTED_FLOAT(kb::scene::AmbientRadianceComponent, horizonColor, z),
    KB_NESTED_FLOAT(kb::scene::AmbientRadianceComponent, zenithColor, x), KB_NESTED_FLOAT(kb::scene::AmbientRadianceComponent, zenithColor, y), KB_NESTED_FLOAT(kb::scene::AmbientRadianceComponent, zenithColor, z),
    FieldBinding{ "environmentAssetId", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::AmbientRadianceComponent*>(component)->environmentAssetId, ScriptValueType::Hash }; }, [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Hash) return false; static_cast<kb::scene::AmbientRadianceComponent*>(component)->environmentAssetId = value.AsUInt64(); return true; } },
    FieldBinding{ "intensity", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::AmbientRadianceComponent*>(component)->intensity }; }, [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Float || !std::isfinite(value.AsFloat()) || value.AsFloat() < 0.0F) return false; static_cast<kb::scene::AmbientRadianceComponent*>(component)->intensity = value.AsFloat(); return true; } },
    FieldBinding{ "diffuseIntensity", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::AmbientRadianceComponent*>(component)->diffuseIntensity }; }, [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Float || !std::isfinite(value.AsFloat()) || value.AsFloat() < 0.0F) return false; static_cast<kb::scene::AmbientRadianceComponent*>(component)->diffuseIntensity = value.AsFloat(); return true; } },
    FieldBinding{ "specularIntensity", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::AmbientRadianceComponent*>(component)->specularIntensity }; }, [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Float || !std::isfinite(value.AsFloat()) || value.AsFloat() < 0.0F) return false; static_cast<kb::scene::AmbientRadianceComponent*>(component)->specularIntensity = value.AsFloat(); return true; } },
    FieldBinding{ "priority", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<int>(static_cast<const kb::scene::AmbientRadianceComponent*>(component)->priority) }; }, [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Int) return false; static_cast<kb::scene::AmbientRadianceComponent*>(component)->priority = value.AsInt(); return true; } },
    KB_BOOL(kb::scene::AmbientRadianceComponent, enabled),
};

constexpr std::array<FieldBinding, 6> kDetailSwitchFields{
    FieldBinding{ "groupId", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::SceneDetailSwitchComponent*>(component)->groupId, ScriptValueType::Hash }; },
        [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Hash && (value.Type() != ScriptValueType::Int || value.AsInt() < 0)) return false; static_cast<kb::scene::SceneDetailSwitchComponent*>(component)->groupId = value.Type() == ScriptValueType::Hash ? value.AsUInt64() : static_cast<std::uint64_t>(value.AsInt()); return true; } },
    FieldBinding{ "minimumLod", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::SceneDetailSwitchComponent*>(component)->minimumLod }; },
        [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::UInt32 && (value.Type() != ScriptValueType::Int || value.AsInt() < 0)) return false; static_cast<kb::scene::SceneDetailSwitchComponent*>(component)->minimumLod = value.Type() == ScriptValueType::UInt32 ? value.AsUInt32() : static_cast<std::uint32_t>(value.AsInt()); return true; } },
    FieldBinding{ "maximumLod", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::SceneDetailSwitchComponent*>(component)->maximumLod }; },
        [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::UInt32 && (value.Type() != ScriptValueType::Int || value.AsInt() < 0)) return false; static_cast<kb::scene::SceneDetailSwitchComponent*>(component)->maximumLod = value.Type() == ScriptValueType::UInt32 ? value.AsUInt32() : static_cast<std::uint32_t>(value.AsInt()); return true; } },
    FieldBinding{ "promoteCoverage", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::SceneDetailSwitchComponent*>(component)->promoteCoverage }; },
        [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Float || !std::isfinite(value.AsFloat()) || value.AsFloat() < 0.0F || value.AsFloat() > 1.0F) return false; static_cast<kb::scene::SceneDetailSwitchComponent*>(component)->promoteCoverage = value.AsFloat(); return true; } },
    FieldBinding{ "demoteCoverage", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::SceneDetailSwitchComponent*>(component)->demoteCoverage }; },
        [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Float || !std::isfinite(value.AsFloat()) || value.AsFloat() < 0.0F || value.AsFloat() > 1.0F) return false; static_cast<kb::scene::SceneDetailSwitchComponent*>(component)->demoteCoverage = value.AsFloat(); return true; } },
    KB_BOOL(kb::scene::SceneDetailSwitchComponent, enabled),
};

constexpr std::array<FieldBinding, 7> kVisibilityBlockerFields{
    FieldBinding{ "localCenter.x", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::SceneVisibilityBlockerComponent*>(component)->localCenter.x }; }, [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Float || !std::isfinite(value.AsFloat())) return false; static_cast<kb::scene::SceneVisibilityBlockerComponent*>(component)->localCenter.x = value.AsFloat(); return true; } },
    FieldBinding{ "localCenter.y", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::SceneVisibilityBlockerComponent*>(component)->localCenter.y }; }, [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Float || !std::isfinite(value.AsFloat())) return false; static_cast<kb::scene::SceneVisibilityBlockerComponent*>(component)->localCenter.y = value.AsFloat(); return true; } },
    FieldBinding{ "localCenter.z", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::SceneVisibilityBlockerComponent*>(component)->localCenter.z }; }, [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Float || !std::isfinite(value.AsFloat())) return false; static_cast<kb::scene::SceneVisibilityBlockerComponent*>(component)->localCenter.z = value.AsFloat(); return true; } },
    FieldBinding{ "size.x", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::SceneVisibilityBlockerComponent*>(component)->size.x }; }, [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Float || !std::isfinite(value.AsFloat()) || value.AsFloat() <= 0.0F) return false; static_cast<kb::scene::SceneVisibilityBlockerComponent*>(component)->size.x = value.AsFloat(); return true; } },
    FieldBinding{ "size.y", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::SceneVisibilityBlockerComponent*>(component)->size.y }; }, [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Float || !std::isfinite(value.AsFloat()) || value.AsFloat() <= 0.0F) return false; static_cast<kb::scene::SceneVisibilityBlockerComponent*>(component)->size.y = value.AsFloat(); return true; } },
    FieldBinding{ "size.z", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::SceneVisibilityBlockerComponent*>(component)->size.z }; }, [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Float || !std::isfinite(value.AsFloat()) || value.AsFloat() <= 0.0F) return false; static_cast<kb::scene::SceneVisibilityBlockerComponent*>(component)->size.z = value.AsFloat(); return true; } },
    KB_BOOL(kb::scene::SceneVisibilityBlockerComponent, enabled),
};

constexpr std::array<FieldBinding, 4> kVisibilityCellFields{
    FieldBinding{ "membershipMask", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<int>(static_cast<const kb::scene::VisibilityCellComponent*>(component)->membershipMask) }; }, [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Int || value.AsInt() <= 0) return false; static_cast<kb::scene::VisibilityCellComponent*>(component)->membershipMask = static_cast<std::uint32_t>(value.AsInt()); return true; } },
    FieldBinding{ "membership", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<int>(static_cast<const kb::scene::VisibilityCellComponent*>(component)->membership) }; }, [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Int || value.AsInt() < 0 || value.AsInt() > static_cast<int>(kb::scene::VisibilityCellMembership::Exclude)) return false; static_cast<kb::scene::VisibilityCellComponent*>(component)->membership = static_cast<kb::scene::VisibilityCellMembership>(value.AsInt()); return true; } },
    FieldBinding{ "visibilityOverride", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<int>(static_cast<const kb::scene::VisibilityCellComponent*>(component)->visibilityOverride) }; }, [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Int || value.AsInt() < 0 || value.AsInt() > static_cast<int>(kb::scene::VisibilityCellOverride::ForceHidden)) return false; static_cast<kb::scene::VisibilityCellComponent*>(component)->visibilityOverride = static_cast<kb::scene::VisibilityCellOverride>(value.AsInt()); return true; } },
    KB_BOOL(kb::scene::VisibilityCellComponent, enabled),
};

constexpr std::array<FieldBinding, 4> kRegionPortalFields{
    FieldBinding{ "sourceCell", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::SceneRegionPortalComponent*>(component)->sourceCell.Id(), ScriptValueType::Entity }; }, [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Entity || value.AsUInt64() == 0U) return false; static_cast<kb::scene::SceneRegionPortalComponent*>(component)->sourceCell = kb::scene::SceneEntity{ value.AsUInt64() }; return true; } },
    FieldBinding{ "targetCell", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::SceneRegionPortalComponent*>(component)->targetCell.Id(), ScriptValueType::Entity }; }, [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Entity || value.AsUInt64() == 0U) return false; static_cast<kb::scene::SceneRegionPortalComponent*>(component)->targetCell = kb::scene::SceneEntity{ value.AsUInt64() }; return true; } },
    FieldBinding{ "purposes", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::SceneRegionPortalComponent*>(component)->purposes }; }, [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::UInt32 || !kb::scene::IsRegionPortalPurposeMaskValid(value.AsUInt32())) return false; static_cast<kb::scene::SceneRegionPortalComponent*>(component)->purposes = value.AsUInt32(); return true; } },
    KB_BOOL(kb::scene::SceneRegionPortalComponent, enabled),
};

constexpr std::array<FieldBinding, 9> kSecondaryFrameFields{
    FieldBinding{ "mode", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<int>(static_cast<const kb::scene::AuxFrameComponent*>(component)->mode) }; }, [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Int || value.AsInt() < 0 || value.AsInt() > static_cast<int>(kb::scene::AuxFrameMode::Panoramic)) return false; static_cast<kb::scene::AuxFrameComponent*>(component)->mode = static_cast<kb::scene::AuxFrameMode>(value.AsInt()); return true; } },
    FieldBinding{ "imageTargetId", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::AuxFrameComponent*>(component)->imageTargetId, ScriptValueType::Hash }; }, [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Hash) return false; static_cast<kb::scene::AuxFrameComponent*>(component)->imageTargetId = value.AsUInt64(); return true; } },
    FieldBinding{ "width", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<std::uint32_t>(static_cast<const kb::scene::AuxFrameComponent*>(component)->width) }; }, [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::UInt32 || value.AsUInt32() == 0U || value.AsUInt32() > UINT16_MAX) return false; static_cast<kb::scene::AuxFrameComponent*>(component)->width = static_cast<std::uint16_t>(value.AsUInt32()); return true; } },
    FieldBinding{ "height", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<std::uint32_t>(static_cast<const kb::scene::AuxFrameComponent*>(component)->height) }; }, [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::UInt32 || value.AsUInt32() == 0U || value.AsUInt32() > UINT16_MAX) return false; static_cast<kb::scene::AuxFrameComponent*>(component)->height = static_cast<std::uint16_t>(value.AsUInt32()); return true; } },
    FieldBinding{ "mirrorPlaneNormal.x", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::AuxFrameComponent*>(component)->mirrorPlaneNormal.x }; }, [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Float || !std::isfinite(value.AsFloat())) return false; static_cast<kb::scene::AuxFrameComponent*>(component)->mirrorPlaneNormal.x = value.AsFloat(); return true; } },
    FieldBinding{ "mirrorPlaneNormal.y", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::AuxFrameComponent*>(component)->mirrorPlaneNormal.y }; }, [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Float || !std::isfinite(value.AsFloat())) return false; static_cast<kb::scene::AuxFrameComponent*>(component)->mirrorPlaneNormal.y = value.AsFloat(); return true; } },
    FieldBinding{ "mirrorPlaneNormal.z", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::AuxFrameComponent*>(component)->mirrorPlaneNormal.z }; }, [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Float || !std::isfinite(value.AsFloat())) return false; static_cast<kb::scene::AuxFrameComponent*>(component)->mirrorPlaneNormal.z = value.AsFloat(); return true; } },
    FieldBinding{ "mirrorPlaneOffset", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::AuxFrameComponent*>(component)->mirrorPlaneOffset }; }, [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Float || !std::isfinite(value.AsFloat())) return false; static_cast<kb::scene::AuxFrameComponent*>(component)->mirrorPlaneOffset = value.AsFloat(); return true; } },
    KB_BOOL(kb::scene::AuxFrameComponent, enabled),
};

constexpr std::array<FieldBinding, 14> kGeometrySwarmFields{
    FieldBinding{ "meshAssetId", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::GeometrySwarmComponent*>(component)->meshAssetId, ScriptValueType::Hash }; },
        [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Hash) return false; static_cast<kb::scene::GeometrySwarmComponent*>(component)->meshAssetId = value.AsUInt64(); return true; } },
    FieldBinding{ "materialAssetId", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::GeometrySwarmComponent*>(component)->materialAssetId, ScriptValueType::Hash }; },
        [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Hash) return false; static_cast<kb::scene::GeometrySwarmComponent*>(component)->materialAssetId = value.AsUInt64(); return true; } },
    FieldBinding{ "instanceCount", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::GeometrySwarmComponent*>(component)->instanceCount }; },
        [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::UInt32 || value.AsUInt32() == 0U || value.AsUInt32() > (1U << 20U)) return false; static_cast<kb::scene::GeometrySwarmComponent*>(component)->instanceCount = value.AsUInt32(); return true; } },
    FieldBinding{ "columns", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<std::uint32_t>(static_cast<const kb::scene::GeometrySwarmComponent*>(component)->columns) }; },
        [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::UInt32 || value.AsUInt32() == 0U || value.AsUInt32() > UINT16_MAX) return false; static_cast<kb::scene::GeometrySwarmComponent*>(component)->columns = static_cast<std::uint16_t>(value.AsUInt32()); return true; } },
    FieldBinding{ "rows", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<std::uint32_t>(static_cast<const kb::scene::GeometrySwarmComponent*>(component)->rows) }; },
        [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::UInt32 || value.AsUInt32() == 0U || value.AsUInt32() > UINT16_MAX) return false; static_cast<kb::scene::GeometrySwarmComponent*>(component)->rows = static_cast<std::uint16_t>(value.AsUInt32()); return true; } },
    FieldBinding{ "layers", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<std::uint32_t>(static_cast<const kb::scene::GeometrySwarmComponent*>(component)->layers) }; },
        [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::UInt32 || value.AsUInt32() == 0U || value.AsUInt32() > UINT16_MAX) return false; static_cast<kb::scene::GeometrySwarmComponent*>(component)->layers = static_cast<std::uint16_t>(value.AsUInt32()); return true; } },
    FieldBinding{ "spacing.x", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::GeometrySwarmComponent*>(component)->spacing.x }; },
        [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Float || !std::isfinite(value.AsFloat())) return false; static_cast<kb::scene::GeometrySwarmComponent*>(component)->spacing.x = value.AsFloat(); return true; } },
    FieldBinding{ "spacing.y", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::GeometrySwarmComponent*>(component)->spacing.y }; },
        [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Float || !std::isfinite(value.AsFloat())) return false; static_cast<kb::scene::GeometrySwarmComponent*>(component)->spacing.y = value.AsFloat(); return true; } },
    FieldBinding{ "spacing.z", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::GeometrySwarmComponent*>(component)->spacing.z }; },
        [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Float || !std::isfinite(value.AsFloat())) return false; static_cast<kb::scene::GeometrySwarmComponent*>(component)->spacing.z = value.AsFloat(); return true; } },
    FieldBinding{ "instanceScale", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::GeometrySwarmComponent*>(component)->instanceScale }; },
        [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Float || !std::isfinite(value.AsFloat()) || value.AsFloat() <= 0.0F) return false; static_cast<kb::scene::GeometrySwarmComponent*>(component)->instanceScale = value.AsFloat(); return true; } },
    FieldBinding{ "layer", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::GeometrySwarmComponent*>(component)->layer }; },
        [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::UInt32) return false; static_cast<kb::scene::GeometrySwarmComponent*>(component)->layer = value.AsUInt32(); return true; } },
    KB_BOOL(kb::scene::GeometrySwarmComponent, castsShadow),
    KB_BOOL(kb::scene::GeometrySwarmComponent, receivesShadow),
    KB_BOOL(kb::scene::GeometrySwarmComponent, enabled),
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
void MarkNavAgentModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { scene.Components().NavAgents().MarkModified(entity); }
void MarkNavObstacleModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { scene.Components().NavObstacles().MarkModified(entity); }
void MarkTagsModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { scene.Components().Tags().MarkModified(entity); }
void MarkRegionShapeModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { scene.Components().RegionShapes().MarkModified(entity); }
void MarkGuideCurveModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { scene.Components().GuideCurves().MarkModified(entity); }
void MarkContentInstanceModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { scene.Components().ContentInstances().MarkModified(entity); }
void MarkStreamFocusModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { scene.Components().StreamFocuses().MarkModified(entity); }
void MarkWorldBackdropModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { scene.Components().WorldBackdrops().MarkModified(entity); }
void MarkAmbientRadianceModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { scene.Components().AmbientRadiances().MarkModified(entity); }
void MarkDetailSwitchModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { scene.Components().DetailSwitches().MarkModified(entity); }
void MarkVisibilityBlockerModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { scene.Components().VisibilityBlockers().MarkModified(entity); }
void MarkVisibilityCellModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { scene.Components().VisibilityCells().MarkModified(entity); }
void MarkRegionPortalModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { scene.Components().RegionPortals().MarkModified(entity); }
void MarkSecondaryFrameModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { scene.Components().AuxFrames().MarkModified(entity); }
void MarkGeometrySwarmModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { scene.Components().GeometrySwarms().MarkModified(entity); }

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
    if (componentName == "3D Radiance Emitter" || componentName == "Light") {
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
    if (componentName == "NavAgent") {
        kb::scene::NavAgent* component = scene.Components().NavAgents().TryGet(entity);
        return ComponentAccess{ component, component, kNavAgentFields, &MarkNavAgentModified };
    }
    if (componentName == "NavObstacle") {
        kb::scene::NavObstacle* component = scene.Components().NavObstacles().TryGet(entity);
        return ComponentAccess{ component, component, kNavObstacleFields, &MarkNavObstacleModified };
    }
    if (componentName == "Tags") {
        kb::scene::TagsComponent* component = scene.Components().Tags().TryGet(entity);
        return ComponentAccess{ component, component, kTagsFields, &MarkTagsModified };
    }
    if (componentName == "RegionShape") {
        kb::scene::RegionShapeComponent* component = scene.Components().RegionShapes().TryGet(entity);
        return ComponentAccess{ component, component, kRegionShapeFields, &MarkRegionShapeModified };
    }
    if (componentName == "GuideCurve") {
        kb::scene::GuideCurveComponent* component = scene.Components().GuideCurves().TryGet(entity);
        return ComponentAccess{ component, component, kGuideCurveFields, &MarkGuideCurveModified };
    }
    if (componentName == "ContentInstance") {
        kb::scene::ContentInstanceComponent* component = scene.Components().ContentInstances().TryGet(entity);
        return ComponentAccess{ component, component, kContentInstanceFields, &MarkContentInstanceModified };
    }
    if (componentName == "StreamFocus") {
        kb::scene::StreamFocusComponent* component = scene.Components().StreamFocuses().TryGet(entity);
        return ComponentAccess{ component, component, kStreamFocusFields, &MarkStreamFocusModified };
    }
    if (componentName == "WorldBackdrop") {
        kb::scene::WorldBackdropComponent* component = scene.Components().WorldBackdrops().TryGet(entity);
        return ComponentAccess{ component, component, kWorldBackdropFields, &MarkWorldBackdropModified };
    }
    if (componentName == "Ambient Radiance") {
        kb::scene::AmbientRadianceComponent* component = scene.Components().AmbientRadiances().TryGet(entity);
        return ComponentAccess{ component, component, kAmbientRadianceFields, &MarkAmbientRadianceModified };
    }
    if (componentName == "Detail Switch") {
        kb::scene::SceneDetailSwitchComponent* component = scene.Components().DetailSwitches().TryGet(entity);
        return ComponentAccess{ component, component, kDetailSwitchFields, &MarkDetailSwitchModified };
    }
    if (componentName == "Visibility Blocker") {
        kb::scene::SceneVisibilityBlockerComponent* component = scene.Components().VisibilityBlockers().TryGet(entity);
        return ComponentAccess{ component, component, kVisibilityBlockerFields, &MarkVisibilityBlockerModified };
    }
    if (componentName == "Visibility Cell") {
        kb::scene::VisibilityCellComponent* component = scene.Components().VisibilityCells().TryGet(entity);
        return ComponentAccess{ component, component, kVisibilityCellFields, &MarkVisibilityCellModified };
    }
    if (componentName == "Region Portal") {
        kb::scene::SceneRegionPortalComponent* component = scene.Components().RegionPortals().TryGet(entity);
        return ComponentAccess{ component, component, kRegionPortalFields, &MarkRegionPortalModified };
    }
    if (componentName == "Secondary Frame") {
        kb::scene::AuxFrameComponent* component = scene.Components().AuxFrames().TryGet(entity);
        return ComponentAccess{ component, component, kSecondaryFrameFields, &MarkSecondaryFrameModified };
    }
    if (componentName == "Geometry Swarm") {
        kb::scene::GeometrySwarmComponent* component = scene.Components().GeometrySwarms().TryGet(entity);
        return ComponentAccess{ component, component, kGeometrySwarmFields, &MarkGeometrySwarmModified };
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
    if (componentName == "RegionShape") {
        return kRegionShapePropertyDescs;
    }
    if (componentName == "GuideCurve") return kGuideCurvePropertyDescs;
    if (componentName == "ContentInstance") return kContentInstancePropertyDescs;
    if (componentName == "StreamFocus") return kStreamFocusPropertyDescs;
    if (componentName == "WorldBackdrop") return kWorldBackdropPropertyDescs;
    if (componentName == "Ambient Radiance") return kAmbientRadiancePropertyDescs;
    if (componentName == "Detail Switch") return kDetailSwitchPropertyDescs;
    if (componentName == "Visibility Blocker") return kVisibilityBlockerPropertyDescs;
    if (componentName == "Visibility Cell") return kVisibilityCellPropertyDescs;
    if (componentName == "Region Portal") return kRegionPortalPropertyDescs;
    if (componentName == "Secondary Frame") return kSecondaryFramePropertyDescs;
    if (componentName == "Geometry Swarm") return kGeometrySwarmPropertyDescs;
    if (componentName == "Camera") {
        return kCameraPropertyDescs;
    }
    if (componentName == "3D Radiance Emitter" || componentName == "Light") {
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
    if (componentName == "NavAgent") return kNavAgentPropertyDescs;
    if (componentName == "NavObstacle") return kNavObstaclePropertyDescs;
    if (componentName == "Tags") return kTagsPropertyDescs;
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

    if (componentName == "Region Portal" && (propertyName == "sourceCell" || propertyName == "targetCell")) {
        const kb::scene::SceneEntity cell{ value.AsUInt64() };
        const kb::scene::SceneRegionPortalComponent& portal = *static_cast<const kb::scene::SceneRegionPortalComponent*>(component.immutable);
        if (value.Type() != ScriptValueType::Entity || cell == entity || !scene.Entities().IsAlive(cell) ||
            !scene.Components().VisibilityCells().Has(cell) ||
            (propertyName == "sourceCell" && cell == portal.targetCell) ||
            (propertyName == "targetCell" && cell == portal.sourceCell)) {
            return ScriptSceneComponentMutationResult{ .error = "portal cell must reference a distinct live Visibility Cell" };
        }
    }

    if (componentName == "Secondary Frame") {
        const kb::scene::AuxFrameComponent& frame = *static_cast<const kb::scene::AuxFrameComponent*>(component.immutable);
        kb::scene::AuxFrameComponent candidate = frame;
        if (!field->write(&candidate, value)) {
            return ScriptSceneComponentMutationResult{ .error = "script value type does not match component property" };
        }
        if (!kb::scene::IsAuxFrameComponentPersistable(candidate) ||
            (candidate.enabled && !kb::scene::IsAuxFrameComponentValid(candidate))) {
            return ScriptSceneComponentMutationResult{ .error = "Secondary Frame values must remain valid before the frame can be enabled" };
        }
    }

    if (componentName == "Geometry Swarm") {
        const kb::scene::GeometrySwarmComponent& swarm = *static_cast<const kb::scene::GeometrySwarmComponent*>(component.immutable);
        kb::scene::GeometrySwarmComponent candidate = swarm;
        if (!field->write(&candidate, value)) {
            return ScriptSceneComponentMutationResult{ .error = "script value type does not match component property" };
        }
        if (!kb::scene::IsGeometrySwarmComponentPersistable(candidate) ||
            (candidate.enabled && !kb::scene::IsGeometrySwarmComponentValid(candidate))) {
            return ScriptSceneComponentMutationResult{ .error = "Geometry Swarm values must remain valid before the swarm can be enabled" };
        }
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
