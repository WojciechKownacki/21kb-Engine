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

} // namespace kb::editor
