#include "inspection/InspectorFieldGuide.hpp"
#include "inspection/InspectorPhysicsModel.hpp"
#include "localization/EditorLocalization.hpp"

namespace kb::editor {
namespace {

using P = InspectorPropertyId;

constexpr std::string_view kDegrees = "\xC2\xB0";

[[nodiscard]] std::string_view MobilityTip() {
    return EditorText("inspector.tip.mobility", "Static objects never move while the game runs, which lets them cost less.");
}

[[nodiscard]] InspectorFieldHelp Slider(std::string_view tooltip, std::string_view unit, float minimum, float maximum) noexcept {
    return InspectorFieldHelp{ .tooltip = tooltip, .unit = unit, .bounded = true, .minimum = minimum, .maximum = maximum };
}

[[nodiscard]] InspectorFieldHelp Plain(std::string_view tooltip, std::string_view unit = {}) noexcept {
    return InspectorFieldHelp{ .tooltip = tooltip, .unit = unit };
}

} // namespace

InspectorFieldHelp InspectorFieldGuide::Help(InspectorPropertyId property) {
    switch (property) {
    // Camera
    case P::CameraProjection:
        return Plain(EditorText("inspector.tip.camera.projection", "Perspective looks like the real world. Orthographic has no depth, for 2D games and plans."));
    case P::CameraVerticalFov:
        return Slider(EditorText("inspector.tip.camera.fieldOfView", "How wide the camera sees. A larger angle shows more of the scene."), kDegrees, 10.0F, 120.0F);
    case P::CameraOrthographicHeight:
        return Plain(EditorText("inspector.tip.camera.size", "How much of the scene fits in the view, from top to bottom."), "m");
    case P::CameraNearClip:
        return Plain(EditorText("inspector.tip.camera.nearClip", "Things closer than this are not drawn."), "m");
    case P::CameraFarClip:
        return Plain(EditorText("inspector.tip.camera.farClip", "Things farther away than this are not drawn."), "m");
    case P::CameraPrimary:
        return Plain(EditorText("inspector.tip.camera.primary", "The game is shown through this camera."));
    case P::CameraCullingMask:
        return Plain(EditorText("inspector.tip.camera.cullingMask", "Which layers this camera draws."));
    case P::CameraClearMode:
        return Plain(EditorText("inspector.tip.camera.background", "What fills the screen behind everything: a colour, the previous picture or nothing."));
    // Light
    case P::LightKind:
        return Plain(EditorText("inspector.tip.light.type", "Point lights shine in every direction, spot lights in a cone, directional lights like the sun."));
    case P::LightColor:
        return Plain(EditorText("inspector.tip.light.color", "The colour of the light."));
    case P::LightIntensity:
        return Plain(EditorText("inspector.tip.light.intensity", "How bright the light is."));
    case P::LightRange:
        return Plain(EditorText("inspector.tip.light.range", "How far the light reaches."), "m");
    case P::LightInnerCone:
        return Slider(EditorText("inspector.tip.light.innerAngle", "The angle of the bright centre of the cone."), kDegrees, 0.0F, 90.0F);
    case P::LightOuterCone:
        return Slider(EditorText("inspector.tip.light.outerAngle", "The angle where the light has faded out completely."), kDegrees, 0.0F, 90.0F);
    case P::LightMobility:
        return Plain(MobilityTip());
    case P::LightCastsShadow:
        return Plain(EditorText("inspector.tip.light.castShadows", "Objects block this light and cast shadows."));
    case P::LightShadowStrength:
        return Slider(EditorText("inspector.tip.light.shadowStrength", "How dark the shadows are."), {}, 0.0F, 1.0F);
    case P::LightColorTemperatureKelvin:
        return Plain({}, "K");
    // Mesh Renderer
    case P::MeshRendererMesh:
        return Plain(EditorText("inspector.tip.meshRenderer.mesh", "The 3D model that is drawn."));
    case P::MeshRendererMaterial:
        return Plain(EditorText("inspector.tip.meshRenderer.material", "The look of the model's surface: colour, shine and texture."));
    case P::MeshRendererCastsShadow:
        return Plain(EditorText("inspector.tip.meshRenderer.castsShadow", "The model throws a shadow on the ground and on other objects."));
    case P::MeshRendererReceivesShadow:
        return Plain(EditorText("inspector.tip.meshRenderer.receivesShadow", "Shadows of other objects fall on this model."));
    case P::MeshRendererLodEnabled:
        return Plain(EditorText("inspector.tip.meshRenderer.levelsOfDetail", "Swaps to simpler models when far away, which saves speed."));
    case P::MeshRendererMobility:
        return Plain(MobilityTip());
    default:
        return {};
    }
}

std::string InspectorFieldGuide::Tooltip(InspectorSectionId section, InspectorPropertyId property, int index) {
    const auto rowTip = [index](const std::vector<PhysicsField>& rows) {
        return index >= 0 && index < static_cast<int>(rows.size()) ? rows[static_cast<std::size_t>(index)].tooltip : std::string{};
    };
    switch (section) {
    case InspectorSectionId::Rigidbody: return rowTip(InspectorPhysicsModel::Fields(kb::scene::RigidbodyComponent{}));
    case InspectorSectionId::Collider: return rowTip(InspectorPhysicsModel::Fields(kb::scene::ColliderComponent{}));
    case InspectorSectionId::CharacterController: return rowTip(InspectorPhysicsModel::Fields(kb::scene::CharacterControllerComponent{}));
    case InspectorSectionId::Joint: return rowTip(InspectorPhysicsModel::Fields(kb::scene::JointComponent{}));
    case InspectorSectionId::Camera:
    case InspectorSectionId::Light:
    case InspectorSectionId::MeshRenderer: return std::string{ Help(property).tooltip };
    default: return {};
    }
}

} // namespace kb::editor
