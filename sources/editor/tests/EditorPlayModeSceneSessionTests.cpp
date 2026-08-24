#include "EditorTestSupport.hpp"

#include "app/EditorPlayModeSceneSession.hpp"
#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneBehaviourComponents.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"

namespace kb::editor::tests {
namespace {

void RunRestoreRevertsRuntimeMutationTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Runtime Target",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 1.0F, 2.0F, 3.0F },
            .localScale = kb::scene::Vec3{ 1.0F, 1.0F, 1.0F },
        },
    });
    Require(object.IsValid(), "Play mode test object was not created");
    scene.Runtime().SetPlaying(false);

    EditorPlayModeSceneSession session;
    Require(session.Begin(scene, "PlayModeSnapshot"), "Play mode scene snapshot was not captured");
    Require(session.Active(), "Play mode scene session should be active after capture");
    Require(scene.Runtime().IsPlaying(), "Play mode scene session did not enable runtime fixed updates");

    scene.Transforms().Set(object, kb::scene::TransformComponent{
        .localPosition = kb::scene::Vec3{ 10.0F, 20.0F, 30.0F },
        .localScale = kb::scene::Vec3{ 2.0F, 2.0F, 2.0F },
    });
    static_cast<void>(scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Runtime Spawn" }));

    Require(session.Restore(scene), "Play mode scene snapshot was not restored");
    Require(!session.Active(), "Play mode scene session should clear after restore");
    Require(!scene.Runtime().IsPlaying(), "Play mode scene session left runtime fixed updates enabled after stop");
    Require(scene.Entities().Count() == 1U, "Play mode restore did not remove runtime-created entities");

    const kb::scene::TransformComponent restored = scene.Transforms().Get(scene.Hierarchy().RootEntities().front());
    Require(restored.localPosition.x == 1.0F && restored.localPosition.y == 2.0F && restored.localPosition.z == 3.0F,
        "Play mode restore did not revert runtime transform changes");
}

void RunRestorePreservesBehaviourComponentTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Scripted" });
    Require(object.IsValid(), "Play mode scripted object was not created");
    scene.Components().Behaviours().Set(object.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = 123,
        .backend = kb::scene::BehaviourBackend::Lua,
        .enabled = true,
        .tickGroup = kb::scene::BehaviourTickGroup::Gameplay,
        .executionOrder = 7,
    });

    EditorPlayModeSceneSession session;
    Require(session.Begin(scene, "PlayModeBehaviourSnapshot"), "Play mode behaviour snapshot was not captured");
    scene.Components().Behaviours().Remove(object.Entity());

    Require(session.Restore(scene), "Play mode behaviour snapshot was not restored");
    const std::vector<kb::scene::SceneEntity> roots = scene.Hierarchy().RootEntities();
    Require(roots.size() == 1U, "Play mode behaviour restore should keep the original object");
    const kb::scene::BehaviourComponent* behaviour = scene.Components().Behaviours().TryGet(roots.front());
    Require(behaviour != nullptr, "Play mode restore dropped BehaviourComponent");
    Require(behaviour->behaviourAssetId == 123 && behaviour->backend == kb::scene::BehaviourBackend::Lua && behaviour->executionOrder == 7,
        "Play mode restore did not preserve BehaviourComponent fields");
}

void RunRestoreMultipleRootMeshRenderersTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject first = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Cube A" });
    const kb::scene::SceneObject second = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Cube B" });
    Require(first.IsValid() && second.IsValid(), "Play mode multi-root mesh test objects were not created");
    scene.Components().MeshRenderers().Set(first.Entity(), kb::scene::MeshRendererComponent{
        .meshAssetId = 101U,
        .materialAssetId = 201U,
    });
    scene.Components().MeshRenderers().Set(second.Entity(), kb::scene::MeshRendererComponent{
        .meshAssetId = 102U,
        .materialAssetId = 202U,
    });

    EditorPlayModeSceneSession session;
    Require(session.Begin(scene, "PlayModeMultiRootMeshSnapshot"), "Play mode multi-root mesh snapshot was not captured");
    scene.Entities().Destroy(first);
    scene.Entities().Destroy(second);
    Require(scene.Hierarchy().RootEntities().empty(), "Play mode multi-root mesh test did not remove scene roots");

    Require(session.Restore(scene), "Play mode multi-root mesh snapshot was not restored");
    Require(!session.Active(), "Play mode multi-root mesh session should clear after restore");
    const std::vector<kb::scene::SceneEntity> roots = scene.Hierarchy().RootEntities();
    Require(roots.size() == 2U, "Play mode multi-root mesh restore did not restore both roots");
    const kb::scene::MeshRendererComponent* firstRenderer = scene.Components().MeshRenderers().TryGet(roots[0]);
    const kb::scene::MeshRendererComponent* secondRenderer = scene.Components().MeshRenderers().TryGet(roots[1]);
    Require(firstRenderer != nullptr && secondRenderer != nullptr, "Play mode multi-root mesh restore dropped MeshRenderer components");
    Require(firstRenderer->meshAssetId == 101U && firstRenderer->materialAssetId == 201U, "Play mode multi-root mesh restore changed first renderer asset ids");
    Require(secondRenderer->meshAssetId == 102U && secondRenderer->materialAssetId == 202U, "Play mode multi-root mesh restore changed second renderer asset ids");
}

} // namespace

void RunEditorPlayModeSceneSessionTests() {
    RunRestoreRevertsRuntimeMutationTest();
    RunRestorePreservesBehaviourComponentTest();
    RunRestoreMultipleRootMeshRenderersTest();
}

} // namespace kb::editor::tests
