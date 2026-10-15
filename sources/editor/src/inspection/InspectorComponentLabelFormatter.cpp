#include "inspection/InspectorComponentLabelFormatter.hpp"

#include "localization/EditorLocalization.hpp"

namespace kb::editor {

std::string_view InspectorComponentLabelFormatter::LightKindName(kb::scene::LightKind kind) {
    switch (kind) {
    case kb::scene::LightKind::Directional:
        return EditorText("inspector.light.kind.directional", "Directional");
    case kb::scene::LightKind::Point:
        return EditorText("inspector.light.kind.point", "Point");
    case kb::scene::LightKind::Spot:
        return EditorText("inspector.light.kind.spot", "Spot");
    case kb::scene::LightKind::AreaRect:
    case kb::scene::LightKind::AreaDisk:
    case kb::scene::LightKind::Tube:
        return EditorText("inspector.light.kind.area", "Area");
    }
    return EditorText("inspector.unknown", "Unknown");
}

std::string_view InspectorComponentLabelFormatter::LightShapeName(kb::scene::LightKind kind) {
    switch (kind) {
    case kb::scene::LightKind::AreaDisk:
        return EditorText("inspector.light.shape.disc", "Disc");
    case kb::scene::LightKind::Tube:
        return EditorText("inspector.light.shape.tube", "Tube");
    default:
        return EditorText("inspector.light.shape.rectangle", "Rectangle");
    }
}

std::string_view InspectorComponentLabelFormatter::LightMobilityName(kb::scene::LightMobility mobility) {
    return mobility == kb::scene::LightMobility::Static
        ? EditorText("inspector.light.mobility.static", "Static")
        : EditorText("inspector.light.mobility.movable", "Movable");
}

std::string_view InspectorComponentLabelFormatter::ProjectionName(kb::scene::CameraProjection projection) {
    switch (projection) {
    case kb::scene::CameraProjection::Perspective:
        return EditorText("inspector.camera.projection.perspective", "Perspective");
    case kb::scene::CameraProjection::Orthographic:
        return EditorText("inspector.camera.projection.orthographic", "Orthographic");
    }
    return EditorText("inspector.unknown", "Unknown");
}

std::string_view InspectorComponentLabelFormatter::CameraClearModeName(kb::scene::CameraClearMode clearMode) {
    switch (clearMode) {
    case kb::scene::CameraClearMode::SolidColor:
        return EditorText("inspector.camera.clearMode.solidColor", "Solid Color");
    case kb::scene::CameraClearMode::DepthOnly:
        return EditorText("inspector.camera.clearMode.depthOnly", "Depth Only");
    case kb::scene::CameraClearMode::DontClear:
        return EditorText("inspector.camera.clearMode.donTClear", "Don't Clear");
    }
    return EditorText("inspector.unknown", "Unknown");
}

std::string_view InspectorComponentLabelFormatter::RigidbodyBodyTypeName(kb::scene::RigidbodyBodyType bodyType) {
    switch (bodyType) {
    case kb::scene::RigidbodyBodyType::Static:
        return EditorText("inspector.rigidbody.bodyType.static", "Static");
    case kb::scene::RigidbodyBodyType::Dynamic:
        return EditorText("inspector.rigidbody.bodyType.dynamic", "Dynamic");
    case kb::scene::RigidbodyBodyType::Kinematic:
        return EditorText("inspector.rigidbody.bodyType.kinematic", "Kinematic");
    }
    return EditorText("inspector.unknown", "Unknown");
}

std::string_view InspectorComponentLabelFormatter::ColliderShapeName(kb::scene::ColliderShape shape) {
    switch (shape) {
    case kb::scene::ColliderShape::Box:
        return EditorText("inspector.collider.shape.box", "Box");
    case kb::scene::ColliderShape::Sphere:
        return EditorText("inspector.collider.shape.sphere", "Sphere");
    case kb::scene::ColliderShape::Capsule:
        return EditorText("inspector.collider.shape.capsule", "Capsule");
    case kb::scene::ColliderShape::Mesh:
        return EditorText("inspector.collider.shape.mesh", "Mesh");
    }
    return EditorText("inspector.unknown", "Unknown");
}

std::string_view InspectorComponentLabelFormatter::AudioAttenuationModelName(kb::audio::AudioAttenuationModel model) {
    switch (model) {
    case kb::audio::AudioAttenuationModel::None:
        return EditorText("inspector.audioSource.attenuation.none", "None");
    case kb::audio::AudioAttenuationModel::Inverse:
        return EditorText("inspector.audioSource.attenuation.inverse", "Inverse");
    case kb::audio::AudioAttenuationModel::Linear:
        return EditorText("inspector.audioSource.attenuation.linear", "Linear");
    case kb::audio::AudioAttenuationModel::Exponential:
        return EditorText("inspector.audioSource.attenuation.exponential", "Exponential");
    }
    return EditorText("inspector.unknown", "Unknown");
}

} // namespace kb::editor
