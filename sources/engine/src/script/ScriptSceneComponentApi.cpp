#include "engine/script/ScriptSceneComponentApi.hpp"

#include "script/ScriptUIComponentApi.hpp"

#include "engine/assets/AssetKind.hpp"
#include "engine/scene/AudioListenerComponent.hpp"
#include "engine/scene/AudioSourceComponent.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneAudioListenerComponents.hpp"
#include "engine/scene/SceneAudioSourceComponents.hpp"
#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/SceneComponentAuthoring.hpp"
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
#include "engine/scene/GridComponent.hpp"
#include "engine/scene/SceneGridComponents.hpp"
#include "engine/scene/TilemapComponent.hpp"
#include "engine/scene/SceneTilemapComponents.hpp"
#include "engine/scene/TilemapRendererComponent.hpp"
#include "engine/scene/SceneTilemapRendererComponents.hpp"
#include "engine/scene/TilemapColliderComponent.hpp"
#include "engine/scene/SceneTilemapColliderComponents.hpp"
#include "engine/scene/CharacterController2DComponent.hpp"
#include "engine/scene/SceneCharacterController2DComponents.hpp"
#include "engine/scene/Collider2DComponent.hpp"
#include "engine/scene/SceneCollider2DComponents.hpp"
#include "engine/scene/Body2DComponent.hpp"
#include "engine/scene/SceneBody2DComponents.hpp"
#include "engine/scene/Sprite2DComponent.hpp"
#include "engine/scene/SceneSprite2DComponents.hpp"
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
#include "engine/scene/SurfaceCastComponent.hpp"
#include "engine/scene/FacingPanelComponent.hpp"
#include "engine/scene/SpaceStrokeComponent.hpp"
#include "engine/scene/AIBrainComponent.hpp"
#include "engine/scene/AnimationAssets.hpp"
#include "engine/scene/DrawD3DeformedGeometryComponent.hpp"
#include "engine/scene/HistoryRibbonComponent.hpp"
#include "engine/scene/ParticleEffectComponent.hpp"
#include "engine/scene/LensEchoComponent.hpp"
#include "engine/scene/SceneBehaviourComponents.hpp"
#include "engine/scene/SceneCameraComponents.hpp"
#include "engine/scene/SceneCharacterControllerComponents.hpp"
#include "engine/scene/SceneColliderComponents.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneTagCatalog.hpp"
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
#include "engine/scene/SceneSurfaceCastComponents.hpp"
#include "engine/scene/SceneFacingPanelComponents.hpp"
#include "engine/scene/SceneSpaceStrokeComponents.hpp"
#include "engine/scene/SceneAnimatorComponents.hpp"
#include "engine/scene/SceneDeformedGeometryComponents.hpp"
#include "engine/scene/SceneHistoryRibbonComponents.hpp"
#include "engine/scene/SceneParticleEffectComponents.hpp"
#include "engine/scene/SceneLensEchoComponents.hpp"
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
#include <vector>

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

#define KB_LIGHT_MOBILITY(Component, field) \
    FieldBinding{ #field, \
        [](const void* component) noexcept -> ScriptValue { \
            KB_ASSERT_NOT_POINTER(static_cast<const Component*>(component)->field); \
            return ScriptValue{ static_cast<int>(static_cast<const Component*>(component)->field) }; }, \
        [](void* component, const ScriptValue& value) noexcept -> bool { \
            if (value.Type() != ScriptValueType::Int || value.AsInt() < 0 || value.AsInt() > static_cast<int>(kb::scene::LightMobility::Static)) { return false; } \
            static_cast<Component*>(component)->field = static_cast<kb::scene::LightMobility>(value.AsInt()); \
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
            if (value.Type() != ScriptValueType::Int || value.AsInt() < 0 || value.AsInt() > static_cast<int>(kb::scene::ColliderShape::OneWayBox)) { return false; } \
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

#define KB_NAV_LINK_KIND(Component, field) \
    FieldBinding{ #field, \
        [](const void* component) noexcept -> ScriptValue { \
            KB_ASSERT_NOT_POINTER(static_cast<const Component*>(component)->field); \
            return ScriptValue{ static_cast<int>(static_cast<const Component*>(component)->field) }; }, \
        [](void* component, const ScriptValue& value) noexcept -> bool { \
            if (value.Type() != ScriptValueType::Int || value.AsInt() < 0 || value.AsInt() > static_cast<int>(kb::scene::NavLinkKind::Walk)) { return false; } \
            static_cast<Component*>(component)->field = static_cast<kb::scene::NavLinkKind>(value.AsInt()); \
            return true; \
        } }

#define KB_NAV_AREA(Component, field) \
    FieldBinding{ #field, \
        [](const void* component) noexcept -> ScriptValue { \
            KB_ASSERT_NOT_POINTER(static_cast<const Component*>(component)->field); \
            return ScriptValue{ static_cast<int>(static_cast<const Component*>(component)->field) }; }, \
        [](void* component, const ScriptValue& value) noexcept -> bool { \
            if (value.Type() != ScriptValueType::Int || value.AsInt() < 0 || value.AsInt() >= static_cast<int>(kb::scene::kNavAreaCount)) { return false; } \
            static_cast<Component*>(component)->field = static_cast<kb::scene::NavAreaId>(value.AsInt()); \
            return true; \
        } }

// clang-format on

constexpr std::array<std::string_view, 48> kComponentNames{
    "AudioSource",
    "AudioListener",
    "Transform",
    "Visibility",
    "Camera",
    "3D Radiance Emitter",
    "Light",
    "MeshRenderer",
    "Behaviour",
    "Rigidbody",
    "Collider",
    "CharacterController",
    "Joint",
    "NavAgent",
    "NavObstacle",
    "NavLink",
    "Tags",
    "RegionShape",
    "GuideCurve",
    "Grid",
    "Tilemap",
    "TilemapRenderer",
    "TilemapCollider",
    "CharacterController2D",
    "Collider2D",
    "Body2D",
    "Sprite2D",
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
    "Surface Cast",
    "Facing Panel",
    "Line Renderer",
    "Trail Renderer",
    "Particle Effect",
    "Lens Flare",
    "Animator",
    "DeformedGeometry",
    "SocketAttachment",
    "Combatant",
    "AIBrain",
};

// Value names of Int enum properties, in value order (tools accept them in place of numbers).
constexpr std::array<std::string_view, 3> kRigidbodyBodyTypeNames{ "Static", "Dynamic", "Kinematic" };
constexpr std::array<std::string_view, 4> kJointTypeNames{ "Fixed", "Hinge", "Distance", "Point" };
constexpr std::array<std::string_view, 2> kCameraProjectionNames{ "Perspective", "Orthographic" };
constexpr std::array<std::string_view, 5> kRegionShapeKindNames{ "Circle2D", "Rectangle2D", "Sphere", "Box", "Capsule" };
constexpr std::array<std::string_view, 5> kContentInstanceKindNames{ "Prefab", "Subscene", "WorldFragment", "PartitionedWorld", "NavigationMesh" };
constexpr std::array<std::string_view, 2> kContentInstanceLifetimeNames{ "Owner", "Persistent" };
constexpr std::array<std::string_view, 3> kNavLinkKindNames{ "Jump", "Ladder", "Walk" };
constexpr std::array<std::string_view, 3> kCameraClearModeNames{ "SolidColor", "DepthOnly", "DontClear" };
constexpr std::array<std::string_view, 6> kLightKindNames{ "Directional", "Point", "Spot", "AreaRect", "AreaDisk", "Tube" };
constexpr std::array<std::string_view, 2> kLightMobilityNames{ "Movable", "Static" };
constexpr std::array<std::string_view, 6> kTickGroupNames{ "Input", "Gameplay", "Physics", "Animation", "Camera", "Presentation" };
constexpr std::array<std::string_view, 4> kColliderShapeNames{ "Box", "Sphere", "Capsule", "Mesh" };
constexpr std::array<std::string_view, 4> kAnimatorRootMotionOwnerNames{ "None", "Animator", "CharacterController", "Rigidbody" };

constexpr std::array<ScriptSceneComponentPropertyDesc, 10> kRegionShapePropertyDescs{
    ScriptSceneComponentPropertyDesc{ "kind", ScriptValueType::Int, true, kRegionShapeKindNames },
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

constexpr std::array<ScriptSceneComponentPropertyDesc, 3> kGridPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "cellSize", ScriptValueType::Float, true },
    ScriptSceneComponentPropertyDesc{ "floorHeight", ScriptValueType::Float, true },
    ScriptSceneComponentPropertyDesc{ "wall", ScriptValueType::Bool, true },
};

constexpr std::array<ScriptSceneComponentPropertyDesc, 2> kTilemapPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "mapAssetId", ScriptValueType::Hash, true },
    ScriptSceneComponentPropertyDesc{ "locked", ScriptValueType::Bool, true },
};

constexpr std::array<ScriptSceneComponentPropertyDesc, 2> kTilemapRendererPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "sortingOrder", ScriptValueType::Int, true },
    ScriptSceneComponentPropertyDesc{ "enabled", ScriptValueType::Bool, true },
};

constexpr std::array<ScriptSceneComponentPropertyDesc, 26> kSprite2DPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "imageAssetId", ScriptValueType::Hash, true },
    ScriptSceneComponentPropertyDesc{ "materialAssetId", ScriptValueType::Hash, true },
    ScriptSceneComponentPropertyDesc{ "cellWidth", ScriptValueType::Int, true },
    ScriptSceneComponentPropertyDesc{ "cellHeight", ScriptValueType::Int, true },
    ScriptSceneComponentPropertyDesc{ "margin", ScriptValueType::Int, true },
    ScriptSceneComponentPropertyDesc{ "spacing", ScriptValueType::Int, true },
    ScriptSceneComponentPropertyDesc{ "imageWidth", ScriptValueType::Int, true },
    ScriptSceneComponentPropertyDesc{ "imageHeight", ScriptValueType::Int, true },
    ScriptSceneComponentPropertyDesc{ "cell", ScriptValueType::Int, true },
    ScriptSceneComponentPropertyDesc{ "frameCount", ScriptValueType::Int, true },
    ScriptSceneComponentPropertyDesc{ "framesPerSecond", ScriptValueType::Float, true },
    ScriptSceneComponentPropertyDesc{ "loop", ScriptValueType::Bool, true },
    ScriptSceneComponentPropertyDesc{ "playing", ScriptValueType::Bool, true },
    ScriptSceneComponentPropertyDesc{ "time", ScriptValueType::Float, true },
    ScriptSceneComponentPropertyDesc{ "pixelsPerUnit", ScriptValueType::Float, true },
    ScriptSceneComponentPropertyDesc{ "pivotX", ScriptValueType::Float, true },
    ScriptSceneComponentPropertyDesc{ "pivotY", ScriptValueType::Float, true },
    ScriptSceneComponentPropertyDesc{ "flipX", ScriptValueType::Bool, true },
    ScriptSceneComponentPropertyDesc{ "flipY", ScriptValueType::Bool, true },
    ScriptSceneComponentPropertyDesc{ "tintR", ScriptValueType::Float, true },
    ScriptSceneComponentPropertyDesc{ "tintG", ScriptValueType::Float, true },
    ScriptSceneComponentPropertyDesc{ "tintB", ScriptValueType::Float, true },
    ScriptSceneComponentPropertyDesc{ "tintA", ScriptValueType::Float, true },
    ScriptSceneComponentPropertyDesc{ "sortingOrder", ScriptValueType::Int, true },
    ScriptSceneComponentPropertyDesc{ "wall", ScriptValueType::Bool, true },
    ScriptSceneComponentPropertyDesc{ "visible", ScriptValueType::Bool, true },
};

constexpr std::array<ScriptSceneComponentPropertyDesc, 11> kBody2DPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "type", ScriptValueType::Int, true },
    ScriptSceneComponentPropertyDesc{ "gravityScale", ScriptValueType::Float, true },
    ScriptSceneComponentPropertyDesc{ "linearDamping", ScriptValueType::Float, true },
    ScriptSceneComponentPropertyDesc{ "angularDamping", ScriptValueType::Float, true },
    ScriptSceneComponentPropertyDesc{ "fixedRotation", ScriptValueType::Bool, true },
    ScriptSceneComponentPropertyDesc{ "bullet", ScriptValueType::Bool, true },
    ScriptSceneComponentPropertyDesc{ "allowSleep", ScriptValueType::Bool, true },
    ScriptSceneComponentPropertyDesc{ "sideView", ScriptValueType::Bool, true },
    ScriptSceneComponentPropertyDesc{ "startVelocityX", ScriptValueType::Float, true },
    ScriptSceneComponentPropertyDesc{ "startVelocityY", ScriptValueType::Float, true },
    ScriptSceneComponentPropertyDesc{ "enabled", ScriptValueType::Bool, true },
};

constexpr std::array<ScriptSceneComponentPropertyDesc, 15> kCollider2DPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "shape", ScriptValueType::Int, true },
    ScriptSceneComponentPropertyDesc{ "width", ScriptValueType::Float, true },
    ScriptSceneComponentPropertyDesc{ "height", ScriptValueType::Float, true },
    ScriptSceneComponentPropertyDesc{ "radius", ScriptValueType::Float, true },
    ScriptSceneComponentPropertyDesc{ "offsetX", ScriptValueType::Float, true },
    ScriptSceneComponentPropertyDesc{ "offsetY", ScriptValueType::Float, true },
    ScriptSceneComponentPropertyDesc{ "angleDegrees", ScriptValueType::Float, true },
    ScriptSceneComponentPropertyDesc{ "isTrigger", ScriptValueType::Bool, true },
    ScriptSceneComponentPropertyDesc{ "density", ScriptValueType::Float, true },
    ScriptSceneComponentPropertyDesc{ "friction", ScriptValueType::Float, true },
    ScriptSceneComponentPropertyDesc{ "restitution", ScriptValueType::Float, true },
    ScriptSceneComponentPropertyDesc{ "layer", ScriptValueType::Int, true },
    ScriptSceneComponentPropertyDesc{ "collidesWith", ScriptValueType::Int, true },
    ScriptSceneComponentPropertyDesc{ "sideView", ScriptValueType::Bool, true },
    ScriptSceneComponentPropertyDesc{ "enabled", ScriptValueType::Bool, true },
};

