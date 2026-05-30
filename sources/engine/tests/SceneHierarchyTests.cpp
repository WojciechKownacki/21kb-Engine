#include "TestSupport.hpp"
#include "TestSuites.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"

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

} // namespace

namespace kb::tests {

void RunSceneHierarchyTests() {
    RunTransformHierarchyTest();
}

} // namespace kb::tests
