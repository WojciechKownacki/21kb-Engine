#include "engine/scene/Scene.hpp"

#include "scene/components/SceneComponentStorage.hpp"
#include "scene/components/SceneComponentRegistry.hpp"
#include "scene/entities/SceneEntityCounter.hpp"
#include "scene/entities/SceneEntityNaming.hpp"

#include <utility>

namespace kb::scene {

SceneObject Scene::CreateObject() {
    return MakeObject(CreateEntity());
}

SceneObject Scene::CreateObject(SceneObjectDesc desc) {
    return MakeObject(CreateEntity(std::move(desc)));
}

SceneEntity Scene::CreateEntity() {
    return CreateEntity(SceneObjectDesc{});
}

SceneEntity Scene::CreateEntity(SceneObjectDesc desc) {
    kb::ecs::Entity entity = desc.name.empty() ? world_.CreateEntity() : world_.CreateEntity(desc.name);
    componentStorage_->SetDefaults(entity, desc.transform, desc.visibility);

    if (desc.parent.EntityHandle().IsValid()) {
        [[maybe_unused]] const bool parentAssigned = SetParent(entity, desc.parent.Entity());
    }

    return entity;
}

void Scene::DestroyObject(SceneObject object) noexcept {
    if (IsAlive(object)) {
        DestroyEntity(object.Entity());
    }
}

void Scene::DestroyEntity(SceneEntity entity) noexcept {
    if (!IsAlive(entity)) {
        return;
    }

    for (const SceneEntity child : ChildEntities(entity)) {
        DestroyEntity(child);
    }

    world_.DestroyEntity(entity);
}

std::string Scene::Name(SceneObject object) const {
    return IsAlive(object) ? Name(object.Entity()) : std::string{};
}

std::string Scene::Name(SceneEntity entity) const {
    return IsAlive(entity) ? SceneEntityNaming::Name(world_, entity) : std::string{};
}

void Scene::SetName(SceneObject object, std::string_view name) {
    if (IsAlive(object)) {
        SetName(object.Entity(), name);
    }
}

void Scene::SetName(SceneEntity entity, std::string_view name) {
    if (IsAlive(entity)) {
        SceneEntityNaming::SetName(world_, entity, name);
    }
}

std::size_t Scene::ObjectCount() const {
    return SceneEntityCounter::CountWithComponent(world_, components_->TransformComponentId());
}

} // namespace kb::scene
