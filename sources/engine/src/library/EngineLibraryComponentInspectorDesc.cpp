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
