#include "inspection/InspectorPhysicsModel.hpp"
#include "localization/EditorLocalization.hpp"

#include <array>
#include <cstdio>

namespace kb::editor {
namespace {

[[nodiscard]] std::string FormatFloat(float value) {
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%.3f", static_cast<double>(value));
    std::string text = buffer;
    if (text.find('.') != std::string::npos) {
        while (!text.empty() && text.back() == '0') {
            text.pop_back();
        }
        if (!text.empty() && text.back() == '.') {
            text.pop_back();
        }
    }
    return text == "-0" ? "0" : text;
}

[[nodiscard]] PhysicsField FloatField(std::string_view label, float value) {
    return PhysicsField{ .label = std::string{ label }, .kind = PhysicsFieldKind::Float, .value = FormatFloat(value) };
}

[[nodiscard]] PhysicsField BoolField(std::string_view label, bool value) {
    return PhysicsField{ .label = std::string{ label }, .kind = PhysicsFieldKind::Bool,
        .value = std::string{ value ? EditorText("inspector.on", "On") : EditorText("inspector.off", "Off") }, .boolValue = value };
}

[[nodiscard]] PhysicsField EnumField(std::string_view label, std::string_view value) {
    return PhysicsField{ .label = std::string{ label }, .kind = PhysicsFieldKind::Enum, .value = std::string{ value } };
}

[[nodiscard]] PhysicsField ReadOnlyField(std::string_view label, std::string value) {
    return PhysicsField{ .label = std::string{ label }, .kind = PhysicsFieldKind::ReadOnly, .value = std::move(value) };
}

[[nodiscard]] std::string_view BodyTypeName(kb::scene::RigidbodyBodyType type) {
    switch (type) {
    case kb::scene::RigidbodyBodyType::Static:
        return EditorText("inspector.rigidbody.static", "Static");
    case kb::scene::RigidbodyBodyType::Dynamic:
        return EditorText("inspector.rigidbody.dynamic", "Dynamic");
    case kb::scene::RigidbodyBodyType::Kinematic:
        return EditorText("inspector.rigidbody.kinematic", "Kinematic");
    }
    return EditorText("inspector.rigidbody.dynamic", "Dynamic");
}

[[nodiscard]] std::string_view ColliderShapeName(kb::scene::ColliderShape shape) {
    switch (shape) {
    case kb::scene::ColliderShape::Box:
        return EditorText("inspector.collider.box", "Box");
    case kb::scene::ColliderShape::Sphere:
        return EditorText("inspector.collider.sphere", "Sphere");
    case kb::scene::ColliderShape::Capsule:
        return EditorText("inspector.collider.capsule", "Capsule");
    case kb::scene::ColliderShape::Mesh:
        return EditorText("inspector.collider.mesh", "Mesh");
    }
    return EditorText("inspector.collider.box", "Box");
}

[[nodiscard]] std::string_view JointTypeName(kb::scene::JointType type) {
    switch (type) {
    case kb::scene::JointType::Fixed:
        return EditorText("inspector.joint.fixed", "Fixed");
    case kb::scene::JointType::Hinge:
        return EditorText("inspector.joint.hinge", "Hinge");
    case kb::scene::JointType::Distance:
        return EditorText("inspector.joint.distance", "Distance");
    case kb::scene::JointType::Point:
        return EditorText("inspector.joint.point", "Point");
    }
    return EditorText("inspector.joint.fixed", "Fixed");
}

} // namespace

// ---------------------------------------------------------------------------
// Rigidbody
// ---------------------------------------------------------------------------
std::vector<PhysicsField> InspectorPhysicsModel::Fields(const kb::scene::RigidbodyComponent& c) {
    return {
        EnumField(EditorText("inspector.rigidbody.bodyType", "Body Type"), BodyTypeName(c.bodyType)),
        FloatField(EditorText("inspector.rigidbody.mass", "Mass"), c.mass),
        FloatField(EditorText("inspector.rigidbody.gravityScale", "Gravity Scale"), c.gravityScale),
        BoolField(EditorText("inspector.rigidbody.useGravity", "Use Gravity"), c.useGravity),
        BoolField(EditorText("inspector.rigidbody.lockRotation", "Lock Rotation"), c.lockRotation),
        BoolField(EditorText("inspector.rigidbody.continuousCD", "Continuous CD"), c.useContinuousCollision),
        FloatField(EditorText("inspector.rigidbody.linVelocityX", "Lin Velocity X"), c.linearVelocity.x),
        FloatField(EditorText("inspector.rigidbody.linVelocityY", "Lin Velocity Y"), c.linearVelocity.y),
        FloatField(EditorText("inspector.rigidbody.linVelocityZ", "Lin Velocity Z"), c.linearVelocity.z),
        FloatField(EditorText("inspector.rigidbody.angVelocityX", "Ang Velocity X"), c.angularVelocity.x),
        FloatField(EditorText("inspector.rigidbody.angVelocityY", "Ang Velocity Y"), c.angularVelocity.y),
        FloatField(EditorText("inspector.rigidbody.angVelocityZ", "Ang Velocity Z"), c.angularVelocity.z),
    };
}

bool InspectorPhysicsModel::ReadFloat(const kb::scene::RigidbodyComponent& c, int index, float& out) noexcept {
    switch (index) {
    case 1: out = c.mass; return true;
    case 2: out = c.gravityScale; return true;
    case 6: out = c.linearVelocity.x; return true;
    case 7: out = c.linearVelocity.y; return true;
    case 8: out = c.linearVelocity.z; return true;
    case 9: out = c.angularVelocity.x; return true;
    case 10: out = c.angularVelocity.y; return true;
    case 11: out = c.angularVelocity.z; return true;
    default: return false;
    }
}

bool InspectorPhysicsModel::ApplyFloat(kb::scene::RigidbodyComponent& c, int index, float v) noexcept {
    switch (index) {
    case 1: c.mass = v; return true;
    case 2: c.gravityScale = v; return true;
    case 6: c.linearVelocity.x = v; return true;
    case 7: c.linearVelocity.y = v; return true;
    case 8: c.linearVelocity.z = v; return true;
    case 9: c.angularVelocity.x = v; return true;
    case 10: c.angularVelocity.y = v; return true;
    case 11: c.angularVelocity.z = v; return true;
    default: return false;
    }
}

bool InspectorPhysicsModel::ToggleBool(kb::scene::RigidbodyComponent& c, int index) noexcept {
    switch (index) {
    case 3: c.useGravity = !c.useGravity; return true;
    case 4: c.lockRotation = !c.lockRotation; return true;
    case 5: c.useContinuousCollision = !c.useContinuousCollision; return true;
    default: return false;
    }
}

bool InspectorPhysicsModel::CycleEnum(kb::scene::RigidbodyComponent& c, int index) noexcept {
    if (index != 0) {
        return false;
    }
    switch (c.bodyType) {
    case kb::scene::RigidbodyBodyType::Static: c.bodyType = kb::scene::RigidbodyBodyType::Dynamic; break;
    case kb::scene::RigidbodyBodyType::Dynamic: c.bodyType = kb::scene::RigidbodyBodyType::Kinematic; break;
    case kb::scene::RigidbodyBodyType::Kinematic: c.bodyType = kb::scene::RigidbodyBodyType::Static; break;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Collider
// ---------------------------------------------------------------------------
std::vector<PhysicsField> InspectorPhysicsModel::Fields(const kb::scene::ColliderComponent& c) {
    return {
        EnumField(EditorText("inspector.collider.shape", "Shape"), ColliderShapeName(c.shape)),
        FloatField(EditorText("inspector.collider.centerX", "Center X"), c.center.x),
        FloatField(EditorText("inspector.collider.centerY", "Center Y"), c.center.y),
        FloatField(EditorText("inspector.collider.centerZ", "Center Z"), c.center.z),
        FloatField(EditorText("inspector.collider.boxSizeX", "Box Size X"), c.boxSize.x),
        FloatField(EditorText("inspector.collider.boxSizeY", "Box Size Y"), c.boxSize.y),
        FloatField(EditorText("inspector.collider.boxSizeZ", "Box Size Z"), c.boxSize.z),
        FloatField(EditorText("inspector.collider.radius", "Radius"), c.radius),
        FloatField(EditorText("inspector.collider.height", "Height"), c.height),
        BoolField(EditorText("inspector.collider.isTrigger", "Is Trigger"), c.trigger),
        FloatField(EditorText("inspector.collider.friction", "Friction"), c.friction),
        FloatField(EditorText("inspector.collider.restitution", "Restitution"), c.restitution),
        ReadOnlyField(EditorText("inspector.collider.layerMask", "Layer Mask"), std::to_string(c.layer)),
        ReadOnlyField(EditorText("inspector.collider.collisionMeshAsset", "Collision Mesh Asset"), std::to_string(c.meshAssetId)),
    };
}

bool InspectorPhysicsModel::ReadFloat(const kb::scene::ColliderComponent& c, int index, float& out) noexcept {
    switch (index) {
    case 1: out = c.center.x; return true;
    case 2: out = c.center.y; return true;
    case 3: out = c.center.z; return true;
    case 4: out = c.boxSize.x; return true;
    case 5: out = c.boxSize.y; return true;
    case 6: out = c.boxSize.z; return true;
    case 7: out = c.radius; return true;
    case 8: out = c.height; return true;
    case 10: out = c.friction; return true;
    case 11: out = c.restitution; return true;
    default: return false;
    }
}

bool InspectorPhysicsModel::ApplyFloat(kb::scene::ColliderComponent& c, int index, float v) noexcept {
    switch (index) {
    case 1: c.center.x = v; return true;
    case 2: c.center.y = v; return true;
    case 3: c.center.z = v; return true;
    case 4: c.boxSize.x = v; return true;
    case 5: c.boxSize.y = v; return true;
    case 6: c.boxSize.z = v; return true;
    case 7: c.radius = v; return true;
    case 8: c.height = v; return true;
    case 10: c.friction = v; return true;
    case 11: c.restitution = v; return true;
    default: return false;
    }
}

bool InspectorPhysicsModel::ToggleBool(kb::scene::ColliderComponent& c, int index) noexcept {
    if (index != 9) {
        return false;
    }
    c.trigger = !c.trigger;
    return true;
}

bool InspectorPhysicsModel::CycleEnum(kb::scene::ColliderComponent& c, int index) noexcept {
    if (index != 0) {
        return false;
    }
    switch (c.shape) {
    case kb::scene::ColliderShape::Box: c.shape = kb::scene::ColliderShape::Sphere; break;
    case kb::scene::ColliderShape::Sphere: c.shape = kb::scene::ColliderShape::Capsule; break;
    case kb::scene::ColliderShape::Capsule: c.shape = kb::scene::ColliderShape::Mesh; break;
    case kb::scene::ColliderShape::Mesh: c.shape = kb::scene::ColliderShape::Box; break;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Character Controller
// ---------------------------------------------------------------------------
std::vector<PhysicsField> InspectorPhysicsModel::Fields(const kb::scene::CharacterControllerComponent& c) {
    return {
        FloatField(EditorText("inspector.characterController.centerX", "Center X"), c.center.x),
        FloatField(EditorText("inspector.characterController.centerY", "Center Y"), c.center.y),
        FloatField(EditorText("inspector.characterController.centerZ", "Center Z"), c.center.z),
        FloatField(EditorText("inspector.characterController.radius", "Radius"), c.radius),
        FloatField(EditorText("inspector.characterController.height", "Height"), c.height),
        FloatField(EditorText("inspector.characterController.slopeLimit", "Slope Limit"), c.slopeLimitDegrees),
        FloatField(EditorText("inspector.characterController.stepOffset", "Step Offset"), c.stepOffset),
        FloatField(EditorText("inspector.characterController.gravityScale", "Gravity Scale"), c.gravityScale),
        BoolField(EditorText("inspector.characterController.useGravity", "Use Gravity"), c.useGravity),
    };
}

bool InspectorPhysicsModel::ReadFloat(const kb::scene::CharacterControllerComponent& c, int index, float& out) noexcept {
    switch (index) {
    case 0: out = c.center.x; return true;
    case 1: out = c.center.y; return true;
    case 2: out = c.center.z; return true;
    case 3: out = c.radius; return true;
    case 4: out = c.height; return true;
    case 5: out = c.slopeLimitDegrees; return true;
    case 6: out = c.stepOffset; return true;
    case 7: out = c.gravityScale; return true;
    default: return false;
    }
}

bool InspectorPhysicsModel::ApplyFloat(kb::scene::CharacterControllerComponent& c, int index, float v) noexcept {
    switch (index) {
    case 0: c.center.x = v; return true;
    case 1: c.center.y = v; return true;
    case 2: c.center.z = v; return true;
    case 3: c.radius = v; return true;
    case 4: c.height = v; return true;
    case 5: c.slopeLimitDegrees = v; return true;
    case 6: c.stepOffset = v; return true;
    case 7: c.gravityScale = v; return true;
    default: return false;
    }
}

bool InspectorPhysicsModel::ToggleBool(kb::scene::CharacterControllerComponent& c, int index) noexcept {
    if (index != 8) {
        return false;
    }
    c.useGravity = !c.useGravity;
    return true;
}

bool InspectorPhysicsModel::CycleEnum(kb::scene::CharacterControllerComponent&, int) noexcept {
    return false; // no enum fields
}

// ---------------------------------------------------------------------------
// Joint
// ---------------------------------------------------------------------------
std::vector<PhysicsField> InspectorPhysicsModel::Fields(const kb::scene::JointComponent& c) {
    return {
        EnumField(EditorText("inspector.joint.type", "Type"), JointTypeName(c.type)),
        FloatField(EditorText("inspector.joint.anchorX", "Anchor X"), c.anchor.x),
        FloatField(EditorText("inspector.joint.anchorY", "Anchor Y"), c.anchor.y),
        FloatField(EditorText("inspector.joint.anchorZ", "Anchor Z"), c.anchor.z),
        FloatField(EditorText("inspector.joint.connAnchorX", "Conn Anchor X"), c.connectedAnchor.x),
        FloatField(EditorText("inspector.joint.connAnchorY", "Conn Anchor Y"), c.connectedAnchor.y),
        FloatField(EditorText("inspector.joint.connAnchorZ", "Conn Anchor Z"), c.connectedAnchor.z),
        FloatField(EditorText("inspector.joint.axisX", "Axis X"), c.axis.x),
        FloatField(EditorText("inspector.joint.axisY", "Axis Y"), c.axis.y),
        FloatField(EditorText("inspector.joint.axisZ", "Axis Z"), c.axis.z),
        FloatField(EditorText("inspector.joint.minLimit", "Min Limit"), c.minLimit),
        FloatField(EditorText("inspector.joint.maxLimit", "Max Limit"), c.maxLimit),
        BoolField(EditorText("inspector.joint.enableLimit", "Enable Limit"), c.enableLimit),
    };
}

bool InspectorPhysicsModel::ReadFloat(const kb::scene::JointComponent& c, int index, float& out) noexcept {
    switch (index) {
    case 1: out = c.anchor.x; return true;
    case 2: out = c.anchor.y; return true;
    case 3: out = c.anchor.z; return true;
    case 4: out = c.connectedAnchor.x; return true;
    case 5: out = c.connectedAnchor.y; return true;
    case 6: out = c.connectedAnchor.z; return true;
    case 7: out = c.axis.x; return true;
    case 8: out = c.axis.y; return true;
    case 9: out = c.axis.z; return true;
    case 10: out = c.minLimit; return true;
    case 11: out = c.maxLimit; return true;
    default: return false;
    }
}

bool InspectorPhysicsModel::ApplyFloat(kb::scene::JointComponent& c, int index, float v) noexcept {
    switch (index) {
    case 1: c.anchor.x = v; return true;
    case 2: c.anchor.y = v; return true;
    case 3: c.anchor.z = v; return true;
    case 4: c.connectedAnchor.x = v; return true;
    case 5: c.connectedAnchor.y = v; return true;
    case 6: c.connectedAnchor.z = v; return true;
    case 7: c.axis.x = v; return true;
    case 8: c.axis.y = v; return true;
    case 9: c.axis.z = v; return true;
    case 10: c.minLimit = v; return true;
    case 11: c.maxLimit = v; return true;
    default: return false;
    }
}

bool InspectorPhysicsModel::ToggleBool(kb::scene::JointComponent& c, int index) noexcept {
    if (index != 12) {
        return false;
    }
    c.enableLimit = !c.enableLimit;
    return true;
}

bool InspectorPhysicsModel::CycleEnum(kb::scene::JointComponent& c, int index) noexcept {
    if (index != 0) {
        return false;
    }
    switch (c.type) {
    case kb::scene::JointType::Fixed: c.type = kb::scene::JointType::Hinge; break;
    case kb::scene::JointType::Hinge: c.type = kb::scene::JointType::Distance; break;
    case kb::scene::JointType::Distance: c.type = kb::scene::JointType::Point; break;
    case kb::scene::JointType::Point: c.type = kb::scene::JointType::Fixed; break;
    }
    return true;
}

PhysicsFieldKind InspectorPhysicsModel::KindOf(PhysicsComponentKind component, int index) noexcept {
    const auto kindAt = [](const std::vector<PhysicsField>& fields, int i) {
        return (i >= 0 && static_cast<std::size_t>(i) < fields.size()) ? fields[static_cast<std::size_t>(i)].kind : PhysicsFieldKind::Float;
    };
    switch (component) {
    case PhysicsComponentKind::Rigidbody: return kindAt(Fields(kb::scene::RigidbodyComponent{}), index);
    case PhysicsComponentKind::Collider: return kindAt(Fields(kb::scene::ColliderComponent{}), index);
    case PhysicsComponentKind::CharacterController: return kindAt(Fields(kb::scene::CharacterControllerComponent{}), index);
    case PhysicsComponentKind::Joint: return kindAt(Fields(kb::scene::JointComponent{}), index);
    }
    return PhysicsFieldKind::Float;
}

} // namespace kb::editor
