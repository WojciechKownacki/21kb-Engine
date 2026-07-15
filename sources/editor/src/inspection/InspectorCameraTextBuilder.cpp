#include "inspection/InspectorCameraTextBuilder.hpp"

#include "inspection/InspectorComponentLabelFormatter.hpp"

#include <cstdio>

namespace kb::editor {

void InspectorCameraTextBuilder::Append(std::string& text, const kb::scene::CameraComponent& camera) const {
    char component[420]{};
    std::snprintf(
        component,
        sizeof(component),
        "\n\nCamera\nProjection: %s\nFOV: %.1f\nClip: %.2f - %.1f\nPrimary: %s\nViewport: %u\nPriority: %d\nCulling Mask: 0x%08X\nClear: %s\nClear Color: %.2f, %.2f, %.2f",
        InspectorComponentLabelFormatter::ProjectionName(camera.projection),
        camera.verticalFovDegrees,
        camera.nearClip,
        camera.farClip,
        camera.primary ? "true" : "false",
        camera.viewportId,
        camera.priority,
        camera.cullingMask,
        InspectorComponentLabelFormatter::CameraClearModeName(camera.clearMode),
        camera.clearColor.x,
        camera.clearColor.y,
        camera.clearColor.z);
    text += component;
}

} // namespace kb::editor