constexpr std::array<ScriptSceneComponentPropertyDesc, 10> kCharacterController2DPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "radius", ScriptValueType::Float, true },
    ScriptSceneComponentPropertyDesc{ "height", ScriptValueType::Float, true },
    ScriptSceneComponentPropertyDesc{ "gravityScale", ScriptValueType::Float, true },
    ScriptSceneComponentPropertyDesc{ "useGravity", ScriptValueType::Bool, true },
    ScriptSceneComponentPropertyDesc{ "slopeLimitDegrees", ScriptValueType::Float, true },
    ScriptSceneComponentPropertyDesc{ "stepHeight", ScriptValueType::Float, true },
    ScriptSceneComponentPropertyDesc{ "layer", ScriptValueType::Int, true },
    ScriptSceneComponentPropertyDesc{ "collidesWith", ScriptValueType::Int, true },
    ScriptSceneComponentPropertyDesc{ "sideView", ScriptValueType::Bool, true },
    ScriptSceneComponentPropertyDesc{ "enabled", ScriptValueType::Bool, true },
};

constexpr std::array<ScriptSceneComponentPropertyDesc, 1> kTilemapColliderPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "enabled", ScriptValueType::Bool, true },
};

constexpr std::array<ScriptSceneComponentPropertyDesc, 4> kContentInstancePropertyDescs{
    ScriptSceneComponentPropertyDesc{ "assetId", ScriptValueType::Hash },
    ScriptSceneComponentPropertyDesc{ "kind", ScriptValueType::Int, true, kContentInstanceKindNames },
    ScriptSceneComponentPropertyDesc{ "lifetime", ScriptValueType::Int, true, kContentInstanceLifetimeNames },
    ScriptSceneComponentPropertyDesc{ "active", ScriptValueType::Bool },
};

constexpr std::array<ScriptSceneComponentPropertyDesc, 5> kStreamFocusPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "innerRadius", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "outerRadius", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "priority", ScriptValueType::Int },
    ScriptSceneComponentPropertyDesc{ "loadMask", ScriptValueType::UInt32 },
    ScriptSceneComponentPropertyDesc{ "enabled", ScriptValueType::Bool },
};

constexpr std::array<ScriptSceneComponentPropertyDesc, 26> kWorldBackdropPropertyDescs{
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
    ScriptSceneComponentPropertyDesc{ "sunDisk", ScriptValueType::Int },
    ScriptSceneComponentPropertyDesc{ "sunSize", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "sunSizeConvergence", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "atmosphereThickness", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "skyTint.x", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "skyTint.y", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "skyTint.z", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "groundColor.x", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "groundColor.y", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "groundColor.z", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "exposure", ScriptValueType::Float },
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
constexpr std::array<ScriptSceneComponentPropertyDesc, 5> kSurfaceCastPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "materialAssetId", ScriptValueType::Hash }, ScriptSceneComponentPropertyDesc{ "receiverLayerMask", ScriptValueType::UInt32 },
    ScriptSceneComponentPropertyDesc{ "order", ScriptValueType::Int }, ScriptSceneComponentPropertyDesc{ "content", ScriptValueType::Int }, ScriptSceneComponentPropertyDesc{ "enabled", ScriptValueType::Bool },
};
constexpr std::array<ScriptSceneComponentPropertyDesc, 11> kFacingPanelPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "mode", ScriptValueType::Int },
    ScriptSceneComponentPropertyDesc{ "targetPoint.x", ScriptValueType::Float }, ScriptSceneComponentPropertyDesc{ "targetPoint.y", ScriptValueType::Float }, ScriptSceneComponentPropertyDesc{ "targetPoint.z", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "axis.x", ScriptValueType::Float }, ScriptSceneComponentPropertyDesc{ "axis.y", ScriptValueType::Float }, ScriptSceneComponentPropertyDesc{ "axis.z", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "up.x", ScriptValueType::Float }, ScriptSceneComponentPropertyDesc{ "up.y", ScriptValueType::Float }, ScriptSceneComponentPropertyDesc{ "up.z", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "enabled", ScriptValueType::Bool },
};
constexpr std::array<ScriptSceneComponentPropertyDesc, 10> kSpaceStrokePropertyDescs{
    ScriptSceneComponentPropertyDesc{ "meshAssetId", ScriptValueType::Hash }, ScriptSceneComponentPropertyDesc{ "materialAssetId", ScriptValueType::Hash },
    ScriptSceneComponentPropertyDesc{ "mode", ScriptValueType::Int }, ScriptSceneComponentPropertyDesc{ "width", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "cableSag", ScriptValueType::Float }, ScriptSceneComponentPropertyDesc{ "splineSegments", ScriptValueType::UInt32 },
    ScriptSceneComponentPropertyDesc{ "layer", ScriptValueType::UInt32 }, ScriptSceneComponentPropertyDesc{ "castsShadow", ScriptValueType::Bool },
    ScriptSceneComponentPropertyDesc{ "receivesShadow", ScriptValueType::Bool }, ScriptSceneComponentPropertyDesc{ "enabled", ScriptValueType::Bool },
};
constexpr std::array<ScriptSceneComponentPropertyDesc, 9> kHistoryRibbonPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "meshAssetId", ScriptValueType::Hash }, ScriptSceneComponentPropertyDesc{ "materialAssetId", ScriptValueType::Hash },
    ScriptSceneComponentPropertyDesc{ "lifetimeSeconds", ScriptValueType::Float }, ScriptSceneComponentPropertyDesc{ "width", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "sampleIntervalSeconds", ScriptValueType::Float }, ScriptSceneComponentPropertyDesc{ "layer", ScriptValueType::UInt32 },
    ScriptSceneComponentPropertyDesc{ "castsShadow", ScriptValueType::Bool }, ScriptSceneComponentPropertyDesc{ "receivesShadow", ScriptValueType::Bool },
    ScriptSceneComponentPropertyDesc{ "enabled", ScriptValueType::Bool },
};
constexpr std::array<ScriptSceneComponentPropertyDesc, 9> kParticleEffectPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "effectAssetId", ScriptValueType::Hash },
    ScriptSceneComponentPropertyDesc{ "deterministicSeed", ScriptValueType::Hash },
    ScriptSceneComponentPropertyDesc{ "rateMultiplier", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "maxParticlesOverride", ScriptValueType::UInt32 },
    ScriptSceneComponentPropertyDesc{ "ownerDeathPolicy", ScriptValueType::Int },
    ScriptSceneComponentPropertyDesc{ "enabled", ScriptValueType::Bool },
    ScriptSceneComponentPropertyDesc{ "autoPlay", ScriptValueType::Bool },
    ScriptSceneComponentPropertyDesc{ "followTransform", ScriptValueType::Bool },
    ScriptSceneComponentPropertyDesc{ "restartOnActivate", ScriptValueType::Bool },
};
constexpr std::array<ScriptSceneComponentPropertyDesc, 7> kLensEchoPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "sourceEntityId", ScriptValueType::Hash }, ScriptSceneComponentPropertyDesc{ "profileMaterialAssetId", ScriptValueType::Hash },
    ScriptSceneComponentPropertyDesc{ "intensity", ScriptValueType::Float }, ScriptSceneComponentPropertyDesc{ "size", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "layer", ScriptValueType::UInt32 }, ScriptSceneComponentPropertyDesc{ "occlusionRule", ScriptValueType::Int },
    ScriptSceneComponentPropertyDesc{ "enabled", ScriptValueType::Bool },
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
    ScriptSceneComponentPropertyDesc{ "projection", ScriptValueType::Int, true, kCameraProjectionNames },
    ScriptSceneComponentPropertyDesc{ "verticalFovDegrees", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "orthographicHeight", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "nearClip", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "farClip", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "primary", ScriptValueType::Bool },
    ScriptSceneComponentPropertyDesc{ "viewportId", ScriptValueType::Int },
    ScriptSceneComponentPropertyDesc{ "priority", ScriptValueType::Int },
    ScriptSceneComponentPropertyDesc{ "cullingMask", ScriptValueType::Int },
    ScriptSceneComponentPropertyDesc{ "clearMode", ScriptValueType::Int, true, kCameraClearModeNames },
    ScriptSceneComponentPropertyDesc{ "clearColor.x", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "clearColor.y", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "clearColor.z", ScriptValueType::Float },
};

constexpr std::array<std::string_view, 4> kAudioAttenuationNames{ "None", "Inverse", "Linear", "Exponential" };
constexpr std::array<ScriptSceneComponentPropertyDesc, 16> kAudioSourcePropertyDescs{
    ScriptSceneComponentPropertyDesc{ "clipAssetId", ScriptValueType::Hash },
    ScriptSceneComponentPropertyDesc{ "volume", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "pitch", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "loop", ScriptValueType::Bool },
    ScriptSceneComponentPropertyDesc{ "spatial", ScriptValueType::Bool },
    ScriptSceneComponentPropertyDesc{ "autoplay", ScriptValueType::Bool },
    ScriptSceneComponentPropertyDesc{ "enabled", ScriptValueType::Bool },
    ScriptSceneComponentPropertyDesc{ "mute", ScriptValueType::Bool },
    ScriptSceneComponentPropertyDesc{ "pan", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "spatialBlend", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "attenuationModel", ScriptValueType::Int, true, kAudioAttenuationNames },
    ScriptSceneComponentPropertyDesc{ "minDistance", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "maxDistance", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "rolloff", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "dopplerFactor", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "outputBus", ScriptValueType::String },
};
constexpr std::array<ScriptSceneComponentPropertyDesc, 4> kAudioListenerPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "priority", ScriptValueType::Int },
    ScriptSceneComponentPropertyDesc{ "localUser", ScriptValueType::UInt32 },
    ScriptSceneComponentPropertyDesc{ "primary", ScriptValueType::Bool },
    ScriptSceneComponentPropertyDesc{ "enabled", ScriptValueType::Bool },
};

constexpr std::array<ScriptSceneComponentPropertyDesc, 20> kLightPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "kind", ScriptValueType::Int, true, kLightKindNames },
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
    ScriptSceneComponentPropertyDesc{ "sourceRadius", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "shadowStrength", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "shadowDistance", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "mobility", ScriptValueType::Int, true, kLightMobilityNames },
};

constexpr std::array<ScriptSceneComponentPropertyDesc, 4> kMeshRendererPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "materialSlotOverrideCount", ScriptValueType::Int },
    ScriptSceneComponentPropertyDesc{ "castsShadow", ScriptValueType::Bool },
    ScriptSceneComponentPropertyDesc{ "receivesShadow", ScriptValueType::Bool },
    ScriptSceneComponentPropertyDesc{ "layer", ScriptValueType::Int },
};

constexpr std::array<ScriptSceneComponentPropertyDesc, 3> kBehaviourPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "enabled", ScriptValueType::Bool },
    ScriptSceneComponentPropertyDesc{ "tickGroup", ScriptValueType::Int, true, kTickGroupNames },
    ScriptSceneComponentPropertyDesc{ "executionOrder", ScriptValueType::Int },
};

constexpr std::array<ScriptSceneComponentPropertyDesc, 14> kRigidbodyPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "bodyType", ScriptValueType::Int, true, kRigidbodyBodyTypeNames },
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
    ScriptSceneComponentPropertyDesc{ "linearDamping", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "angularDamping", ScriptValueType::Float },
};

constexpr std::array<ScriptSceneComponentPropertyDesc, 14> kColliderPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "shape", ScriptValueType::Int, true, kColliderShapeNames },
    ScriptSceneComponentPropertyDesc{ "meshAssetId", ScriptValueType::Hash },
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
constexpr std::array<ScriptSceneComponentPropertyDesc, 18> kJointPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "type", ScriptValueType::Int, true, kJointTypeNames },
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
    ScriptSceneComponentPropertyDesc{ "motor.targetVelocity", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "motor.force", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "spring.spring", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "spring.damper", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "breakForce", ScriptValueType::Float },
};

// controllerAssetId: kb_cli takes `controller=/Game/...kbanimcontroller`.
constexpr std::array<ScriptSceneComponentPropertyDesc, 5> kAnimatorPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "controllerAssetId", ScriptValueType::Hash },
    ScriptSceneComponentPropertyDesc{ "speed", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "poseUpdateRateHz", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "enabled", ScriptValueType::Bool },
    ScriptSceneComponentPropertyDesc{ "rootMotionOwner", ScriptValueType::Int, true, kAnimatorRootMotionOwnerNames },
};

// The mesh is chosen with its skeleton (kb_cli `skeletalMesh=` or MeshRenderer `mesh=`), so it reads only.
constexpr std::array<ScriptSceneComponentPropertyDesc, 8> kDeformedGeometryPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "skeletalMeshAssetId", ScriptValueType::Hash, false },
    ScriptSceneComponentPropertyDesc{ "lodBias", ScriptValueType::Int },
    ScriptSceneComponentPropertyDesc{ "lodEnabled", ScriptValueType::Bool },
    ScriptSceneComponentPropertyDesc{ "fixedBounds", ScriptValueType::Bool },
    ScriptSceneComponentPropertyDesc{ "castsShadow", ScriptValueType::Bool },
    ScriptSceneComponentPropertyDesc{ "receivesShadow", ScriptValueType::Bool },
    ScriptSceneComponentPropertyDesc{ "layer", ScriptValueType::Int },
    ScriptSceneComponentPropertyDesc{ "enabled", ScriptValueType::Bool },
};

