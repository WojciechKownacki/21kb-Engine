#pragma once

#include "engine/ecs/World.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/VisibilityComponent.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace kb::scene {

class SceneComponentRegistry;
class SceneComponentStorage;

class Scene {
public:
    using ConstTransformVisitor = void (*)(SceneEntity entity, const TransformComponent& transform, void* context);
    using MutableTransformVisitor = void (*)(SceneEntity entity, TransformComponent& transform, void* context);
    using CameraVisitor = void (*)(SceneEntity entity, const TransformComponent& transform, const CameraComponent& camera, void* context);
    using MeshRendererVisitor = void (*)(SceneEntity entity, const TransformComponent& transform, const MeshRendererComponent& renderer, void* context);
    using LightVisitor = void (*)(SceneEntity entity, const TransformComponent& transform, const LightComponent& light, void* context);

    Scene();
    ~Scene();

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) = delete;
    Scene& operator=(Scene&&) = delete;

    [[nodiscard]] SceneObject CreateObject();
    [[nodiscard]] SceneObject CreateObject(SceneObjectDesc desc);

    [[nodiscard]] SceneEntity CreateEntity();
    [[nodiscard]] SceneEntity CreateEntity(SceneObjectDesc desc);

    void DestroyObject(SceneObject object) noexcept;
    void DestroyEntity(SceneEntity entity) noexcept;

    [[nodiscard]] bool IsAlive(SceneObject object) const noexcept;
    [[nodiscard]] bool IsAlive(SceneEntity entity) const noexcept;

    [[nodiscard]] std::string Name(SceneObject object) const;
    [[nodiscard]] std::string Name(SceneEntity entity) const;
    void SetName(SceneObject object, std::string_view name);
    void SetName(SceneEntity entity, std::string_view name);

    [[nodiscard]] TransformComponent Transform(SceneObject object) const;
    [[nodiscard]] TransformComponent Transform(SceneEntity entity) const;
    [[nodiscard]] const TransformComponent* TryGetTransform(SceneEntity entity) const noexcept;
    [[nodiscard]] TransformComponent* TryGetTransform(SceneEntity entity) noexcept;
    void SetTransform(SceneObject object, const TransformComponent& transform);
    void SetTransform(SceneEntity entity, const TransformComponent& transform);
    void MarkTransformModified(SceneEntity entity) noexcept;

    [[nodiscard]] VisibilityComponent Visibility(SceneEntity entity) const;
    [[nodiscard]] const VisibilityComponent* TryGetVisibility(SceneEntity entity) const noexcept;
    [[nodiscard]] VisibilityComponent* TryGetVisibility(SceneEntity entity) noexcept;
    void SetVisibility(SceneEntity entity, const VisibilityComponent& visibility);
    void MarkVisibilityModified(SceneEntity entity) noexcept;

    [[nodiscard]] bool HasCamera(SceneEntity entity) const noexcept;
    [[nodiscard]] const CameraComponent* TryGetCamera(SceneEntity entity) const noexcept;
    [[nodiscard]] CameraComponent* TryGetCamera(SceneEntity entity) noexcept;
    void SetCamera(SceneEntity entity, const CameraComponent& camera);
    void RemoveCamera(SceneEntity entity) noexcept;
    void MarkCameraModified(SceneEntity entity) noexcept;

    [[nodiscard]] bool HasMeshRenderer(SceneEntity entity) const noexcept;
    [[nodiscard]] const MeshRendererComponent* TryGetMeshRenderer(SceneEntity entity) const noexcept;
    [[nodiscard]] MeshRendererComponent* TryGetMeshRenderer(SceneEntity entity) noexcept;
    void SetMeshRenderer(SceneEntity entity, const MeshRendererComponent& renderer);
    void RemoveMeshRenderer(SceneEntity entity) noexcept;
    void MarkMeshRendererModified(SceneEntity entity) noexcept;

    [[nodiscard]] bool HasLight(SceneEntity entity) const noexcept;
    [[nodiscard]] const LightComponent* TryGetLight(SceneEntity entity) const noexcept;
    [[nodiscard]] LightComponent* TryGetLight(SceneEntity entity) noexcept;
    void SetLight(SceneEntity entity, const LightComponent& light);
    void RemoveLight(SceneEntity entity) noexcept;
    void MarkLightModified(SceneEntity entity) noexcept;

    [[nodiscard]] SceneObject Parent(SceneObject object) const;
    [[nodiscard]] SceneEntity Parent(SceneEntity entity) const noexcept;
    [[nodiscard]] std::vector<SceneObject> Children(SceneObject object) const;
    [[nodiscard]] std::vector<SceneEntity> ChildEntities(SceneEntity entity) const;
    [[nodiscard]] std::vector<SceneObject> RootObjects() const;
    [[nodiscard]] std::vector<SceneEntity> RootEntities() const;
    [[nodiscard]] bool SetParent(SceneObject child, SceneObject parent) noexcept;
    [[nodiscard]] bool SetParent(SceneEntity child, SceneEntity parent) noexcept;

    [[nodiscard]] std::size_t ObjectCount() const;
    void ForEachTransform(ConstTransformVisitor visitor, void* context = nullptr) const;
    // Call MarkTransformModified explicitly when observers or change tracking need a notification.
    void ForEachMutableTransform(MutableTransformVisitor visitor, void* context = nullptr);
    void ForEachCamera(CameraVisitor visitor, void* context = nullptr) const;
    void ForEachMeshRenderer(MeshRendererVisitor visitor, void* context = nullptr) const;
    void ForEachVisibleMeshRenderer(MeshRendererVisitor visitor, void* context = nullptr) const;
    void ForEachLight(LightVisitor visitor, void* context = nullptr) const;

    [[nodiscard]] kb::ecs::World& EcsWorld() noexcept;
    [[nodiscard]] const kb::ecs::World& EcsWorld() const noexcept;

private:
    [[nodiscard]] SceneObject MakeObject(SceneEntity entity) noexcept;
    [[nodiscard]] bool BelongsToThisScene(SceneObject object) const noexcept;

    kb::ecs::World world_;
    std::unique_ptr<SceneComponentRegistry> components_;
    std::unique_ptr<SceneComponentStorage> componentStorage_;
};

} // namespace kb::scene
