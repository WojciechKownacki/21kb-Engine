#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SocketAttachmentComponent.hpp"

namespace kb::scene {

class Scene;

class SceneSocketAttachmentComponentQueries {
public:
    explicit SceneSocketAttachmentComponentQueries(const Scene& scene) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const SocketAttachmentComponent* TryGet(SceneEntity entity) const noexcept;

private:
    const Scene& scene_;
};

class SceneSocketAttachmentComponents {
public:
    explicit SceneSocketAttachmentComponents(Scene& scene) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const SocketAttachmentComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] SocketAttachmentComponent* TryGet(SceneEntity entity) noexcept;
    [[nodiscard]] bool Set(SceneEntity entity, const SocketAttachmentComponent& attachment);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;

private:
    Scene& scene_;
};

} // namespace kb::scene
