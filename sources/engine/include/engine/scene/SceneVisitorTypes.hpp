#pragma once

#include "engine/scene/SceneEntity.hpp"

namespace kb::scene {

struct CameraComponent;
struct LightComponent;
struct MeshRendererComponent;
struct TransformComponent;

using ConstTransformVisitor = void (*)(SceneEntity entity, const TransformComponent& transform, void* context);
using MutableTransformVisitor = void (*)(SceneEntity entity, TransformComponent& transform, void* context);
using CameraVisitor = void (*)(SceneEntity entity, const TransformComponent& transform, const CameraComponent& camera, void* context);
using MeshRendererVisitor = void (*)(SceneEntity entity, const TransformComponent& transform, const MeshRendererComponent& renderer, void* context);
using LightVisitor = void (*)(SceneEntity entity, const TransformComponent& transform, const LightComponent& light, void* context);

} // namespace kb::scene
