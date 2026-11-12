#include "inspection/InspectorPhysicsModel.hpp"
#include "localization/EditorLocalization.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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
    return PhysicsField{ .label = std::string{ label }, .kind = PhysicsFieldKind::Float, .value = FormatFloat(value), .number = value };
}

// What a row says beyond its label: hover help, a unit after the number, a natural range for a slider.
[[nodiscard]] PhysicsField Tip(PhysicsField field, std::string_view tooltip) {
    field.tooltip = std::string{ tooltip };
    return field;
}

[[nodiscard]] PhysicsField Unit(PhysicsField field, std::string_view unit) {
    field.unit = unit;
    return field;
}

[[nodiscard]] PhysicsField Range(PhysicsField field, float minimum, float maximum) {
    field.bounded = true;
    field.minimum = minimum;
    field.maximum = maximum;
    return field;
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
    case kb::scene::ColliderShape::OneWayBox:
        return EditorText("inspector.collider.oneWay", "One-way platform");
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
    case kb::scene::JointType::Slider:
        return EditorText("inspector.joint.slider", "Slider");
    case kb::scene::JointType::Cone:
        return EditorText("inspector.joint.cone", "Cone");
    case kb::scene::JointType::SixDof:
        return EditorText("inspector.joint.sixDof", "Six DOF");
    case kb::scene::JointType::Rope:
        return EditorText("inspector.joint.rope", "Rope");
    }
    return EditorText("inspector.joint.fixed", "Fixed");
}

[[nodiscard]] std::string_view JointAxisModeName(kb::scene::JointAxisMode mode) {
    switch (mode) {
    case kb::scene::JointAxisMode::Locked:
        return EditorText("inspector.joint.modeLocked", "Locked");
    case kb::scene::JointAxisMode::Limited:
        return EditorText("inspector.joint.modeLimited", "Limited");
    case kb::scene::JointAxisMode::Free:
        return EditorText("inspector.joint.modeFree", "Free");
    }
    return EditorText("inspector.joint.modeLocked", "Locked");
}

// How strongly a motor pushes when its speed is set while its force is still zero: enough for ordinary doors and lifts.
constexpr float kJointMotorStartForce = 1000.0F;

// The unit axes a joint can be pointed along from the Direction row; anything else reads "Custom".
[[nodiscard]] int JointAxisChoice(const kb::scene::Vec3& axis) noexcept {
    const float length = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    if (!(length > 1e-6F)) {
        return -1;
    }
    constexpr float tolerance = 1e-4F;
    const float x = axis.x / length;
    const float y = axis.y / length;
    const float z = axis.z / length;
    if (std::fabs(x - 1.0F) < tolerance && std::fabs(y) < tolerance && std::fabs(z) < tolerance) return 0;
    if (std::fabs(y - 1.0F) < tolerance && std::fabs(x) < tolerance && std::fabs(z) < tolerance) return 1;
    if (std::fabs(z - 1.0F) < tolerance && std::fabs(x) < tolerance && std::fabs(y) < tolerance) return 2;
    return -1;
}

[[nodiscard]] std::string_view JointDirectionName(const kb::scene::Vec3& axis) {
    switch (JointAxisChoice(axis)) {
    case 0: return EditorText("inspector.joint.direction.x", "X (sideways)");
    case 1: return EditorText("inspector.joint.direction.y", "Y (up)");
    case 2: return EditorText("inspector.joint.direction.z", "Z (forward)");
    default: return EditorText("inspector.joint.direction.custom", "Custom");
    }
}

// The Direction row says what the axis is for: a hinge turns around it, a slider slides along it.
[[nodiscard]] std::string_view JointDirectionLabel(kb::scene::JointType type) {
    switch (type) {
    case kb::scene::JointType::Hinge: return EditorText("inspector.joint.turnsAround", "Turns around");
    case kb::scene::JointType::Slider: return EditorText("inspector.joint.slidesAlong", "Slides along");
    case kb::scene::JointType::Cone: return EditorText("inspector.joint.pointsAlong", "Points along");
    default: return EditorText("inspector.joint.direction", "Direction");
    }
}

[[nodiscard]] float ClampDegrees(float value) noexcept {
    return value < 0.0F ? 0.0F : (value > 180.0F ? 180.0F : value);
}

// The first three axes of a SixDof joint are translations, the last three rotations, each named X, Y, Z.
[[nodiscard]] std::string JointAxisLabel(int axis) {
    const std::string_view letter = axis % 3 == 0 ? "X" : (axis % 3 == 1 ? "Y" : "Z");
    return axis < 3 ? EditorLocalization::Format("inspector.joint.linearAxis", "Linear {axis}", { { "axis", std::string{ letter } } })
                    : EditorLocalization::Format("inspector.joint.angularAxis", "Angular {axis}", { { "axis", std::string{ letter } } });
}