// The AI preset (choosing one rewrites the rules), movement, and eight "when ... do ..." rules;
// editing a rule turns the preset to Custom.
constexpr std::array<ScriptSceneComponentPropertyDesc, 37> kAIBrainPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "preset", ScriptValueType::Int, true, kb::scene::kAIBrainPresetNames },
    ScriptSceneComponentPropertyDesc{ "moveSpeed", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "wanderRadius", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "leashDistance", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "rule1When", ScriptValueType::Int, true, kb::scene::kAIConditionNames },
    ScriptSceneComponentPropertyDesc{ "rule1WhenValue", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "rule1Do", ScriptValueType::Int, true, kb::scene::kAIActionNames },
    ScriptSceneComponentPropertyDesc{ "rule1DoValue", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "rule2When", ScriptValueType::Int, true, kb::scene::kAIConditionNames },
    ScriptSceneComponentPropertyDesc{ "rule2WhenValue", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "rule2Do", ScriptValueType::Int, true, kb::scene::kAIActionNames },
    ScriptSceneComponentPropertyDesc{ "rule2DoValue", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "rule3When", ScriptValueType::Int, true, kb::scene::kAIConditionNames },
    ScriptSceneComponentPropertyDesc{ "rule3WhenValue", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "rule3Do", ScriptValueType::Int, true, kb::scene::kAIActionNames },
    ScriptSceneComponentPropertyDesc{ "rule3DoValue", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "rule4When", ScriptValueType::Int, true, kb::scene::kAIConditionNames },
    ScriptSceneComponentPropertyDesc{ "rule4WhenValue", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "rule4Do", ScriptValueType::Int, true, kb::scene::kAIActionNames },
    ScriptSceneComponentPropertyDesc{ "rule4DoValue", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "rule5When", ScriptValueType::Int, true, kb::scene::kAIConditionNames },
    ScriptSceneComponentPropertyDesc{ "rule5WhenValue", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "rule5Do", ScriptValueType::Int, true, kb::scene::kAIActionNames },
    ScriptSceneComponentPropertyDesc{ "rule5DoValue", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "rule6When", ScriptValueType::Int, true, kb::scene::kAIConditionNames },
    ScriptSceneComponentPropertyDesc{ "rule6WhenValue", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "rule6Do", ScriptValueType::Int, true, kb::scene::kAIActionNames },
    ScriptSceneComponentPropertyDesc{ "rule6DoValue", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "rule7When", ScriptValueType::Int, true, kb::scene::kAIConditionNames },
    ScriptSceneComponentPropertyDesc{ "rule7WhenValue", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "rule7Do", ScriptValueType::Int, true, kb::scene::kAIActionNames },
    ScriptSceneComponentPropertyDesc{ "rule7DoValue", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "rule8When", ScriptValueType::Int, true, kb::scene::kAIConditionNames },
    ScriptSceneComponentPropertyDesc{ "rule8WhenValue", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "rule8Do", ScriptValueType::Int, true, kb::scene::kAIActionNames },
    ScriptSceneComponentPropertyDesc{ "rule8DoValue", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "enabled", ScriptValueType::Bool },
};

// Health, mana, resistances (percent), team, senses and the eight ability slots (.kbability ids).
constexpr std::array<ScriptSceneComponentPropertyDesc, 26> kCombatantPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "maximumHealth", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "health", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "healthRegeneration", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "maximumMana", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "mana", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "manaRegeneration", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "physicalResistance", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "fireResistance", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "coldResistance", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "lightningResistance", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "poisonResistance", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "team", ScriptValueType::Int },
    ScriptSceneComponentPropertyDesc{ "sightRange", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "sightAngleDegrees", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "hearingRange", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "requireLineOfSight", ScriptValueType::Bool },
    ScriptSceneComponentPropertyDesc{ "ability1", ScriptValueType::Hash },
    ScriptSceneComponentPropertyDesc{ "ability2", ScriptValueType::Hash },
    ScriptSceneComponentPropertyDesc{ "ability3", ScriptValueType::Hash },
    ScriptSceneComponentPropertyDesc{ "ability4", ScriptValueType::Hash },
    ScriptSceneComponentPropertyDesc{ "ability5", ScriptValueType::Hash },
    ScriptSceneComponentPropertyDesc{ "ability6", ScriptValueType::Hash },
    ScriptSceneComponentPropertyDesc{ "ability7", ScriptValueType::Hash },
    ScriptSceneComponentPropertyDesc{ "ability8", ScriptValueType::Hash },
    ScriptSceneComponentPropertyDesc{ "invulnerable", ScriptValueType::Bool },
    ScriptSceneComponentPropertyDesc{ "enabled", ScriptValueType::Bool },
};

// The socket (or bone) name and the offset in its space; rotation is a quaternion like Transform's.
// Scale is the object's own Transform's.
constexpr std::array<ScriptSceneComponentPropertyDesc, 9> kSocketAttachmentPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "socket", ScriptValueType::String },
    ScriptSceneComponentPropertyDesc{ "offset.position.x", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "offset.position.y", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "offset.position.z", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "offset.rotation.x", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "offset.rotation.y", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "offset.rotation.z", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "offset.rotation.w", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "enabled", ScriptValueType::Bool },
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

constexpr std::array<ScriptSceneComponentPropertyDesc, 11> kNavLinkPropertyDescs{
    ScriptSceneComponentPropertyDesc{ "start.x", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "start.y", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "start.z", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "end.x", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "end.y", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "end.z", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "radius", ScriptValueType::Float },
    ScriptSceneComponentPropertyDesc{ "kind", ScriptValueType::Int, true, kNavLinkKindNames },
    ScriptSceneComponentPropertyDesc{ "area", ScriptValueType::Int },
    ScriptSceneComponentPropertyDesc{ "bidirectional", ScriptValueType::Bool },
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

constexpr std::array<FieldBinding, 16> kAudioSourceFields{
    FieldBinding{ "clipAssetId",
        [](const void* c) noexcept -> ScriptValue {
            return ScriptValue{ static_cast<const kb::scene::AudioSourceComponent*>(c)->clipAssetId, ScriptValueType::Hash };
        },
        [](void* c, const ScriptValue& v) noexcept -> bool {
            if (v.Type() != ScriptValueType::Hash) return false;
            static_cast<kb::scene::AudioSourceComponent*>(c)->clipAssetId = v.AsUInt64();
            return true;
        } },
    KB_FLOAT(kb::scene::AudioSourceComponent, volume),
    KB_FLOAT(kb::scene::AudioSourceComponent, pitch),
    KB_BOOL(kb::scene::AudioSourceComponent, loop),
    KB_BOOL(kb::scene::AudioSourceComponent, spatial),
    KB_BOOL(kb::scene::AudioSourceComponent, autoplay),
    KB_BOOL(kb::scene::AudioSourceComponent, enabled),
    KB_BOOL(kb::scene::AudioSourceComponent, mute),
    KB_FLOAT(kb::scene::AudioSourceComponent, pan),
    KB_FLOAT(kb::scene::AudioSourceComponent, spatialBlend),
    FieldBinding{ "attenuationModel",
        [](const void* c) noexcept -> ScriptValue {
            return ScriptValue{ static_cast<int>(static_cast<const kb::scene::AudioSourceComponent*>(c)->attenuationModel) };
        },
        [](void* c, const ScriptValue& v) noexcept -> bool {
            if (v.Type() != ScriptValueType::Int) return false;
            static_cast<kb::scene::AudioSourceComponent*>(c)->attenuationModel = static_cast<kb::audio::AudioAttenuationModel>(v.AsInt());
            return true;
        } },
    KB_FLOAT(kb::scene::AudioSourceComponent, minDistance),
    KB_FLOAT(kb::scene::AudioSourceComponent, maxDistance),
    KB_FLOAT(kb::scene::AudioSourceComponent, rolloff),
    KB_FLOAT(kb::scene::AudioSourceComponent, dopplerFactor),
    FieldBinding{ "outputBus",
        [](const void* c) noexcept -> ScriptValue {
            return ScriptValue{ std::string{kb::scene::AudioSourceOutputBus(*static_cast<const kb::scene::AudioSourceComponent*>(c))} };
        },
        [](void* c, const ScriptValue& v) noexcept -> bool {
            return v.Type() == ScriptValueType::String && kb::scene::SetAudioSourceOutputBus(
                *static_cast<kb::scene::AudioSourceComponent*>(c), v.AsString());
        } },
};
constexpr std::array<FieldBinding, 4> kAudioListenerFields{
    KB_INT(kb::scene::AudioListenerComponent, priority),
    FieldBinding{ "localUser",
        [](const void* c) noexcept -> ScriptValue {
            return ScriptValue{ static_cast<const kb::scene::AudioListenerComponent*>(c)->localUser.value };
        },
        [](void* c, const ScriptValue& v) noexcept -> bool {
            if (v.Type() != ScriptValueType::UInt32 && (v.Type() != ScriptValueType::Int || v.AsInt() < 0)) return false;
            const auto user = v.Type() == ScriptValueType::UInt32 ? v.AsUInt32() : static_cast<std::uint32_t>(v.AsInt());
            static_cast<kb::scene::AudioListenerComponent*>(c)->localUser = kb::input::LocalUserId{user};
            return true;
        } },
    KB_BOOL(kb::scene::AudioListenerComponent, primary),
    KB_BOOL(kb::scene::AudioListenerComponent, enabled),
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

constexpr std::array<FieldBinding, 20> kLightFields{
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
    KB_FLOAT(kb::scene::LightComponent, sourceRadius),
    KB_FLOAT(kb::scene::LightComponent, shadowStrength),
    KB_FLOAT(kb::scene::LightComponent, shadowDistance),
    KB_LIGHT_MOBILITY(kb::scene::LightComponent, mobility),
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

constexpr std::array<FieldBinding, 14> kRigidbodyFields{
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
    KB_FLOAT(kb::scene::RigidbodyComponent, linearDamping),
    KB_FLOAT(kb::scene::RigidbodyComponent, angularDamping),
};

constexpr std::array<FieldBinding, 14> kColliderFields{
    KB_COLLIDER_SHAPE(kb::scene::ColliderComponent, shape),
    FieldBinding{"meshAssetId",
        [](const void* c) noexcept -> ScriptValue { return ScriptValue{static_cast<const kb::scene::ColliderComponent*>(c)->meshAssetId, ScriptValueType::Hash}; },
        [](void* c, const ScriptValue& v) noexcept -> bool {
            if (v.Type() != ScriptValueType::Hash) return false;
            static_cast<kb::scene::ColliderComponent*>(c)->meshAssetId = v.AsUInt64();
            return true;
        }},
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

constexpr std::array<FieldBinding, 18> kJointFields{
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
    KB_NESTED_FLOAT(kb::scene::JointComponent, motor, targetVelocity),
    KB_NESTED_FLOAT(kb::scene::JointComponent, motor, force),
    KB_NESTED_FLOAT(kb::scene::JointComponent, spring, spring),
    KB_NESTED_FLOAT(kb::scene::JointComponent, spring, damper),
    KB_FLOAT(kb::scene::JointComponent, breakForce),
};

[[nodiscard]] bool FiniteNonNegative(const ScriptValue& value, float& out) noexcept {
    const float number = value.Type() == ScriptValueType::Float ? value.AsFloat()
        : value.Type() == ScriptValueType::Int ? static_cast<float>(value.AsInt()) : -1.0F;
    if (!std::isfinite(number) || number < 0.0F) return false;
    out = number;
    return true;
}

constexpr std::array<FieldBinding, 5> kAnimatorFields{
    FieldBinding{ "controllerAssetId", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::Animator*>(c)->controllerAssetId, ScriptValueType::Hash }; }, [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Hash) return false; static_cast<kb::scene::Animator*>(c)->controllerAssetId = v.AsUInt64(); return true; } },
    FieldBinding{ "speed", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::Animator*>(c)->speed }; }, [](void* c, const ScriptValue& v) noexcept -> bool { return FiniteNonNegative(v, static_cast<kb::scene::Animator*>(c)->speed); } },
    FieldBinding{ "poseUpdateRateHz", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::Animator*>(c)->poseUpdateRateHz }; }, [](void* c, const ScriptValue& v) noexcept -> bool { return FiniteNonNegative(v, static_cast<kb::scene::Animator*>(c)->poseUpdateRateHz); } },
    KB_BOOL(kb::scene::Animator, enabled),
    FieldBinding{ "rootMotionOwner", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<int>(static_cast<const kb::scene::Animator*>(c)->rootMotionOwner) }; }, [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Int || v.AsInt() < 0 || v.AsInt() > static_cast<int>(kb::scene::AnimatorRootMotionOwner::Rigidbody)) return false; static_cast<kb::scene::Animator*>(c)->rootMotionOwner = static_cast<kb::scene::AnimatorRootMotionOwner>(v.AsInt()); return true; } },
};

constexpr std::array<FieldBinding, 8> kDeformedGeometryFields{
    FieldBinding{ "skeletalMeshAssetId", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::DrawD3DeformedGeometryComponent*>(c)->skeletalMeshAssetId, ScriptValueType::Hash }; }, [](void*, const ScriptValue&) noexcept -> bool { return false; } },
    KB_INT(kb::scene::DrawD3DeformedGeometryComponent, lodBias),
    KB_BOOL(kb::scene::DrawD3DeformedGeometryComponent, lodEnabled),
    KB_BOOL(kb::scene::DrawD3DeformedGeometryComponent, fixedBounds),
    KB_BOOL(kb::scene::DrawD3DeformedGeometryComponent, castsShadow),
    KB_BOOL(kb::scene::DrawD3DeformedGeometryComponent, receivesShadow),
    KB_UINT32(kb::scene::DrawD3DeformedGeometryComponent, layer),
    KB_BOOL(kb::scene::DrawD3DeformedGeometryComponent, enabled),
};

