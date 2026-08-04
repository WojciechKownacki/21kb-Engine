#pragma once

#include "scene/AnimationPreviewContext.hpp"
#include "scene/EditorViewportCameraState.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SkeletonAsset.hpp"

#include <memory>
#include <string>
#include <vector>

namespace kb::editor {

struct AnimationPreviewOverlayLine {
    kb::scene::Vec3 from{};
    kb::scene::Vec3 to{};
    kb::scene::Vec3 color{ 1.0F, 1.0F, 1.0F };
    kb::scene::SkeletonBoneId boneId = 0U;
};

struct AnimationPreviewOverlayLabel {
    kb::scene::Vec3 position{};
    std::string text;
};

struct AnimationPreviewOverlaySnapshot {
    std::vector<AnimationPreviewOverlayLine> lines;
    std::vector<AnimationPreviewOverlayLabel> labels;
    std::uint32_t lodCount = 0U;
    std::uint64_t poseEvaluationCount = 0U;
};

class EditorAnimationPreviewScene {
public:
    [[nodiscard]] const kb::scene::Scene& SceneFor(
        const kb::scene::Scene& source, AnimationPreviewContext& context);
    [[nodiscard]] const kb::scene::Scene* CurrentScene() const noexcept { return scene_.get(); }
    [[nodiscard]] kb::scene::Scene* MutableScene() noexcept { return scene_.get(); }
    [[nodiscard]] EditorViewportCameraState& Camera() noexcept { return camera_; }
    [[nodiscard]] const EditorViewportCameraState& Camera() const noexcept { return camera_; }
    [[nodiscard]] kb::scene::SceneEntity PreviewEntity() const noexcept { return previewEntity_; }
    [[nodiscard]] kb::scene::SceneEntity CameraEntity() const noexcept { return cameraEntity_; }
    [[nodiscard]] kb::scene::SceneEntity FloorEntity() const noexcept { return floorEntity_; }
    [[nodiscard]] kb::scene::SceneEntity EnvironmentEntity() const noexcept { return environmentEntity_; }
    void Focus(float durationSeconds = 0.0F) noexcept;
    [[nodiscard]] bool TickCamera(float deltaSeconds) noexcept;
    [[nodiscard]] bool TickPlayback(AnimationPreviewContext& context, float deltaSeconds) noexcept;
    [[nodiscard]] AnimationPreviewOverlaySnapshot BuildOverlays(const AnimationPreviewContext& context) const;
    [[nodiscard]] std::uint64_t Revision() const noexcept { return revision_; }
    void Clear() noexcept;

private:
    void Rebuild(const kb::scene::Scene& source, const AnimationPreviewContext& context);
    void SynchronizeCamera() noexcept;
    void SynchronizePlayback(AnimationPreviewContext& context) noexcept;

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
    std::uint64_t playbackRevision_ = 0U;
    std::uint64_t revision_ = 1U;
};

} // namespace kb::editor
