#include "engine/scene/SceneObject.hpp"

#include "engine/scene/Scene.hpp"

namespace kb::scene {

SceneObject::SceneObject(Scene& scene, SceneEntity entity) noexcept
    : scene_(&scene)
    , entity_(entity) {}

bool SceneObject::IsValid() const noexcept {
    return scene_ != nullptr && scene_->IsAlive(*this);
}

kb::ecs::Entity SceneObject::EntityHandle() const noexcept {
    return entity_;
}

SceneEntity SceneObject::Entity() const noexcept {
    return entity_;
}

std::string SceneObject::Name() const {
    return scene_ == nullptr ? std::string{} : scene_->Name(entity_);
}

void SceneObject::SetName(std::string_view name) const {
    if (scene_ != nullptr) {
        scene_->SetName(entity_, name);
    }
}

TransformComponent SceneObject::Transform() const {
    return scene_ == nullptr ? TransformComponent{} : scene_->Transform(entity_);
}

void SceneObject::SetTransform(const TransformComponent& transform) const {
    if (scene_ != nullptr) {
        scene_->SetTransform(entity_, transform);
    }
}

SceneObject SceneObject::Parent() const {
    return scene_ == nullptr ? SceneObject{} : scene_->Parent(*this);
}

std::vector<SceneObject> SceneObject::Children() const {
    return scene_ == nullptr ? std::vector<SceneObject>{} : scene_->Children(*this);
}

bool SceneObject::SetParent(SceneObject parent) const noexcept {
    return scene_ != nullptr && scene_->SetParent(entity_, parent.Entity());
}

void SceneObject::Destroy() const noexcept {
    if (scene_ != nullptr) {
        scene_->DestroyEntity(entity_);
    }
}

} // namespace kb::scene
