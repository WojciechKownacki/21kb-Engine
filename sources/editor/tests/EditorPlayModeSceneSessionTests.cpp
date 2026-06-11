#include "EditorTestSupport.hpp"

#include "app/EditorPlayModeSceneSession.hpp"
#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneBehaviourComponents.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
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

    EditorPlayModeSceneSession session;
    Require(session.Begin(scene, "PlayModeSnapshot"), "Play mode scene snapshot was not captured");
    Require(session.Active(), "Play mode scene session should be active after capture");

    scene.Transforms().Set(object, kb::scene::TransformComponent{
        .localPosition = kb::scene::Vec3{ 10.0F, 20.0F, 30.0F },
        .localScale = kb::scene::Vec3{ 2.0F, 2.0F, 2.0F },
    });
    static_cast<void>(scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Runtime Spawn" }));

    Require(session.Restore(scene), "Play mode scene snapshot was not restored");
    Require(!session.Active(), "Play mode scene session should clear after restore");
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

} // namespace

void RunEditorPlayModeSceneSessionTests() {
    RunRestoreRevertsRuntimeMutationTest();
    RunRestorePreservesBehaviourComponentTest();
}

} // namespace kb::editor::tests
