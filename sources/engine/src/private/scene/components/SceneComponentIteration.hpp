#pragma once

#include "engine/ecs/World.hpp"
#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneVisitors.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "engine/scene/VisibilityComponent.hpp"

#include <cstdint>

namespace kb::scene {

class SceneComponentIteration {
public:
    SceneComponentIteration() = delete;

    static void ForEachTransform(const kb::ecs::World& world, std::uint64_t transformComponentId, ConstTransformVisitor visitor, void* context);
    static void ForEachMutableTransform(kb::ecs::World& world, std::uint64_t transformComponentId, MutableTransformVisitor visitor, void* context);
    static void ForEachBehaviour(const kb::ecs::World& world, std::uint64_t behaviourComponentId, BehaviourVisitor visitor, void* context);
    static void ForEachCamera(const kb::ecs::World& world, std::uint64_t transformComponentId, std::uint64_t cameraComponentId, CameraVisitor visitor, void* context);
    static void ForEachMeshRenderer(const kb::ecs::World& world, std::uint64_t transformComponentId, std::uint64_t meshRendererComponentId, MeshRendererVisitor visitor, void* context);
    static void ForEachVisibleMeshRenderer(const kb::ecs::World& world, std::uint64_t transformComponentId, std::uint64_t visibilityComponentId, std::uint64_t meshRendererComponentId, MeshRendererVisitor visitor, void* context);
    static void ForEachLight(const kb::ecs::World& world, std::uint64_t transformComponentId, std::uint64_t lightComponentId, LightVisitor visitor, void* context);
};

} // namespace kb::scene
