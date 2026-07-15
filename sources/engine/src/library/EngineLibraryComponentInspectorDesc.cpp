#include "engine/library/EngineLibraryComponentInspectorDesc.hpp"

#include <algorithm>

namespace kb::library {

const std::vector<LibraryComponentInspectorDesc>& EngineLibraryComponentInspectorRegistry::Catalog() {
    // Every componentName/fieldName pair here is verified against
    // kb::script::ScriptSceneComponentApi::ComponentNames()/
    // ComponentProperties() by RunComponentInspectorDescCatalogTest — zero
    // drift in either direction (missing entry or stale extra entry both
    // fail that test).
    static const std::vector<LibraryComponentInspectorDesc> kCatalog{
        LibraryComponentInspectorDesc{
            .componentName = "Transform",
            .displayName = "Transform",
            .category = "Transform",
            .fields = {
                LibraryComponentInspectorFieldDesc{ "localPosition.x", "Position X", "Local position along the X axis, relative to the parent." },
                LibraryComponentInspectorFieldDesc{ "localPosition.y", "Position Y", "Local position along the Y axis, relative to the parent." },
                LibraryComponentInspectorFieldDesc{ "localPosition.z", "Position Z", "Local position along the Z axis, relative to the parent." },
                LibraryComponentInspectorFieldDesc{ "localRotation.x", "Rotation X", "Local rotation quaternion, X component, relative to the parent." },
                LibraryComponentInspectorFieldDesc{ "localRotation.y", "Rotation Y", "Local rotation quaternion, Y component, relative to the parent." },
                LibraryComponentInspectorFieldDesc{ "localRotation.z", "Rotation Z", "Local rotation quaternion, Z component, relative to the parent." },
                LibraryComponentInspectorFieldDesc{ "localRotation.w", "Rotation W", "Local rotation quaternion, W component, relative to the parent." },
                LibraryComponentInspectorFieldDesc{ "localScale.x", "Scale X", "Local scale along the X axis, relative to the parent." },
                LibraryComponentInspectorFieldDesc{ "localScale.y", "Scale Y", "Local scale along the Y axis, relative to the parent." },
                LibraryComponentInspectorFieldDesc{ "localScale.z", "Scale Z", "Local scale along the Z axis, relative to the parent." },
                LibraryComponentInspectorFieldDesc{ "worldPosition.x", "World Position X", "Read-only world-space position along the X axis." },
                LibraryComponentInspectorFieldDesc{ "worldPosition.y", "World Position Y", "Read-only world-space position along the Y axis." },
                LibraryComponentInspectorFieldDesc{ "worldPosition.z", "World Position Z", "Read-only world-space position along the Z axis." },
            },
        },
        LibraryComponentInspectorDesc{
            .componentName = "Visibility",
            .displayName = "Visibility",
            .category = "Rendering",
            .fields = {
                LibraryComponentInspectorFieldDesc{ "visible", "Visible", "Whether this entity is rendered." },
            },
        },
        LibraryComponentInspectorDesc{
            .componentName = "Camera",
            .displayName = "Camera",
            .category = "Rendering",
            .fields = {
                LibraryComponentInspectorFieldDesc{ "projection", "Projection", "Perspective or orthographic projection mode." },
                LibraryComponentInspectorFieldDesc{ "verticalFovDegrees", "Vertical FOV", "Vertical field of view, in degrees (perspective mode)." },
                LibraryComponentInspectorFieldDesc{ "orthographicHeight", "Orthographic Height", "Half-height of the view volume (orthographic mode)." },
                LibraryComponentInspectorFieldDesc{ "nearClip", "Near Clip", "Distance to the near clipping plane." },
                LibraryComponentInspectorFieldDesc{ "farClip", "Far Clip", "Distance to the far clipping plane." },
                LibraryComponentInspectorFieldDesc{ "primary", "Primary", "Whether this is the primary camera used for the main render pass." },
            },
        },
        LibraryComponentInspectorDesc{
            .componentName = "Light",
            .displayName = "Light",
            .category = "Rendering",
            .fields = {
                LibraryComponentInspectorFieldDesc{ "kind", "Kind", "Directional, point, or spot light type." },
                LibraryComponentInspectorFieldDesc{ "color.x", "Color R", "Light color, red channel." },
                LibraryComponentInspectorFieldDesc{ "color.y", "Color G", "Light color, green channel." },
                LibraryComponentInspectorFieldDesc{ "color.z", "Color B", "Light color, blue channel." },
                LibraryComponentInspectorFieldDesc{ "intensity", "Intensity", "Overall brightness of the light." },
                LibraryComponentInspectorFieldDesc{ "range", "Range", "Maximum distance the light affects (point/spot)." },
                LibraryComponentInspectorFieldDesc{ "innerConeDegrees", "Inner Cone Angle", "Spot light inner cone angle, in degrees, where falloff begins." },
                LibraryComponentInspectorFieldDesc{ "outerConeDegrees", "Outer Cone Angle", "Spot light outer cone angle, in degrees, where falloff ends." },
                LibraryComponentInspectorFieldDesc{ "contactShadowLength", "Contact Shadow Length", "Screen-space contact shadow trace distance." },
                LibraryComponentInspectorFieldDesc{ "volumetricScattering", "Volumetric Scattering", "Strength of volumetric light scattering." },
                LibraryComponentInspectorFieldDesc{ "castsShadow", "Casts Shadow", "Whether this light casts shadows." },
            },
        },
        LibraryComponentInspectorDesc{
            .componentName = "MeshRenderer",
            .displayName = "Mesh Renderer",
            .category = "Rendering",
            .fields = {
                LibraryComponentInspectorFieldDesc{ "materialSlotOverrideCount", "Material Overrides", "Number of material slots overridden on this instance." },
                LibraryComponentInspectorFieldDesc{ "castsShadow", "Casts Shadow", "Whether this mesh casts shadows." },
                LibraryComponentInspectorFieldDesc{ "receivesShadow", "Receives Shadow", "Whether this mesh receives shadows." },
            },
        },
        LibraryComponentInspectorDesc{
            .componentName = "Behaviour",
            .displayName = "Behaviour",
            .category = "Scripting",
            .fields = {
                LibraryComponentInspectorFieldDesc{ "enabled", "Enabled", "Whether this behaviour's lifecycle callbacks run." },
                LibraryComponentInspectorFieldDesc{ "tickGroup", "Tick Group", "Which scheduler phase this behaviour ticks in." },
                LibraryComponentInspectorFieldDesc{ "executionOrder", "Execution Order", "Relative ordering against other behaviours in the same tick group." },
            },
        },
        LibraryComponentInspectorDesc{
            .componentName = "Rigidbody",
            .displayName = "Rigidbody",
            .category = "Physics",
            .fields = {
                LibraryComponentInspectorFieldDesc{ "bodyType", "Body Type", "Static, Dynamic, or Kinematic simulation behavior." },
                LibraryComponentInspectorFieldDesc{ "mass", "Mass", "Mass in kilograms, used for Dynamic bodies." },
                LibraryComponentInspectorFieldDesc{ "linearVelocity.x", "Linear Velocity X", "Current linear velocity along the X axis." },
                LibraryComponentInspectorFieldDesc{ "linearVelocity.y", "Linear Velocity Y", "Current linear velocity along the Y axis." },
                LibraryComponentInspectorFieldDesc{ "linearVelocity.z", "Linear Velocity Z", "Current linear velocity along the Z axis." },
                LibraryComponentInspectorFieldDesc{ "angularVelocity.x", "Angular Velocity X", "Current angular velocity around the X axis." },
                LibraryComponentInspectorFieldDesc{ "angularVelocity.y", "Angular Velocity Y", "Current angular velocity around the Y axis." },
                LibraryComponentInspectorFieldDesc{ "angularVelocity.z", "Angular Velocity Z", "Current angular velocity around the Z axis." },
                LibraryComponentInspectorFieldDesc{ "gravityScale", "Gravity Scale", "Multiplier applied to world gravity for this body." },
                LibraryComponentInspectorFieldDesc{ "useGravity", "Use Gravity", "Whether world gravity affects this body." },
                LibraryComponentInspectorFieldDesc{ "lockRotation", "Lock Rotation", "Whether physics simulation is prevented from rotating this body." },
                LibraryComponentInspectorFieldDesc{ "useContinuousCollision", "Continuous Collision", "Sweeps this body's shape along its path each step so it cannot tunnel through thin colliders at high speed." },
            },
        },
        LibraryComponentInspectorDesc{
            .componentName = "Collider",
            .displayName = "Collider",
            .category = "Physics",
            .fields = {
                LibraryComponentInspectorFieldDesc{ "shape", "Shape", "Box, Sphere, or Capsule collision shape." },
                LibraryComponentInspectorFieldDesc{ "center.x", "Center X", "Local-space offset of the shape's center, X axis." },
                LibraryComponentInspectorFieldDesc{ "center.y", "Center Y", "Local-space offset of the shape's center, Y axis." },
                LibraryComponentInspectorFieldDesc{ "center.z", "Center Z", "Local-space offset of the shape's center, Z axis." },
                LibraryComponentInspectorFieldDesc{ "boxSize.x", "Box Size X", "Box shape full extent along the X axis." },
                LibraryComponentInspectorFieldDesc{ "boxSize.y", "Box Size Y", "Box shape full extent along the Y axis." },
                LibraryComponentInspectorFieldDesc{ "boxSize.z", "Box Size Z", "Box shape full extent along the Z axis." },
                LibraryComponentInspectorFieldDesc{ "radius", "Radius", "Sphere or Capsule radius." },
                LibraryComponentInspectorFieldDesc{ "height", "Height", "Capsule total height, including both end caps." },
                LibraryComponentInspectorFieldDesc{ "trigger", "Trigger", "Whether this collider only reports overlaps instead of physically colliding." },
                LibraryComponentInspectorFieldDesc{ "friction", "Friction", "Surface friction coefficient." },
                LibraryComponentInspectorFieldDesc{ "restitution", "Restitution", "Bounciness - 0 absorbs all energy, 1 is a perfectly elastic bounce." },
                LibraryComponentInspectorFieldDesc{ "layer", "Layer", "Raw collision layer bitmask this collider belongs to, matched against a query's layer mask." },
            },
        },
        LibraryComponentInspectorDesc{
            .componentName = "CharacterController",
            .displayName = "Character Controller",
            .category = "Physics",
            .fields = {
                LibraryComponentInspectorFieldDesc{ "center.x", "Center X", "Local-space offset of the capsule's center, X axis." },
                LibraryComponentInspectorFieldDesc{ "center.y", "Center Y", "Local-space offset of the capsule's center, Y axis." },
                LibraryComponentInspectorFieldDesc{ "center.z", "Center Z", "Local-space offset of the capsule's center, Z axis." },
                LibraryComponentInspectorFieldDesc{ "radius", "Radius", "Capsule radius." },
                LibraryComponentInspectorFieldDesc{ "height", "Height", "Capsule total height, including both end caps." },
                LibraryComponentInspectorFieldDesc{ "slopeLimitDegrees", "Slope Limit", "Maximum slope angle, in degrees, the character can stand on or walk up." },
                LibraryComponentInspectorFieldDesc{ "stepOffset", "Step Offset", "Maximum height of a step or ledge the character can walk over." },
                LibraryComponentInspectorFieldDesc{ "gravityScale", "Gravity Scale", "Multiplier applied to world gravity for this character." },
                LibraryComponentInspectorFieldDesc{ "useGravity", "Use Gravity", "Whether world gravity affects this character." },
            },
        },
        LibraryComponentInspectorDesc{
            .componentName = "Joint",
            .displayName = "Joint",
            .category = "Physics",
            .fields = {
                LibraryComponentInspectorFieldDesc{ "type", "Type", "Fixed, Hinge, Distance, or Point constraint." },
                LibraryComponentInspectorFieldDesc{ "anchor.x", "Anchor X", "Local-space anchor point on this entity, X axis." },
                LibraryComponentInspectorFieldDesc{ "anchor.y", "Anchor Y", "Local-space anchor point on this entity, Y axis." },
                LibraryComponentInspectorFieldDesc{ "anchor.z", "Anchor Z", "Local-space anchor point on this entity, Z axis." },
                LibraryComponentInspectorFieldDesc{ "connectedAnchor.x", "Connected Anchor X", "Anchor point on the connected entity (or world space), X axis." },
                LibraryComponentInspectorFieldDesc{ "connectedAnchor.y", "Connected Anchor Y", "Anchor point on the connected entity (or world space), Y axis." },
                LibraryComponentInspectorFieldDesc{ "connectedAnchor.z", "Connected Anchor Z", "Anchor point on the connected entity (or world space), Z axis." },
                LibraryComponentInspectorFieldDesc{ "axis.x", "Axis X", "Hinge/constraint axis, X component." },
                LibraryComponentInspectorFieldDesc{ "axis.y", "Axis Y", "Hinge/constraint axis, Y component." },
                LibraryComponentInspectorFieldDesc{ "axis.z", "Axis Z", "Hinge/constraint axis, Z component." },
                LibraryComponentInspectorFieldDesc{ "minLimit", "Min Limit", "Lower constraint limit, interpreted per joint type." },
                LibraryComponentInspectorFieldDesc{ "maxLimit", "Max Limit", "Upper constraint limit, interpreted per joint type." },
                LibraryComponentInspectorFieldDesc{ "enableLimit", "Enable Limit", "Whether minLimit/maxLimit are enforced." },
            },
        },
    };
    return kCatalog;
}

const LibraryComponentInspectorDesc* EngineLibraryComponentInspectorRegistry::Find(std::string_view componentName) noexcept {
    const std::vector<LibraryComponentInspectorDesc>& catalog = Catalog();
    const auto iterator = std::ranges::find_if(catalog, [componentName](const LibraryComponentInspectorDesc& desc) { return desc.componentName == componentName; });
    return iterator == catalog.end() ? nullptr : &*iterator;
}

const LibraryComponentInspectorFieldDesc* EngineLibraryComponentInspectorRegistry::FindField(std::string_view componentName, std::string_view fieldName) noexcept {
    const LibraryComponentInspectorDesc* component = Find(componentName);
    if (component == nullptr) {
        return nullptr;
    }
    const auto iterator = std::ranges::find_if(component->fields, [fieldName](const LibraryComponentInspectorFieldDesc& field) { return field.fieldName == fieldName; });
    return iterator == component->fields.end() ? nullptr : &*iterator;
}

} // namespace kb::library
