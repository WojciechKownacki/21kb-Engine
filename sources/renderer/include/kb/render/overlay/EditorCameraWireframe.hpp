#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace kb::render {

enum class EditorCameraWireframeProjection : std::uint8_t {
    Perspective,
    Orthographic,
};

struct EditorCameraWireframeDesc {
    EditorCameraWireframeProjection projection = EditorCameraWireframeProjection::Perspective;
    std::array<float, 3> position{0.0F, 0.0F, 0.0F};
    std::array<float, 3> forward{0.0F, 0.0F, 1.0F};
    std::array<float, 3> right{1.0F, 0.0F, 0.0F};
    std::array<float, 3> up{0.0F, 1.0F, 0.0F};
    float verticalFovDegrees = 60.0F;
    float orthographicHeight = 10.0F;
    float nearClip = 0.01F;
    float farClip = 1000.0F;
    // Editor-only bounded visualization depth. The authored farClip remains
    // unchanged and continues to drive the runtime Camera projection.
    // A non-positive value requests the full authored frustum.
    float displayFarClip = 0.0F;
    float aspect = 1.0F;
};

struct EditorCameraWireframeLine {
    std::array<float, 3> from{0.0F, 0.0F, 0.0F};
    std::array<float, 3> to{0.0F, 0.0F, 0.0F};
    float alpha = 1.0F;
};

inline constexpr std::size_t kEditorCameraWireframeLineCount = 12U;

// Produces four near-plane edges, four far-plane edges, and four connectors.
// Scene View navigation must never add or remove edges from this geometry.
[[nodiscard]] std::array<EditorCameraWireframeLine, kEditorCameraWireframeLineCount>
BuildEditorCameraWireframeLines(const EditorCameraWireframeDesc& camera) noexcept;

} // namespace kb::render
