#include "scene/EditorSceneViewportStateStore.hpp"

namespace kb::editor {

EditorViewportPreviewState& EditorSceneViewportStateStore::Preview() noexcept {
    return Preview(0U);
}

const EditorViewportPreviewState& EditorSceneViewportStateStore::Preview() const noexcept {
    return Preview(0U);
}

EditorViewportPreviewState& EditorSceneViewportStateStore::Preview(std::uint64_t viewportKey) noexcept {
    return previews_.try_emplace(viewportKey).first->second;
}

const EditorViewportPreviewState& EditorSceneViewportStateStore::Preview(std::uint64_t viewportKey) const noexcept {
    return previews_.try_emplace(viewportKey).first->second;
}

EditorViewportCameraState& EditorSceneViewportStateStore::Camera() noexcept {
    return Camera(0U);
}

const EditorViewportCameraState& EditorSceneViewportStateStore::Camera() const noexcept {
    return Camera(0U);
}

EditorViewportCameraState& EditorSceneViewportStateStore::Camera(std::uint64_t viewportKey) noexcept {
    return cameras_.try_emplace(viewportKey).first->second;
}

const EditorViewportCameraState& EditorSceneViewportStateStore::Camera(std::uint64_t viewportKey) const noexcept {
    return cameras_.try_emplace(viewportKey).first->second;
}

void EditorSceneViewportStateStore::BeginCameraNavigation(std::uint64_t viewportKey, EditorViewportCameraNavigationMode mode, int x, int y) noexcept {
    EndCameraNavigation();
    activeCameraKey_ = viewportKey;
    hasActiveCameraNavigation_ = true;
    Camera(viewportKey).BeginNavigation(mode, x, y);
}

bool EditorSceneViewportStateStore::HasActiveCameraNavigation() const noexcept {
    return hasActiveCameraNavigation_ && ActiveCamera() != nullptr;
}

std::uint64_t EditorSceneViewportStateStore::ActiveCameraKey() const noexcept {
    return activeCameraKey_;
}

EditorViewportCameraState* EditorSceneViewportStateStore::ActiveCamera() noexcept {
    if (!hasActiveCameraNavigation_) {
        return nullptr;
    }
    auto it = cameras_.find(activeCameraKey_);
    if (it == cameras_.end() || !it->second.IsNavigating()) {
        return nullptr;
    }
    return &it->second;
}

const EditorViewportCameraState* EditorSceneViewportStateStore::ActiveCamera() const noexcept {
    if (!hasActiveCameraNavigation_) {
        return nullptr;
    }
    auto it = cameras_.find(activeCameraKey_);
    if (it == cameras_.end() || !it->second.IsNavigating()) {
        return nullptr;
    }
    return &it->second;
}

void EditorSceneViewportStateStore::EndCameraNavigation() noexcept {
    if (EditorViewportCameraState* camera = ActiveCamera(); camera != nullptr) {
        camera->EndNavigation();
    }
    hasActiveCameraNavigation_ = false;
    activeCameraKey_ = 0U;
}

EditorSceneGizmoState& EditorSceneViewportStateStore::Gizmo() noexcept {
    return gizmo_;
}

const EditorSceneGizmoState& EditorSceneViewportStateStore::Gizmo() const noexcept {
    return gizmo_;
}

} // namespace kb::editor
