#pragma once

#include "scene/EditorViewportCameraState.hpp"

#include "kb/render/scene/SceneRenderTypes.hpp"

#include <cstdint>

namespace kb::editor {

// The view and projection a preview viewport of `width` x `height` pixels draws with, from the camera an asset
// editor orbits.
[[nodiscard]] kb::render::SceneRenderCamera BuildEditorPreviewRenderCamera(
    const EditorViewportCameraState& camera, std::uint32_t width, std::uint32_t height) noexcept;

} // namespace kb::editor
