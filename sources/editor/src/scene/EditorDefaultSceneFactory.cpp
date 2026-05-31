#include "scene/EditorDefaultSceneFactory.hpp"

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

} // namespace

kb::scene::SceneEntity EditorDefaultSceneFactory::Seed(kb::scene::Scene& scene) {
    const kb::scene::SceneEntity camera = scene.Entities().CreateEntity(MakeObjectDesc("Main Camera", { 0.0F, 2.0F, -6.0F }));
    scene.Components().Cameras().Set(camera, kb::scene::CameraComponent{ .primary = true });

    const kb::scene::SceneEntity light = scene.Entities().CreateEntity(MakeObjectDesc("Directional Light", { 0.0F, 3.0F, 0.0F }));
    scene.Components().Lights().Set(
        light,
        kb::scene::LightComponent{
            .kind = kb::scene::LightKind::Directional,
            .intensity = 2.0F,
        });

    return camera;
}

} // namespace kb::editor
