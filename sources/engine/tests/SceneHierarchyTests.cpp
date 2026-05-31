#include "TestSupport.hpp"
#include "TestSuites.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneHistory.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include <array>
#include <vector>

namespace {

void RunTransformHierarchyTest() {
    kb::scene::Scene scene;

    kb::scene::SceneObject parent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Parent",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 10.0F, 1.0F, 0.0F },
        },
    });

    kb::scene::SceneObject child = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Child",
        .parent = parent,
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 2.0F, 3.0F, 4.0F },
        },
    });

    [[maybe_unused]] const bool firstProgress = scene.Runtime().Update(0.016F);
    kb::scene::TransformComponent childTransform = scene.Transforms().Get(child);
    kb::tests::Require(kb::tests::NearlyEqual(childTransform.worldPosition.x, 12.0F), "Child world X was not composed with parent");
    kb::tests::Require(kb::tests::NearlyEqual(childTransform.worldPosition.y, 4.0F), "Child world Y was not composed with parent");
    kb::tests::Require(kb::tests::NearlyEqual(childTransform.worldPosition.z, 4.0F), "Child world Z was not composed with parent");
    kb::tests::Require(!childTransform.worldDirty, "Child transform stayed dirty after update");

    kb::scene::TransformComponent parentTransform = scene.Transforms().Get(parent);
    parentTransform.localPosition = kb::scene::Vec3{ 20.0F, 0.0F, 1.0F };
    scene.Transforms().Set(parent, parentTransform);

    [[maybe_unused]] const bool secondProgress = scene.Runtime().Update(0.016F);
    childTransform = scene.Transforms().Get(child);
    kb::tests::Require(kb::tests::NearlyEqual(childTransform.worldPosition.x, 22.0F), "Child world X did not react to parent change");
    kb::tests::Require(kb::tests::NearlyEqual(childTransform.worldPosition.y, 3.0F), "Child world Y did not react to parent change");
    kb::tests::Require(kb::tests::NearlyEqual(childTransform.worldPosition.z, 5.0F), "Child world Z did not react to parent change");

    kb::tests::Require(scene.Hierarchy().SetParent(child, {}), "Child could not detach from parent");
    [[maybe_unused]] const bool thirdProgress = scene.Runtime().Update(0.016F);
    childTransform = scene.Transforms().Get(child);
    kb::tests::Require(kb::tests::NearlyEqual(childTransform.worldPosition.x, 2.0F), "Detached child world X should match local X");
    kb::tests::Require(kb::tests::NearlyEqual(childTransform.worldPosition.y, 3.0F), "Detached child world Y should match local Y");
    kb::tests::Require(kb::tests::NearlyEqual(childTransform.worldPosition.z, 4.0F), "Detached child world Z should match local Z");
}

void RunParentChildrenOwnershipTest() {
    kb::scene::Scene scene;
    kb::tests::Require(scene.Prefabs().RegisteredCount() == 0, "New scene should not start with registered prefabs");

    const kb::scene::SceneObject firstParent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "First Parent" });
    const kb::scene::SceneObject secondParent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Second Parent" });
    const kb::scene::SceneObject child = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Child", .parent = firstParent });
    const kb::scene::SceneObject grandchild = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Grandchild", .parent = child });

    kb::tests::Require(scene.Prefabs().RegisteredCount() == 0, "Plain parent-child scene objects should not register prefabs");

    std::vector<kb::scene::SceneEntity> firstChildren = scene.Hierarchy().ChildEntities(firstParent.Entity());
    kb::tests::Require(firstChildren.size() == 1 && firstChildren[0] == child.Entity(), "Parent did not expose its child after creation");
    std::vector<kb::scene::SceneEntity> childChildren = scene.Hierarchy().ChildEntities(child.Entity());
    kb::tests::Require(childChildren.size() == 1 && childChildren[0] == grandchild.Entity(), "Plain child did not expose its own child");

    kb::tests::Require(scene.Hierarchy().SetParent(child, secondParent), "Child could not be reparented");
    firstChildren = scene.Hierarchy().ChildEntities(firstParent.Entity());
    const std::vector<kb::scene::SceneEntity> secondChildren = scene.Hierarchy().ChildEntities(secondParent.Entity());
    kb::tests::Require(firstChildren.empty(), "Old parent still exposes a reparented child");
    kb::tests::Require(secondChildren.size() == 1 && secondChildren[0] == child.Entity(), "New parent does not expose a reparented child");
    childChildren = scene.Hierarchy().ChildEntities(child.Entity());
    kb::tests::Require(childChildren.size() == 1 && childChildren[0] == grandchild.Entity(), "Reparented child lost its own child");
    kb::tests::Require(scene.Prefabs().RegisteredCount() == 0, "Reparenting plain scene objects should not register prefabs");

    scene.Entities().Destroy(child);
    kb::tests::Require(scene.Hierarchy().ChildEntities(secondParent.Entity()).empty(), "Parent still exposes a destroyed child");
    kb::tests::Require(!scene.Entities().IsAlive(grandchild), "Destroying a child did not destroy its descendant");
    kb::tests::Require(scene.Prefabs().RegisteredCount() == 0, "Destroying plain hierarchy should not register prefabs");
}

void RunHierarchyStableCreationOrderTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject first = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "First" });
    const kb::scene::SceneObject second = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Second" });
    const kb::scene::SceneObject parent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Parent" });
    const kb::scene::SceneObject firstChild = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "First Child", .parent = parent });
    const kb::scene::SceneObject secondChild = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Second Child", .parent = parent });

    const std::vector<kb::scene::SceneEntity> roots = scene.Hierarchy().RootEntities();
    kb::tests::Require(roots.size() == 3, "Hierarchy roots should expose every root object");
    kb::tests::Require(roots[0] == first.Entity(), "First created root should stay at the top of hierarchy roots");
    kb::tests::Require(roots[1] == second.Entity(), "Second created root should stay after the first root");
    kb::tests::Require(roots[2] == parent.Entity(), "Newest root should be listed at the bottom of hierarchy roots");

    const std::vector<kb::scene::SceneEntity> children = scene.Hierarchy().ChildEntities(parent.Entity());
    kb::tests::Require(children.size() == 2, "Hierarchy children should expose every child object");
    kb::tests::Require(children[0] == firstChild.Entity(), "First created child should stay before later siblings");
    kb::tests::Require(children[1] == secondChild.Entity(), "Newest child should be listed at the bottom of siblings");
}

void RunHierarchyCreationOrderSurvivesDeletionTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject first = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "First" });
    const kb::scene::SceneObject second = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Second" });

    scene.Entities().Destroy(first);
    const kb::scene::SceneObject third = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Third" });

    const std::vector<kb::scene::SceneEntity> roots = scene.Hierarchy().RootEntities();
    kb::tests::Require(roots.size() == 2, "Hierarchy roots should expose remaining root objects");
    kb::tests::Require(roots[0] == second.Entity(), "Existing root should stay above newly created root after deletion");
    kb::tests::Require(roots[1] == third.Entity(), "New root should be appended at the bottom after deletion");
}

void RunSceneBatchDuplicateTest() {
    kb::scene::Scene scene;
    kb::scene::SceneObject parent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Parent" });
    kb::scene::SceneObject root = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Root",
        .parent = parent,
        .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 1.0F, 2.0F, 3.0F } },
    });
    kb::scene::SceneObject child = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Child", .parent = root });
    scene.Components().MeshRenderers().Set(root.Entity(), kb::scene::MeshRendererComponent{ .meshAssetId = 7, .materialAssetId = 11 });
    scene.Components().Lights().Set(child.Entity(), kb::scene::LightComponent{ .intensity = 4.0F });

    kb::scene::SceneObject duplicate = scene.Entities().Duplicate(root);
    kb::tests::Require(duplicate.IsValid(), "Duplicate did not create an object");
    kb::tests::Require(scene.Hierarchy().Parent(duplicate.Entity()) == parent.Entity(), "Duplicate did not preserve parent");
    kb::tests::Require(scene.Entities().Name(duplicate) == "Root", "Duplicate did not preserve name");
    kb::tests::Require(scene.Components().MeshRenderers().Has(duplicate.Entity()), "Duplicate did not copy mesh renderer");
    const std::vector<kb::scene::SceneEntity> duplicateChildren = scene.Hierarchy().ChildEntities(duplicate.Entity());
    kb::tests::Require(duplicateChildren.size() == 1, "Duplicate did not copy child hierarchy");
    kb::tests::Require(scene.Components().Lights().Has(duplicateChildren[0]), "Duplicate did not copy child light component");

    kb::scene::SceneObject batchParent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Batch Parent" });
    const std::array<kb::scene::SceneObject, 2> selection{ root, duplicate };
    kb::tests::Require(scene.Entities().SetParent(selection, batchParent), "Batch reparent failed");
    kb::tests::Require(scene.Hierarchy().Parent(root.Entity()) == batchParent.Entity(), "Batch reparent did not move first object");
    kb::tests::Require(scene.Hierarchy().Parent(duplicate.Entity()) == batchParent.Entity(), "Batch reparent did not move duplicate");
    scene.Entities().Destroy(selection);
    kb::tests::Require(!scene.Entities().IsAlive(root), "Batch destroy did not destroy first object");
    kb::tests::Require(!scene.Entities().IsAlive(duplicate), "Batch destroy did not destroy duplicate");
    kb::tests::Require(!scene.Entities().IsAlive(child), "Batch destroy did not destroy child hierarchy");
}

void RunSceneHistoryUndoRedoTest() {
    kb::scene::Scene scene;
    kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Initial" });
    kb::tests::Require(scene.History().Record("before rename"), "Scene history did not record initial snapshot");
    scene.Entities().SetName(object, "Changed");
    kb::tests::Require(scene.History().CanUndo(), "Scene history should be able to undo after record");
    kb::tests::Require(scene.History().Undo(), "Scene history undo failed");
    object = scene.Hierarchy().RootObjects().front();
    kb::tests::Require(scene.Entities().Name(object) == "Initial", "Scene history undo did not restore entity name");
    kb::tests::Require(scene.History().CanRedo(), "Scene history should be able to redo after undo");
    kb::tests::Require(scene.History().Redo(), "Scene history redo failed");
    object = scene.Hierarchy().RootObjects().front();
    kb::tests::Require(scene.Entities().Name(object) == "Changed", "Scene history redo did not restore entity name");

    kb::tests::Require(scene.History().Undo(), "Scene history second undo failed");
    object = scene.Hierarchy().RootObjects().front();
    scene.Entities().SetName(object, "Branch");
    kb::tests::Require(scene.History().Record("branch"), "Scene history branch record failed");
    kb::tests::Require(!scene.History().CanRedo(), "Scene history did not clear redo stack after branch record");
}

} // namespace

namespace kb::tests {

void RunSceneHierarchyTests() {
    RunTransformHierarchyTest();
    RunParentChildrenOwnershipTest();
    RunHierarchyStableCreationOrderTest();
    RunHierarchyCreationOrderSurvivesDeletionTest();
    RunSceneBatchDuplicateTest();
    RunSceneHistoryUndoRedoTest();
}

} // namespace kb::tests
