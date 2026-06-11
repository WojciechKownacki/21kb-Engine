#include "scene/EditorDefaultSceneFactory.hpp"

#include "engine/assets/AssetRegistry.hpp"
#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/RigidbodyComponent.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"

namespace kb::editor {
namespace {

[[nodiscard]] kb::scene::SceneObjectDesc MakeObjectDesc(const char* name, kb::scene::Vec3 position) {
    return kb::scene::SceneObjectDesc{
        .name = name,
        .transform = kb::scene::TransformComponent{
            .localPosition = position,
        },
    };
}

[[nodiscard]] std::uint64_t DefaultCubeMeshAssetId(kb::scene::Scene& scene) noexcept {
    const kb::assets::AssetMetadata* cube = scene.Assets().Manager().Registry().FindByPath("/Game/Cube.21kb");
    if (cube == nullptr || cube->type != "RenderMesh") {
        return 0U;
    }
    return cube->id.value;
}

void AttachMeshIfAvailable(kb::scene::Scene& scene, kb::scene::SceneEntity entity, std::uint64_t meshAssetId) {
    if (meshAssetId == 0U) {
        return;
    }
    scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshAssetId,
    });
}

} // namespace

kb::scene::SceneEntity EditorDefaultSceneFactory::Seed(kb::scene::Scene& scene) {
    const kb::scene::SceneEntity camera = scene.Entities().CreateEntity(MakeObjectDesc("Main Camera", { 0.0F, 3.0F, -8.0F }));
    scene.Components().Cameras().Set(camera, kb::scene::CameraComponent{ .primary = true });

    const kb::scene::SceneEntity light = scene.Entities().CreateEntity(MakeObjectDesc("Directional Light", { 0.0F, 3.0F, 0.0F }));
    scene.Components().Lights().Set(
        light,
        kb::scene::LightComponent{
            .kind = kb::scene::LightKind::Directional,
            .intensity = 2.0F,
        });

    const std::uint64_t cubeMeshAssetId = DefaultCubeMeshAssetId(scene);

    const kb::scene::SceneEntity floor = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{
        .name = "Physics Floor",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 0.0F, -0.5F, 0.0F },
            .localScale = kb::scene::Vec3{ 10.0F, 1.0F, 10.0F },
        },
    });
    scene.Components().Rigidbodies().Set(floor, kb::scene::RigidbodyComponent{
        .bodyType = kb::scene::RigidbodyBodyType::Static,
    });
    scene.Components().Colliders().Set(floor, kb::scene::ColliderComponent{
        .shape = kb::scene::ColliderShape::Box,
        .boxSize = kb::scene::Vec3{ 10.0F, 1.0F, 10.0F },
    });
    AttachMeshIfAvailable(scene, floor, cubeMeshAssetId);

    const kb::scene::SceneEntity cube = scene.Entities().CreateEntity(MakeObjectDesc("Falling Cube", { 0.0F, 4.0F, 0.0F }));
    scene.Components().Rigidbodies().Set(cube, kb::scene::RigidbodyComponent{
        .bodyType = kb::scene::RigidbodyBodyType::Dynamic,
        .mass = 1.0F,
    });
    scene.Components().Colliders().Set(cube, kb::scene::ColliderComponent{
        .shape = kb::scene::ColliderShape::Box,
        .boxSize = kb::scene::Vec3{ 1.0F, 1.0F, 1.0F },
    });
    AttachMeshIfAvailable(scene, cube, cubeMeshAssetId);

    return camera;
}

} // namespace kb::editor
