#include "inspection/InspectorComponentLabelFormatter.hpp"

namespace kb::editor {

const char* InspectorComponentLabelFormatter::LightKindName(kb::scene::LightKind kind) noexcept {
    switch (kind) {
    case kb::scene::LightKind::Directional:
        return "Directional";
    case kb::scene::LightKind::Point:
        return "Point";
    case kb::scene::LightKind::Spot:
        return "Spot";
    }
    return "Unknown";
}

const char* InspectorComponentLabelFormatter::ProjectionName(kb::scene::CameraProjection projection) noexcept {
    switch (projection) {
    case kb::scene::CameraProjection::Perspective:
        return "Perspective";
    case kb::scene::CameraProjection::Orthographic:
        return "Orthographic";
    }
    return "Unknown";
}

const char* InspectorComponentLabelFormatter::RigidbodyBodyTypeName(kb::scene::RigidbodyBodyType bodyType) noexcept {
    switch (bodyType) {
    case kb::scene::RigidbodyBodyType::Static:
        return "Static";
    case kb::scene::RigidbodyBodyType::Dynamic:
        return "Dynamic";
    case kb::scene::RigidbodyBodyType::Kinematic:
        return "Kinematic";
    }
    return "Unknown";
}

const char* InspectorComponentLabelFormatter::ColliderShapeName(kb::scene::ColliderShape shape) noexcept {
    switch (shape) {
    case kb::scene::ColliderShape::Box:
        return "Box";
    case kb::scene::ColliderShape::Sphere:
        return "Sphere";
    case kb::scene::ColliderShape::Capsule:
        return "Capsule";
    }
    return "Unknown";
}

const char* InspectorComponentLabelFormatter::AudioAttenuationModelName(kb::audio::AudioAttenuationModel model) noexcept {
    switch (model) {
    case kb::audio::AudioAttenuationModel::None:
        return "None";
    case kb::audio::AudioAttenuationModel::Inverse:
        return "Inverse";
    case kb::audio::AudioAttenuationModel::Linear:
        return "Linear";
    case kb::audio::AudioAttenuationModel::Exponential:
        return "Exponential";
    }
    return "Unknown";
}

} // namespace kb::editor
