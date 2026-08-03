#pragma once

#include "scene/AnimationPreviewContext.hpp"

#include "engine/scene/Scene.hpp"

#include <memory>

namespace kb::editor {

class EditorAnimationPreviewScene {
public:
    [[nodiscard]] const kb::scene::Scene& SceneFor(
        const kb::scene::Scene& source, const AnimationPreviewContext& context);
    [[nodiscard]] kb::scene::Scene* MutableScene() noexcept { return scene_.get(); }
    [[nodiscard]] std::uint64_t Revision() const noexcept { return revision_; }
    void Clear() noexcept;

private:
    void Rebuild(const kb::scene::Scene& source, const AnimationPreviewContext& context);

    std::unique_ptr<kb::scene::Scene> scene_;
    std::uint64_t sourceSceneId_ = 0U;
    std::uint64_t contextRevision_ = 0U;
    std::uint64_t revision_ = 1U;
};

} // namespace kb::editor
