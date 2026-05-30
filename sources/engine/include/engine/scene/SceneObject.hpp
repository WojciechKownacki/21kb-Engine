#pragma once

#include "engine/ecs/Entity.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace kb::scene {

class Scene;

class SceneObject {
public:
    SceneObject() noexcept = default;

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] kb::ecs::Entity EntityHandle() const noexcept;
    [[nodiscard]] SceneEntity Entity() const noexcept;

    [[nodiscard]] std::string Name() const;
    void SetName(std::string_view name) const;

    [[nodiscard]] TransformComponent Transform() const;
    void SetTransform(const TransformComponent& transform) const;

    [[nodiscard]] SceneObject Parent() const;
    [[nodiscard]] std::vector<SceneObject> Children() const;
    [[nodiscard]] bool SetParent(SceneObject parent) const noexcept;

    void Destroy() const noexcept;

private:
    friend class Scene;

    SceneObject(Scene& scene, SceneEntity entity) noexcept;

    Scene* scene_ = nullptr;
    SceneEntity entity_{};
};

} // namespace kb::scene