constexpr std::array<FieldBinding, 37> kAIBrainFields{
    FieldBinding{ "preset", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<int>(static_cast<const kb::scene::AIBrainComponent*>(c)->preset) }; },
        [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Int || v.AsInt() < 0 || v.AsInt() >= static_cast<int>(kb::scene::kAIBrainPresetCount)) return false;
            kb::scene::ApplyAIBrainPreset(*static_cast<kb::scene::AIBrainComponent*>(c), static_cast<kb::scene::AIBrainPreset>(v.AsInt())); return true; } },
    KB_FLOAT(kb::scene::AIBrainComponent, moveSpeed),
    KB_FLOAT(kb::scene::AIBrainComponent, wanderRadius),
    KB_FLOAT(kb::scene::AIBrainComponent, leashDistance),
    FieldBinding{ "rule1When", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<int>(static_cast<const kb::scene::AIBrainComponent*>(c)->rules[0].when) }; },
        [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Int || v.AsInt() < 0 || v.AsInt() >= static_cast<int>(kb::scene::kAIConditionCount)) return false;
            auto& b = *static_cast<kb::scene::AIBrainComponent*>(c); b.rules[0].when = static_cast<kb::scene::AICondition>(v.AsInt()); b.preset = kb::scene::AIBrainPreset::Custom; return true; } },
    FieldBinding{ "rule1WhenValue", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::AIBrainComponent*>(c)->rules[0].whenValue }; },
        [](void* c, const ScriptValue& v) noexcept -> bool { auto& b = *static_cast<kb::scene::AIBrainComponent*>(c);
            if (v.Type() == ScriptValueType::Float) b.rules[0].whenValue = v.AsFloat(); else if (v.Type() == ScriptValueType::Int) b.rules[0].whenValue = static_cast<float>(v.AsInt()); else return false; b.preset = kb::scene::AIBrainPreset::Custom; return true; } },
    FieldBinding{ "rule1Do", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<int>(static_cast<const kb::scene::AIBrainComponent*>(c)->rules[0].action) }; },
        [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Int || v.AsInt() < 0 || v.AsInt() >= static_cast<int>(kb::scene::kAIActionCount)) return false;
            auto& b = *static_cast<kb::scene::AIBrainComponent*>(c); b.rules[0].action = static_cast<kb::scene::AIAction>(v.AsInt());
            if (b.rules[0].action == kb::scene::AIAction::UseAbility && b.rules[0].actionValue < 1.0F) {
                b.rules[0].actionValue = 1.0F;
            }
            b.preset = kb::scene::AIBrainPreset::Custom; return true; } },
    FieldBinding{ "rule1DoValue", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::AIBrainComponent*>(c)->rules[0].actionValue }; },
        [](void* c, const ScriptValue& v) noexcept -> bool { auto& b = *static_cast<kb::scene::AIBrainComponent*>(c);
            if (v.Type() == ScriptValueType::Float) b.rules[0].actionValue = v.AsFloat(); else if (v.Type() == ScriptValueType::Int) b.rules[0].actionValue = static_cast<float>(v.AsInt()); else return false; b.preset = kb::scene::AIBrainPreset::Custom; return true; } },
    FieldBinding{ "rule2When", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<int>(static_cast<const kb::scene::AIBrainComponent*>(c)->rules[1].when) }; },
        [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Int || v.AsInt() < 0 || v.AsInt() >= static_cast<int>(kb::scene::kAIConditionCount)) return false;
            auto& b = *static_cast<kb::scene::AIBrainComponent*>(c); b.rules[1].when = static_cast<kb::scene::AICondition>(v.AsInt()); b.preset = kb::scene::AIBrainPreset::Custom; return true; } },
    FieldBinding{ "rule2WhenValue", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::AIBrainComponent*>(c)->rules[1].whenValue }; },
        [](void* c, const ScriptValue& v) noexcept -> bool { auto& b = *static_cast<kb::scene::AIBrainComponent*>(c);
            if (v.Type() == ScriptValueType::Float) b.rules[1].whenValue = v.AsFloat(); else if (v.Type() == ScriptValueType::Int) b.rules[1].whenValue = static_cast<float>(v.AsInt()); else return false; b.preset = kb::scene::AIBrainPreset::Custom; return true; } },
    FieldBinding{ "rule2Do", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<int>(static_cast<const kb::scene::AIBrainComponent*>(c)->rules[1].action) }; },
        [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Int || v.AsInt() < 0 || v.AsInt() >= static_cast<int>(kb::scene::kAIActionCount)) return false;
            auto& b = *static_cast<kb::scene::AIBrainComponent*>(c); b.rules[1].action = static_cast<kb::scene::AIAction>(v.AsInt());
            if (b.rules[1].action == kb::scene::AIAction::UseAbility && b.rules[1].actionValue < 1.0F) {
                b.rules[1].actionValue = 1.0F;
            }
            b.preset = kb::scene::AIBrainPreset::Custom; return true; } },
    FieldBinding{ "rule2DoValue", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::AIBrainComponent*>(c)->rules[1].actionValue }; },
        [](void* c, const ScriptValue& v) noexcept -> bool { auto& b = *static_cast<kb::scene::AIBrainComponent*>(c);
            if (v.Type() == ScriptValueType::Float) b.rules[1].actionValue = v.AsFloat(); else if (v.Type() == ScriptValueType::Int) b.rules[1].actionValue = static_cast<float>(v.AsInt()); else return false; b.preset = kb::scene::AIBrainPreset::Custom; return true; } },
    FieldBinding{ "rule3When", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<int>(static_cast<const kb::scene::AIBrainComponent*>(c)->rules[2].when) }; },
        [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Int || v.AsInt() < 0 || v.AsInt() >= static_cast<int>(kb::scene::kAIConditionCount)) return false;
            auto& b = *static_cast<kb::scene::AIBrainComponent*>(c); b.rules[2].when = static_cast<kb::scene::AICondition>(v.AsInt()); b.preset = kb::scene::AIBrainPreset::Custom; return true; } },
    FieldBinding{ "rule3WhenValue", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::AIBrainComponent*>(c)->rules[2].whenValue }; },
        [](void* c, const ScriptValue& v) noexcept -> bool { auto& b = *static_cast<kb::scene::AIBrainComponent*>(c);
            if (v.Type() == ScriptValueType::Float) b.rules[2].whenValue = v.AsFloat(); else if (v.Type() == ScriptValueType::Int) b.rules[2].whenValue = static_cast<float>(v.AsInt()); else return false; b.preset = kb::scene::AIBrainPreset::Custom; return true; } },
    FieldBinding{ "rule3Do", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<int>(static_cast<const kb::scene::AIBrainComponent*>(c)->rules[2].action) }; },
        [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Int || v.AsInt() < 0 || v.AsInt() >= static_cast<int>(kb::scene::kAIActionCount)) return false;
            auto& b = *static_cast<kb::scene::AIBrainComponent*>(c); b.rules[2].action = static_cast<kb::scene::AIAction>(v.AsInt());
            if (b.rules[2].action == kb::scene::AIAction::UseAbility && b.rules[2].actionValue < 1.0F) {
                b.rules[2].actionValue = 1.0F;
            }
            b.preset = kb::scene::AIBrainPreset::Custom; return true; } },
    FieldBinding{ "rule3DoValue", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::AIBrainComponent*>(c)->rules[2].actionValue }; },
        [](void* c, const ScriptValue& v) noexcept -> bool { auto& b = *static_cast<kb::scene::AIBrainComponent*>(c);
            if (v.Type() == ScriptValueType::Float) b.rules[2].actionValue = v.AsFloat(); else if (v.Type() == ScriptValueType::Int) b.rules[2].actionValue = static_cast<float>(v.AsInt()); else return false; b.preset = kb::scene::AIBrainPreset::Custom; return true; } },
    FieldBinding{ "rule4When", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<int>(static_cast<const kb::scene::AIBrainComponent*>(c)->rules[3].when) }; },
        [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Int || v.AsInt() < 0 || v.AsInt() >= static_cast<int>(kb::scene::kAIConditionCount)) return false;
            auto& b = *static_cast<kb::scene::AIBrainComponent*>(c); b.rules[3].when = static_cast<kb::scene::AICondition>(v.AsInt()); b.preset = kb::scene::AIBrainPreset::Custom; return true; } },
    FieldBinding{ "rule4WhenValue", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::AIBrainComponent*>(c)->rules[3].whenValue }; },
        [](void* c, const ScriptValue& v) noexcept -> bool { auto& b = *static_cast<kb::scene::AIBrainComponent*>(c);
            if (v.Type() == ScriptValueType::Float) b.rules[3].whenValue = v.AsFloat(); else if (v.Type() == ScriptValueType::Int) b.rules[3].whenValue = static_cast<float>(v.AsInt()); else return false; b.preset = kb::scene::AIBrainPreset::Custom; return true; } },
    FieldBinding{ "rule4Do", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<int>(static_cast<const kb::scene::AIBrainComponent*>(c)->rules[3].action) }; },
        [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Int || v.AsInt() < 0 || v.AsInt() >= static_cast<int>(kb::scene::kAIActionCount)) return false;
            auto& b = *static_cast<kb::scene::AIBrainComponent*>(c); b.rules[3].action = static_cast<kb::scene::AIAction>(v.AsInt());
            if (b.rules[3].action == kb::scene::AIAction::UseAbility && b.rules[3].actionValue < 1.0F) {
                b.rules[3].actionValue = 1.0F;
            }
            b.preset = kb::scene::AIBrainPreset::Custom; return true; } },
    FieldBinding{ "rule4DoValue", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::AIBrainComponent*>(c)->rules[3].actionValue }; },
        [](void* c, const ScriptValue& v) noexcept -> bool { auto& b = *static_cast<kb::scene::AIBrainComponent*>(c);
            if (v.Type() == ScriptValueType::Float) b.rules[3].actionValue = v.AsFloat(); else if (v.Type() == ScriptValueType::Int) b.rules[3].actionValue = static_cast<float>(v.AsInt()); else return false; b.preset = kb::scene::AIBrainPreset::Custom; return true; } },
    FieldBinding{ "rule5When", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<int>(static_cast<const kb::scene::AIBrainComponent*>(c)->rules[4].when) }; },
        [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Int || v.AsInt() < 0 || v.AsInt() >= static_cast<int>(kb::scene::kAIConditionCount)) return false;
            auto& b = *static_cast<kb::scene::AIBrainComponent*>(c); b.rules[4].when = static_cast<kb::scene::AICondition>(v.AsInt()); b.preset = kb::scene::AIBrainPreset::Custom; return true; } },
    FieldBinding{ "rule5WhenValue", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::AIBrainComponent*>(c)->rules[4].whenValue }; },
        [](void* c, const ScriptValue& v) noexcept -> bool { auto& b = *static_cast<kb::scene::AIBrainComponent*>(c);
            if (v.Type() == ScriptValueType::Float) b.rules[4].whenValue = v.AsFloat(); else if (v.Type() == ScriptValueType::Int) b.rules[4].whenValue = static_cast<float>(v.AsInt()); else return false; b.preset = kb::scene::AIBrainPreset::Custom; return true; } },
    FieldBinding{ "rule5Do", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<int>(static_cast<const kb::scene::AIBrainComponent*>(c)->rules[4].action) }; },
        [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Int || v.AsInt() < 0 || v.AsInt() >= static_cast<int>(kb::scene::kAIActionCount)) return false;
            auto& b = *static_cast<kb::scene::AIBrainComponent*>(c); b.rules[4].action = static_cast<kb::scene::AIAction>(v.AsInt());
            if (b.rules[4].action == kb::scene::AIAction::UseAbility && b.rules[4].actionValue < 1.0F) {
                b.rules[4].actionValue = 1.0F;
            }
            b.preset = kb::scene::AIBrainPreset::Custom; return true; } },
    FieldBinding{ "rule5DoValue", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::AIBrainComponent*>(c)->rules[4].actionValue }; },
        [](void* c, const ScriptValue& v) noexcept -> bool { auto& b = *static_cast<kb::scene::AIBrainComponent*>(c);
            if (v.Type() == ScriptValueType::Float) b.rules[4].actionValue = v.AsFloat(); else if (v.Type() == ScriptValueType::Int) b.rules[4].actionValue = static_cast<float>(v.AsInt()); else return false; b.preset = kb::scene::AIBrainPreset::Custom; return true; } },
    FieldBinding{ "rule6When", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<int>(static_cast<const kb::scene::AIBrainComponent*>(c)->rules[5].when) }; },
        [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Int || v.AsInt() < 0 || v.AsInt() >= static_cast<int>(kb::scene::kAIConditionCount)) return false;
            auto& b = *static_cast<kb::scene::AIBrainComponent*>(c); b.rules[5].when = static_cast<kb::scene::AICondition>(v.AsInt()); b.preset = kb::scene::AIBrainPreset::Custom; return true; } },
    FieldBinding{ "rule6WhenValue", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::AIBrainComponent*>(c)->rules[5].whenValue }; },
        [](void* c, const ScriptValue& v) noexcept -> bool { auto& b = *static_cast<kb::scene::AIBrainComponent*>(c);
            if (v.Type() == ScriptValueType::Float) b.rules[5].whenValue = v.AsFloat(); else if (v.Type() == ScriptValueType::Int) b.rules[5].whenValue = static_cast<float>(v.AsInt()); else return false; b.preset = kb::scene::AIBrainPreset::Custom; return true; } },
    FieldBinding{ "rule6Do", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<int>(static_cast<const kb::scene::AIBrainComponent*>(c)->rules[5].action) }; },
        [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Int || v.AsInt() < 0 || v.AsInt() >= static_cast<int>(kb::scene::kAIActionCount)) return false;
            auto& b = *static_cast<kb::scene::AIBrainComponent*>(c); b.rules[5].action = static_cast<kb::scene::AIAction>(v.AsInt());
            if (b.rules[5].action == kb::scene::AIAction::UseAbility && b.rules[5].actionValue < 1.0F) {
                b.rules[5].actionValue = 1.0F;
            }
            b.preset = kb::scene::AIBrainPreset::Custom; return true; } },
    FieldBinding{ "rule6DoValue", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::AIBrainComponent*>(c)->rules[5].actionValue }; },
        [](void* c, const ScriptValue& v) noexcept -> bool { auto& b = *static_cast<kb::scene::AIBrainComponent*>(c);
            if (v.Type() == ScriptValueType::Float) b.rules[5].actionValue = v.AsFloat(); else if (v.Type() == ScriptValueType::Int) b.rules[5].actionValue = static_cast<float>(v.AsInt()); else return false; b.preset = kb::scene::AIBrainPreset::Custom; return true; } },
    FieldBinding{ "rule7When", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<int>(static_cast<const kb::scene::AIBrainComponent*>(c)->rules[6].when) }; },
        [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Int || v.AsInt() < 0 || v.AsInt() >= static_cast<int>(kb::scene::kAIConditionCount)) return false;
            auto& b = *static_cast<kb::scene::AIBrainComponent*>(c); b.rules[6].when = static_cast<kb::scene::AICondition>(v.AsInt()); b.preset = kb::scene::AIBrainPreset::Custom; return true; } },
    FieldBinding{ "rule7WhenValue", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::AIBrainComponent*>(c)->rules[6].whenValue }; },
        [](void* c, const ScriptValue& v) noexcept -> bool { auto& b = *static_cast<kb::scene::AIBrainComponent*>(c);
            if (v.Type() == ScriptValueType::Float) b.rules[6].whenValue = v.AsFloat(); else if (v.Type() == ScriptValueType::Int) b.rules[6].whenValue = static_cast<float>(v.AsInt()); else return false; b.preset = kb::scene::AIBrainPreset::Custom; return true; } },
    FieldBinding{ "rule7Do", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<int>(static_cast<const kb::scene::AIBrainComponent*>(c)->rules[6].action) }; },
        [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Int || v.AsInt() < 0 || v.AsInt() >= static_cast<int>(kb::scene::kAIActionCount)) return false;
            auto& b = *static_cast<kb::scene::AIBrainComponent*>(c); b.rules[6].action = static_cast<kb::scene::AIAction>(v.AsInt());
            if (b.rules[6].action == kb::scene::AIAction::UseAbility && b.rules[6].actionValue < 1.0F) {
                b.rules[6].actionValue = 1.0F;
            }
            b.preset = kb::scene::AIBrainPreset::Custom; return true; } },
    FieldBinding{ "rule7DoValue", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::AIBrainComponent*>(c)->rules[6].actionValue }; },
        [](void* c, const ScriptValue& v) noexcept -> bool { auto& b = *static_cast<kb::scene::AIBrainComponent*>(c);
            if (v.Type() == ScriptValueType::Float) b.rules[6].actionValue = v.AsFloat(); else if (v.Type() == ScriptValueType::Int) b.rules[6].actionValue = static_cast<float>(v.AsInt()); else return false; b.preset = kb::scene::AIBrainPreset::Custom; return true; } },
    FieldBinding{ "rule8When", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<int>(static_cast<const kb::scene::AIBrainComponent*>(c)->rules[7].when) }; },
        [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Int || v.AsInt() < 0 || v.AsInt() >= static_cast<int>(kb::scene::kAIConditionCount)) return false;
            auto& b = *static_cast<kb::scene::AIBrainComponent*>(c); b.rules[7].when = static_cast<kb::scene::AICondition>(v.AsInt()); b.preset = kb::scene::AIBrainPreset::Custom; return true; } },
    FieldBinding{ "rule8WhenValue", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::AIBrainComponent*>(c)->rules[7].whenValue }; },
        [](void* c, const ScriptValue& v) noexcept -> bool { auto& b = *static_cast<kb::scene::AIBrainComponent*>(c);
            if (v.Type() == ScriptValueType::Float) b.rules[7].whenValue = v.AsFloat(); else if (v.Type() == ScriptValueType::Int) b.rules[7].whenValue = static_cast<float>(v.AsInt()); else return false; b.preset = kb::scene::AIBrainPreset::Custom; return true; } },
    FieldBinding{ "rule8Do", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<int>(static_cast<const kb::scene::AIBrainComponent*>(c)->rules[7].action) }; },
        [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Int || v.AsInt() < 0 || v.AsInt() >= static_cast<int>(kb::scene::kAIActionCount)) return false;
            auto& b = *static_cast<kb::scene::AIBrainComponent*>(c); b.rules[7].action = static_cast<kb::scene::AIAction>(v.AsInt());
            if (b.rules[7].action == kb::scene::AIAction::UseAbility && b.rules[7].actionValue < 1.0F) {
                b.rules[7].actionValue = 1.0F;
            }
            b.preset = kb::scene::AIBrainPreset::Custom; return true; } },
    FieldBinding{ "rule8DoValue", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::AIBrainComponent*>(c)->rules[7].actionValue }; },
        [](void* c, const ScriptValue& v) noexcept -> bool { auto& b = *static_cast<kb::scene::AIBrainComponent*>(c);
            if (v.Type() == ScriptValueType::Float) b.rules[7].actionValue = v.AsFloat(); else if (v.Type() == ScriptValueType::Int) b.rules[7].actionValue = static_cast<float>(v.AsInt()); else return false; b.preset = kb::scene::AIBrainPreset::Custom; return true; } },
    KB_BOOL(kb::scene::AIBrainComponent, enabled),
};