// Where a Joint row index falls in the per-axis block: the axis and which of its JointAxisRows rows.
struct JointAxisRow {
    int axis = 0;
    int row = 0;
};

[[nodiscard]] bool LocateJointAxisRow(int index, JointAxisRow& out) noexcept {
    const int local = index - InspectorPhysicsModel::JointFirstAxisRow;
    if (local < 0 || local >= InspectorPhysicsModel::JointAxisRows * InspectorPhysicsModel::JointAxisCount) {
        return false;
    }
    out = JointAxisRow{ .axis = local / InspectorPhysicsModel::JointAxisRows, .row = local % InspectorPhysicsModel::JointAxisRows };
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Rigidbody
// ---------------------------------------------------------------------------
std::vector<PhysicsField> InspectorPhysicsModel::Fields(const kb::scene::RigidbodyComponent& c) {
    return {
        Tip(EnumField(EditorText("inspector.rigidbody.bodyType", "Body Type"), BodyTypeName(c.bodyType)),
            EditorText("inspector.tip.rigidbody.bodyType", "Dynamic moves by physics, Kinematic moves only when you move it, Static never moves.")),
        Unit(Tip(FloatField(EditorText("inspector.rigidbody.mass", "Mass"), c.mass),
            EditorText("inspector.tip.rigidbody.mass", "How heavy the object is. Heavier objects push lighter ones away.")), "kg"),
        FloatField(EditorText("inspector.rigidbody.gravityScale", "Gravity Scale"), c.gravityScale),
        BoolField(EditorText("inspector.rigidbody.useGravity", "Use Gravity"), c.useGravity),
        Tip(BoolField(EditorText("inspector.rigidbody.lockRotation", "Lock Rotation"), c.lockRotation),
            EditorText("inspector.tip.rigidbody.lockRotation", "The object slides but never tips over or spins.")),
        Tip(BoolField(EditorText("inspector.rigidbody.fastMoving", "Fast Moving"), c.useContinuousCollision),
            EditorText("inspector.tip.rigidbody.fastMoving", "Turn on for fast objects, such as bullets, so they cannot pass through walls.")),
        Unit(Tip(FloatField(EditorLocalization::Format("inspector.rigidbody.velocity", "Speed {axis}", { { "axis", "X" } }), c.linearVelocity.x),
            EditorText("inspector.tip.rigidbody.linearVelocity", "How fast the object moves along this axis.")), "m/s"),
        Unit(FloatField(EditorLocalization::Format("inspector.rigidbody.velocity", "Speed {axis}", { { "axis", "Y" } }), c.linearVelocity.y), "m/s"),
        Unit(FloatField(EditorLocalization::Format("inspector.rigidbody.velocity", "Speed {axis}", { { "axis", "Z" } }), c.linearVelocity.z), "m/s"),
        Unit(Tip(FloatField(EditorLocalization::Format("inspector.rigidbody.spin", "Spin {axis}", { { "axis", "X" } }), c.angularVelocity.x),
            EditorText("inspector.tip.rigidbody.angularVelocity", "How fast the object spins around this axis.")), "\xC2\xB0/s"),
        Unit(FloatField(EditorLocalization::Format("inspector.rigidbody.spin", "Spin {axis}", { { "axis", "Y" } }), c.angularVelocity.y), "\xC2\xB0/s"),
        Unit(FloatField(EditorLocalization::Format("inspector.rigidbody.spin", "Spin {axis}", { { "axis", "Z" } }), c.angularVelocity.z), "\xC2\xB0/s"),
        Range(Tip(FloatField(EditorText("inspector.rigidbody.linearDamping", "Linear Damping"), c.linearDamping),
            EditorText("inspector.tip.rigidbody.linearDamping", "Slows the movement down over time, like air drag.")), 0.0F, 1.0F),
        Range(Tip(FloatField(EditorText("inspector.rigidbody.angularDamping", "Angular Damping"), c.angularDamping),
            EditorText("inspector.tip.rigidbody.angularDamping", "Slows the spinning down over time.")), 0.0F, 1.0F),
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
    case 12: out = c.linearDamping; return true;
    case 13: out = c.angularDamping; return true;
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
    case 12: c.linearDamping = v < 0.0F ? 0.0F : v; return true;
    case 13: c.angularDamping = v < 0.0F ? 0.0F : v; return true;
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
        Tip(EnumField(EditorText("inspector.collider.shape", "Shape"), ColliderShapeName(c.shape)),
            EditorText("inspector.tip.collider.shape", "The outline that detects hits: a box, a sphere, a capsule or the shape of a mesh.")),
        Unit(Tip(FloatField(EditorText("inspector.collider.centerX", "Center X"), c.center.x),
            EditorText("inspector.tip.collider.center", "Moves the shape away from the middle of the object.")), "m"),
        Unit(FloatField(EditorText("inspector.collider.centerY", "Center Y"), c.center.y), "m"),
        Unit(FloatField(EditorText("inspector.collider.centerZ", "Center Z"), c.center.z), "m"),
        Unit(Tip(FloatField(EditorText("inspector.collider.boxSizeX", "Box Size X"), c.boxSize.x),
            EditorText("inspector.tip.collider.boxSize", "Width, height and depth of the box.")), "m"),
        Unit(FloatField(EditorText("inspector.collider.boxSizeY", "Box Size Y"), c.boxSize.y), "m"),
        Unit(FloatField(EditorText("inspector.collider.boxSizeZ", "Box Size Z"), c.boxSize.z), "m"),
        Unit(FloatField(EditorText("inspector.collider.radius", "Radius"), c.radius), "m"),
        Unit(FloatField(EditorText("inspector.collider.height", "Height"), c.height), "m"),
        Tip(BoolField(EditorText("inspector.collider.isTrigger", "Is Trigger"), c.trigger),
            EditorText("inspector.tip.collider.isTrigger", "Notices things passing through but does not block them.")),
        Range(Tip(FloatField(EditorText("inspector.collider.friction", "Friction"), c.friction),
            EditorText("inspector.tip.collider.friction", "How much the surface grips. 0 is slippery like ice, 1 is rough.")), 0.0F, 1.0F),
        Range(Tip(FloatField(EditorText("inspector.collider.bounciness", "Bounciness"), c.restitution),
            EditorText("inspector.tip.collider.bounciness", "How much things bounce off. 0 does not bounce, 1 bounces back fully.")), 0.0F, 1.0F),
        ReadOnlyField(EditorText("inspector.collider.layerMask", "Layer Mask"), std::to_string(c.layer)),
        Tip(ReadOnlyField(EditorText("inspector.collider.collisionMeshAsset", "Collision Mesh Asset"), std::to_string(c.meshAssetId)),
            EditorText("inspector.tip.collider.meshAsset", "The mesh whose surface is used as the shape.")),
        Tip(BoolField(EditorText("inspector.affectsNavigation", "Affects Navigation"), c.affectsNavigation),
            EditorText("inspector.tip.affectsNavigation", "Characters walk on this object or around it. Turn off for things they should ignore, such as grass or small props.")),
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
    switch (index) {
    case 9: c.trigger = !c.trigger; return true;
    case 14: c.affectsNavigation = !c.affectsNavigation; return true;
    default: return false;
    }
}

bool InspectorPhysicsModel::CycleEnum(kb::scene::ColliderComponent& c, int index) noexcept {
    if (index != 0) {
        return false;
    }
    switch (c.shape) {
    case kb::scene::ColliderShape::Box: c.shape = kb::scene::ColliderShape::Sphere; break;
    case kb::scene::ColliderShape::Sphere: c.shape = kb::scene::ColliderShape::Capsule; break;
    case kb::scene::ColliderShape::Capsule: c.shape = kb::scene::ColliderShape::Mesh; break;
    case kb::scene::ColliderShape::Mesh: c.shape = kb::scene::ColliderShape::OneWayBox; break;
    case kb::scene::ColliderShape::OneWayBox: c.shape = kb::scene::ColliderShape::Box; break;
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
        Unit(Range(Tip(FloatField(EditorText("inspector.characterController.slopeLimit", "Slope Limit"), c.slopeLimitDegrees),
            EditorText("inspector.tip.characterController.slopeLimit", "The steepest slope the character can walk up.")), 0.0F, 90.0F), "\xC2\xB0"),
        Unit(Tip(FloatField(EditorText("inspector.characterController.stepOffset", "Step Offset"), c.stepOffset),
            EditorText("inspector.tip.characterController.stepOffset", "The tallest step the character climbs without jumping.")), "m"),
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
    // The limits are angles for a hinge and the twist of a cone, distances for the other kinds.
    const std::string_view limitUnit = c.type == kb::scene::JointType::Hinge || c.type == kb::scene::JointType::Cone ? "\xC2\xB0" : "m";
    const std::string_view motorUnit = c.type == kb::scene::JointType::Hinge ? "\xC2\xB0/s" : "m/s";
    const bool distance = c.type == kb::scene::JointType::Distance;
    std::vector<PhysicsField> fields{
        Tip(EnumField(EditorText("inspector.joint.type", "Type"), JointTypeName(c.type)),
            EditorText("inspector.tip.joint.type", "How the two objects are connected: held together, a hinge, a slider, a rope and more.")),
        Unit(Tip(FloatField(EditorText("inspector.joint.anchorX", "Anchor X"), c.anchor.x),
            EditorText("inspector.tip.joint.anchor", "Where on this object the joint is attached.")), "m"),
        Unit(FloatField(EditorText("inspector.joint.anchorY", "Anchor Y"), c.anchor.y), "m"),
        Unit(FloatField(EditorText("inspector.joint.anchorZ", "Anchor Z"), c.anchor.z), "m"),
        Unit(Tip(FloatField(EditorLocalization::Format("inspector.joint.otherAnchor", "Other Anchor {axis}", { { "axis", "X" } }), c.connectedAnchor.x),
            EditorText("inspector.tip.joint.connectedAnchor", "Where on the other object, or in the world, the joint is attached.")), "m"),
        Unit(FloatField(EditorLocalization::Format("inspector.joint.otherAnchor", "Other Anchor {axis}", { { "axis", "Y" } }), c.connectedAnchor.y), "m"),
        Unit(FloatField(EditorLocalization::Format("inspector.joint.otherAnchor", "Other Anchor {axis}", { { "axis", "Z" } }), c.connectedAnchor.z), "m"),
        Tip(FloatField(EditorText("inspector.joint.axisX", "Axis X"), c.axis.x),
            EditorText("inspector.tip.joint.axis", "The direction the joint turns around or slides along.")),
        FloatField(EditorText("inspector.joint.axisY", "Axis Y"), c.axis.y),
        FloatField(EditorText("inspector.joint.axisZ", "Axis Z"), c.axis.z),
        Unit(Tip(FloatField(distance ? EditorText("inspector.joint.minDistance", "Min Distance") : EditorText("inspector.joint.minLimit", "Min Limit"), c.minLimit),
            EditorText("inspector.tip.joint.minLimit", "The lowest angle or distance the joint can reach.")), limitUnit),
        Unit(Tip(FloatField(distance ? EditorText("inspector.joint.maxDistance", "Max Distance") : EditorText("inspector.joint.maxLimit", "Max Limit"), c.maxLimit),
            EditorText("inspector.tip.joint.maxLimit", "The highest angle or distance the joint can reach.")), limitUnit),
        Tip(BoolField(EditorText("inspector.joint.enableLimit", "Enable Limit"), c.enableLimit),
            EditorText("inspector.tip.joint.enableLimit", "Stops the joint from moving past the minimum and maximum.")),
        Tip(FloatField(EditorText("inspector.joint.motorTargetVelocity", "Motor Target Velocity"), c.motor.targetVelocity),
            EditorText("inspector.tip.joint.motorVelocity", "The speed the motor drives the joint at.")),
        Tip(FloatField(EditorText("inspector.joint.motorForce", "Motor Force"), c.motor.force),
            EditorText("inspector.tip.joint.motorForce", "How strongly the motor pushes. Zero turns the motor off.")),
        Tip(FloatField(EditorText("inspector.joint.spring", "Spring"), c.spring.spring),
            EditorText("inspector.tip.joint.spring", "How stiff the spring is. Zero keeps the joint rigid.")),
        Tip(FloatField(EditorText("inspector.joint.springDamper", "Spring Damper"), c.spring.damper),
            EditorText("inspector.tip.joint.springDamper", "How quickly the spring stops bouncing.")),
        Unit(Tip(FloatField(EditorText("inspector.joint.breakForce", "Break Force"), c.breakForce),
            EditorText("inspector.tip.joint.breakForce", "The joint snaps when pushed harder than this. Zero never breaks.")), "N"),
        Tip(FloatField(EditorLocalization::Format("inspector.joint.secondaryAxis", "Secondary Axis {axis}", { { "axis", "X" } }), c.freedom.secondaryAxis.x),
            EditorText("inspector.tip.joint.secondaryAxis", "A second direction that fixes how the joint is turned.")),
        FloatField(EditorLocalization::Format("inspector.joint.secondaryAxis", "Secondary Axis {axis}", { { "axis", "Y" } }), c.freedom.secondaryAxis.y),
        FloatField(EditorLocalization::Format("inspector.joint.secondaryAxis", "Secondary Axis {axis}", { { "axis", "Z" } }), c.freedom.secondaryAxis.z),
        Unit(Range(Tip(FloatField(EditorText("inspector.joint.swing1Limit", "Swing 1 Limit"), c.freedom.swing1Limit),
            EditorText("inspector.tip.joint.swing", "How far the joint may tilt, in degrees. 180 leaves it free.")), 0.0F, 180.0F), "\xC2\xB0"),
        Unit(Range(Tip(FloatField(EditorText("inspector.joint.swing2Limit", "Swing 2 Limit"), c.freedom.swing2Limit),
            EditorText("inspector.tip.joint.swing", "How far the joint may tilt, in degrees. 180 leaves it free.")), 0.0F, 180.0F), "\xC2\xB0"),
    };
    // The axis blocks of a SixDof joint come before the plain rows (see JointPlainFirstRow).
    for (int axis = 0; axis < InspectorPhysicsModel::JointAxisCount; ++axis) {
        const kb::scene::JointAxis& limit = c.freedom.axes[static_cast<std::size_t>(axis)];
        const std::string label = JointAxisLabel(axis);
        fields.push_back(EnumField(EditorLocalization::Format("inspector.joint.axisMode", "{axis} Mode", { { "axis", label } }), JointAxisModeName(limit.mode)));
        fields.push_back(FloatField(EditorLocalization::Format("inspector.joint.axisMin", "{axis} Min", { { "axis", label } }), limit.minLimit));
        fields.push_back(FloatField(EditorLocalization::Format("inspector.joint.axisMax", "{axis} Max", { { "axis", label } }), limit.maxLimit));
        fields.push_back(FloatField(EditorLocalization::Format("inspector.joint.axisDriveVelocity", "{axis} Drive Velocity", { { "axis", label } }), limit.motor.targetVelocity));
        fields.push_back(FloatField(EditorLocalization::Format("inspector.joint.axisDriveForce", "{axis} Drive Force", { { "axis", label } }), limit.motor.force));
    }
    fields.push_back(Range(Unit(Tip(FloatField(EditorText("inspector.joint.opensUpTo", "Opens up to"), c.maxLimit),
        EditorText("inspector.tip.joint.opens", "How far it swings open, to each side of where it starts.")), "\xC2\xB0"), 0.0F, 180.0F));
    fields.push_back(Unit(Tip(FloatField(EditorText("inspector.joint.slidesUpTo", "Slides up to"), c.maxLimit),
        EditorText("inspector.tip.joint.slides", "How far it slides from where it starts.")), "m"));
    fields.push_back(Unit(Tip(FloatField(EditorText("inspector.joint.maxLength", "Max length"), c.maxLimit),
        EditorText("inspector.tip.joint.length", "The longest the rope can get. Past this it pulls tight.")), "m"));
    fields.push_back(Range(Unit(Tip(FloatField(EditorText("inspector.joint.tiltsUpTo", "Tilts up to"), c.freedom.swing1Limit),
        EditorText("inspector.tip.joint.tilt", "How far it can tilt in any direction. 180 leaves it free.")), "\xC2\xB0"), 0.0F, 180.0F));
    fields.push_back(Range(Unit(Tip(FloatField(EditorText("inspector.joint.twistsUpTo", "Twists up to"), c.maxLimit),
        EditorText("inspector.tip.joint.twist", "How far it can twist to each side.")), "\xC2\xB0"), 0.0F, 180.0F));
    fields.push_back(Unit(Tip(FloatField(EditorText("inspector.joint.motorSpeed", "Motor speed"), c.motor.targetVelocity),
        EditorText("inspector.tip.joint.motorSpeed", "How fast the motor moves it. 0 turns the motor off.")), motorUnit));
    fields.push_back(Tip(EnumField(JointDirectionLabel(c.type), JointDirectionName(c.axis)),
        EditorText("inspector.tip.joint.direction", "Which way it turns, slides or points. Click to change it.")));
    return fields;
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
    case 13: out = c.motor.targetVelocity; return true;
    case 14: out = c.motor.force; return true;
    case 15: out = c.spring.spring; return true;
    case 16: out = c.spring.damper; return true;
    case 17: out = c.breakForce; return true;
    case JointSecondaryAxisRow: out = c.freedom.secondaryAxis.x; return true;
    case JointSecondaryAxisRow + 1: out = c.freedom.secondaryAxis.y; return true;
    case JointSecondaryAxisRow + 2: out = c.freedom.secondaryAxis.z; return true;
    case JointSwing1Row: out = c.freedom.swing1Limit; return true;
    case JointSwing1Row + 1: out = c.freedom.swing2Limit; return true;
    case JointOpensRow:
    case JointSlidesRow:
    case JointLengthRow:
    case JointTwistRow: out = c.maxLimit; return true;
    case JointTiltRow: out = c.freedom.swing1Limit; return true;
    case JointMotorSpeedRow: out = c.motor.targetVelocity; return true;
    default: break;
    }
    JointAxisRow located;
    if (!LocateJointAxisRow(index, located)) {
        return false;
    }
    const kb::scene::JointAxis& axis = c.freedom.axes[static_cast<std::size_t>(located.axis)];
    switch (located.row) {
    case 1: out = axis.minLimit; return true;
    case 2: out = axis.maxLimit; return true;
    case 3: out = axis.motor.targetVelocity; return true;
    case 4: out = axis.motor.force; return true;
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
    case 13: c.motor.targetVelocity = v; return true;
    case 14: c.motor.force = v < 0.0F ? 0.0F : v; return true;
    case 15: c.spring.spring = v < 0.0F ? 0.0F : v; return true;
    case 16: c.spring.damper = v < 0.0F ? 0.0F : v; return true;
    case 17: c.breakForce = v < 0.0F ? 0.0F : v; return true;
    case JointSecondaryAxisRow: c.freedom.secondaryAxis.x = v; return true;
    case JointSecondaryAxisRow + 1: c.freedom.secondaryAxis.y = v; return true;
    case JointSecondaryAxisRow + 2: c.freedom.secondaryAxis.z = v; return true;
    case JointSwing1Row: c.freedom.swing1Limit = v < 0.0F ? 0.0F : (v > 180.0F ? 180.0F : v); return true;
    case JointSwing1Row + 1: c.freedom.swing2Limit = v < 0.0F ? 0.0F : (v > 180.0F ? 180.0F : v); return true;
    case JointOpensRow:
    case JointTwistRow: // the same amount to either side of where it starts
        c.maxLimit = ClampDegrees(v);
        c.minLimit = -c.maxLimit;
        return true;
    case JointSlidesRow:
    case JointLengthRow: c.maxLimit = v < 0.0F ? 0.0F : v; return true;
    case JointTiltRow:
        c.freedom.swing1Limit = ClampDegrees(v);
        c.freedom.swing2Limit = c.freedom.swing1Limit;
        return true;
    case JointMotorSpeedRow:
        // A motor with no force does nothing, so a speed switches it on and a zero speed switches it off.
        c.motor.targetVelocity = v;
        c.motor.force = v == 0.0F ? 0.0F : (c.motor.force == 0.0F ? kJointMotorStartForce : c.motor.force);
        return true;
    default: break;
    }
    JointAxisRow located;
    if (!LocateJointAxisRow(index, located)) {
        return false;
    }
    kb::scene::JointAxis& axis = c.freedom.axes[static_cast<std::size_t>(located.axis)];
    switch (located.row) {
    case 1: axis.minLimit = v; return true;
    case 2: axis.maxLimit = v; return true;
    case 3: axis.motor.targetVelocity = v; return true;
    case 4: axis.motor.force = v < 0.0F ? 0.0F : v; return true;
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
    JointAxisRow located;
    if (LocateJointAxisRow(index, located) && located.row == 0) {
        kb::scene::JointAxisMode& mode = c.freedom.axes[static_cast<std::size_t>(located.axis)].mode;
        switch (mode) {
        case kb::scene::JointAxisMode::Locked: mode = kb::scene::JointAxisMode::Limited; break;
        case kb::scene::JointAxisMode::Limited: mode = kb::scene::JointAxisMode::Free; break;
        case kb::scene::JointAxisMode::Free: mode = kb::scene::JointAxisMode::Locked; break;
        }
        return true;
    }
    if (index == JointDirectionRow) {
        switch (JointAxisChoice(c.axis)) {
        case 0: c.axis = { 0.0F, 1.0F, 0.0F }; break;
        case 1: c.axis = { 0.0F, 0.0F, 1.0F }; break;
        default: c.axis = { 1.0F, 0.0F, 0.0F }; break;
        }
        return true;
    }
    if (index != 0) {
        return false;
    }
    switch (c.type) {
    case kb::scene::JointType::Fixed: c.type = kb::scene::JointType::Hinge; break;
    case kb::scene::JointType::Hinge: c.type = kb::scene::JointType::Distance; break;
    case kb::scene::JointType::Distance: c.type = kb::scene::JointType::Point; break;
    case kb::scene::JointType::Point: c.type = kb::scene::JointType::Slider; break;
    case kb::scene::JointType::Slider: c.type = kb::scene::JointType::Cone; break;
    case kb::scene::JointType::Cone: c.type = kb::scene::JointType::SixDof; break;
    case kb::scene::JointType::SixDof: c.type = kb::scene::JointType::Rope; break;
    case kb::scene::JointType::Rope: c.type = kb::scene::JointType::Fixed; break;
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

// ---------------------------------------------------------------------------
// Layout, reset to default and Joint presets
// ---------------------------------------------------------------------------
namespace {

// Builds a PhysicsLayout: rows added before Advanced() are always shown, the rest sit behind the Advanced row.
class LayoutBuilder {
public:
    LayoutBuilder& Row(int index) noexcept {
        layout_.rows[static_cast<std::size_t>(layout_.count++)] = static_cast<std::uint8_t>(index);
        if (!advanced_) {
            layout_.basicCount = layout_.count;
        }
        return *this;
    }
    LayoutBuilder& Rows(int first, int count) noexcept {
        for (int offset = 0; offset < count; ++offset) {
            Row(first + offset);
        }
        return *this;
    }
    LayoutBuilder& Advanced() noexcept {
        advanced_ = true;
        return *this;
    }
    [[nodiscard]] PhysicsLayout Done() const noexcept { return layout_; }

private:
    PhysicsLayout layout_{};
    bool advanced_ = false;
};

template <typename Component>
[[nodiscard]] std::vector<int> ChangedRowsOf(const Component& component) {
    const std::vector<PhysicsField> now = InspectorPhysicsModel::Fields(component);
    const std::vector<PhysicsField> fresh = InspectorPhysicsModel::Fields(Component{});
    std::vector<int> changed;
    for (std::size_t index = 0U; index < now.size(); ++index) {
        if (now[index].kind != PhysicsFieldKind::ReadOnly && now[index].value != fresh[index].value) {
            changed.push_back(static_cast<int>(index));
        }
    }
    return changed;
}

// Float rows take the fresh value, check boxes are flipped when they differ, and a choice is advanced until it
// shows the fresh one (a choice has at most a handful of values).
template <typename Component>
[[nodiscard]] bool ResetRowOf(Component& component, int index) {
    const Component fresh{};
    const std::vector<PhysicsField> before = InspectorPhysicsModel::Fields(component);
    const std::vector<PhysicsField> target = InspectorPhysicsModel::Fields(fresh);
    if (index < 0 || index >= static_cast<int>(before.size())) {
        return false;
    }
    const PhysicsField& want = target[static_cast<std::size_t>(index)];
    switch (before[static_cast<std::size_t>(index)].kind) {
    case PhysicsFieldKind::Float:
        return InspectorPhysicsModel::ApplyFloat(component, index, want.number);
    case PhysicsFieldKind::Bool:
        return before[static_cast<std::size_t>(index)].boolValue == want.boolValue || InspectorPhysicsModel::ToggleBool(component, index);
    case PhysicsFieldKind::Enum:
        for (int step = 0; step < 16; ++step) {
            if (InspectorPhysicsModel::Fields(component)[static_cast<std::size_t>(index)].value == want.value) {
                return true;
            }
            if (!InspectorPhysicsModel::CycleEnum(component, index)) {
                return false;
            }
        }
        return false;
    case PhysicsFieldKind::ReadOnly:
        break;
    }
    return false;
}

} // namespace

PhysicsLayout InspectorPhysicsModel::Layout(const kb::scene::RigidbodyComponent&) noexcept {
    // Body type, mass and the two switches people reach for first; drag, and the speeds the body has right now, behind Advanced.
    return LayoutBuilder{}.Row(0).Row(1).Row(3).Row(2).Row(4).Row(5).Advanced().Row(12).Row(13).Rows(6, 6).Done();
}

PhysicsLayout InspectorPhysicsModel::Layout(const kb::scene::ColliderComponent& component) noexcept {
    LayoutBuilder layout;
    layout.Row(0);
    switch (component.shape) {
    case kb::scene::ColliderShape::Box:
    case kb::scene::ColliderShape::OneWayBox: layout.Rows(4, 3); break;
    case kb::scene::ColliderShape::Sphere: layout.Row(7); break;
    case kb::scene::ColliderShape::Capsule: layout.Row(7).Row(8); break;
    case kb::scene::ColliderShape::Mesh: layout.Row(13); break;
    }
    layout.Rows(1, 3).Row(9).Row(14).Advanced().Row(10).Row(11).Row(12);
    return layout.Done();
}

PhysicsLayout InspectorPhysicsModel::Layout(const kb::scene::CharacterControllerComponent&) noexcept {
    return LayoutBuilder{}.Rows(0, FieldCount(PhysicsComponentKind::CharacterController)).Done();
}

PhysicsLayout InspectorPhysicsModel::Layout(const kb::scene::JointComponent& component) noexcept {
    using Type = kb::scene::JointType;
    const Type type = component.type;
    const bool hinge = type == Type::Hinge;
    const bool slider = type == Type::Slider;
    const bool cone = type == Type::Cone;
    const bool sixDof = type == Type::SixDof;
    const bool rope = type == Type::Rope;
    const bool distance = type == Type::Distance;
    LayoutBuilder layout;
    layout.Row(0);
    // What most authors set, in plain terms: the direction, whether the movement is limited, and how far it goes.
    if (hinge || slider || cone) {
        layout.Row(JointDirectionRow);
    }
    if (cone) {
        layout.Row(JointTiltRow);
    }
    if (hinge || slider || cone) {
        layout.Row(12);
        if (component.enableLimit) {
            layout.Row(hinge ? JointOpensRow : (slider ? JointSlidesRow : JointTwistRow));
        }
    }
    if (slider) {
        layout.Row(JointMotorSpeedRow);
    }
    if (rope) {
        layout.Row(JointLengthRow);
    }
    if (distance) {
        layout.Row(12).Row(10).Row(11);
    }
    // The raw values behind those rows, and everything else, sit behind Advanced.
    layout.Advanced().Rows(1, 6); // where each side is attached
    if (hinge || slider || cone) {
        layout.Rows(7, 3); // the axis as numbers
    }
    if (cone) {
        layout.Rows(JointSwing1Row, 2);
    }
    if (hinge || slider || cone || rope) {
        layout.Row(10).Row(11);
    }
    if (type == Type::Distance || slider || type == Type::Rope) {
        layout.Rows(15, 2);
    }
    if (hinge || slider) {
        layout.Rows(13, 2);
    }
    layout.Row(17);
    if (slider || cone || sixDof) {
        layout.Rows(JointSecondaryAxisRow, 3);
    }
    if (sixDof) {
        layout.Rows(7, 3);
        layout.Rows(JointFirstAxisRow, JointAxisRows * JointAxisCount);
    }
    return layout.Done();
}

std::vector<int> InspectorPhysicsModel::ChangedRows(const kb::scene::RigidbodyComponent& component) { return ChangedRowsOf(component); }
std::vector<int> InspectorPhysicsModel::ChangedRows(const kb::scene::ColliderComponent& component) { return ChangedRowsOf(component); }
std::vector<int> InspectorPhysicsModel::ChangedRows(const kb::scene::CharacterControllerComponent& component) { return ChangedRowsOf(component); }
std::vector<int> InspectorPhysicsModel::ChangedRows(const kb::scene::JointComponent& component) {
    // A plain row stands for raw values the other kinds show too; only the rows this kind lists are reported.
    const PhysicsLayout layout = Layout(component);
    std::vector<int> changed = ChangedRowsOf(component);
    std::erase_if(changed, [&layout](int row) {
        return row >= JointPlainFirstRow && std::find(layout.rows.begin(), layout.rows.begin() + layout.count, static_cast<std::uint8_t>(row)) == layout.rows.begin() + layout.count;
    });
    return changed;
}

bool InspectorPhysicsModel::ResetRow(kb::scene::RigidbodyComponent& component, int index) { return ResetRowOf(component, index); }
bool InspectorPhysicsModel::ResetRow(kb::scene::ColliderComponent& component, int index) { return ResetRowOf(component, index); }
bool InspectorPhysicsModel::ResetRow(kb::scene::CharacterControllerComponent& component, int index) { return ResetRowOf(component, index); }
bool InspectorPhysicsModel::ResetRow(kb::scene::JointComponent& component, int index) { return ResetRowOf(component, index); }

std::string_view JointPresetName(JointPreset preset) {
    switch (preset) {
    case JointPreset::Door: return EditorText("inspector.joint.preset.door", "Door");
    case JointPreset::SlidingDoor: return EditorText("inspector.joint.preset.slidingDoor", "Sliding door");
    case JointPreset::Pendulum: return EditorText("inspector.joint.preset.pendulum", "Pendulum");
    case JointPreset::Rope: return EditorText("inspector.joint.preset.rope", "Rope");
    case JointPreset::CharacterLimb: return EditorText("inspector.joint.preset.limb", "Character limb");
    }
    return {};
}

std::optional<JointPreset> MatchJointPreset(const kb::scene::JointComponent& joint) noexcept {
    for (int index = 0; index < kJointPresetCount; ++index) {
        // Applying a preset to a copy changes only the values the preset sets, so the copy equals the joint exactly
        // when the joint already has them.
        kb::scene::JointComponent probe = joint;
        ApplyJointPreset(probe, static_cast<JointPreset>(index));
        if (probe.type == joint.type && probe.axis.x == joint.axis.x && probe.axis.y == joint.axis.y && probe.axis.z == joint.axis.z &&
            probe.enableLimit == joint.enableLimit && probe.minLimit == joint.minLimit && probe.maxLimit == joint.maxLimit &&
            probe.freedom.swing1Limit == joint.freedom.swing1Limit && probe.freedom.swing2Limit == joint.freedom.swing2Limit) {
            return static_cast<JointPreset>(index);
        }
    }
    return std::nullopt;
}

void ApplyJointPreset(kb::scene::JointComponent& joint, JointPreset preset) noexcept {
    using Type = kb::scene::JointType;
    switch (preset) {
    case JointPreset::Door:
        joint.type = Type::Hinge;
        joint.axis = { 0.0F, 1.0F, 0.0F };
        joint.enableLimit = true;
        joint.minLimit = -90.0F;
        joint.maxLimit = 90.0F;
        break;
    case JointPreset::SlidingDoor:
        joint.type = Type::Slider;
        joint.axis = { 1.0F, 0.0F, 0.0F };
        joint.enableLimit = true;
        joint.minLimit = 0.0F;
        joint.maxLimit = 1.5F;
        break;
    case JointPreset::Pendulum:
        joint.type = Type::Hinge;
        joint.axis = { 0.0F, 0.0F, 1.0F };
        joint.enableLimit = false;
        joint.minLimit = 0.0F;
        joint.maxLimit = 0.0F;
        break;
    case JointPreset::Rope:
        joint.type = Type::Rope;
        joint.minLimit = 0.0F;
        joint.maxLimit = 3.0F;
        break;
    case JointPreset::CharacterLimb:
        joint.type = Type::Cone;
        joint.axis = { 0.0F, 1.0F, 0.0F };
        joint.freedom.swing1Limit = 45.0F;
        joint.freedom.swing2Limit = 45.0F;
        joint.enableLimit = true;
        joint.minLimit = -45.0F;
        joint.maxLimit = 45.0F;
        break;
    }
}

} // namespace kb::editor
