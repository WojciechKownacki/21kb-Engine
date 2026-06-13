#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TagsComponent.hpp"

namespace kb::scene {

class Scene;

class SceneTagsComponentQueries {
public:
    explicit SceneTagsComponentQueries(const Scene& scene) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const TagsComponent* TryGet(SceneEntity entity) const noexcept;

private:
    const Scene& scene_;
};

class SceneTagsComponents {
public:
    explicit SceneTagsComponents(Scene& scene) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const TagsComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] TagsComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const TagsComponent& tags);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;

private:
    Scene& scene_;
};

} // namespace kb::scene
