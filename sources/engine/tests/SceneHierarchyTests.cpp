#include "TestSupport.hpp"
#include "TestSuites.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"

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

} // namespace

namespace kb::tests {

void RunSceneHierarchyTests() {
    RunTransformHierarchyTest();
    RunParentChildrenOwnershipTest();
}

} // namespace kb::tests
