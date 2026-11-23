#pragma once

#include <string>

namespace kb::scene {
class Scene;
}

namespace kb::game {

// Which camera a captured frame is rendered from: one of the scene's cameras by name, or a free view like the editor's scene
// view. A free view is a named framing (overview, top, front, side) or an eye and a point to look at.
struct CaptureCameraRequest {
    std::string camera;
    std::string view;
    std::string eye;
    std::string lookAt;

    [[nodiscard]] bool Empty() const noexcept { return camera.empty() && view.empty() && eye.empty() && lookAt.empty(); }
};

// Makes the requested camera the one the frame is drawn from. Returns the empty string on success, else what is wrong
// (naming the cameras the scene has, or the views that exist).
[[nodiscard]] std::string ApplyCaptureCamera(kb::scene::Scene& scene, const CaptureCameraRequest& request);

} // namespace kb::game
