#include "inspection/InspectorFieldGuide.hpp"
#include "inspection/InspectorGenericComponentModel.hpp"
#include "inspection/ui/InspectorUIComponentModel.hpp"
#include "inspection/InspectorPhysicsModel.hpp"
#include "localization/EditorLocalization.hpp"

namespace kb::editor {
namespace {

using P = InspectorPropertyId;

constexpr std::string_view kDegrees = "\xC2\xB0";

[[nodiscard]] std::string_view MobilityTip() {
    return EditorText("inspector.tip.mobility", "Static objects never move while the game runs, which lets them cost less.");
}

// The help of the Text, Image, Border, Selectable and Button rows, by the name of the property.
struct UiTip {
    kb::scene::UIComponentType component;
    std::string_view name;
    std::string_view key;
    std::string_view text;
};

constexpr UiTip kUiTips[] = {
    { kb::scene::UIComponentType::Text, "content", "inspector.tip.ui.text.content",
      "The words that are shown." },
    { kb::scene::UIComponentType::Text, "localizationKey", "inspector.tip.ui.text.localizationKey",
      "Looks the words up in your translations instead of using the text above." },
    { kb::scene::UIComponentType::Text, "localizationNumber", "inspector.tip.ui.text.localizationNumber",
      "A number that goes into the translated text, for example a score." },
    { kb::scene::UIComponentType::Text, "fontVariations", "inspector.tip.ui.text.fontVariations",
      "Extra font settings such as weight, written as name=value." },
    { kb::scene::UIComponentType::Text, "fontAssetId", "inspector.tip.ui.text.fontAssetId",
      "The font used for the text." },
    { kb::scene::UIComponentType::Text, "fallbackFontAssetId", "inspector.tip.ui.text.fallbackFontAssetId",
      "Used for letters the main font does not have." },
    { kb::scene::UIComponentType::Text, "fontRenderMode", "inspector.tip.ui.text.fontRenderMode",
      "How the letters are drawn on screen." },
    { kb::scene::UIComponentType::Text, "fontSize", "inspector.tip.ui.text.fontSize",
      "How tall the letters are, in pixels." },
    { kb::scene::UIComponentType::Text, "color", "inspector.tip.ui.text.color",
      "The colour of the text." },
    { kb::scene::UIComponentType::Text, "horizontalAlignment", "inspector.tip.ui.text.horizontalAlignment",
      "Where the text sits from left to right." },
    { kb::scene::UIComponentType::Text, "verticalAlignment", "inspector.tip.ui.text.verticalAlignment",
      "Where the text sits from top to bottom." },
    { kb::scene::UIComponentType::Text, "wrapMode", "inspector.tip.ui.text.wrapMode",
      "How long text is split into lines." },
    { kb::scene::UIComponentType::Text, "lineSpacing", "inspector.tip.ui.text.lineSpacing",
      "The gap between lines. 1 is normal." },
    { kb::scene::UIComponentType::Text, "richText", "inspector.tip.ui.text.richText",
      "Lets you use tags such as bold inside the text." },
    { kb::scene::UIComponentType::Text, "overflow", "inspector.tip.ui.text.overflow",
      "What happens when the text does not fit its box." },
    { kb::scene::UIComponentType::Text, "maxLines", "inspector.tip.ui.text.maxLines",
      "The most lines shown. 0 shows all of them." },
    { kb::scene::UIComponentType::Text, "autoSize", "inspector.tip.ui.text.autoSize",
      "Shrinks the text so it fits its box." },
    { kb::scene::UIComponentType::Text, "minFontSize", "inspector.tip.ui.text.minFontSize",
      "The smallest size automatic sizing may shrink the text to, in pixels." },
    { kb::scene::UIComponentType::Text, "characterSpacing", "inspector.tip.ui.text.characterSpacing",
      "Extra space between letters, in pixels." },
    { kb::scene::UIComponentType::Image, "imageAssetId", "inspector.tip.ui.image.imageAssetId",
      "The picture that is shown." },
    { kb::scene::UIComponentType::Image, "uvRect", "inspector.tip.ui.image.uvRect",
      "Which part of the picture is shown: left, top, width and height from 0 to 1." },
    { kb::scene::UIComponentType::Image, "scaleMode", "inspector.tip.ui.image.scaleMode",
      "How the picture fills its box." },
    { kb::scene::UIComponentType::Image, "preserveAspect", "inspector.tip.ui.image.preserveAspect",
      "Keeps the picture's proportions instead of stretching it." },
    { kb::scene::UIComponentType::Image, "nineSlice", "inspector.tip.ui.image.nineSlice",
      "Edges that stay unstretched, for frames and buttons." },
    { kb::scene::UIComponentType::Image, "color", "inspector.tip.ui.image.color",
      "Tints the picture. White leaves it as it is." },
    { kb::scene::UIComponentType::Image, "fillMethod", "inspector.tip.ui.image.fillMethod",
      "Fills the picture like a bar or a clock hand." },
    { kb::scene::UIComponentType::Image, "fillOrigin", "inspector.tip.ui.image.fillOrigin",
      "Where the fill starts." },
    { kb::scene::UIComponentType::Image, "fillAmount", "inspector.tip.ui.image.fillAmount",
      "How much is filled, from 0 to 1." },
    { kb::scene::UIComponentType::Image, "fillClockwise", "inspector.tip.ui.image.fillClockwise",
      "Fills clockwise instead of counter-clockwise." },
    { kb::scene::UIComponentType::Image, "alphaHitThreshold", "inspector.tip.ui.image.alphaHitThreshold",
      "Transparent parts below this value do not react to clicks." },
    { kb::scene::UIComponentType::Image, "materialAssetId", "inspector.tip.ui.image.materialAssetId",
      "A custom look for the picture." },
    { kb::scene::UIComponentType::Border, "backgroundColor", "inspector.tip.ui.border.backgroundColor",
      "The colour inside the box." },
    { kb::scene::UIComponentType::Border, "borderColor", "inspector.tip.ui.border.borderColor",
      "The colour of the border line." },
    { kb::scene::UIComponentType::Border, "borderWidth", "inspector.tip.ui.border.borderWidth",
      "How thick the border is on each side, in pixels." },
    { kb::scene::UIComponentType::Border, "cornerRadius", "inspector.tip.ui.border.cornerRadius",
      "How round the corners are, in pixels." },
    { kb::scene::UIComponentType::Border, "opacity", "inspector.tip.ui.border.opacity",
      "How see-through the box is. 1 is solid." },
    { kb::scene::UIComponentType::Selectable, "interactable", "inspector.tip.ui.selectable.interactable",
      "When off, the widget is greyed out and cannot be used." },
    { kb::scene::UIComponentType::Selectable, "onClick", "inspector.tip.ui.selectable.onClick",
      "What happens when the widget is clicked: pick a script." },
    { kb::scene::UIComponentType::Selectable, "transition", "inspector.tip.ui.selectable.transition",
      "How the widget shows its state, for example by changing colour." },
    { kb::scene::UIComponentType::Selectable, "normalColor", "inspector.tip.ui.selectable.normalColor",
      "The colour when nothing is happening." },
    { kb::scene::UIComponentType::Selectable, "highlightedColor", "inspector.tip.ui.selectable.highlightedColor",
      "The colour when the pointer is over the widget." },
    { kb::scene::UIComponentType::Selectable, "pressedColor", "inspector.tip.ui.selectable.pressedColor",
      "The colour while the widget is pressed." },
    { kb::scene::UIComponentType::Selectable, "disabledColor", "inspector.tip.ui.selectable.disabledColor",
      "The colour when the widget cannot be used." },
    { kb::scene::UIComponentType::Selectable, "raycastTarget", "inspector.tip.ui.selectable.raycastTarget",
      "Lets the widget react to clicks and touches." },
    { kb::scene::UIComponentType::Selectable, "raycastPadding", "inspector.tip.ui.selectable.raycastPadding",
      "Makes the clickable area bigger or smaller than the widget." },
    { kb::scene::UIComponentType::Selectable, "colorFadeSeconds", "inspector.tip.ui.selectable.colorFadeSeconds",
      "How long the colour change takes, in seconds." },
    { kb::scene::UIComponentType::Selectable, "tooltip", "inspector.tip.ui.selectable.tooltip",
      "Text shown when the pointer rests on the widget." },
    { kb::scene::UIComponentType::Selectable, "draggable", "inspector.tip.ui.selectable.draggable",
      "Lets the widget be dragged with the pointer." },
    { kb::scene::UIComponentType::Button, "submitOnRelease", "inspector.tip.ui.button.submitOnRelease",
      "Clicks when the pointer is let go instead of when it is pressed." },
};

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
    case P::MeshRendererAffectsNavigation:
        return Plain(EditorText("inspector.tip.affectsNavigation", "Characters walk on this object or around it. Turn off for things they should ignore, such as grass or small props."));
    case P::NavAgentHorde:
        return Plain(EditorText("inspector.tip.navAgentHorde", "Walks with everyone heading to the same place, sharing one route. Much faster for large groups; they push apart instead of dodging."));
    // Audio Source
    case P::AudioSourceClip:
        return Plain(EditorText("inspector.tip.audioSource.clip", "The sound file that plays."));
    case P::AudioSourceVolume:
        return Slider(EditorText("inspector.tip.audioSource.volume", "How loud the sound is. 0 is silent, 1 is full volume."), {}, 0.0F, 1.0F);
    case P::AudioSourceLoop:
        return Plain(EditorText("inspector.tip.audioSource.loop", "Plays again from the start when it ends."));
    case P::AudioSourceAutoplay:
        return Plain(EditorText("inspector.tip.audioSource.autoplay", "Starts playing as soon as the game starts."));
    case P::AudioSourceSpatial:
        return Plain(EditorText("inspector.tip.audioSource.spatial", "The sound comes from this object: it gets quieter with distance and moves left or right."));
    case P::AudioSourceEnabled:
        return Plain(EditorText("inspector.tip.audioSource.enabled", "Turns this sound on or off."));
    case P::AudioSourceMute:
        return Plain(EditorText("inspector.tip.audioSource.mute", "Silences the sound without stopping it."));
    case P::AudioSourcePitch:
        return Plain(EditorText("inspector.tip.audioSource.pitch", "How high or low the sound is. 1 is normal, 2 is an octave higher."));
    case P::AudioSourcePan:
        return Slider(EditorText("inspector.tip.audioSource.pan", "Moves the sound to the left (-1) or to the right (1)."), {}, -1.0F, 1.0F);
    case P::AudioSourceSpatialBlend:
        return Slider(EditorText("inspector.tip.audioSource.spatialBlend", "0 plays the sound everywhere equally, 1 places it fully in the world."), {}, 0.0F, 1.0F);
    case P::AudioSourceAttenuation:
        return Plain(EditorText("inspector.tip.audioSource.attenuation", "How the sound fades as you move away from it."));
    case P::AudioSourceMinDistance:
        return Plain(EditorText("inspector.tip.audioSource.minDistance", "Up to this distance the sound is at full volume."), "m");
    case P::AudioSourceMaxDistance:
        return Plain(EditorText("inspector.tip.audioSource.maxDistance", "Past this distance the sound no longer gets quieter."), "m");
    case P::AudioSourceRolloff:
        return Plain(EditorText("inspector.tip.audioSource.rolloff", "How quickly the sound fades with distance. Higher fades faster."));
    case P::AudioSourceDopplerFactor:
        return Plain(EditorText("inspector.tip.audioSource.doppler", "How much the pitch changes as the sound passes by, like a siren. 0 turns it off."));
    case P::AudioSourceOutputBus:
        return Plain(EditorText("inspector.tip.audioSource.outputBus", "The mixer group this sound plays through."));
    case P::AudioSourceClipClear:
        return Plain(EditorText("inspector.tip.audioSource.clearClip", "Removes the sound file from this source."));
    // Particle Effect
    case P::ParticleEffectAsset:
        return Plain(EditorText("inspector.tip.particleEffect.effect", "The particle effect that plays."));
    case P::ParticleEffectEnabled:
        return Plain(EditorText("inspector.tip.particleEffect.enabled", "Turns the effect on or off."));
    case P::ParticleEffectAutoPlay:
        return Plain(EditorText("inspector.tip.particleEffect.autoPlay", "Starts the effect as soon as the game starts."));
    default:
        return {};
    }
}

