#include "engine/scene/SceneObject.hpp"

#include "scene/SceneEntityService.hpp"
#include "scene/SceneHierarchyService.hpp"
#include "scene/SceneTransformService.hpp"

namespace kb::scene {

SceneObject::SceneObject(Scene& scene, SceneEntity entity) noexcept
    : scene_(&scene)
    , entity_(entity) {}

bool SceneObject::IsValid() const noexcept {
    return scene_ != nullptr && SceneEntityService::IsAlive(*scene_, *this);
}

kb::ecs::Entity SceneObject::EntityHandle() const noexcept {
    return entity_;
}

SceneEntity SceneObject::Entity() const noexcept {
    return entity_;
}

std::string SceneObject::Name() const {
    return scene_ == nullptr ? std::string{} : SceneEntityService::Name(*scene_, entity_);
}

void SceneObject::SetName(std::string_view name) const {
    if (scene_ != nullptr) {
        SceneEntityService::SetName(*scene_, entity_, name);
    }
}

TransformComponent SceneObject::Transform() const {
    return scene_ == nullptr ? TransformComponent{} : SceneTransformService::Get(*scene_, entity_);
}

void SceneObject::SetTransform(const TransformComponent& transform) const {
    if (scene_ != nullptr) {
        SceneTransformService::Set(*scene_, entity_, transform);
    }
}

SceneObject SceneObject::Parent() const {
    return scene_ == nullptr ? SceneObject{} : SceneHierarchyService::Parent(*scene_, *this);
}

std::vector<SceneObject> SceneObject::Children() const {
    return scene_ == nullptr ? std::vector<SceneObject>{} : SceneHierarchyService::Children(*scene_, *this);
}

bool SceneObject::SetParent(SceneObject parent) const noexcept {
    return scene_ != nullptr && SceneHierarchyService::SetParent(*scene_, entity_, parent.Entity());
}

void SceneObject::Destroy() const noexcept {
    if (scene_ != nullptr) {
        SceneEntityService::DestroyEntity(*scene_, entity_);
    }
}

} // namespace kb::scene
