#include "scene/components/SceneComponentRegistry.hpp"

#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "engine/scene/VisibilityComponent.hpp"

#include <flecs.h>

#include <stdexcept>

namespace kb::scene {

SceneComponentRegistry::SceneComponentRegistry(ecs_world_t& world)
    : transformComponentId_(RegisterComponent(world, "kb.scene.TransformComponent", sizeof(TransformComponent), alignof(TransformComponent)))
    , visibilityComponentId_(RegisterComponent(world, "kb.scene.VisibilityComponent", sizeof(VisibilityComponent), alignof(VisibilityComponent)))
    , cameraComponentId_(RegisterComponent(world, "kb.scene.CameraComponent", sizeof(CameraComponent), alignof(CameraComponent)))
    , meshRendererComponentId_(RegisterComponent(world, "kb.scene.MeshRendererComponent", sizeof(MeshRendererComponent), alignof(MeshRendererComponent)))
    , lightComponentId_(RegisterComponent(world, "kb.scene.LightComponent", sizeof(LightComponent), alignof(LightComponent))) {}

std::uint64_t SceneComponentRegistry::TransformComponentId() const noexcept {
    return transformComponentId_;
}

std::uint64_t SceneComponentRegistry::VisibilityComponentId() const noexcept {
    return visibilityComponentId_;
}

std::uint64_t SceneComponentRegistry::CameraComponentId() const noexcept {
    return cameraComponentId_;
}

std::uint64_t SceneComponentRegistry::MeshRendererComponentId() const noexcept {
    return meshRendererComponentId_;
}

std::uint64_t SceneComponentRegistry::LightComponentId() const noexcept {
    return lightComponentId_;
}

std::uint64_t SceneComponentRegistry::RegisterComponent(ecs_world_t& world, const char* name, std::size_t size, std::size_t alignment) {
    ecs_component_desc_t desc{};
    desc.type.size = static_cast<ecs_size_t>(size);
    desc.type.alignment = static_cast<ecs_size_t>(alignment);
    desc.type.name = name;

    const ecs_entity_t component = ecs_component_init(&world, &desc);
    if (component == 0) {
        throw std::runtime_error("Failed to register scene component");
    }

    ecs_set_name(&world, component, name);
    return component;
}

} // namespace kb::scene