constexpr std::array<FieldBinding, 26> kCombatantFields{
    KB_FLOAT(kb::scene::CombatantComponent, maximumHealth),
    KB_FLOAT(kb::scene::CombatantComponent, health),
    KB_FLOAT(kb::scene::CombatantComponent, healthRegeneration),
    KB_FLOAT(kb::scene::CombatantComponent, maximumMana),
    KB_FLOAT(kb::scene::CombatantComponent, mana),
    KB_FLOAT(kb::scene::CombatantComponent, manaRegeneration),
    FieldBinding{ "physicalResistance", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::CombatantComponent*>(c)->resistances[static_cast<std::size_t>(kb::gameplay::DamageType::Physical)] }; },
        [](void* c, const ScriptValue& v) noexcept -> bool { float& r = static_cast<kb::scene::CombatantComponent*>(c)->resistances[static_cast<std::size_t>(kb::gameplay::DamageType::Physical)];
            if (v.Type() == ScriptValueType::Float) { r = v.AsFloat(); return true; } if (v.Type() == ScriptValueType::Int) { r = static_cast<float>(v.AsInt()); return true; } return false; } },
    FieldBinding{ "fireResistance", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::CombatantComponent*>(c)->resistances[static_cast<std::size_t>(kb::gameplay::DamageType::Fire)] }; },
        [](void* c, const ScriptValue& v) noexcept -> bool { float& r = static_cast<kb::scene::CombatantComponent*>(c)->resistances[static_cast<std::size_t>(kb::gameplay::DamageType::Fire)];
            if (v.Type() == ScriptValueType::Float) { r = v.AsFloat(); return true; } if (v.Type() == ScriptValueType::Int) { r = static_cast<float>(v.AsInt()); return true; } return false; } },
    FieldBinding{ "coldResistance", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::CombatantComponent*>(c)->resistances[static_cast<std::size_t>(kb::gameplay::DamageType::Cold)] }; },
        [](void* c, const ScriptValue& v) noexcept -> bool { float& r = static_cast<kb::scene::CombatantComponent*>(c)->resistances[static_cast<std::size_t>(kb::gameplay::DamageType::Cold)];
            if (v.Type() == ScriptValueType::Float) { r = v.AsFloat(); return true; } if (v.Type() == ScriptValueType::Int) { r = static_cast<float>(v.AsInt()); return true; } return false; } },
    FieldBinding{ "lightningResistance", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::CombatantComponent*>(c)->resistances[static_cast<std::size_t>(kb::gameplay::DamageType::Lightning)] }; },
        [](void* c, const ScriptValue& v) noexcept -> bool { float& r = static_cast<kb::scene::CombatantComponent*>(c)->resistances[static_cast<std::size_t>(kb::gameplay::DamageType::Lightning)];
            if (v.Type() == ScriptValueType::Float) { r = v.AsFloat(); return true; } if (v.Type() == ScriptValueType::Int) { r = static_cast<float>(v.AsInt()); return true; } return false; } },
    FieldBinding{ "poisonResistance", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::CombatantComponent*>(c)->resistances[static_cast<std::size_t>(kb::gameplay::DamageType::Poison)] }; },
        [](void* c, const ScriptValue& v) noexcept -> bool { float& r = static_cast<kb::scene::CombatantComponent*>(c)->resistances[static_cast<std::size_t>(kb::gameplay::DamageType::Poison)];
            if (v.Type() == ScriptValueType::Float) { r = v.AsFloat(); return true; } if (v.Type() == ScriptValueType::Int) { r = static_cast<float>(v.AsInt()); return true; } return false; } },
    KB_UINT32(kb::scene::CombatantComponent, team),
    KB_FLOAT(kb::scene::CombatantComponent, sightRange),
    KB_FLOAT(kb::scene::CombatantComponent, sightAngleDegrees),
    KB_FLOAT(kb::scene::CombatantComponent, hearingRange),
    KB_BOOL(kb::scene::CombatantComponent, requireLineOfSight),
    FieldBinding{ "ability1", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::CombatantComponent*>(c)->abilities[0], ScriptValueType::Hash }; },
        [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Hash) return false; static_cast<kb::scene::CombatantComponent*>(c)->abilities[0] = v.AsUInt64(); return true; } },
    FieldBinding{ "ability2", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::CombatantComponent*>(c)->abilities[1], ScriptValueType::Hash }; },
        [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Hash) return false; static_cast<kb::scene::CombatantComponent*>(c)->abilities[1] = v.AsUInt64(); return true; } },
    FieldBinding{ "ability3", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::CombatantComponent*>(c)->abilities[2], ScriptValueType::Hash }; },
        [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Hash) return false; static_cast<kb::scene::CombatantComponent*>(c)->abilities[2] = v.AsUInt64(); return true; } },
    FieldBinding{ "ability4", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::CombatantComponent*>(c)->abilities[3], ScriptValueType::Hash }; },
        [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Hash) return false; static_cast<kb::scene::CombatantComponent*>(c)->abilities[3] = v.AsUInt64(); return true; } },
    FieldBinding{ "ability5", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::CombatantComponent*>(c)->abilities[4], ScriptValueType::Hash }; },
        [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Hash) return false; static_cast<kb::scene::CombatantComponent*>(c)->abilities[4] = v.AsUInt64(); return true; } },
    FieldBinding{ "ability6", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::CombatantComponent*>(c)->abilities[5], ScriptValueType::Hash }; },
        [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Hash) return false; static_cast<kb::scene::CombatantComponent*>(c)->abilities[5] = v.AsUInt64(); return true; } },
    FieldBinding{ "ability7", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::CombatantComponent*>(c)->abilities[6], ScriptValueType::Hash }; },
        [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Hash) return false; static_cast<kb::scene::CombatantComponent*>(c)->abilities[6] = v.AsUInt64(); return true; } },
    FieldBinding{ "ability8", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::CombatantComponent*>(c)->abilities[7], ScriptValueType::Hash }; },
        [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Hash) return false; static_cast<kb::scene::CombatantComponent*>(c)->abilities[7] = v.AsUInt64(); return true; } },
    KB_BOOL(kb::scene::CombatantComponent, invulnerable),
    KB_BOOL(kb::scene::CombatantComponent, enabled),
};

constexpr std::array<FieldBinding, 9> kSocketAttachmentFields{
    FieldBinding{ "socket",
        [](const void* c) noexcept -> ScriptValue { return ScriptValue{ std::string{ kb::scene::SocketAttachmentName(*static_cast<const kb::scene::SocketAttachmentComponent*>(c)) } }; },
        [](void* c, const ScriptValue& v) noexcept -> bool {
            return v.Type() == ScriptValueType::String &&
                kb::scene::SetSocketAttachmentName(*static_cast<kb::scene::SocketAttachmentComponent*>(c), v.AsString());
        } },
    KB_NESTED_FLOAT(kb::scene::SocketAttachmentComponent, offset.position, x),
    KB_NESTED_FLOAT(kb::scene::SocketAttachmentComponent, offset.position, y),
    KB_NESTED_FLOAT(kb::scene::SocketAttachmentComponent, offset.position, z),
    KB_NESTED_FLOAT(kb::scene::SocketAttachmentComponent, offset.rotation, x),
    KB_NESTED_FLOAT(kb::scene::SocketAttachmentComponent, offset.rotation, y),
    KB_NESTED_FLOAT(kb::scene::SocketAttachmentComponent, offset.rotation, z),
    KB_NESTED_FLOAT(kb::scene::SocketAttachmentComponent, offset.rotation, w),
    KB_BOOL(kb::scene::SocketAttachmentComponent, enabled),
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

constexpr std::array<FieldBinding, 11> kNavLinkFields{
    KB_NESTED_FLOAT(kb::scene::NavLink, start, x),
    KB_NESTED_FLOAT(kb::scene::NavLink, start, y),
    KB_NESTED_FLOAT(kb::scene::NavLink, start, z),
    KB_NESTED_FLOAT(kb::scene::NavLink, end, x),
    KB_NESTED_FLOAT(kb::scene::NavLink, end, y),
    KB_NESTED_FLOAT(kb::scene::NavLink, end, z),
    KB_FLOAT(kb::scene::NavLink, radius),
    KB_NAV_LINK_KIND(kb::scene::NavLink, kind),
    KB_NAV_AREA(kb::scene::NavLink, area),
    KB_BOOL(kb::scene::NavLink, bidirectional),
    KB_BOOL(kb::scene::NavLink, enabled),
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

constexpr std::array<FieldBinding, 3> kGridFields{
    FieldBinding{ "cellSize", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::GridComponent*>(component)->cellSize }; },
        [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Float || !std::isfinite(value.AsFloat()) || value.AsFloat() <= 0.0F || value.AsFloat() > 1000000.0F) return false; static_cast<kb::scene::GridComponent*>(component)->cellSize = value.AsFloat(); return true; } },
    FieldBinding{ "floorHeight", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::GridComponent*>(component)->floorHeight }; },
        [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Float || !std::isfinite(value.AsFloat()) || value.AsFloat() < 0.0F || value.AsFloat() > 1000000.0F) return false; static_cast<kb::scene::GridComponent*>(component)->floorHeight = value.AsFloat(); return true; } },
    KB_BOOL(kb::scene::GridComponent, wall),
};

