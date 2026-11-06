#include "rendering/EditorPreviewRenderCamera.hpp"

#include "kb/render/SceneDepthPolicy.hpp"

#include <algorithm>

#include <bx/math.h>

namespace kb::editor {

kb::render::SceneRenderCamera BuildEditorPreviewRenderCamera(
    const EditorViewportCameraState& camera, std::uint32_t width, std::uint32_t height) noexcept {
    const EditorViewportCameraAxes axes = camera.Axes();
    kb::render::SceneRenderCamera output{};
    bx::mtxLookAt(
        output.view.data(),
        bx::Vec3{ axes.position.x, axes.position.y, axes.position.z },
        bx::Vec3{
            axes.position.x + axes.forward.x,
            axes.position.y + axes.forward.y,
            axes.position.z + axes.forward.z,
        },
        bx::Vec3{ axes.up.x, axes.up.y, axes.up.z });
    const float aspect = height == 0U ? 1.0F :
        static_cast<float>(std::max(1U, width)) / static_cast<float>(height);
    kb::render::SceneDepthPolicy::MakePerspective(
        output.projection.data(), camera.VerticalFovDegrees(), aspect,
        camera.NearClip(), camera.FarClip(),
        kb::render::SceneDepthPolicy::HomogeneousDepth());
    return output;
}

} // namespace kb::editor
