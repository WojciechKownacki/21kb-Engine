#pragma once

#include "engine/scene/SceneEntity.hpp"

namespace kb::scene {

struct CameraComponent;
struct BehaviourComponent;
struct LightComponent;
struct MeshRendererComponent;
struct ColliderComponent;
struct RigidbodyComponent;
struct TransformComponent;
struct WorldTransformAffine3x4;

using ConstTransformVisitor = void (*)(SceneEntity entity, const TransformComponent& transform, void* context);
using MutableTransformVisitor = void (*)(SceneEntity entity, TransformComponent& transform, void* context);
using BehaviourVisitor = void (*)(SceneEntity entity, const BehaviourComponent& behaviour, void* context);
using CameraVisitor = void (*)(SceneEntity entity, const TransformComponent& transform, const CameraComponent& camera, void* context);
using CameraRenderProxyVisitor = void (*)(SceneEntity entity, const WorldTransformAffine3x4& worldTransform, const CameraComponent& camera, void* context);
using MeshRendererVisitor = void (*)(SceneEntity entity, const TransformComponent& transform, const MeshRendererComponent& renderer, void* context);
using MeshRendererRenderProxyVisitor = void (*)(SceneEntity entity, const WorldTransformAffine3x4& worldTransform, const MeshRendererComponent& renderer, void* context);
using LightVisitor = void (*)(SceneEntity entity, const TransformComponent& transform, const LightComponent& light, void* context);
using LightRenderProxyVisitor = void (*)(SceneEntity entity, const WorldTransformAffine3x4& worldTransform, const LightComponent& light, void* context);
using PhysicsBodyVisitor = void (*)(SceneEntity entity, const TransformComponent& transform, const RigidbodyComponent& rigidbody, const ColliderComponent& collider, void* context);

} // namespace kb::scene