constexpr std::array<FieldBinding, 2> kTilemapFields{
    FieldBinding{ "mapAssetId", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::TilemapComponent*>(component)->mapAssetId, ScriptValueType::Hash }; },
        [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Hash) return false; static_cast<kb::scene::TilemapComponent*>(component)->mapAssetId = value.AsUInt64(); return true; } },
    KB_BOOL(kb::scene::TilemapComponent, locked),
};

constexpr std::array<FieldBinding, 2> kTilemapRendererFields{
    KB_INT(kb::scene::TilemapRendererComponent, sortingOrder),
    KB_BOOL(kb::scene::TilemapRendererComponent, enabled),
};

constexpr std::array<FieldBinding, 26> kSprite2DFields{
    FieldBinding{ "imageAssetId", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::Sprite2DComponent*>(component)->imageAssetId, ScriptValueType::Hash }; },
        [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Hash) return false; static_cast<kb::scene::Sprite2DComponent*>(component)->imageAssetId = value.AsUInt64(); return true; } },
    FieldBinding{ "materialAssetId", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::Sprite2DComponent*>(component)->materialAssetId, ScriptValueType::Hash }; },
        [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Hash) return false; static_cast<kb::scene::Sprite2DComponent*>(component)->materialAssetId = value.AsUInt64(); return true; } },
    KB_UINT32(kb::scene::Sprite2DComponent, cellWidth),
    KB_UINT32(kb::scene::Sprite2DComponent, cellHeight),
    KB_UINT32(kb::scene::Sprite2DComponent, margin),
    KB_UINT32(kb::scene::Sprite2DComponent, spacing),
    KB_UINT32(kb::scene::Sprite2DComponent, imageWidth),
    KB_UINT32(kb::scene::Sprite2DComponent, imageHeight),
    KB_UINT32(kb::scene::Sprite2DComponent, cell),
    KB_UINT32(kb::scene::Sprite2DComponent, frameCount),
    KB_FLOAT(kb::scene::Sprite2DComponent, framesPerSecond),
    KB_BOOL(kb::scene::Sprite2DComponent, loop),
    KB_BOOL(kb::scene::Sprite2DComponent, playing),
    KB_FLOAT(kb::scene::Sprite2DComponent, time),
    KB_FLOAT(kb::scene::Sprite2DComponent, pixelsPerUnit),
    KB_FLOAT(kb::scene::Sprite2DComponent, pivotX),
    KB_FLOAT(kb::scene::Sprite2DComponent, pivotY),
    KB_BOOL(kb::scene::Sprite2DComponent, flipX),
    KB_BOOL(kb::scene::Sprite2DComponent, flipY),
    KB_FLOAT(kb::scene::Sprite2DComponent, tintR),
    KB_FLOAT(kb::scene::Sprite2DComponent, tintG),
    KB_FLOAT(kb::scene::Sprite2DComponent, tintB),
    KB_FLOAT(kb::scene::Sprite2DComponent, tintA),
    KB_INT(kb::scene::Sprite2DComponent, sortingOrder),
    KB_BOOL(kb::scene::Sprite2DComponent, wall),
    KB_BOOL(kb::scene::Sprite2DComponent, visible),
};

constexpr std::array<FieldBinding, 11> kBody2DFields{
    KB_INT(kb::scene::Body2DComponent, type),
    KB_FLOAT(kb::scene::Body2DComponent, gravityScale),
    KB_FLOAT(kb::scene::Body2DComponent, linearDamping),
    KB_FLOAT(kb::scene::Body2DComponent, angularDamping),
    KB_BOOL(kb::scene::Body2DComponent, fixedRotation),
    KB_BOOL(kb::scene::Body2DComponent, bullet),
    KB_BOOL(kb::scene::Body2DComponent, allowSleep),
    KB_BOOL(kb::scene::Body2DComponent, sideView),
    KB_FLOAT(kb::scene::Body2DComponent, startVelocityX),
    KB_FLOAT(kb::scene::Body2DComponent, startVelocityY),
    KB_BOOL(kb::scene::Body2DComponent, enabled),
};

constexpr std::array<FieldBinding, 15> kCollider2DFields{
    KB_INT(kb::scene::Collider2DComponent, shape),
    KB_FLOAT(kb::scene::Collider2DComponent, width),
    KB_FLOAT(kb::scene::Collider2DComponent, height),
    KB_FLOAT(kb::scene::Collider2DComponent, radius),
    KB_FLOAT(kb::scene::Collider2DComponent, offsetX),
    KB_FLOAT(kb::scene::Collider2DComponent, offsetY),
    KB_FLOAT(kb::scene::Collider2DComponent, angleDegrees),
    KB_BOOL(kb::scene::Collider2DComponent, isTrigger),
    KB_FLOAT(kb::scene::Collider2DComponent, density),
    KB_FLOAT(kb::scene::Collider2DComponent, friction),
    KB_FLOAT(kb::scene::Collider2DComponent, restitution),
    KB_INT(kb::scene::Collider2DComponent, layer),
    KB_UINT32(kb::scene::Collider2DComponent, collidesWith),
    KB_BOOL(kb::scene::Collider2DComponent, sideView),
    KB_BOOL(kb::scene::Collider2DComponent, enabled),
};

constexpr std::array<FieldBinding, 10> kCharacterController2DFields{
    KB_FLOAT(kb::scene::CharacterController2DComponent, radius),
    KB_FLOAT(kb::scene::CharacterController2DComponent, height),
    KB_FLOAT(kb::scene::CharacterController2DComponent, gravityScale),
    KB_BOOL(kb::scene::CharacterController2DComponent, useGravity),
    KB_FLOAT(kb::scene::CharacterController2DComponent, slopeLimitDegrees),
    KB_FLOAT(kb::scene::CharacterController2DComponent, stepHeight),
    KB_INT(kb::scene::CharacterController2DComponent, layer),
    KB_UINT32(kb::scene::CharacterController2DComponent, collidesWith),
    KB_BOOL(kb::scene::CharacterController2DComponent, sideView),
    KB_BOOL(kb::scene::CharacterController2DComponent, enabled),
};

