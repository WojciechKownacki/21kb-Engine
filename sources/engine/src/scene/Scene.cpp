#include "engine/scene/Scene.hpp"

#include "scene/components/SceneComponentRegistry.hpp"
#include "scene/components/SceneComponentStorage.hpp"

#include <memory>

namespace kb::scene {

Scene::Scene()
    : components_(std::make_unique<SceneComponentRegistry>(*world_.NativeHandle()))
    , componentStorage_(std::make_unique<SceneComponentStorage>(world_.NativeHandle(), *components_)) {}

Scene::~Scene() = default;

bool Scene::IsAlive(SceneObject object) const noexcept {
    return BelongsToThisScene(object) && IsAlive(object.Entity());
}

bool Scene::IsAlive(SceneEntity entity) const noexcept {
    return world_.IsAlive(entity);
}

kb::ecs::World& Scene::EcsWorld() noexcept {
    return world_;
}

const kb::ecs::World& Scene::EcsWorld() const noexcept {
    return world_;
}

SceneObject Scene::MakeObject(SceneEntity entity) noexcept {
    return SceneObject{ *this, entity };
}

bool Scene::BelongsToThisScene(SceneObject object) const noexcept {
    return object.scene_ == this && object.EntityHandle().IsValid();
}

} // namespace kb::scene
