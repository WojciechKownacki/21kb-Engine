#include "EditorTestSupport.hpp"

#include "app/EditorPlayModeSceneSession.hpp"
#include "engine/scene/Scene.hpp"
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

} // namespace

void RunEditorPlayModeSceneSessionTests() {
    RunRestoreRevertsRuntimeMutationTest();
}

} // namespace kb::editor::tests
