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
                LibraryComponentInspectorFieldDesc{ "mode", "Visibility Mode", "Explicit visible, hidden, or inherited state in the parent hierarchy." },
                LibraryComponentInspectorFieldDesc{ "mask", "Visibility Mask", "Render visibility bits checked against a camera mask." },
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
                LibraryComponentInspectorFieldDesc{ "viewportId", "Viewport", "Which render viewport this camera targets. 0 means every viewport." },
                LibraryComponentInspectorFieldDesc{ "priority", "Priority", "Breaks ties when more than one primary camera targets the same viewport; higher wins." },
                LibraryComponentInspectorFieldDesc{ "cullingMask", "Culling Mask", "Render layer bitmask this camera draws." },
                LibraryComponentInspectorFieldDesc{ "clearMode", "Clear Mode", "Solid color, depth-only, or no clear before this camera draws." },
                LibraryComponentInspectorFieldDesc{ "clearColor.x", "Clear Color R", "Clear color, red channel (solid-color clear mode)." },
                LibraryComponentInspectorFieldDesc{ "clearColor.y", "Clear Color G", "Clear color, green channel (solid-color clear mode)." },
                LibraryComponentInspectorFieldDesc{ "clearColor.z", "Clear Color B", "Clear color, blue channel (solid-color clear mode)." },
            },
        },
        LibraryComponentInspectorDesc{
            .componentName = "3D Radiance Emitter",
            .displayName = "3D Radiance Emitter",
            .category = "Rendering",
            .fields = {
                LibraryComponentInspectorFieldDesc{ "kind", "Kind", "Directional, point, spot, rectangular, disk, or tube emitter type." },
                LibraryComponentInspectorFieldDesc{ "color.x", "Color R", "Emitter color, red channel." },
                LibraryComponentInspectorFieldDesc{ "color.y", "Color G", "Emitter color, green channel." },
                LibraryComponentInspectorFieldDesc{ "color.z", "Color B", "Emitter color, blue channel." },
                LibraryComponentInspectorFieldDesc{ "intensity", "Intensity", "Overall emitted brightness." },
                LibraryComponentInspectorFieldDesc{ "range", "Range", "Maximum distance the emitter affects." },
                LibraryComponentInspectorFieldDesc{ "innerConeDegrees", "Inner Cone Angle", "Spot light inner cone angle, in degrees, where falloff begins." },
                LibraryComponentInspectorFieldDesc{ "outerConeDegrees", "Outer Cone Angle", "Spot light outer cone angle, in degrees, where falloff ends." },
                LibraryComponentInspectorFieldDesc{ "areaWidth", "Area Width", "Area light width (area rect/disk/tube light types)." },
                LibraryComponentInspectorFieldDesc{ "areaHeight", "Area Height", "Area light height (area rect/disk/tube light types)." },
                LibraryComponentInspectorFieldDesc{ "contactShadowLength", "Contact Shadow Length", "Screen-space contact shadow trace distance." },
                LibraryComponentInspectorFieldDesc{ "volumetricScattering", "Volumetric Scattering", "Strength of volumetric light scattering." },
                LibraryComponentInspectorFieldDesc{ "castsShadow", "Casts Shadow", "Whether this light casts shadows." },
                LibraryComponentInspectorFieldDesc{ "useColorTemperature", "Use Color Temperature", "Tint Color by a blackbody radiator color derived from Color Temperature (Kelvin)." },
                LibraryComponentInspectorFieldDesc{ "colorTemperatureKelvin", "Color Temperature (Kelvin)", "Blackbody radiator temperature used when Use Color Temperature is enabled." },
                LibraryComponentInspectorFieldDesc{ "layerMask", "Layer Mask", "Bitmask of which camera cullingMask layers this light contributes to." },
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
                LibraryComponentInspectorFieldDesc{ "layer", "Layer", "Render layer bitmask this mesh belongs to, checked against a camera's culling mask." },
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
        LibraryComponentInspectorDesc{
            .componentName = "NavAgent",
            .displayName = "Nav Agent",
            .category = "Navigation",
            .fields = {
                LibraryComponentInspectorFieldDesc{ "radius", "Radius", "Agent radius used for navigation clearance." },
                LibraryComponentInspectorFieldDesc{ "height", "Height", "Agent height used for navigation clearance." },
                LibraryComponentInspectorFieldDesc{ "maxSpeed", "Max Speed", "Maximum horizontal movement speed." },
                LibraryComponentInspectorFieldDesc{ "acceleration", "Acceleration", "Maximum horizontal velocity change per second." },
                LibraryComponentInspectorFieldDesc{ "angularSpeedDegrees", "Angular Speed", "Maximum turning speed in degrees per second." },
                LibraryComponentInspectorFieldDesc{ "stoppingDistance", "Stopping Distance", "Distance from destination at which steering stops." },
                LibraryComponentInspectorFieldDesc{ "areaMask", "Area Mask", "Navigation areas this agent may traverse." },
                LibraryComponentInspectorFieldDesc{ "destination.x", "Destination X", "World destination X coordinate." },
                LibraryComponentInspectorFieldDesc{ "destination.y", "Destination Y", "World destination Y coordinate." },
                LibraryComponentInspectorFieldDesc{ "destination.z", "Destination Z", "World destination Z coordinate." },
                LibraryComponentInspectorFieldDesc{ "enabled", "Enabled", "Whether navigation steering is active." },
            },
        },
        LibraryComponentInspectorDesc{
            .componentName = "NavObstacle",
            .displayName = "Nav Obstacle",
            .category = "Navigation",
            .fields = {
                LibraryComponentInspectorFieldDesc{ "center.x", "Center X", "Local obstacle center X coordinate." },
                LibraryComponentInspectorFieldDesc{ "center.y", "Center Y", "Local obstacle center Y coordinate." },
                LibraryComponentInspectorFieldDesc{ "center.z", "Center Z", "Local obstacle center Z coordinate." },
                LibraryComponentInspectorFieldDesc{ "size.x", "Size X", "Obstacle box extent along X." },
                LibraryComponentInspectorFieldDesc{ "size.y", "Size Y", "Obstacle box extent along Y." },
                LibraryComponentInspectorFieldDesc{ "size.z", "Size Z", "Obstacle box extent along Z." },
                LibraryComponentInspectorFieldDesc{ "radius", "Radius", "Obstacle cylinder radius." },
                LibraryComponentInspectorFieldDesc{ "height", "Height", "Obstacle cylinder height." },
                LibraryComponentInspectorFieldDesc{ "enabled", "Enabled", "Whether this obstacle participates in navigation." },
            },
        },
        LibraryComponentInspectorDesc{
            .componentName = "Tags",
            .displayName = "Object Classification",
            .category = "Scene",
            .fields = {
                LibraryComponentInspectorFieldDesc{ "text", "Tags", "Comma- or semicolon-separated semantic labels used to classify this object." },
            },
        },
        LibraryComponentInspectorDesc{
            .componentName = "RegionShape",
            .displayName = "Region Shape",
            .category = "Scene",
            .fields = {
                LibraryComponentInspectorFieldDesc{ "kind", "Shape", "Circle 2D, Rectangle 2D, Sphere, Box or Capsule." },
                LibraryComponentInspectorFieldDesc{ "center.x", "Center X", "Local region center X coordinate." },
                LibraryComponentInspectorFieldDesc{ "center.y", "Center Y", "Local region center Y coordinate." },
                LibraryComponentInspectorFieldDesc{ "center.z", "Center Z", "Local region center Z coordinate." },
                LibraryComponentInspectorFieldDesc{ "size.x", "Size X", "Full local extent along X." },
                LibraryComponentInspectorFieldDesc{ "size.y", "Size Y", "Full local extent along Y." },
                LibraryComponentInspectorFieldDesc{ "size.z", "Size Z", "Full local extent along Z." },
                LibraryComponentInspectorFieldDesc{ "radius", "Radius", "Radius of circular, spherical and capsule regions." },
                LibraryComponentInspectorFieldDesc{ "height", "Height", "Capsule height including hemispherical caps." },
                LibraryComponentInspectorFieldDesc{ "enabled", "Enabled", "Whether queries include this region." },
            },
        },
        LibraryComponentInspectorDesc{
            .componentName = "GuideCurve",
            .displayName = "Guide Curve",
            .category = "Scene",
            .fields = {
                LibraryComponentInspectorFieldDesc{ "controlPointCount", "Control Points", "Number of authored local control points." },
                LibraryComponentInspectorFieldDesc{ "interpolation", "Interpolation", "Linear segments or a smooth Catmull-Rom curve." },
                LibraryComponentInspectorFieldDesc{ "closed", "Closed", "Whether the last point connects back to the first." },
                LibraryComponentInspectorFieldDesc{ "enabled", "Enabled", "Whether runtime systems can evaluate this curve." },
            },
        },
        LibraryComponentInspectorDesc{
            .componentName = "ContentInstance",
            .displayName = "Content Instance",
            .category = "Scene",
            .fields = {
                LibraryComponentInspectorFieldDesc{ "assetId", "Asset ID", "Stable asset reference for the content source." },
                LibraryComponentInspectorFieldDesc{ "kind", "Source Type", "Prefab, subscene or world fragment source." },
                LibraryComponentInspectorFieldDesc{ "lifetime", "Lifetime", "Release with owner or preserve when owner is destroyed." },
                LibraryComponentInspectorFieldDesc{ "active", "Active", "Whether runtime creates the configured content." },
            },
        },
        LibraryComponentInspectorDesc{
            .componentName = "StreamFocus",
            .displayName = "Stream Focus",
            .category = "Scene",
            .fields = {
                LibraryComponentInspectorFieldDesc{ "innerRadius", "Inner Radius", "Distance at which eligible content begins loading." },
                LibraryComponentInspectorFieldDesc{ "outerRadius", "Outer Radius", "Distance at which already-loaded content is released." },
                LibraryComponentInspectorFieldDesc{ "priority", "Priority", "Tie-break priority for eligible content." },
                LibraryComponentInspectorFieldDesc{ "loadMask", "Load Mask", "Content source kinds this focus may activate." },
                LibraryComponentInspectorFieldDesc{ "enabled", "Enabled", "Whether this focus participates in streaming." },
            },
        },
        LibraryComponentInspectorDesc{
            .componentName = "WorldBackdrop",
            .displayName = "World Backdrop",
            .category = "Rendering",
            .fields = {
                LibraryComponentInspectorFieldDesc{ "mode", "Mode", "Solid color, gradient, environment map or procedural sky." },
                LibraryComponentInspectorFieldDesc{ "color.x", "Color Red", "Solid background red channel." },
                LibraryComponentInspectorFieldDesc{ "color.y", "Color Green", "Solid background green channel." },
                LibraryComponentInspectorFieldDesc{ "color.z", "Color Blue", "Solid background blue channel." },
                LibraryComponentInspectorFieldDesc{ "horizonColor.x", "Horizon Red", "Lower gradient or sky red channel." },
                LibraryComponentInspectorFieldDesc{ "horizonColor.y", "Horizon Green", "Lower gradient or sky green channel." },
                LibraryComponentInspectorFieldDesc{ "horizonColor.z", "Horizon Blue", "Lower gradient or sky blue channel." },
                LibraryComponentInspectorFieldDesc{ "zenithColor.x", "Zenith Red", "Upper gradient or sky red channel." },
                LibraryComponentInspectorFieldDesc{ "zenithColor.y", "Zenith Green", "Upper gradient or sky green channel." },
                LibraryComponentInspectorFieldDesc{ "zenithColor.z", "Zenith Blue", "Upper gradient or sky blue channel." },
                LibraryComponentInspectorFieldDesc{ "environmentAssetId", "Environment Asset", "2D equirectangular environment texture reference." },
                LibraryComponentInspectorFieldDesc{ "horizonHeight", "Horizon Height", "Vertical horizon offset for gradient and procedural sky modes." },
                LibraryComponentInspectorFieldDesc{ "gradientExponent", "Gradient Exponent", "Vertical blend curve for gradient and procedural sky modes." },
                LibraryComponentInspectorFieldDesc{ "priority", "Priority", "Deterministic selection when more than one backdrop is enabled." },
                LibraryComponentInspectorFieldDesc{ "enabled", "Enabled", "Whether this backdrop participates in rendering." },
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