std::string InspectorFieldGuide::Tooltip(InspectorSectionId section, InspectorPropertyId property, int index) {
    if (property == P::GenericComponentField) {
        return std::string{ InspectorGenericComponentModel::Tooltip(index) };
    }
    if (property == P::JointPreset) {
        return std::string{ EditorText("inspector.tip.joint.preset", "A ready-made setup, such as a door or a rope. Pick one, then change the values you like.") };
    }
    if (property == P::LightPreset) {
        return std::string{ EditorText("inspector.tip.light.preset", "A ready-made light, such as the sun or a room bulb. Pick one, then change the values you like.") };
    }
    if (const std::optional<kb::scene::UIComponentType> component = InspectorUIComponentModel::Component(section)) {
        const std::string_view name = InspectorUIComponentModel::PropertyNameAt(*component, index);
        for (const UiTip& tip : kUiTips) {
            if (tip.component == *component && tip.name == name) {
                return std::string{ EditorText(tip.key, tip.text) };
            }
        }
        return {};
    }
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
    case InspectorSectionId::MeshRenderer:
    case InspectorSectionId::AudioSource:
    case InspectorSectionId::ParticleEffect: return std::string{ Help(property).tooltip };
    case InspectorSectionId::Transform:
        switch (property) {
        case P::PositionX: return std::string{ EditorText("inspector.tip.transform.position", "Where the object is, in metres, measured from its parent or from the centre of the world.") };
        case P::RotationX: return std::string{ EditorText("inspector.tip.transform.rotation", "How the object is turned, in degrees around each axis.") };
        case P::ScaleX: return std::string{ EditorText("inspector.tip.transform.scale", "How big the object is. 1 is its original size.") };
        default: return {};
        }
    default: return {};
    }
}

} // namespace kb::editor
