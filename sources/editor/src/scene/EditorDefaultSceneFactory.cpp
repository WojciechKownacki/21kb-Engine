#include "scene/EditorDefaultSceneFactory.hpp"

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

} // namespace

kb::scene::SceneEntity EditorDefaultSceneFactory::Seed(kb::scene::Scene& scene) {
    const kb::scene::SceneEntity camera = scene.CreateEntity(MakeObjectDesc("Main Camera", { 0.0F, 2.0F, -6.0F }));
    scene.SetCamera(camera, kb::scene::CameraComponent{ .primary = true });

    const kb::scene::SceneEntity light = scene.CreateEntity(MakeObjectDesc("Directional Light", { 0.0F, 3.0F, 0.0F }));
    scene.SetLight(
        light,
        kb::scene::LightComponent{
            .kind = kb::scene::LightKind::Directional,
            .intensity = 2.0F,
        });

    const kb::scene::SceneEntity cube = scene.CreateEntity(MakeObjectDesc("Cube", { 0.0F, 0.0F, 0.0F }));
    scene.SetMeshRenderer(
        cube,
        kb::scene::MeshRendererComponent{
            .meshAssetId = 1,
            .materialAssetId = 1,
        });

    const kb::scene::SceneEntity child = scene.CreateEntity(MakeObjectDesc("Cube Child", { 1.5F, 0.0F, 0.0F }));
    scene.SetMeshRenderer(
        child,
        kb::scene::MeshRendererComponent{
            .meshAssetId = 1,
            .materialAssetId = 2,
        });
    [[maybe_unused]] const bool parented = scene.SetParent(child, cube);

    return camera;
}

} // namespace kb::editor
