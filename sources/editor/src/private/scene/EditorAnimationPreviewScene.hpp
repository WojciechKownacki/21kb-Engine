#pragma once

#include "scene/AnimationPreviewContext.hpp"
#include "scene/EditorViewportCameraState.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <memory>

namespace kb::editor {

class EditorAnimationPreviewScene {
public:
    [[nodiscard]] const kb::scene::Scene& SceneFor(
        const kb::scene::Scene& source, const AnimationPreviewContext& context);
    [[nodiscard]] kb::scene::Scene* MutableScene() noexcept { return scene_.get(); }
    [[nodiscard]] EditorViewportCameraState& Camera() noexcept { return camera_; }
    [[nodiscard]] const EditorViewportCameraState& Camera() const noexcept { return camera_; }
    [[nodiscard]] kb::scene::SceneEntity PreviewEntity() const noexcept { return previewEntity_; }
    [[nodiscard]] kb::scene::SceneEntity CameraEntity() const noexcept { return cameraEntity_; }
    [[nodiscard]] kb::scene::SceneEntity FloorEntity() const noexcept { return floorEntity_; }
    [[nodiscard]] kb::scene::SceneEntity EnvironmentEntity() const noexcept { return environmentEntity_; }
    void Focus(float durationSeconds = 0.0F) noexcept;
    [[nodiscard]] bool TickCamera(float deltaSeconds) noexcept;
    [[nodiscard]] std::uint64_t Revision() const noexcept { return revision_; }
    void Clear() noexcept;

private:
    void Rebuild(const kb::scene::Scene& source, const AnimationPreviewContext& context);
    void SynchronizeCamera() noexcept;

    std::unique_ptr<kb::scene::Scene> scene_;
    EditorViewportCameraState camera_;
    kb::scene::SceneEntity previewEntity_{};
    kb::scene::SceneEntity cameraEntity_{};
    kb::scene::SceneEntity floorEntity_{};
    kb::scene::SceneEntity environmentEntity_{};
    kb::scene::Vec3 focusCenter_{};
    float focusRadius_ = 1.0F;
    std::uint64_t sourceSceneId_ = 0U;
    std::uint64_t contextRevision_ = 0U;
    std::uint64_t revision_ = 1U;
};

} // namespace kb::editor
