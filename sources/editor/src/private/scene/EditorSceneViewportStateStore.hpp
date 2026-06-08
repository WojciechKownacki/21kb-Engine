#pragma once

#include "scene/EditorViewportCameraState.hpp"
#include "scene/EditorViewportPreviewState.hpp"

#include <cstdint>
#include <unordered_map>

namespace kb::editor {

struct EditorSceneGizmoAxisDrag {
    float axis[3]{};
    float planeNormal[3]{};
    float removeNormal[3]{};
    float startPoint[3]{};
};

struct EditorSceneGizmoState {
    int hoveredAxis = -1;
    int draggedAxis = -1;
    bool centerDrag = false;
    float dragStartTargetX = 0.0F;
    float dragStartTargetY = 0.0F;
    float dragStartTargetZ = 0.0F;
    float centerPlaneNx = 0.0F;
    float centerPlaneNy = 0.0F;
    float centerPlaneNz = 1.0F;
    float centerStartPx = 0.0F;
    float centerStartPy = 0.0F;
    float centerStartPz = 0.0F;
    EditorSceneGizmoAxisDrag axisDrag{};

    [[nodiscard]] bool IsDragging() const noexcept {
        return draggedAxis >= 0 || centerDrag;
    }
};

class EditorSceneViewportStateStore {
public:
    [[nodiscard]] EditorViewportPreviewState& Preview() noexcept;
    [[nodiscard]] const EditorViewportPreviewState& Preview() const noexcept;
    [[nodiscard]] EditorViewportPreviewState& Preview(std::uint64_t viewportKey) noexcept;
    [[nodiscard]] const EditorViewportPreviewState& Preview(std::uint64_t viewportKey) const noexcept;

    [[nodiscard]] EditorViewportCameraState& Camera() noexcept;
    [[nodiscard]] const EditorViewportCameraState& Camera() const noexcept;
    [[nodiscard]] EditorViewportCameraState& Camera(std::uint64_t viewportKey) noexcept;
    [[nodiscard]] const EditorViewportCameraState& Camera(std::uint64_t viewportKey) const noexcept;

    void BeginCameraNavigation(std::uint64_t viewportKey, EditorViewportCameraNavigationMode mode, int x, int y) noexcept;
    [[nodiscard]] bool HasActiveCameraNavigation() const noexcept;
    [[nodiscard]] std::uint64_t ActiveCameraKey() const noexcept;
    [[nodiscard]] EditorViewportCameraState* ActiveCamera() noexcept;
    [[nodiscard]] const EditorViewportCameraState* ActiveCamera() const noexcept;
    void EndCameraNavigation() noexcept;

    [[nodiscard]] EditorSceneGizmoState& Gizmo() noexcept;
    [[nodiscard]] const EditorSceneGizmoState& Gizmo() const noexcept;
    [[nodiscard]] bool CloseToolbarDropdowns() noexcept;

private:
    mutable std::unordered_map<std::uint64_t, EditorViewportPreviewState> previews_;
    mutable std::unordered_map<std::uint64_t, EditorViewportCameraState> cameras_;
    EditorSceneGizmoState gizmo_;
    std::uint64_t activeCameraKey_ = 0U;
    bool hasActiveCameraNavigation_ = false;
};

} // namespace kb::editor
