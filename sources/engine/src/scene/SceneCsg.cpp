#include "engine/scene/CsgAsset.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetKind.hpp"
#include "engine/assets/AssetFileName.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneMeshRendererComponents.hpp"
#include "engine/scene/SceneColliderComponents.hpp"
#include "engine/scene/SceneRigidbodyComponents.hpp"
namespace kb::scene {
bool AttachCompiledCsg(Scene& scene,SceneEntity owner,std::string_view meshPath,std::string_view collisionPath,std::string& error) {
    error.clear();if(!scene.Entities().IsAlive(owner)) {error="CSG owner is not alive";return false;}
    auto& assets=scene.Assets().Manager();const auto* render=assets.Registry().FindByPath(kb::assets::Utf8Path(meshPath));const auto* collision=assets.Registry().FindByPath(kb::assets::Utf8Path(collisionPath));
    if(!render||!kb::assets::AssetMatchesKind(*render,kb::assets::AssetKind::Mesh)||!collision||collision->type!=kb::assets::kCollisionMeshAssetType) {error="CSG requires an imported mesh and saved collision mesh";return false;}
    const auto renderPayload=assets.LoadOpaqueHandle(render->id);if(!renderPayload.IsLoaded()) {error=assets.LastError();return false;}
    const auto shape=assets.Load<kb::assets::CollisionMeshAsset>(collision->id);if(!shape.IsLoaded()||!kb::assets::ValidateCollisionMesh(*shape.Shared(),error)) {if(error.empty()) error=assets.LastError();return false;}
    const auto* body=scene.Components().Rigidbodies().TryGet(owner);if(body&&body->bodyType!=RigidbodyBodyType::Static) {error="compiled CSG triangle collision requires a static body";return false;}
    MeshRendererComponent renderer;if(const auto* old=scene.Components().MeshRenderers().TryGet(owner)) renderer=*old;
    if(renderer.transient) {error="compiled CSG cannot replace a system-owned transient renderer";return false;}
    renderer.meshAssetId=render->id.value;
    auto collider=ColliderComponent{};collider.shape=ColliderShape::Mesh;collider.meshAssetId=collision->id.value;
    scene.Components().MeshRenderers().Set(owner,renderer);scene.Components().Colliders().Set(owner,collider);scene.Components().Rigidbodies().Set(owner,RigidbodyComponent{.bodyType=RigidbodyBodyType::Static});return true;
}
}