constexpr std::array<FieldBinding, 1> kTilemapColliderFields{
    KB_BOOL(kb::scene::TilemapColliderComponent, enabled),
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

constexpr std::array<FieldBinding, 26> kWorldBackdropFields{
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
    FieldBinding{ "sunDisk", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<int>(static_cast<const kb::scene::WorldBackdropComponent*>(component)->sunDisk) }; },
        [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Int || value.AsInt() < 0 || value.AsInt() > 255) return false; static_cast<kb::scene::WorldBackdropComponent*>(component)->sunDisk = static_cast<kb::scene::WorldBackdropSunDisk>(value.AsInt()); return true; } },
    KB_FLOAT(kb::scene::WorldBackdropComponent, sunSize),
    KB_FLOAT(kb::scene::WorldBackdropComponent, sunSizeConvergence),
    KB_FLOAT(kb::scene::WorldBackdropComponent, atmosphereThickness),
    KB_NESTED_FLOAT(kb::scene::WorldBackdropComponent, skyTint, x),
    KB_NESTED_FLOAT(kb::scene::WorldBackdropComponent, skyTint, y),
    KB_NESTED_FLOAT(kb::scene::WorldBackdropComponent, skyTint, z),
    KB_NESTED_FLOAT(kb::scene::WorldBackdropComponent, groundColor, x),
    KB_NESTED_FLOAT(kb::scene::WorldBackdropComponent, groundColor, y),
    KB_NESTED_FLOAT(kb::scene::WorldBackdropComponent, groundColor, z),
    KB_FLOAT(kb::scene::WorldBackdropComponent, exposure),
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
constexpr std::array<FieldBinding, 5> kSurfaceCastFields{
    FieldBinding{ "materialAssetId", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::SurfaceCastComponent*>(component)->materialAssetId, ScriptValueType::Hash }; }, [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Hash) return false; static_cast<kb::scene::SurfaceCastComponent*>(component)->materialAssetId = value.AsUInt64(); return true; } },
    FieldBinding{ "receiverLayerMask", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::SurfaceCastComponent*>(component)->receiverLayerMask }; }, [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::UInt32 || value.AsUInt32() == 0U) return false; static_cast<kb::scene::SurfaceCastComponent*>(component)->receiverLayerMask = value.AsUInt32(); return true; } },
    FieldBinding{ "order", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::SurfaceCastComponent*>(component)->order }; }, [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Int) return false; static_cast<kb::scene::SurfaceCastComponent*>(component)->order = value.AsInt(); return true; } },
    FieldBinding{ "content", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<int>(static_cast<const kb::scene::SurfaceCastComponent*>(component)->content) }; }, [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Int || value.AsInt() < 0 || value.AsInt() > static_cast<int>(kb::scene::SurfaceCastContent::Detail)) return false; static_cast<kb::scene::SurfaceCastComponent*>(component)->content = static_cast<kb::scene::SurfaceCastContent>(value.AsInt()); return true; } },
    KB_BOOL(kb::scene::SurfaceCastComponent, enabled),
};
constexpr std::array<FieldBinding, 11> kFacingPanelFields{
    FieldBinding{ "mode", [](const void* component) noexcept -> ScriptValue { return ScriptValue{ static_cast<int>(static_cast<const kb::scene::FacingPanelComponent*>(component)->mode) }; }, [](void* component, const ScriptValue& value) noexcept -> bool { if (value.Type() != ScriptValueType::Int || value.AsInt() < 0 || value.AsInt() > static_cast<int>(kb::scene::FacingPanelMode::Fixed)) return false; static_cast<kb::scene::FacingPanelComponent*>(component)->mode = static_cast<kb::scene::FacingPanelMode>(value.AsInt()); return true; } },
    FieldBinding{ "targetPoint.x", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::FacingPanelComponent*>(c)->targetPoint.x }; }, [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Float || !std::isfinite(v.AsFloat())) return false; static_cast<kb::scene::FacingPanelComponent*>(c)->targetPoint.x = v.AsFloat(); return true; } },
    FieldBinding{ "targetPoint.y", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::FacingPanelComponent*>(c)->targetPoint.y }; }, [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Float || !std::isfinite(v.AsFloat())) return false; static_cast<kb::scene::FacingPanelComponent*>(c)->targetPoint.y = v.AsFloat(); return true; } },
    FieldBinding{ "targetPoint.z", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::FacingPanelComponent*>(c)->targetPoint.z }; }, [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Float || !std::isfinite(v.AsFloat())) return false; static_cast<kb::scene::FacingPanelComponent*>(c)->targetPoint.z = v.AsFloat(); return true; } },
    FieldBinding{ "axis.x", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::FacingPanelComponent*>(c)->axis.x }; }, [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Float || !std::isfinite(v.AsFloat())) return false; static_cast<kb::scene::FacingPanelComponent*>(c)->axis.x = v.AsFloat(); return true; } },
    FieldBinding{ "axis.y", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::FacingPanelComponent*>(c)->axis.y }; }, [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Float || !std::isfinite(v.AsFloat())) return false; static_cast<kb::scene::FacingPanelComponent*>(c)->axis.y = v.AsFloat(); return true; } },
    FieldBinding{ "axis.z", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::FacingPanelComponent*>(c)->axis.z }; }, [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Float || !std::isfinite(v.AsFloat())) return false; static_cast<kb::scene::FacingPanelComponent*>(c)->axis.z = v.AsFloat(); return true; } },
    FieldBinding{ "up.x", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::FacingPanelComponent*>(c)->up.x }; }, [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Float || !std::isfinite(v.AsFloat())) return false; static_cast<kb::scene::FacingPanelComponent*>(c)->up.x = v.AsFloat(); return true; } },
    FieldBinding{ "up.y", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::FacingPanelComponent*>(c)->up.y }; }, [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Float || !std::isfinite(v.AsFloat())) return false; static_cast<kb::scene::FacingPanelComponent*>(c)->up.y = v.AsFloat(); return true; } },
    FieldBinding{ "up.z", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::FacingPanelComponent*>(c)->up.z }; }, [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Float || !std::isfinite(v.AsFloat())) return false; static_cast<kb::scene::FacingPanelComponent*>(c)->up.z = v.AsFloat(); return true; } },
    KB_BOOL(kb::scene::FacingPanelComponent, enabled),
};
constexpr std::array<FieldBinding, 10> kSpaceStrokeFields{
    FieldBinding{ "meshAssetId", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::SpaceStrokeComponent*>(c)->meshAssetId, ScriptValueType::Hash }; }, [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Hash) return false; static_cast<kb::scene::SpaceStrokeComponent*>(c)->meshAssetId = v.AsUInt64(); return true; } },
    FieldBinding{ "materialAssetId", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::SpaceStrokeComponent*>(c)->materialAssetId, ScriptValueType::Hash }; }, [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Hash) return false; static_cast<kb::scene::SpaceStrokeComponent*>(c)->materialAssetId = v.AsUInt64(); return true; } },
    FieldBinding{ "mode", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<int>(static_cast<const kb::scene::SpaceStrokeComponent*>(c)->mode) }; }, [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Int || v.AsInt() < 0 || v.AsInt() > static_cast<int>(kb::scene::SpaceStrokeMode::Cable)) return false; static_cast<kb::scene::SpaceStrokeComponent*>(c)->mode = static_cast<kb::scene::SpaceStrokeMode>(v.AsInt()); return true; } },
    FieldBinding{ "width", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::SpaceStrokeComponent*>(c)->width }; }, [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Float || !std::isfinite(v.AsFloat())) return false; static_cast<kb::scene::SpaceStrokeComponent*>(c)->width = v.AsFloat(); return true; } },
    FieldBinding{ "cableSag", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::SpaceStrokeComponent*>(c)->cableSag }; }, [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Float || !std::isfinite(v.AsFloat())) return false; static_cast<kb::scene::SpaceStrokeComponent*>(c)->cableSag = v.AsFloat(); return true; } },
    FieldBinding{ "splineSegments", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<std::uint32_t>(static_cast<const kb::scene::SpaceStrokeComponent*>(c)->splineSegments) }; }, [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::UInt32 || v.AsUInt32() > UINT8_MAX) return false; static_cast<kb::scene::SpaceStrokeComponent*>(c)->splineSegments = static_cast<std::uint8_t>(v.AsUInt32()); return true; } },
    FieldBinding{ "layer", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::SpaceStrokeComponent*>(c)->layer }; }, [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::UInt32) return false; static_cast<kb::scene::SpaceStrokeComponent*>(c)->layer = v.AsUInt32(); return true; } },
    KB_BOOL(kb::scene::SpaceStrokeComponent, castsShadow), KB_BOOL(kb::scene::SpaceStrokeComponent, receivesShadow), KB_BOOL(kb::scene::SpaceStrokeComponent, enabled),
};
constexpr std::array<FieldBinding, 9> kHistoryRibbonFields{
    FieldBinding{ "meshAssetId", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::HistoryRibbonComponent*>(c)->meshAssetId, ScriptValueType::Hash }; }, [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Hash) return false; static_cast<kb::scene::HistoryRibbonComponent*>(c)->meshAssetId = v.AsUInt64(); return true; } },
    FieldBinding{ "materialAssetId", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::HistoryRibbonComponent*>(c)->materialAssetId, ScriptValueType::Hash }; }, [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Hash) return false; static_cast<kb::scene::HistoryRibbonComponent*>(c)->materialAssetId = v.AsUInt64(); return true; } },
    FieldBinding{ "lifetimeSeconds", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::HistoryRibbonComponent*>(c)->lifetimeSeconds }; }, [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Float || !std::isfinite(v.AsFloat())) return false; static_cast<kb::scene::HistoryRibbonComponent*>(c)->lifetimeSeconds = v.AsFloat(); return true; } },
    FieldBinding{ "width", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::HistoryRibbonComponent*>(c)->width }; }, [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Float || !std::isfinite(v.AsFloat())) return false; static_cast<kb::scene::HistoryRibbonComponent*>(c)->width = v.AsFloat(); return true; } },
    FieldBinding{ "sampleIntervalSeconds", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::HistoryRibbonComponent*>(c)->sampleIntervalSeconds }; }, [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Float || !std::isfinite(v.AsFloat())) return false; static_cast<kb::scene::HistoryRibbonComponent*>(c)->sampleIntervalSeconds = v.AsFloat(); return true; } },
    FieldBinding{ "layer", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::HistoryRibbonComponent*>(c)->layer }; }, [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::UInt32) return false; static_cast<kb::scene::HistoryRibbonComponent*>(c)->layer = v.AsUInt32(); return true; } },
    KB_BOOL(kb::scene::HistoryRibbonComponent, castsShadow), KB_BOOL(kb::scene::HistoryRibbonComponent, receivesShadow), KB_BOOL(kb::scene::HistoryRibbonComponent, enabled),
};
constexpr std::array<FieldBinding, 9> kParticleEffectFields{
    FieldBinding{ "effectAssetId", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::ParticleEffectComponent*>(c)->effectAssetId, ScriptValueType::Hash }; }, [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Hash) return false; static_cast<kb::scene::ParticleEffectComponent*>(c)->effectAssetId = v.AsUInt64(); return true; } },
    FieldBinding{ "deterministicSeed", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::ParticleEffectComponent*>(c)->deterministicSeed, ScriptValueType::Hash }; }, [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Hash) return false; static_cast<kb::scene::ParticleEffectComponent*>(c)->deterministicSeed = v.AsUInt64(); return true; } },
    FieldBinding{ "rateMultiplier", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::ParticleEffectComponent*>(c)->rateMultiplier }; }, [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Float || !std::isfinite(v.AsFloat())) return false; static_cast<kb::scene::ParticleEffectComponent*>(c)->rateMultiplier = v.AsFloat(); return true; } },
    FieldBinding{ "maxParticlesOverride", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::ParticleEffectComponent*>(c)->maxParticlesOverride }; }, [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::UInt32) return false; static_cast<kb::scene::ParticleEffectComponent*>(c)->maxParticlesOverride = v.AsUInt32(); return true; } },
    FieldBinding{ "ownerDeathPolicy", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<int>(static_cast<const kb::scene::ParticleEffectComponent*>(c)->ownerDeathPolicy) }; }, [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Int || v.AsInt() < 0 || v.AsInt() > static_cast<int>(kb::scene::ParticleOwnerDeathPolicy::Clear)) return false; static_cast<kb::scene::ParticleEffectComponent*>(c)->ownerDeathPolicy = static_cast<kb::scene::ParticleOwnerDeathPolicy>(v.AsInt()); return true; } },
    KB_BOOL(kb::scene::ParticleEffectComponent, enabled),
    KB_BOOL(kb::scene::ParticleEffectComponent, autoPlay),
    KB_BOOL(kb::scene::ParticleEffectComponent, followTransform),
    KB_BOOL(kb::scene::ParticleEffectComponent, restartOnActivate),
};
constexpr std::array<FieldBinding, 7> kLensEchoFields{
    FieldBinding{ "sourceEntityId", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::LensEchoComponent*>(c)->sourceEntityId, ScriptValueType::Hash }; }, [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Hash) return false; static_cast<kb::scene::LensEchoComponent*>(c)->sourceEntityId = v.AsUInt64(); return true; } },
    FieldBinding{ "profileMaterialAssetId", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::LensEchoComponent*>(c)->profileMaterialAssetId, ScriptValueType::Hash }; }, [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Hash) return false; static_cast<kb::scene::LensEchoComponent*>(c)->profileMaterialAssetId = v.AsUInt64(); return true; } },
    FieldBinding{ "intensity", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::LensEchoComponent*>(c)->intensity }; }, [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Float || !std::isfinite(v.AsFloat())) return false; static_cast<kb::scene::LensEchoComponent*>(c)->intensity = v.AsFloat(); return true; } },
    FieldBinding{ "size", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::LensEchoComponent*>(c)->size }; }, [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Float || !std::isfinite(v.AsFloat())) return false; static_cast<kb::scene::LensEchoComponent*>(c)->size = v.AsFloat(); return true; } },
    FieldBinding{ "layer", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<const kb::scene::LensEchoComponent*>(c)->layer }; }, [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::UInt32) return false; static_cast<kb::scene::LensEchoComponent*>(c)->layer = v.AsUInt32(); return true; } },
    FieldBinding{ "occlusionRule", [](const void* c) noexcept -> ScriptValue { return ScriptValue{ static_cast<int>(static_cast<const kb::scene::LensEchoComponent*>(c)->occlusionRule) }; }, [](void* c, const ScriptValue& v) noexcept -> bool { if (v.Type() != ScriptValueType::Int || v.AsInt() < 0 || v.AsInt() > static_cast<int>(kb::scene::LensEchoOcclusionRule::AlwaysVisible)) return false; static_cast<kb::scene::LensEchoComponent*>(c)->occlusionRule = static_cast<kb::scene::LensEchoOcclusionRule>(v.AsInt()); return true; } },
    KB_BOOL(kb::scene::LensEchoComponent, enabled),
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
void MarkAnimatorModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { scene.Components().Animators().MarkModified(entity); }
void MarkDeformedGeometryModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { scene.Components().DeformedGeometries().MarkModified(entity); }
void MarkSocketAttachmentModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { scene.Components().SocketAttachments().MarkModified(entity); }
void MarkCombatantModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { scene.Components().Combatants().MarkModified(entity); }
void MarkAIBrainModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { scene.Components().AIBrains().MarkModified(entity); }
void MarkNavObstacleModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { scene.Components().NavObstacles().MarkModified(entity); }
void MarkNavLinkModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { scene.Components().NavLinks().MarkModified(entity); }
void MarkTagsModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { scene.Components().Tags().MarkModified(entity); }
void MarkRegionShapeModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { scene.Components().RegionShapes().MarkModified(entity); }
void MarkGuideCurveModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { scene.Components().GuideCurves().MarkModified(entity); }
void MarkGridModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { scene.Components().Grids().MarkModified(entity); }
void MarkTilemapModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { scene.Components().Tilemaps().MarkModified(entity); }
void MarkTilemapRendererModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { scene.Components().TilemapRenderers().MarkModified(entity); }
void MarkSprite2DModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { scene.Components().Sprites2D().MarkModified(entity); }
void MarkBody2DModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { scene.Components().Bodies2D().MarkModified(entity); }
void MarkCollider2DModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { scene.Components().Colliders2D().MarkModified(entity); }
void MarkCharacterController2DModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { scene.Components().CharacterControllers2D().MarkModified(entity); }
void MarkTilemapColliderModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { scene.Components().TilemapColliders().MarkModified(entity); }
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
void MarkSurfaceCastModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { scene.Components().SurfaceCasts().MarkModified(entity); }
void MarkFacingPanelModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { scene.Components().FacingPanels().MarkModified(entity); }
void MarkSpaceStrokeModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { scene.Components().SpaceStrokes().MarkModified(entity); }
void MarkHistoryRibbonModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { scene.Components().HistoryRibbons().MarkModified(entity); }
void MarkParticleEffectModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { scene.Components().ParticleEffects().MarkModified(entity); }
void MarkAudioSourceModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { scene.Components().AudioSources().MarkModified(entity); }
void MarkAudioListenerModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { scene.Components().AudioListeners().MarkModified(entity); }
void MarkLensEchoModified(kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept { scene.Components().LensEchoes().MarkModified(entity); }

[[nodiscard]] ComponentAccess AccessComponent(kb::scene::Scene& scene, kb::scene::SceneEntity entity, std::string_view componentName) noexcept {
    if (!entity.IsValid() || !scene.Entities().IsAlive(entity)) {
        return {};
    }
    if (componentName == "AudioSource") {
        auto* component = scene.Components().AudioSources().TryGet(entity);
        return ComponentAccess{ component, component, kAudioSourceFields, &MarkAudioSourceModified };
    }
    if (componentName == "AudioListener") {
        auto* component = scene.Components().AudioListeners().TryGet(entity);
        return ComponentAccess{ component, component, kAudioListenerFields, &MarkAudioListenerModified };
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
    if (componentName == "Animator") {
        kb::scene::Animator* component = scene.Components().Animators().TryGet(entity);
        return ComponentAccess{ component, component, kAnimatorFields, &MarkAnimatorModified };
    }
    if (componentName == "DeformedGeometry") {
        kb::scene::DrawD3DeformedGeometryComponent* component = scene.Components().DeformedGeometries().TryGet(entity);
        return ComponentAccess{ component, component, kDeformedGeometryFields, &MarkDeformedGeometryModified };
    }
    if (componentName == "SocketAttachment") {
        kb::scene::SocketAttachmentComponent* component = scene.Components().SocketAttachments().TryGet(entity);
        return ComponentAccess{ component, component, kSocketAttachmentFields, &MarkSocketAttachmentModified };
    }
    if (componentName == "Combatant") {
        kb::scene::CombatantComponent* component = scene.Components().Combatants().TryGet(entity);
        return ComponentAccess{ component, component, kCombatantFields, &MarkCombatantModified };
    }
    if (componentName == "AIBrain") {
        kb::scene::AIBrainComponent* component = scene.Components().AIBrains().TryGet(entity);
        return ComponentAccess{ component, component, kAIBrainFields, &MarkAIBrainModified };
    }
    if (componentName == "NavAgent") {
        kb::scene::NavAgent* component = scene.Components().NavAgents().TryGet(entity);
        return ComponentAccess{ component, component, kNavAgentFields, &MarkNavAgentModified };
    }
    if (componentName == "NavObstacle") {
        kb::scene::NavObstacle* component = scene.Components().NavObstacles().TryGet(entity);
        return ComponentAccess{ component, component, kNavObstacleFields, &MarkNavObstacleModified };
    }
    if (componentName == "NavLink") {
        kb::scene::NavLink* component = scene.Components().NavLinks().TryGet(entity);
        return ComponentAccess{ component, component, kNavLinkFields, &MarkNavLinkModified };
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
    if (componentName == "Grid") {
        auto* component = scene.Components().Grids().TryGet(entity);
        return ComponentAccess{ component, component, kGridFields, &MarkGridModified };
    }
    if (componentName == "Tilemap") {
        auto* component = scene.Components().Tilemaps().TryGet(entity);
        return ComponentAccess{ component, component, kTilemapFields, &MarkTilemapModified };
    }
    if (componentName == "TilemapRenderer") {
        auto* component = scene.Components().TilemapRenderers().TryGet(entity);
        return ComponentAccess{ component, component, kTilemapRendererFields, &MarkTilemapRendererModified };
    }
    if (componentName == "Sprite2D") {
        auto* component = scene.Components().Sprites2D().TryGet(entity);
        return ComponentAccess{ component, component, kSprite2DFields, &MarkSprite2DModified };
    }
    if (componentName == "Body2D") {
        auto* component = scene.Components().Bodies2D().TryGet(entity);
        return ComponentAccess{ component, component, kBody2DFields, &MarkBody2DModified };
    }
    if (componentName == "Collider2D") {
        auto* component = scene.Components().Colliders2D().TryGet(entity);
        return ComponentAccess{ component, component, kCollider2DFields, &MarkCollider2DModified };
    }
    if (componentName == "CharacterController2D") {
        auto* component = scene.Components().CharacterControllers2D().TryGet(entity);
        return ComponentAccess{ component, component, kCharacterController2DFields, &MarkCharacterController2DModified };
    }
    if (componentName == "TilemapCollider") {
        auto* component = scene.Components().TilemapColliders().TryGet(entity);
        return ComponentAccess{ component, component, kTilemapColliderFields, &MarkTilemapColliderModified };
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
    if (componentName == "Surface Cast") {
        kb::scene::SurfaceCastComponent* component = scene.Components().SurfaceCasts().TryGet(entity);
        return ComponentAccess{ component, component, kSurfaceCastFields, &MarkSurfaceCastModified };
    }
    if (componentName == "Facing Panel") {
        kb::scene::FacingPanelComponent* component = scene.Components().FacingPanels().TryGet(entity);
        return ComponentAccess{ component, component, kFacingPanelFields, &MarkFacingPanelModified };
    }
    if (componentName == "Line Renderer") {
        kb::scene::SpaceStrokeComponent* component = scene.Components().SpaceStrokes().TryGet(entity);
        return ComponentAccess{ component, component, kSpaceStrokeFields, &MarkSpaceStrokeModified };
    }
    if (componentName == "Trail Renderer") {
        kb::scene::HistoryRibbonComponent* component = scene.Components().HistoryRibbons().TryGet(entity);
        return ComponentAccess{ component, component, kHistoryRibbonFields, &MarkHistoryRibbonModified };
    }
    if (componentName == "Particle Effect") {
        kb::scene::ParticleEffectComponent* component = scene.Components().ParticleEffects().TryGet(entity);
        return ComponentAccess{ component, component, kParticleEffectFields, &MarkParticleEffectModified };
    }
    if (componentName == "Lens Flare") {
        kb::scene::LensEchoComponent* component = scene.Components().LensEchoes().TryGet(entity);
        return ComponentAccess{ component, component, kLensEchoFields, &MarkLensEchoModified };
    }
    return {};
}

} // namespace

std::span<const std::string_view> ScriptSceneComponentApi::ComponentNames() noexcept {
    static const std::vector<std::string_view> names = [] {
        std::vector<std::string_view> output{kComponentNames.begin(), kComponentNames.end()};
        const std::span<const std::string_view> uiNames = ScriptUIComponentApi::ComponentNames();
        output.insert(output.end(), uiNames.begin(), uiNames.end());
        return output;
    }();
    return names;
}

std::span<const ScriptSceneComponentPropertyDesc> ScriptSceneComponentApi::ComponentProperties(std::string_view componentName) noexcept {
    componentName = kb::scene::SceneComponentAuthoring::CanonicalName(componentName);
    if (componentName == "AudioSource") return kAudioSourcePropertyDescs;
    if (componentName == "AudioListener") return kAudioListenerPropertyDescs;
    if (ScriptUIComponentApi::IsComponent(componentName)) {
        return ScriptUIComponentApi::ComponentProperties(componentName);
    }
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
    if (componentName == "Grid") return kGridPropertyDescs;
    if (componentName == "Tilemap") return kTilemapPropertyDescs;
    if (componentName == "TilemapRenderer") return kTilemapRendererPropertyDescs;
    if (componentName == "TilemapCollider") return kTilemapColliderPropertyDescs;
    if (componentName == "CharacterController2D") return kCharacterController2DPropertyDescs;
    if (componentName == "Collider2D") return kCollider2DPropertyDescs;
    if (componentName == "Body2D") return kBody2DPropertyDescs;
    if (componentName == "Sprite2D") return kSprite2DPropertyDescs;
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
    if (componentName == "Surface Cast") return kSurfaceCastPropertyDescs;
    if (componentName == "Facing Panel") return kFacingPanelPropertyDescs;
    if (componentName == "Line Renderer") return kSpaceStrokePropertyDescs;
    if (componentName == "Trail Renderer") return kHistoryRibbonPropertyDescs;
    if (componentName == "Particle Effect") return kParticleEffectPropertyDescs;
    if (componentName == "Lens Flare") return kLensEchoPropertyDescs;
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
    if (componentName == "NavAgent") return kNavAgentPropertyDescs;
    if (componentName == "Animator") return kAnimatorPropertyDescs;
    if (componentName == "DeformedGeometry") return kDeformedGeometryPropertyDescs;
    if (componentName == "SocketAttachment") return kSocketAttachmentPropertyDescs;
    if (componentName == "Combatant") return kCombatantPropertyDescs;
    if (componentName == "AIBrain") return kAIBrainPropertyDescs;
    if (componentName == "NavObstacle") return kNavObstaclePropertyDescs;
    if (componentName == "NavLink") return kNavLinkPropertyDescs;
    if (componentName == "Tags") return kTagsPropertyDescs;
    return {};
}

bool ScriptSceneComponentApi::HasComponent(kb::scene::Scene& scene, kb::scene::SceneEntity entity, std::string_view componentName) noexcept {
    componentName = kb::scene::SceneComponentAuthoring::CanonicalName(componentName);
    if (ScriptUIComponentApi::IsComponent(componentName)) {
        return ScriptUIComponentApi::HasComponent(scene, entity, componentName);
    }
    return AccessComponent(scene, entity, componentName).immutable != nullptr;
}

ScriptSceneComponentPropertyResult ScriptSceneComponentApi::GetProperty(
    kb::scene::Scene& scene,
    kb::scene::SceneEntity entity,
    std::string_view componentName,
    std::string_view propertyName) {
    componentName = kb::scene::SceneComponentAuthoring::CanonicalName(componentName);
    if (ScriptUIComponentApi::IsComponent(componentName)) {
        return ScriptUIComponentApi::GetProperty(scene, entity, componentName, propertyName);
    }
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
    componentName = kb::scene::SceneComponentAuthoring::CanonicalName(componentName);
    if (ScriptUIComponentApi::IsComponent(componentName)) {
        return ScriptUIComponentApi::SetProperty(scene, entity, componentName, propertyName, value);
    }
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

    if (componentName == "AudioSource") {
        auto candidate = *static_cast<const kb::scene::AudioSourceComponent*>(component.immutable);
        if (!field->write(&candidate, value)) {
            return ScriptSceneComponentMutationResult{ .error = "value does not match the audio property type or format" };
        }
        if (!kb::scene::IsAudioSourceComponentPersistable(candidate)) {
            return ScriptSceneComponentMutationResult{ .error = "Audio Source values must remain finite and within valid playback and distance ranges" };
        }
        if (propertyName == "clipAssetId" && candidate.clipAssetId != 0U) {
            const auto* metadata = scene.Assets().Manager().Registry().Find(kb::assets::AssetId{candidate.clipAssetId});
            if (metadata == nullptr || !kb::assets::AssetMatchesKind(*metadata, kb::assets::AssetKind::Audio)) {
                return ScriptSceneComponentMutationResult{ .error = "audio clip must reference an existing audio asset" };
            }
        }
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
    if (componentName == "Surface Cast") {
        const kb::scene::SurfaceCastComponent& surfaceCast = *static_cast<const kb::scene::SurfaceCastComponent*>(component.immutable);
        kb::scene::SurfaceCastComponent candidate = surfaceCast;
        if (!field->write(&candidate, value)) return ScriptSceneComponentMutationResult{ .error = "script value type does not match component property" };
        if (!kb::scene::IsSurfaceCastComponentPersistable(candidate) || (candidate.enabled && !kb::scene::IsSurfaceCastComponentValid(candidate))) {
            return ScriptSceneComponentMutationResult{ .error = "Surface Cast values must remain valid before it can be enabled" };
        }
    }
    if (componentName == "Facing Panel") {
        const kb::scene::FacingPanelComponent& panel = *static_cast<const kb::scene::FacingPanelComponent*>(component.immutable);
        kb::scene::FacingPanelComponent candidate = panel;
        if (!field->write(&candidate, value)) return ScriptSceneComponentMutationResult{ .error = "script value type does not match component property" };
        if (!kb::scene::IsFacingPanelComponentPersistable(candidate)) return ScriptSceneComponentMutationResult{ .error = "Facing Panel values must remain valid" };
    }
    if (componentName == "Line Renderer") {
        const kb::scene::SpaceStrokeComponent& stroke = *static_cast<const kb::scene::SpaceStrokeComponent*>(component.immutable);
        kb::scene::SpaceStrokeComponent candidate = stroke;
        if (!field->write(&candidate, value)) return ScriptSceneComponentMutationResult{ .error = "script value type does not match component property" };
        if (!kb::scene::IsSpaceStrokeComponentPersistable(candidate)) return ScriptSceneComponentMutationResult{ .error = "Line Renderer values must remain valid" };
    }
    if (componentName == "Trail Renderer") {
        const kb::scene::HistoryRibbonComponent& ribbon = *static_cast<const kb::scene::HistoryRibbonComponent*>(component.immutable);
        kb::scene::HistoryRibbonComponent candidate = ribbon;
        if (!field->write(&candidate, value)) return ScriptSceneComponentMutationResult{ .error = "script value type does not match component property" };
        if (!kb::scene::IsHistoryRibbonComponentPersistable(candidate)) return ScriptSceneComponentMutationResult{ .error = "Trail Renderer values must remain valid" };
    }
    if (componentName == "WorldBackdrop") {
        kb::scene::WorldBackdropComponent candidate = *static_cast<const kb::scene::WorldBackdropComponent*>(component.immutable);
        if (!field->write(&candidate, value)) return ScriptSceneComponentMutationResult{ .error = "script value type does not match component property" };
        if (!kb::scene::IsWorldBackdropComponentValid(candidate)) {
            return ScriptSceneComponentMutationResult{ .error = "World Backdrop value out of range (mode 0..3, gradientExponent > 0, sunDisk 0..2, "
                "sunSize 0..1, sunSizeConvergence 1..10, atmosphereThickness 0..5, exposure 0..8)" };
        }
    }
    if (componentName == "DeformedGeometry") {
        kb::scene::DrawD3DeformedGeometryComponent candidate = *static_cast<const kb::scene::DrawD3DeformedGeometryComponent*>(component.immutable);
        if (!field->write(&candidate, value)) return ScriptSceneComponentMutationResult{ .error = "script value type does not match component property" };
        if (!kb::scene::IsDrawD3DeformedGeometryComponentPersistable(candidate) ||
            (candidate.enabled && !kb::scene::IsDrawD3DeformedGeometryComponentValid(candidate))) {
            return ScriptSceneComponentMutationResult{ .error = "Deformed Geometry needs its Skeletal Mesh before it can be enabled" };
        }
    }
    if (componentName == "AIBrain") {
        kb::scene::AIBrainComponent candidate = *static_cast<const kb::scene::AIBrainComponent*>(component.immutable);
        if (!field->write(&candidate, value)) return ScriptSceneComponentMutationResult{ .error = "script value type does not match component property" };
        if (!kb::scene::IsAIBrainComponentValid(candidate)) {
            return ScriptSceneComponentMutationResult{ .error = "AI Brain values must be finite and not negative; an ability slot is 1 to 8" };
        }
        *static_cast<kb::scene::AIBrainComponent*>(component.mutableComponent) = candidate;
        component.markModified(scene, entity);
        return ScriptSceneComponentMutationResult{ .succeeded = true };
    }
    if (componentName == "Combatant") {
        kb::scene::CombatantComponent candidate = *static_cast<const kb::scene::CombatantComponent*>(component.immutable);
        if (!field->write(&candidate, value)) return ScriptSceneComponentMutationResult{ .error = "script value type does not match component property" };
        // A larger maximum fills a full bar; a smaller one caps what is left.
        const kb::scene::CombatantComponent& before = *static_cast<const kb::scene::CombatantComponent*>(component.immutable);
        if (propertyName == "maximumHealth" && (before.health >= before.maximumHealth || candidate.health > candidate.maximumHealth)) {
            candidate.health = candidate.maximumHealth;
        }
        if (propertyName == "maximumMana" && (before.mana >= before.maximumMana || candidate.mana > candidate.maximumMana)) {
            candidate.mana = candidate.maximumMana;
        }
        if (!kb::scene::IsCombatantComponentValid(candidate)) {
            return ScriptSceneComponentMutationResult{ .error = "Combatant values must be finite: health and mana from 0 to their maximum, "
                "ranges not negative, sight angle 0..360, resistances at most 100" };
        }
        *static_cast<kb::scene::CombatantComponent*>(component.mutableComponent) = candidate;
        component.markModified(scene, entity);
        return ScriptSceneComponentMutationResult{ .succeeded = true };
    }
    if (componentName == "SocketAttachment") {
        kb::scene::SocketAttachmentComponent candidate = *static_cast<const kb::scene::SocketAttachmentComponent*>(component.immutable);
        if (!field->write(&candidate, value)) return ScriptSceneComponentMutationResult{ .error = "script value type does not match component property" };
        if (!kb::scene::IsSocketAttachmentComponentValid(candidate)) {
            return ScriptSceneComponentMutationResult{ .error = "Socket Attachment needs a socket name up to 128 bytes, a finite offset and a non-zero rotation" };
        }
    }
    if (componentName == "Particle Effect") {
        const kb::scene::ParticleEffectComponent& particleEffect = *static_cast<const kb::scene::ParticleEffectComponent*>(component.immutable);
        kb::scene::ParticleEffectComponent candidate = particleEffect;
        if (!field->write(&candidate, value)) return ScriptSceneComponentMutationResult{ .error = "script value type does not match component property" };
        if (!kb::scene::IsParticleEffectComponentPersistable(candidate)) return ScriptSceneComponentMutationResult{ .error = "Particle Effect values must remain valid" };
    }
    if (componentName == "Lens Flare") {
        const kb::scene::LensEchoComponent& echo = *static_cast<const kb::scene::LensEchoComponent*>(component.immutable);
        kb::scene::LensEchoComponent candidate = echo;
        if (!field->write(&candidate, value)) return ScriptSceneComponentMutationResult{ .error = "script value type does not match component property" };
        if (!kb::scene::IsLensEchoComponentPersistable(candidate)) return ScriptSceneComponentMutationResult{ .error = "Lens Flare values must remain valid" };
    }

    if (!field->write(component.mutableComponent, value)) {
        return ScriptSceneComponentMutationResult{ .error = "script value type does not match component property" };
    }

    if (componentName == "Tags" && propertyName == "text") {
        scene.Tags().RegisterAssignedTags(entity);
    }

    if (component.markModified != nullptr) {
        component.markModified(scene, entity);
    }
    return ScriptSceneComponentMutationResult{ .succeeded = true };
}

} // namespace kb::script
