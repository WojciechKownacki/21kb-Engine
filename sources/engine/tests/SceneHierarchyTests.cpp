#include "TestSupport.hpp"
#include "TestSuites.hpp"

#include "engine/ecs/World.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAudioListenerAccess.hpp"
#include "engine/scene/SceneAudioMixerAccess.hpp"
#include "engine/scene/SceneAudioOcclusionAccess.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneHistory.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include <array>
#include <span>
#include <vector>

namespace {

struct SceneCameraLightVisitorStats {
    std::size_t cameraCount = 0;
    std::size_t lightCount = 0;
    double checksum = 0.0;
};

struct TransformReplaySnapshot {
    std::vector<kb::scene::TransformComponent> transforms;
    kb::scene::SceneRuntimeHotPathReport report{};
};

struct BehaviourIterationHotQueryStats {
    std::size_t visited = 0U;
    std::uint64_t checksum = 0U;
};

void AccumulateCameraVisit(
    kb::scene::SceneEntity,
    const kb::scene::TransformComponent& transform,
    const kb::scene::CameraComponent& camera,
    void* context) {
    auto& stats = *static_cast<SceneCameraLightVisitorStats*>(context);
    ++stats.cameraCount;
    stats.checksum += static_cast<double>(transform.worldPosition.x);
    stats.checksum += static_cast<double>(camera.verticalFovDegrees);
}

void AccumulateLightVisit(
    kb::scene::SceneEntity,
    const kb::scene::TransformComponent& transform,
    const kb::scene::LightComponent& light,
    void* context) {
    auto& stats = *static_cast<SceneCameraLightVisitorStats*>(context);
    ++stats.lightCount;
    stats.checksum += static_cast<double>(transform.worldPosition.y);
    stats.checksum += static_cast<double>(light.intensity);
}

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

[[nodiscard]] TransformReplaySnapshot RunTransformHierarchyReplay() {
    kb::scene::Scene scene;

    std::vector<kb::scene::SceneObject> objects;
    objects.reserve(5U);
    objects.push_back(scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Replay Root",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 1.0F, 2.0F, 3.0F },
        },
    }));
    objects.push_back(scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Replay Child A",
        .parent = objects[0],
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 4.0F, 0.0F, 0.0F },
            .localScale = kb::scene::Vec3{ 2.0F, 1.0F, 1.0F },
        },
    }));
    objects.push_back(scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Replay Child B",
        .parent = objects[0],
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 0.0F, 5.0F, 0.0F },
        },
    }));
    objects.push_back(scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Replay Grandchild",
        .parent = objects[1],
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 0.0F, 0.0F, 6.0F },
        },
    }));
    objects.push_back(scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Replay Other Root",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 10.0F, 0.0F, 0.0F },
        },
    }));

    scene.Runtime().SynchronizeTransforms();

    kb::scene::TransformComponent rootTransform = scene.Transforms().Get(objects[0]);
    rootTransform.localPosition = kb::scene::Vec3{ 3.0F, 1.0F, 2.0F };
    scene.Transforms().Set(objects[0], rootTransform);
    kb::scene::TransformComponent childATransform = scene.Transforms().Get(objects[1]);
    childATransform.localPosition = kb::scene::Vec3{ -2.0F, 0.0F, 4.0F };
    scene.Transforms().Set(objects[1], childATransform);
    kb::tests::Require(scene.Hierarchy().SetParent(objects[3], objects[2]), "Transform replay could not reparent the grandchild");
    scene.Runtime().SynchronizeTransforms();

    kb::scene::TransformComponent childBTransform = scene.Transforms().Get(objects[2]);
    childBTransform.localPosition = kb::scene::Vec3{ 0.0F, -3.0F, 5.0F };
    scene.Transforms().Set(objects[2], childBTransform);
    kb::tests::Require(scene.Hierarchy().SetParent(objects[1], objects[4]), "Transform replay could not reparent child A");
    scene.Runtime().SynchronizeTransforms();

    TransformReplaySnapshot snapshot;
    snapshot.transforms.reserve(objects.size());
    for (kb::scene::SceneObject object : objects) {
        snapshot.transforms.push_back(scene.Transforms().Get(object));
    }
    snapshot.report = scene.Runtime().HotPathReport();
    return snapshot;
}

void RunTransformHierarchyReplayDeterminismTest() {
    const TransformReplaySnapshot first = RunTransformHierarchyReplay();
    const TransformReplaySnapshot second = RunTransformHierarchyReplay();

    kb::tests::Require(first.transforms.size() == second.transforms.size(), "Transform replay determinism changed transform count");
    for (std::size_t index = 0; index < first.transforms.size(); ++index) {
        const kb::scene::TransformComponent& lhs = first.transforms[index];
        const kb::scene::TransformComponent& rhs = second.transforms[index];
        kb::tests::Require(kb::tests::NearlyEqual(lhs.worldPosition.x, rhs.worldPosition.x), "Transform replay determinism changed world position X");
        kb::tests::Require(kb::tests::NearlyEqual(lhs.worldPosition.y, rhs.worldPosition.y), "Transform replay determinism changed world position Y");
        kb::tests::Require(kb::tests::NearlyEqual(lhs.worldPosition.z, rhs.worldPosition.z), "Transform replay determinism changed world position Z");
        kb::tests::Require(kb::tests::NearlyEqual(lhs.worldScale.x, rhs.worldScale.x), "Transform replay determinism changed world scale X");
        kb::tests::Require(kb::tests::NearlyEqual(lhs.worldScale.y, rhs.worldScale.y), "Transform replay determinism changed world scale Y");
        kb::tests::Require(kb::tests::NearlyEqual(lhs.worldScale.z, rhs.worldScale.z), "Transform replay determinism changed world scale Z");
        kb::tests::Require(lhs.localVersion == rhs.localVersion, "Transform replay determinism changed local version");
        kb::tests::Require(lhs.parentVersion == rhs.parentVersion, "Transform replay determinism changed parent version");
        kb::tests::Require(lhs.worldVersion == rhs.worldVersion, "Transform replay determinism changed world version");
        kb::tests::Require(!lhs.worldDirty && !rhs.worldDirty, "Transform replay left dirty world transforms after synchronization");
    }

    kb::tests::Require(first.report.transformHierarchyUsesBatchPath && second.report.transformHierarchyUsesBatchPath, "Transform replay determinism did not use batch path");
    kb::tests::Require(first.report.transformHierarchyUpdatedCount == second.report.transformHierarchyUpdatedCount, "Transform replay determinism changed updated count");
    kb::tests::Require(first.report.transformHierarchyInspectedCount == second.report.transformHierarchyInspectedCount, "Transform replay determinism changed inspected count");
    kb::tests::Require(first.report.transformTopologicalBatchCount == second.report.transformTopologicalBatchCount, "Transform replay determinism changed topological batch count");
    kb::tests::Require(first.report.transformTopologicalBatchBuildCount == second.report.transformTopologicalBatchBuildCount, "Transform replay determinism changed topological build count");
}

void RunTransformRootFastPathReportTest() {
    kb::scene::Scene scene;

    const kb::scene::SceneObject firstRoot = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Fast Root A",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 4.0F, 1.0F, 0.0F },
        },
    });
    const kb::scene::SceneObject secondRoot = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Fast Root B",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ -2.0F, 3.0F, 1.0F },
        },
    });
    const kb::scene::SceneObject child = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Fast Child",
        .parent = firstRoot,
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 1.0F, 2.0F, 3.0F },
        },
    });
    const kb::scene::SceneObject scaledRoot = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Scaled Root",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 10.0F, 0.0F, 0.0F },
            .localScale = kb::scene::Vec3{ 2.0F, 3.0F, 4.0F },
        },
    });
    const kb::scene::SceneObject scaledChild = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Scaled Child",
        .parent = scaledRoot,
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 2.0F, 2.0F, 2.0F },
            .localScale = kb::scene::Vec3{ 0.5F, 2.0F, 1.5F },
        },
    });
    const kb::scene::Quat quarterTurnZ{ 0.0F, 0.0F, 0.70710677F, 0.70710677F };
    const kb::scene::SceneObject unitScaleRotatedRoot = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Unit Scale Rotated Root",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 20.0F, 0.0F, 0.0F },
            .localRotation = quarterTurnZ,
        },
    });
    const kb::scene::SceneObject unitScaleRotatedChild = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Unit Scale Rotated Child",
        .parent = unitScaleRotatedRoot,
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 2.0F, 0.0F, 0.0F },
            .localRotation = quarterTurnZ,
            .localScale = kb::scene::Vec3{ 1.5F, 2.0F, 2.5F },
        },
    });
    const kb::scene::SceneObject uniformScaleRotatedRoot = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Uniform Scale Rotated Root",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 30.0F, 0.0F, 0.0F },
            .localRotation = quarterTurnZ,
            .localScale = kb::scene::Vec3{ 3.0F, 3.0F, 3.0F },
        },
    });
    const kb::scene::SceneObject uniformScaleRotatedChild = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Uniform Scale Rotated Child",
        .parent = uniformScaleRotatedRoot,
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 1.0F, 0.0F, 0.0F },
            .localRotation = quarterTurnZ,
            .localScale = kb::scene::Vec3{ 2.0F, 1.0F, 0.5F },
        },
    });
    const kb::scene::SceneObject staticRotationParent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Static Rotation Parent",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 40.0F, 0.0F, 0.0F },
            .localRotation = quarterTurnZ,
            .localScale = kb::scene::Vec3{ 2.0F, 3.0F, 4.0F },
        },
    });
    const kb::scene::SceneObject staticRotationChild = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Static Rotation Child",
        .parent = staticRotationParent,
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 1.0F, 1.0F, 1.0F },
            .localScale = kb::scene::Vec3{ 0.5F, 0.25F, 2.0F },
        },
    });
    scene.Transforms().Set(firstRoot, scene.Transforms().Get(firstRoot));
    scene.Transforms().Set(secondRoot, scene.Transforms().Get(secondRoot));
    scene.Transforms().Set(scaledRoot, scene.Transforms().Get(scaledRoot));
    scene.Transforms().Set(unitScaleRotatedRoot, scene.Transforms().Get(unitScaleRotatedRoot));
    scene.Transforms().Set(uniformScaleRotatedRoot, scene.Transforms().Get(uniformScaleRotatedRoot));
    scene.Transforms().Set(staticRotationParent, scene.Transforms().Get(staticRotationParent));

    scene.Runtime().SynchronizeTransforms();
    const kb::scene::SceneRuntimeHotPathReport firstReport = scene.Runtime().HotPathReport();
    kb::tests::Require(firstReport.transformHierarchyInspectedCount == 11U, "Transform root fast path report did not inspect every transform");
    kb::tests::Require(firstReport.transformHierarchyRootFastPathCount == 3U, "Transform root fast path did not isolate identity-rotation roots");
    kb::tests::Require(firstReport.transformHierarchyTranslatedParentFastPathCount == 1U, "Transform translated-parent fast path did not process the child");
    kb::tests::Require(firstReport.transformHierarchyUnrotatedParentFastPathCount == 1U, "Transform unrotated-parent fast path did not process the scaled child");
    kb::tests::Require(firstReport.transformHierarchyUnitScaleParentFastPathCount == 1U, "Transform unit-scale parent fast path did not process the rotated child");
    kb::tests::Require(firstReport.transformHierarchyUniformScaleParentFastPathCount == 1U, "Transform uniform-scale parent fast path did not process the rotated child");
    kb::tests::Require(firstReport.transformHierarchyStaticLocalRotationFastPathCount == 1U, "Transform static local rotation fast path did not process the child");
    kb::tests::Require(firstReport.transformHierarchyUpdatedCount >= firstReport.transformHierarchyRootFastPathCount, "Transform root fast path report counted more root updates than total updates");
    const kb::scene::TransformComponent secondRootTransform = scene.Transforms().Get(secondRoot);
    kb::tests::Require(kb::tests::NearlyEqual(secondRootTransform.worldPosition.x, -2.0F), "Transform root fast path did not copy local X to world X");
    kb::tests::Require(kb::tests::NearlyEqual(secondRootTransform.worldPosition.y, 3.0F), "Transform root fast path did not copy local Y to world Y");
    kb::tests::Require(kb::tests::NearlyEqual(secondRootTransform.worldPosition.z, 1.0F), "Transform root fast path did not copy local Z to world Z");
    const kb::scene::TransformComponent childTransform = scene.Transforms().Get(child);
    kb::tests::Require(kb::tests::NearlyEqual(childTransform.worldPosition.x, 5.0F), "Transform translated-parent fast path did not compose child X");
    kb::tests::Require(kb::tests::NearlyEqual(childTransform.worldPosition.y, 3.0F), "Transform translated-parent fast path did not compose child Y");
    kb::tests::Require(kb::tests::NearlyEqual(childTransform.worldPosition.z, 3.0F), "Transform translated-parent fast path did not compose child Z");
    const kb::scene::TransformComponent scaledChildTransform = scene.Transforms().Get(scaledChild);
    kb::tests::Require(kb::tests::NearlyEqual(scaledChildTransform.worldPosition.x, 14.0F), "Transform unrotated-parent fast path did not compose scaled child X");
    kb::tests::Require(kb::tests::NearlyEqual(scaledChildTransform.worldPosition.y, 6.0F), "Transform unrotated-parent fast path did not compose scaled child Y");
    kb::tests::Require(kb::tests::NearlyEqual(scaledChildTransform.worldPosition.z, 8.0F), "Transform unrotated-parent fast path did not compose scaled child Z");
    kb::tests::Require(kb::tests::NearlyEqual(scaledChildTransform.worldScale.x, 1.0F), "Transform unrotated-parent fast path did not compose scaled child scale X");
    kb::tests::Require(kb::tests::NearlyEqual(scaledChildTransform.worldScale.y, 6.0F), "Transform unrotated-parent fast path did not compose scaled child scale Y");
    kb::tests::Require(kb::tests::NearlyEqual(scaledChildTransform.worldScale.z, 6.0F), "Transform unrotated-parent fast path did not compose scaled child scale Z");
    const kb::scene::TransformComponent unitScaleChildTransform = scene.Transforms().Get(unitScaleRotatedChild);
    kb::tests::Require(kb::tests::NearlyEqual(unitScaleChildTransform.worldPosition.x, 20.0F), "Transform unit-scale parent fast path did not rotate child X");
    kb::tests::Require(kb::tests::NearlyEqual(unitScaleChildTransform.worldPosition.y, 2.0F), "Transform unit-scale parent fast path did not rotate child Y");
    kb::tests::Require(kb::tests::NearlyEqual(unitScaleChildTransform.worldScale.x, 1.5F), "Transform unit-scale parent fast path did not preserve local scale X");
    kb::tests::Require(kb::tests::NearlyEqual(unitScaleChildTransform.worldScale.y, 2.0F), "Transform unit-scale parent fast path did not preserve local scale Y");
    kb::tests::Require(kb::tests::NearlyEqual(unitScaleChildTransform.worldScale.z, 2.5F), "Transform unit-scale parent fast path did not preserve local scale Z");
    const kb::scene::TransformComponent uniformScaleChildTransform = scene.Transforms().Get(uniformScaleRotatedChild);
    kb::tests::Require(kb::tests::NearlyEqual(uniformScaleChildTransform.worldPosition.x, 30.0F), "Transform uniform-scale parent fast path did not rotate child X");
    kb::tests::Require(kb::tests::NearlyEqual(uniformScaleChildTransform.worldPosition.y, 3.0F), "Transform uniform-scale parent fast path did not rotate scaled child Y");
    kb::tests::Require(kb::tests::NearlyEqual(uniformScaleChildTransform.worldScale.x, 6.0F), "Transform uniform-scale parent fast path did not compose scale X");
    kb::tests::Require(kb::tests::NearlyEqual(uniformScaleChildTransform.worldScale.y, 3.0F), "Transform uniform-scale parent fast path did not compose scale Y");
    kb::tests::Require(kb::tests::NearlyEqual(uniformScaleChildTransform.worldScale.z, 1.5F), "Transform uniform-scale parent fast path did not compose scale Z");
    const kb::scene::TransformComponent staticRotationChildTransform = scene.Transforms().Get(staticRotationChild);
    kb::tests::Require(kb::tests::NearlyEqual(staticRotationChildTransform.worldPosition.x, 37.0F), "Transform static local rotation fast path did not rotate scaled child X");
    kb::tests::Require(kb::tests::NearlyEqual(staticRotationChildTransform.worldPosition.y, 2.0F), "Transform static local rotation fast path did not rotate scaled child Y");
    kb::tests::Require(kb::tests::NearlyEqual(staticRotationChildTransform.worldPosition.z, 4.0F), "Transform static local rotation fast path did not scale child Z");
    kb::tests::Require(kb::tests::NearlyEqual(staticRotationChildTransform.worldScale.x, 1.0F), "Transform static local rotation fast path did not compose scale X");
    kb::tests::Require(kb::tests::NearlyEqual(staticRotationChildTransform.worldScale.y, 0.75F), "Transform static local rotation fast path did not compose scale Y");
    kb::tests::Require(kb::tests::NearlyEqual(staticRotationChildTransform.worldScale.z, 8.0F), "Transform static local rotation fast path did not compose scale Z");

    scene.Runtime().SynchronizeTransforms();
    const kb::scene::SceneRuntimeHotPathReport secondReport = scene.Runtime().HotPathReport();
    kb::tests::Require(secondReport.transformHierarchyInspectedCount == 0U, "Clean transform hierarchy should not inspect clean transforms");
    kb::tests::Require(secondReport.transformHierarchyUpdatedCount == 0U, "Clean transform hierarchy reported unexpected updates");
    kb::tests::Require(secondReport.transformHierarchyRootFastPathCount == 0U, "Clean transform hierarchy reported unexpected root fast path updates");
    kb::tests::Require(secondReport.transformHierarchyTranslatedParentFastPathCount == 0U, "Clean transform hierarchy reported unexpected translated-parent fast path updates");
    kb::tests::Require(secondReport.transformHierarchyUnrotatedParentFastPathCount == 0U, "Clean transform hierarchy reported unexpected unrotated-parent fast path updates");
    kb::tests::Require(secondReport.transformHierarchyUnitScaleParentFastPathCount == 0U, "Clean transform hierarchy reported unexpected unit-scale parent fast path updates");
    kb::tests::Require(secondReport.transformHierarchyUniformScaleParentFastPathCount == 0U, "Clean transform hierarchy reported unexpected uniform-scale parent fast path updates");
    kb::tests::Require(secondReport.transformHierarchyStaticLocalRotationFastPathCount == 0U, "Clean transform hierarchy reported unexpected static local rotation fast path updates");
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

void RunHierarchyNoOpParentingKeepsSiblingOrderTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject parent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Parent" });
    const kb::scene::SceneObject firstChild = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "First Child", .parent = parent });
    const kb::scene::SceneObject secondChild = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Second Child", .parent = parent });

    kb::tests::Require(!scene.Hierarchy().SetParent(firstChild, parent), "Reparenting to the current parent should be reported as a no-op");

    const std::vector<kb::scene::SceneEntity> children = scene.Hierarchy().ChildEntities(parent.Entity());
    kb::tests::Require(children.size() == 2, "No-op reparent should not change child count");
    kb::tests::Require(children[0] == firstChild.Entity(), "No-op reparent should keep first child before later siblings");
    kb::tests::Require(children[1] == secondChild.Entity(), "No-op reparent should keep second child after earlier siblings");
}

void RunTransformTopologicalBatchMassParentingTest() {
    kb::scene::Scene scene;
    constexpr std::size_t kRootCount = 160U;

    std::vector<kb::scene::SceneObject> roots;
    std::vector<kb::scene::SceneObject> children;
    roots.reserve(kRootCount);
    children.reserve(kRootCount);

    for (std::size_t index = 0; index < kRootCount; ++index) {
        kb::scene::SceneObject root = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
            .name = "Root",
            .transform = kb::scene::TransformComponent{
                .localPosition = kb::scene::Vec3{ static_cast<float>(index), 0.0F, 0.0F },
            },
        });
        kb::scene::SceneObject child = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
            .name = "Child",
            .parent = root,
            .transform = kb::scene::TransformComponent{
                .localPosition = kb::scene::Vec3{ 0.0F, 1.0F, 0.0F },
            },
        });
        roots.push_back(root);
        children.push_back(child);
    }

    static_cast<void>(scene.Runtime().Update(0.016F));
    kb::scene::TransformComponent childSevenTransform = scene.Transforms().Get(children[7]);
    kb::tests::Require(kb::tests::NearlyEqual(childSevenTransform.worldPosition.x, 7.0F), "Topological transform batch did not compose child X");
    kb::tests::Require(kb::tests::NearlyEqual(childSevenTransform.worldPosition.y, 1.0F), "Topological transform batch did not compose child Y");
    kb::tests::Require(childSevenTransform.localVersion == 1U, "Initial transform sync should not change local version");
    kb::tests::Require(childSevenTransform.parentVersion == scene.Transforms().Get(roots[7]).worldVersion, "Transform parent version did not observe parent world version");
    kb::tests::Require(childSevenTransform.worldVersion > 0U, "Transform world version was not initialized by world sync");

    kb::scene::SceneObject batchParent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Batch Parent",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 100.0F, 0.0F, 0.0F },
        },
    });
    for (std::size_t index = 0; index < kRootCount / 2U; ++index) {
        kb::tests::Require(scene.Hierarchy().SetParent(roots[index], batchParent), "Mass reparent failed");
    }

    static_cast<void>(scene.Runtime().Update(0.016F));
    const std::span<const kb::scene::SceneEntity> dirtyRenderEntities = scene.Runtime().TransformRenderProxyUpdateEntities();
    kb::tests::Require(dirtyRenderEntities.size() >= kRootCount + 1U, "Transform runtime did not cache dirty render proxy update candidates after mass reparent");
    childSevenTransform = scene.Transforms().Get(children[7]);
    kb::tests::Require(kb::tests::NearlyEqual(childSevenTransform.worldPosition.x, 107.0F), "Mass reparent did not propagate parent dirty transform");
    kb::tests::Require(kb::tests::NearlyEqual(childSevenTransform.worldPosition.y, 1.0F), "Mass reparent changed child local Y unexpectedly");
    kb::tests::Require(childSevenTransform.localVersion == 1U, "Reparent should not increment local transform version");
    kb::tests::Require(childSevenTransform.parentVersion == scene.Transforms().Get(roots[7]).worldVersion, "Reparent did not refresh observed parent version");
    kb::tests::Require(kb::tests::NearlyEqual(scene.Transforms().Get(children[120]).worldPosition.x, 120.0F), "Independent subtree was dirtied by unrelated reparent");
}

void RunTransformTopologicalBatchCacheInvalidationTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject parent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Cache Parent",
        .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 4.0F, 0.0F, 0.0F } },
    });
    const kb::scene::SceneObject child = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Cache Child",
        .parent = parent,
        .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 2.0F, 0.0F, 0.0F } },
    });

    static_cast<void>(scene.Runtime().Update(0.016F));
    const kb::scene::SceneRuntimeHotPathReport firstReport = scene.Runtime().HotPathReport();
    kb::tests::Require(firstReport.transformTopologicalBatchCount == 2U, "Transform topological cache did not build root and child batches");
    kb::tests::Require(firstReport.transformTopologicalBatchBuildCount >= 1U, "Transform topological cache did not report the initial build");

    static_cast<void>(scene.Runtime().Update(0.016F));
    const kb::scene::SceneRuntimeHotPathReport secondReport = scene.Runtime().HotPathReport();
    kb::tests::Require(
        secondReport.transformTopologicalBatchBuildCount == firstReport.transformTopologicalBatchBuildCount,
        "Transform topological cache rebuilt without a hierarchy change");

    kb::tests::Require(scene.Hierarchy().SetParent(child, {}), "Transform topological cache test could not detach child");
    static_cast<void>(scene.Runtime().Update(0.016F));
    const kb::scene::SceneRuntimeHotPathReport detachedReport = scene.Runtime().HotPathReport();
    kb::tests::Require(
        detachedReport.transformTopologicalBatchBuildCount == secondReport.transformTopologicalBatchBuildCount + 1U,
        "Transform topological cache did not rebuild after hierarchy change");
    kb::tests::Require(detachedReport.transformTopologicalBatchCount == 1U, "Transform topological cache kept stale child level after detach");

    const kb::scene::TransformComponent childTransform = scene.Transforms().Get(child);
    kb::tests::Require(kb::tests::NearlyEqual(childTransform.worldPosition.x, 2.0F), "Transform topological cache invalidation did not update detached child world position");
}

void RunTransformPropagationBudgetTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject root = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Budget Root",
        .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 10.0F, 0.0F, 0.0F } },
    });
    const kb::scene::SceneObject child = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Budget Child",
        .parent = root,
        .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 2.0F, 0.0F, 0.0F } },
    });
    const kb::scene::SceneObject grandchild = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Budget Grandchild",
        .parent = child,
        .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 3.0F, 0.0F, 0.0F } },
    });
    scene.Runtime().SetTransformPropagationBudget(kb::scene::SceneTransformPropagationBudget{ .maxInspectedEntitiesPerSync = 1U });
    kb::tests::Require(
        scene.Runtime().TransformPropagationBudget().maxInspectedEntitiesPerSync == 1U,
        "Transform propagation budget getter returned an invalid limit");

    scene.Runtime().SynchronizeTransforms();
    kb::scene::SceneRuntimeHotPathReport report = scene.Runtime().HotPathReport();
    kb::tests::Require(report.transformHierarchyBudgetLimit == 1U, "Transform propagation report lost the configured budget");
    kb::tests::Require(report.transformHierarchyBudgetExhausted, "Transform propagation budget did not pause after the first level");
    kb::tests::Require(report.transformHierarchyInspectedCount == 1U, "Transform propagation budget inspected more than one root level entity");
    kb::tests::Require(kb::tests::NearlyEqual(scene.Transforms().Get(root).worldPosition.x, 10.0F), "Budgeted transform propagation did not update the root");
    kb::tests::Require(scene.Transforms().Get(child).worldDirty, "Budgeted transform propagation updated the child before its budget slice");
    kb::tests::Require(!kb::tests::NearlyEqual(scene.Transforms().Get(child).worldPosition.x, 12.0F), "Budgeted transform propagation composed the child before its budget slice");

    scene.Runtime().SynchronizeTransforms();
    report = scene.Runtime().HotPathReport();
    kb::tests::Require(report.transformHierarchyBudgetExhausted, "Transform propagation budget did not pause after the child level");
    kb::tests::Require(report.transformHierarchyInspectedCount == 1U, "Transform propagation budget inspected more than one child level entity");
    kb::tests::Require(kb::tests::NearlyEqual(scene.Transforms().Get(child).worldPosition.x, 12.0F), "Budgeted transform propagation did not update the child");
    kb::tests::Require(scene.Transforms().Get(grandchild).worldDirty, "Budgeted transform propagation updated the grandchild before its budget slice");
    kb::tests::Require(!kb::tests::NearlyEqual(scene.Transforms().Get(grandchild).worldPosition.x, 15.0F), "Budgeted transform propagation composed the grandchild before its budget slice");

    scene.Runtime().SynchronizeTransforms();
    report = scene.Runtime().HotPathReport();
    kb::tests::Require(!report.transformHierarchyBudgetExhausted, "Transform propagation budget reported exhaustion after completing the chain");
    kb::tests::Require(report.transformHierarchyInspectedCount == 1U, "Transform propagation budget inspected an unexpected grandchild count");
    kb::tests::Require(kb::tests::NearlyEqual(scene.Transforms().Get(grandchild).worldPosition.x, 15.0F), "Budgeted transform propagation did not update the grandchild");

    scene.Runtime().SetTransformPropagationBudget(kb::scene::SceneTransformPropagationBudget{});
    scene.Transforms().Set(root, scene.Transforms().Get(root));
    scene.Runtime().SynchronizeTransforms();
    report = scene.Runtime().HotPathReport();
    kb::tests::Require(!report.transformHierarchyBudgetExhausted, "Unlimited transform propagation reported budget exhaustion");
    kb::tests::Require(report.transformHierarchyInspectedCount == 3U, "Unlimited transform propagation did not inspect the full chain");
}

void RunTransformSparseFlushReportTest() {
    kb::scene::Scene scene;
    std::vector<kb::scene::SceneObject> roots;
    roots.reserve(80U);
    for (std::size_t index = 0; index < 80U; ++index) {
        roots.push_back(scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
            .name = "Sparse Flush Root",
            .transform = kb::scene::TransformComponent{
                .localPosition = kb::scene::Vec3{ static_cast<float>(index), 0.0F, 0.0F },
            },
        }));
    }
    [[maybe_unused]] const kb::scene::SceneObject hierarchySentinel = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Sparse Flush Sentinel",
        .parent = roots.back(),
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 1.0F, 0.0F, 0.0F },
        },
    });

    scene.Runtime().SynchronizeTransforms();
    kb::scene::TransformComponent moved = scene.Transforms().Get(roots.front());
    moved.localPosition.x = 42.0F;
    scene.Transforms().Set(roots.front(), moved);
    scene.Runtime().SynchronizeTransforms();

    const kb::scene::SceneRuntimeHotPathReport report = scene.Runtime().HotPathReport();
    kb::tests::Require(report.transformHierarchySparseFlushCount == 1U, "Transform runtime did not report sparse dirty flush");
    kb::tests::Require(report.transformHierarchyBatchFlushCount == 0U, "Transform runtime used a batch flush for sparse dirty roots");
    kb::tests::Require(report.transformHierarchyFlushedEntityCount == 1U, "Transform runtime reported an unexpected sparse flush entity count");
    kb::tests::Require(report.transformHierarchyDirtyFrontierCount == 1U, "Transform runtime did not use the dirty frontier queue");
    kb::tests::Require(report.transformHierarchyUpdatedCount == 1U, "Transform runtime updated more than the moved sparse root");
    kb::tests::Require(report.transformHierarchyInspectedCount == 1U, "Transform runtime inspected clean roots during sparse flush");
    kb::tests::Require(kb::tests::NearlyEqual(scene.Transforms().Get(roots.front()).worldPosition.x, 42.0F), "Sparse flush did not write the moved root");
}

void RunTransformHierarchyDirtyFrontierReportTest() {
    kb::scene::Scene scene;
    std::vector<kb::scene::SceneObject> roots;
    std::vector<kb::scene::SceneObject> children;
    roots.reserve(64U);
    children.reserve(64U);
    for (std::size_t index = 0; index < 64U; ++index) {
        roots.push_back(scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
            .name = "Frontier Root",
            .transform = kb::scene::TransformComponent{
                .localPosition = kb::scene::Vec3{ static_cast<float>(index), 0.0F, 0.0F },
            },
        }));
        children.push_back(scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
            .name = "Frontier Child",
            .parent = roots.back(),
            .transform = kb::scene::TransformComponent{
                .localPosition = kb::scene::Vec3{ 1.0F, 0.0F, 0.0F },
            },
        }));
    }

    scene.Runtime().SynchronizeTransforms();
    kb::scene::TransformComponent moved = scene.Transforms().Get(roots.front());
    moved.localPosition.x = 24.0F;
    scene.Transforms().Set(roots.front(), moved);
    scene.Runtime().SynchronizeTransforms();

    const kb::scene::SceneRuntimeHotPathReport report = scene.Runtime().HotPathReport();
    kb::tests::Require(report.transformHierarchyDirtyFrontierCount == 2U, "Transform hierarchy did not propagate root dirty frontier to the child level");
    kb::tests::Require(report.transformHierarchyInspectedCount == 2U, "Transform hierarchy dirty frontier inspected unrelated clean branches");
    kb::tests::Require(report.transformHierarchyUpdatedCount == 2U, "Transform hierarchy dirty frontier did not update root and child");
    kb::tests::Require(report.transformHierarchySparseFlushCount == 1U, "Transform hierarchy dirty frontier did not use sparse flush");
    kb::tests::Require(report.transformHierarchyFlushedEntityCount == 2U, "Transform hierarchy dirty frontier flushed an unexpected entity count");
    kb::tests::Require(kb::tests::NearlyEqual(scene.Transforms().Get(children.front()).worldPosition.x, 25.0F), "Transform hierarchy dirty frontier did not update the child world position");
}

void RunTransformHierarchyDeepDirtyFrontierReportTest() {
    kb::scene::Scene scene;
    std::vector<kb::scene::SceneObject> roots;
    std::vector<kb::scene::SceneObject> leaves;
    roots.reserve(128U);
    leaves.reserve(128U);
    for (std::size_t chain = 0; chain < 128U; ++chain) {
        kb::scene::SceneObject parent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
            .name = "Deep Frontier Root",
            .transform = kb::scene::TransformComponent{
                .localPosition = kb::scene::Vec3{ static_cast<float>(chain), 0.0F, 0.0F },
            },
        });
        roots.push_back(parent);
        for (std::size_t depth = 1; depth < 8U; ++depth) {
            parent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
                .name = "Deep Frontier Child",
                .parent = parent,
                .transform = kb::scene::TransformComponent{
                    .localPosition = kb::scene::Vec3{ 1.0F, 0.0F, 0.0F },
                },
            });
        }
        leaves.push_back(parent);
    }

    scene.Runtime().SynchronizeTransforms();
    kb::scene::TransformComponent moved = scene.Transforms().Get(roots.front());
    moved.localPosition.x = 10.0F;
    scene.Transforms().Set(roots.front(), moved);
    scene.Runtime().SynchronizeTransforms();

    const kb::scene::SceneRuntimeHotPathReport report = scene.Runtime().HotPathReport();
    kb::tests::Require(report.transformHierarchyDirtyFrontierCount == 8U, "Deep transform frontier did not visit exactly one dirty chain");
    kb::tests::Require(report.transformHierarchyInspectedCount == 8U, "Deep transform frontier inspected unrelated clean chains");
    kb::tests::Require(report.transformHierarchyUpdatedCount == 8U, "Deep transform frontier did not update the full dirty chain");
    kb::tests::Require(report.transformHierarchySparseFlushCount == 1U, "Deep transform frontier did not use sparse flush");
    kb::tests::Require(report.transformHierarchyFlushedEntityCount == 8U, "Deep transform frontier flushed an unexpected entity count");
    kb::tests::Require(kb::tests::NearlyEqual(scene.Transforms().Get(leaves.front()).worldPosition.x, 17.0F), "Deep transform frontier did not update the leaf world position");
}

void RunTransformHierarchyNestedDirtyFrontierReportTest() {
    kb::scene::Scene scene;
    std::vector<kb::scene::SceneObject> dirtyChain;
    dirtyChain.reserve(8U);
    for (std::size_t chain = 0; chain < 16U; ++chain) {
        kb::scene::SceneObject parent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
            .name = "Nested Frontier Root",
            .transform = kb::scene::TransformComponent{
                .localPosition = kb::scene::Vec3{ static_cast<float>(chain), 0.0F, 0.0F },
            },
        });
        if (chain == 0U) {
            dirtyChain.push_back(parent);
        }
        for (std::size_t depth = 1; depth < 8U; ++depth) {
            parent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
                .name = "Nested Frontier Child",
                .parent = parent,
                .transform = kb::scene::TransformComponent{
                    .localPosition = kb::scene::Vec3{ 1.0F, 0.0F, 0.0F },
                },
            });
            if (chain == 0U) {
                dirtyChain.push_back(parent);
            }
        }
    }

    scene.Runtime().SynchronizeTransforms();
    kb::scene::TransformComponent moved = scene.Transforms().Get(dirtyChain[3]);
    moved.localPosition.x = 20.0F;
    scene.Transforms().Set(dirtyChain[3], moved);
    scene.Runtime().SynchronizeTransforms();

    const kb::scene::SceneRuntimeHotPathReport report = scene.Runtime().HotPathReport();
    kb::tests::Require(report.transformHierarchyDirtyFrontierCount == 5U, "Nested transform frontier did not visit exactly the dirty subtree");
    kb::tests::Require(report.transformHierarchyInspectedCount == 5U, "Nested transform frontier inspected clean ancestors or unrelated chains");
    kb::tests::Require(report.transformHierarchyUpdatedCount == 5U, "Nested transform frontier did not update the full dirty subtree");
    kb::tests::Require(report.transformHierarchySparseFlushCount == 1U, "Nested transform frontier did not use sparse flush");
    kb::tests::Require(report.transformHierarchyFlushedEntityCount == 5U, "Nested transform frontier flushed an unexpected entity count");
    kb::tests::Require(kb::tests::NearlyEqual(scene.Transforms().Get(dirtyChain.back()).worldPosition.x, 26.0F), "Nested transform frontier did not update the leaf world position");
}

void RunTransformHierarchyDirtyFrontierDuplicateSetTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject root = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Duplicate Frontier Root",
        .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 1.0F, 0.0F, 0.0F } },
    });
    const kb::scene::SceneObject child = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Duplicate Frontier Child",
        .parent = root,
        .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 2.0F, 0.0F, 0.0F } },
    });

    scene.Runtime().SynchronizeTransforms();
    kb::scene::TransformComponent moved = scene.Transforms().Get(root);
    moved.localPosition.x = 11.0F;
    scene.Transforms().Set(root, moved);
    moved.localPosition.x = 12.0F;
    scene.Transforms().Set(root, moved);
    scene.Runtime().SynchronizeTransforms();

    const kb::scene::SceneRuntimeHotPathReport report = scene.Runtime().HotPathReport();
    kb::tests::Require(report.transformHierarchyDirtyFrontierCount == 2U, "Duplicate transform frontier set added redundant dirty entries");
    kb::tests::Require(report.transformHierarchyUpdatedCount == 2U, "Duplicate transform frontier set did not update the root and child");
    kb::tests::Require(kb::tests::NearlyEqual(scene.Transforms().Get(child).worldPosition.x, 14.0F), "Duplicate transform frontier set did not use the latest transform value");
}

void RunTransformHierarchyMultiRootDirtyFrontierReportTest() {
    kb::scene::Scene scene;
    std::vector<kb::scene::SceneObject> roots;
    std::vector<kb::scene::SceneObject> leaves;
    roots.reserve(128U);
    leaves.reserve(128U);
    for (std::size_t chain = 0; chain < 128U; ++chain) {
        kb::scene::SceneObject parent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
            .name = "Multi Frontier Root",
            .transform = kb::scene::TransformComponent{
                .localPosition = kb::scene::Vec3{ static_cast<float>(chain), 0.0F, 0.0F },
            },
        });
        roots.push_back(parent);
        for (std::size_t depth = 1; depth < 8U; ++depth) {
            parent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
                .name = "Multi Frontier Child",
                .parent = parent,
                .transform = kb::scene::TransformComponent{
                    .localPosition = kb::scene::Vec3{ 1.0F, 0.0F, 0.0F },
                },
            });
        }
        leaves.push_back(parent);
    }

    scene.Runtime().SynchronizeTransforms();
    for (std::size_t index = 0; index < 4U; ++index) {
        kb::scene::TransformComponent moved = scene.Transforms().Get(roots[index]);
        moved.localPosition.x = 30.0F + static_cast<float>(index);
        scene.Transforms().Set(roots[index], moved);
    }
    scene.Runtime().SynchronizeTransforms();

    const kb::scene::SceneRuntimeHotPathReport report = scene.Runtime().HotPathReport();
    kb::tests::Require(report.transformHierarchyDirtyFrontierCount == 32U, "Multi-root transform frontier did not visit exactly four dirty chains");
    kb::tests::Require(report.transformHierarchyInspectedCount == 32U, "Multi-root transform frontier inspected unrelated clean chains");
    kb::tests::Require(report.transformHierarchyUpdatedCount == 32U, "Multi-root transform frontier did not update all dirty chains");
    kb::tests::Require(report.transformHierarchySparseFlushCount == 1U, "Multi-root transform frontier did not use sparse flush");
    kb::tests::Require(report.transformHierarchyFlushedEntityCount == 32U, "Multi-root transform frontier flushed an unexpected entity count");
    kb::tests::Require(kb::tests::NearlyEqual(scene.Transforms().Get(leaves[3]).worldPosition.x, 40.0F), "Multi-root transform frontier did not update the fourth dirty leaf");
}

void RunTransformHierarchyWideFanoutDirtyFrontierReportTest() {
    kb::scene::Scene scene;
    std::vector<kb::scene::SceneObject> roots;
    std::vector<kb::scene::SceneObject> children;
    constexpr std::size_t rootCount = 16U;
    constexpr std::size_t fanout = 64U;
    roots.reserve(rootCount);
    children.reserve(rootCount * fanout);
    for (std::size_t rootIndex = 0; rootIndex < rootCount; ++rootIndex) {
        roots.push_back(scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
            .name = "Fanout Frontier Root",
            .transform = kb::scene::TransformComponent{
                .localPosition = kb::scene::Vec3{ static_cast<float>(rootIndex), 0.0F, 0.0F },
            },
        }));
        for (std::size_t childIndex = 0; childIndex < fanout; ++childIndex) {
            children.push_back(scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
                .name = "Fanout Frontier Child",
                .parent = roots.back(),
                .transform = kb::scene::TransformComponent{
                    .localPosition = kb::scene::Vec3{ 1.0F, 0.0F, 0.0F },
                },
            }));
        }
    }

    scene.Runtime().SynchronizeTransforms();
    kb::scene::TransformComponent moved = scene.Transforms().Get(roots.front());
    moved.localPosition.x = 50.0F;
    scene.Transforms().Set(roots.front(), moved);
    scene.Runtime().SynchronizeTransforms();

    const kb::scene::SceneRuntimeHotPathReport report = scene.Runtime().HotPathReport();
    kb::tests::Require(report.transformHierarchyDirtyFrontierCount == fanout + 1U, "Wide fanout transform frontier did not visit one root and its children");
    kb::tests::Require(report.transformHierarchyInspectedCount == fanout + 1U, "Wide fanout transform frontier inspected unrelated clean branches");
    kb::tests::Require(report.transformHierarchyUpdatedCount == fanout + 1U, "Wide fanout transform frontier did not update root and children");
    kb::tests::Require(report.transformHierarchySparseFlushCount == 1U, "Wide fanout transform frontier did not use sparse flush");
    kb::tests::Require(report.transformHierarchyDirtyListFlushCount == 1U, "Wide fanout transform frontier did not use dirty-list flush");
    kb::tests::Require(report.transformHierarchyDirtyListFlushEntityCount == fanout + 1U, "Wide fanout transform frontier reported an unexpected dirty-list flush count");
    kb::tests::Require(report.transformHierarchyBatchFlushCount == 0U, "Wide fanout transform frontier used batch flush for a sparse branch");
    kb::tests::Require(report.transformHierarchyFlushedEntityCount == fanout + 1U, "Wide fanout transform frontier flushed an unexpected entity count");
    kb::tests::Require(report.transformHierarchyParallelBatchCount == 0U, "Wide fanout transform frontier used parallel batch below the grain size");
    kb::tests::Require(report.transformHierarchyParallelChunkCount == 0U, "Wide fanout transform frontier reported parallel chunks below the grain size");
    kb::tests::Require(report.transformHierarchyParallelEntityCount == 0U, "Wide fanout transform frontier reported parallel entities below the grain size");
    kb::tests::Require(report.transformHierarchyWorkerCount == 1U, "Wide fanout transform frontier reported workers below the grain size");
    kb::tests::Require(report.transformHierarchyParallelFlushCount == 0U, "Wide fanout transform frontier used parallel flush below the sparse threshold");
    kb::tests::Require(report.transformHierarchyParallelFlushChunkCount == 0U, "Wide fanout transform frontier reported parallel flush chunks below the sparse threshold");
    kb::tests::Require(report.transformHierarchyParallelFlushEntityCount == 0U, "Wide fanout transform frontier reported parallel flush entities below the sparse threshold");
    kb::tests::Require(kb::tests::NearlyEqual(scene.Transforms().Get(children.front()).worldPosition.x, 51.0F), "Wide fanout transform frontier did not update the first child");
}

void RunTransformHierarchyParallelFanoutDirtyFrontierReportTest() {
    kb::scene::Scene scene;
    std::vector<kb::scene::SceneObject> roots;
    std::vector<kb::scene::SceneObject> children;
    constexpr std::size_t rootCount = 4U;
    constexpr std::size_t fanout = 256U;
    roots.reserve(rootCount);
    children.reserve(rootCount * fanout);
    for (std::size_t rootIndex = 0; rootIndex < rootCount; ++rootIndex) {
        roots.push_back(scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
            .name = "Parallel Fanout Root",
            .transform = kb::scene::TransformComponent{
                .localPosition = kb::scene::Vec3{ static_cast<float>(rootIndex), 0.0F, 0.0F },
            },
        }));
        for (std::size_t childIndex = 0; childIndex < fanout; ++childIndex) {
            children.push_back(scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
                .name = "Parallel Fanout Child",
                .parent = roots.back(),
                .transform = kb::scene::TransformComponent{
                    .localPosition = kb::scene::Vec3{ 1.0F, 0.0F, 0.0F },
                },
            }));
        }
    }

    scene.Runtime().SynchronizeTransforms();
    kb::scene::TransformComponent moved = scene.Transforms().Get(roots.front());
    moved.localPosition.x = 70.0F;
    scene.Transforms().Set(roots.front(), moved);
    scene.Runtime().SynchronizeTransforms();

    const kb::scene::SceneRuntimeHotPathReport report = scene.Runtime().HotPathReport();
    kb::tests::Require(report.transformHierarchyDirtyFrontierCount == fanout + 1U, "Parallel fanout transform frontier did not visit one root and its children");
    kb::tests::Require(report.transformHierarchyInspectedCount == fanout + 1U, "Parallel fanout transform frontier inspected unrelated branches");
    kb::tests::Require(report.transformHierarchyUpdatedCount == fanout + 1U, "Parallel fanout transform frontier did not update root and children");
    kb::tests::Require(report.transformHierarchySparseFlushCount == 0U, "Parallel fanout transform frontier used sparse flush above the sparse threshold");
    kb::tests::Require(report.transformHierarchyDirtyListFlushCount == 1U, "Parallel fanout transform frontier did not use dirty-list flush for a medium dirty subtree");
    kb::tests::Require(report.transformHierarchyDirtyListFlushEntityCount == fanout + 1U, "Parallel fanout transform frontier reported an unexpected dirty-list flush count");
    kb::tests::Require(report.transformHierarchyBatchFlushCount == 0U, "Parallel fanout transform frontier used full batch flush for a medium dirty subtree");
    kb::tests::Require(report.transformHierarchyFlushedEntityCount == fanout + 1U, "Parallel fanout transform frontier flushed an unexpected entity count");
    kb::tests::Require(report.transformHierarchyParallelBatchCount == 1U, "Parallel fanout transform frontier did not report one parallel batch");
    kb::tests::Require(report.transformHierarchyParallelChunkCount == 2U, "Parallel fanout transform frontier did not report two worker chunks");
    kb::tests::Require(report.transformHierarchyParallelEntityCount == fanout, "Parallel fanout transform frontier reported an unexpected parallel entity count");
    kb::tests::Require(report.transformHierarchyWorkerCount >= 1U, "Parallel fanout transform frontier did not report worker count");
    kb::tests::Require(report.transformHierarchyParallelFlushCount == 0U, "Parallel fanout transform frontier used full parallel flush for a medium dirty subtree");
    kb::tests::Require(report.transformHierarchyParallelFlushChunkCount == 0U, "Parallel fanout transform frontier reported full flush chunks for a medium dirty subtree");
    kb::tests::Require(report.transformHierarchyParallelFlushEntityCount == 0U, "Parallel fanout transform frontier reported full flush scan count for a medium dirty subtree");
    kb::tests::Require(report.transformHierarchyParallelFlushWorkerCount == 1U, "Parallel fanout transform frontier reported full flush worker count for a medium dirty subtree");
    kb::tests::Require(kb::tests::NearlyEqual(scene.Transforms().Get(children.front()).worldPosition.x, 71.0F), "Parallel fanout transform frontier did not update the first child");
}

void RunTransformVersioningTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject parent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Parent",
        .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 1.0F, 0.0F, 0.0F } },
    });
    const kb::scene::SceneObject child = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Child",
        .parent = parent,
        .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 2.0F, 0.0F, 0.0F } },
    });

    static_cast<void>(scene.Runtime().Update(0.016F));
    const kb::scene::TransformComponent initialChild = scene.Transforms().Get(child);

    kb::scene::TransformComponent parentTransform = scene.Transforms().Get(parent);
    parentTransform.localPosition = kb::scene::Vec3{ 3.0F, 0.0F, 0.0F };
    scene.Transforms().Set(parent, parentTransform);
    static_cast<void>(scene.Runtime().Update(0.016F));

    const kb::scene::TransformComponent movedParent = scene.Transforms().Get(parent);
    const kb::scene::TransformComponent movedChild = scene.Transforms().Get(child);
    kb::tests::Require(movedParent.localVersion == parentTransform.localVersion + 1U, "Set should increment local transform version");
    kb::tests::Require(movedParent.worldVersion > parentTransform.worldVersion, "Set should produce a newer world transform version");
    kb::tests::Require(movedChild.localVersion == initialChild.localVersion, "Parent movement should not increment child local version");
    kb::tests::Require(movedChild.parentVersion == movedParent.worldVersion, "Child parent version did not track moved parent world version");
    kb::tests::Require(movedChild.worldVersion > initialChild.worldVersion, "Child world version did not change after parent moved");
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

    constexpr std::uint64_t initialMixer = 101U;
    const kb::scene::AudioOcclusionSettings initialOcclusion{
        .enabled = true,
        .occludedVolumeScale = 0.25F,
        .maxDistance = 120.5F,
        .layerMask = 0x0F0F00FFU,
        .maxRaycastsPerTick = 17U,
    };
    kb::scene::SceneAudioMixerAccess::SetActiveMixer(scene, initialMixer);
    kb::tests::Require(kb::scene::SceneAudioMixerAccess::SetActiveSnapshot(scene, "InitialSnapshot"),
        "Scene history setup rejected the initial audio snapshot");
    kb::tests::Require(kb::scene::SceneAudioOcclusionAccess::Configure(scene, initialOcclusion),
        "Scene history setup rejected the initial audio occlusion settings");
    kb::scene::SceneAudioListenerAccess::SetLocalUser(scene, kb::input::LocalUserId{ 3U });

    kb::tests::Require(scene.History().Record("before rename"), "Scene history did not record initial snapshot");
    scene.Entities().SetName(object, "Changed");
    constexpr std::uint64_t changedMixer = 202U;
    const kb::scene::AudioOcclusionSettings changedOcclusion{
        .enabled = false,
        .occludedVolumeScale = 0.75F,
        .maxDistance = 240.25F,
        .layerMask = 0xFF0000FFU,
        .maxRaycastsPerTick = 31U,
    };
    kb::scene::SceneAudioMixerAccess::SetActiveMixer(scene, changedMixer);
    kb::tests::Require(kb::scene::SceneAudioMixerAccess::SetActiveSnapshot(scene, "ChangedSnapshot"),
        "Scene history setup rejected the changed audio snapshot");
    kb::tests::Require(kb::scene::SceneAudioOcclusionAccess::Configure(scene, changedOcclusion),
        "Scene history setup rejected the changed audio occlusion settings");
    kb::tests::Require(kb::scene::SceneAudioMixerAccess::SetBusVolumeOverride(scene, "RuntimeBus", 0.5F),
        "Scene history setup rejected the runtime bus override");
    kb::tests::Require(kb::scene::SceneAudioMixerAccess::BeginSnapshotTransition(scene, "RuntimeSnapshot", 2.0F),
        "Scene history setup rejected the runtime snapshot transition");
    kb::scene::SceneAudioListenerAccess::SetLocalUser(scene, kb::input::LocalUserId{ 9U });

    kb::tests::Require(scene.History().CanUndo(), "Scene history should be able to undo after record");
    kb::tests::Require(scene.History().Undo(), "Scene history undo failed");
    object = scene.Hierarchy().RootObjects().front();
    kb::tests::Require(scene.Entities().Name(object) == "Initial", "Scene history undo did not restore entity name");
    kb::tests::Require(kb::scene::SceneAudioMixerAccess::ActiveMixer(scene) == initialMixer,
        "Scene history undo did not restore the audio mixer");
    kb::tests::Require(kb::scene::SceneAudioMixerAccess::ActiveSnapshot(scene) == "InitialSnapshot",
        "Scene history undo did not restore the audio snapshot");
    const kb::scene::AudioOcclusionSettings& restoredInitialOcclusion = kb::scene::SceneAudioOcclusionAccess::Settings(scene);
    kb::tests::Require(restoredInitialOcclusion.enabled == initialOcclusion.enabled
            && kb::tests::NearlyEqual(restoredInitialOcclusion.occludedVolumeScale, initialOcclusion.occludedVolumeScale)
            && kb::tests::NearlyEqual(restoredInitialOcclusion.maxDistance, initialOcclusion.maxDistance)
            && restoredInitialOcclusion.layerMask == initialOcclusion.layerMask
            && restoredInitialOcclusion.maxRaycastsPerTick == initialOcclusion.maxRaycastsPerTick,
        "Scene history undo did not restore every audio occlusion field");
    kb::tests::Require(kb::scene::SceneAudioMixerAccess::BusVolumeOverrides(scene).empty()
            && !kb::scene::SceneAudioMixerAccess::SnapshotTransition(scene).IsActive(),
        "Scene history undo did not clear transient mixer state");
    kb::tests::Require(kb::scene::SceneAudioListenerAccess::LocalUser(scene) == kb::input::LocalUserId{ 9U },
        "Scene history undo restored transient listener local-user selection");

    kb::tests::Require(kb::scene::SceneAudioMixerAccess::SetBusVolumeOverride(scene, "RedoRuntimeBus", 0.4F),
        "Scene history redo setup rejected the runtime bus override");
    kb::tests::Require(kb::scene::SceneAudioMixerAccess::BeginSnapshotTransition(scene, "RedoRuntimeSnapshot", 3.0F),
        "Scene history redo setup rejected the runtime snapshot transition");
    kb::tests::Require(scene.History().CanRedo(), "Scene history should be able to redo after undo");
    kb::tests::Require(scene.History().Redo(), "Scene history redo failed");
    object = scene.Hierarchy().RootObjects().front();
    kb::tests::Require(scene.Entities().Name(object) == "Changed", "Scene history redo did not restore entity name");
    kb::tests::Require(kb::scene::SceneAudioMixerAccess::ActiveMixer(scene) == changedMixer,
        "Scene history redo did not restore the audio mixer");
    kb::tests::Require(kb::scene::SceneAudioMixerAccess::ActiveSnapshot(scene) == "ChangedSnapshot",
        "Scene history redo did not restore the audio snapshot");
    const kb::scene::AudioOcclusionSettings& restoredChangedOcclusion = kb::scene::SceneAudioOcclusionAccess::Settings(scene);
    kb::tests::Require(restoredChangedOcclusion.enabled == changedOcclusion.enabled
            && kb::tests::NearlyEqual(restoredChangedOcclusion.occludedVolumeScale, changedOcclusion.occludedVolumeScale)
            && kb::tests::NearlyEqual(restoredChangedOcclusion.maxDistance, changedOcclusion.maxDistance)
            && restoredChangedOcclusion.layerMask == changedOcclusion.layerMask
            && restoredChangedOcclusion.maxRaycastsPerTick == changedOcclusion.maxRaycastsPerTick,
        "Scene history redo did not restore every audio occlusion field");
    kb::tests::Require(kb::scene::SceneAudioMixerAccess::BusVolumeOverrides(scene).empty()
            && !kb::scene::SceneAudioMixerAccess::SnapshotTransition(scene).IsActive(),
        "Scene history redo did not clear transient mixer state");
    kb::tests::Require(kb::scene::SceneAudioListenerAccess::LocalUser(scene) == kb::input::LocalUserId{ 9U },
        "Scene history redo restored transient listener local-user selection");

    kb::tests::Require(scene.History().Undo(), "Scene history second undo failed");
    object = scene.Hierarchy().RootObjects().front();
    scene.Entities().SetName(object, "Branch");
    kb::tests::Require(scene.History().Record("branch"), "Scene history branch record failed");
    kb::tests::Require(!scene.History().CanRedo(), "Scene history did not clear redo stack after branch record");
}

void RunDestroyedEntityHandleDoesNotAffectNewEntityTest() {
    kb::scene::Scene scene;

    const kb::scene::SceneObject destroyed = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Destroyed Handle" });
    const kb::scene::SceneEntity destroyedEntity = destroyed.Entity();
    scene.Entities().Destroy(destroyed);
    kb::tests::Require(!scene.Entities().IsAlive(destroyed), "Destroyed entity handle test did not destroy the source object");

    const kb::scene::SceneObject replacement = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Replacement" });
    kb::tests::Require(scene.Entities().IsAlive(replacement), "Destroyed entity handle test did not create a replacement object");
    kb::tests::Require(replacement.Entity() != destroyedEntity, "Scene entity creation reused a stale destroyed handle");

    scene.Entities().SetName(destroyed, "Stale Rename");
    scene.Entities().SetName(destroyedEntity, "Stale Entity Rename");
    kb::tests::Require(scene.Entities().Name(destroyed).empty(), "Destroyed object returned a stale name");
    kb::tests::Require(scene.Entities().Name(destroyedEntity).empty(), "Destroyed entity returned a stale name");
    kb::tests::Require(scene.Entities().Name(replacement) == "Replacement", "Stale destroyed handle renamed the replacement object");

    kb::scene::TagsComponent staleTags;
    kb::scene::SetTagsText(staleTags, "stale");
    scene.Components().Tags().Set(destroyedEntity, staleTags);
    kb::tests::Require(!scene.Components().Tags().Has(destroyedEntity), "Destroyed entity accepted a stale component write");
    kb::tests::Require(!scene.Components().Tags().Has(replacement.Entity()), "Stale destroyed component write affected the replacement object");
}

void RunSceneCameraLightVisitorBatchPathTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject cameraObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Visitor Camera",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 3.0F, 4.0F, 5.0F },
        },
    });
    const kb::scene::SceneObject lightObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Visitor Light",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 7.0F, 11.0F, 13.0F },
        },
    });
    scene.Components().Cameras().Set(cameraObject.Entity(), kb::scene::CameraComponent{ .verticalFovDegrees = 72.0F, .primary = true });
    scene.Components().Lights().Set(lightObject.Entity(), kb::scene::LightComponent{ .kind = kb::scene::LightKind::Point, .intensity = 6.5F });
    scene.Runtime().SynchronizeTransforms();

    SceneCameraLightVisitorStats firstPass;
    scene.Components().Visitors().ForEachCamera(&AccumulateCameraVisit, &firstPass);
    scene.Components().Visitors().ForEachLight(&AccumulateLightVisit, &firstPass);
    kb::tests::Require(firstPass.cameraCount == 1U, "Scene camera visitor did not visit the camera batch");
    kb::tests::Require(firstPass.lightCount == 1U, "Scene light visitor did not visit the light batch");
    kb::tests::Require(kb::tests::NearlyEqual(static_cast<float>(firstPass.checksum), 92.5F), "Scene camera/light visitor returned unexpected component data");

    SceneCameraLightVisitorStats secondPass;
    scene.Components().Visitors().ForEachCamera(&AccumulateCameraVisit, &secondPass);
    scene.Components().Visitors().ForEachLight(&AccumulateLightVisit, &secondPass);
    kb::tests::Require(secondPass.cameraCount == 1U, "Cached scene camera visitor did not visit the camera batch");
    kb::tests::Require(secondPass.lightCount == 1U, "Cached scene light visitor did not visit the light batch");
    kb::tests::Require(
        kb::tests::NearlyEqual(static_cast<float>(secondPass.checksum), static_cast<float>(firstPass.checksum)),
        "Cached scene camera/light visitor changed output");
}

void RunTransformSplitPayloadContractTest() {
    const kb::scene::LocalTransform local{
        .position = kb::scene::Vec3{ 1.0F, 2.0F, 3.0F },
        .rotation = kb::scene::Quat{ 0.0F, 0.25F, 0.0F, 0.75F },
        .scale = kb::scene::Vec3{ 2.0F, 3.0F, 4.0F },
    };
    const kb::scene::WorldTransform world{
        .position = kb::scene::Vec3{ 10.0F, 20.0F, 30.0F },
        .rotation = kb::scene::Quat{ 0.0F, 0.5F, 0.0F, 0.5F },
        .scale = kb::scene::Vec3{ 4.0F, 5.0F, 6.0F },
    };
    const kb::scene::TransformVersionMetadata metadata{
        .localVersion = 7U,
        .parentVersion = 11U,
        .worldVersion = 13U,
        .worldDirty = false,
    };

    const kb::scene::TransformComponent transform = kb::scene::TransformComponent::FromPayloads(local, world, metadata);
    const kb::scene::LocalTransform extractedLocal = transform.LocalPayload();
    const kb::scene::WorldTransform extractedWorld = transform.WorldPayload();
    const kb::scene::TransformVersionMetadata extractedMetadata = transform.VersionMetadata();

    kb::tests::Require(kb::tests::NearlyEqual(extractedLocal.position.x, local.position.x), "Transform local payload lost position X");
    kb::tests::Require(kb::tests::NearlyEqual(extractedLocal.rotation.y, local.rotation.y), "Transform local payload lost rotation Y");
    kb::tests::Require(kb::tests::NearlyEqual(extractedLocal.scale.z, local.scale.z), "Transform local payload lost scale Z");
    kb::tests::Require(kb::tests::NearlyEqual(extractedWorld.position.z, world.position.z), "Transform world payload lost position Z");
    kb::tests::Require(kb::tests::NearlyEqual(extractedWorld.rotation.w, world.rotation.w), "Transform world payload lost rotation W");
    kb::tests::Require(kb::tests::NearlyEqual(extractedWorld.scale.y, world.scale.y), "Transform world payload lost scale Y");
    kb::tests::Require(extractedMetadata.localVersion == metadata.localVersion, "Transform metadata lost local version");
    kb::tests::Require(extractedMetadata.parentVersion == metadata.parentVersion, "Transform metadata lost parent version");
    kb::tests::Require(extractedMetadata.worldVersion == metadata.worldVersion, "Transform metadata lost world version");
    kb::tests::Require(extractedMetadata.worldDirty == metadata.worldDirty, "Transform metadata lost dirty state");

    const kb::scene::TransformHierarchyRelation relation{ .parentEntityId = 42U, .topologyVersion = 99U };
    kb::tests::Require(relation.parentEntityId == 42U, "Transform hierarchy relation lost parent id");
    kb::tests::Require(relation.topologyVersion == 99U, "Transform hierarchy relation lost topology version");
}

void RunSceneHierarchyRootCollectorUsesUnsafeHotQueryTest() {
    kb::scene::Scene scene;

    const kb::scene::SceneObject firstRoot = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Hot Root A",
    });
    const kb::scene::SceneObject secondRoot = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Hot Root B",
    });
    static_cast<void>(scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Hot Child",
        .parent = firstRoot,
    }));

    const kb::ecs::WorldTelemetrySnapshot before = scene.Runtime().EcsWorld().TelemetrySnapshot();
    const std::vector<kb::scene::SceneEntity> roots = scene.Hierarchy().RootEntities();
    const kb::ecs::WorldTelemetrySnapshot after = scene.Runtime().EcsWorld().TelemetrySnapshot();

    kb::tests::Require(roots.size() == 2U, "Scene hierarchy hot root collector returned an invalid root count");
    kb::tests::Require(roots[0] == firstRoot.Entity(), "Scene hierarchy hot root collector lost stable root order");
    kb::tests::Require(roots[1] == secondRoot.Entity(), "Scene hierarchy hot root collector lost the second root");
    kb::tests::Require(after.queryExecutions == before.queryExecutions, "Scene hierarchy root collector used the safe query executor");
}

void RunSceneBehaviourIterationUsesUnsafeHotQueryTest() {
    kb::scene::Scene scene;

    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Hot Behaviour",
    });
    scene.Components().Behaviours().Set(object.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = 77U,
        .backend = kb::scene::BehaviourBackend::Lua,
        .enabled = true,
        .tickGroup = kb::scene::BehaviourTickGroup::Gameplay,
        .executionOrder = 5,
    });

    const kb::ecs::WorldTelemetrySnapshot before = scene.Runtime().EcsWorld().TelemetrySnapshot();
    BehaviourIterationHotQueryStats stats;
    scene.Components().Behaviours().ForEach([](kb::scene::SceneEntity entity, const kb::scene::BehaviourComponent& behaviour, void* context) {
        auto& data = *static_cast<BehaviourIterationHotQueryStats*>(context);
        if (entity.IsValid()) {
            ++data.visited;
            data.checksum += behaviour.behaviourAssetId;
        }
    }, &stats);
    const kb::ecs::WorldTelemetrySnapshot after = scene.Runtime().EcsWorld().TelemetrySnapshot();

    kb::tests::Require(stats.visited == 1U, "Scene behaviour hot iterator did not visit the behaviour component");
    kb::tests::Require(stats.checksum == 77U, "Scene behaviour hot iterator provided an invalid behaviour payload");
    kb::tests::Require(after.queryExecutions == before.queryExecutions, "Scene behaviour iterator used the safe query executor");
}

} // namespace

namespace kb::tests {

void RunSceneHierarchyTests() {
    RunTransformHierarchyTest();
    RunTransformHierarchyReplayDeterminismTest();
    RunTransformRootFastPathReportTest();
    RunParentChildrenOwnershipTest();
    RunHierarchyStableCreationOrderTest();
    RunHierarchyCreationOrderSurvivesDeletionTest();
    RunHierarchyNoOpParentingKeepsSiblingOrderTest();
    RunTransformTopologicalBatchMassParentingTest();
    RunTransformTopologicalBatchCacheInvalidationTest();
    RunTransformPropagationBudgetTest();
    RunTransformSparseFlushReportTest();
    RunTransformHierarchyDirtyFrontierReportTest();
    RunTransformHierarchyDeepDirtyFrontierReportTest();
    RunTransformHierarchyNestedDirtyFrontierReportTest();
    RunTransformHierarchyDirtyFrontierDuplicateSetTest();
    RunTransformHierarchyMultiRootDirtyFrontierReportTest();
    RunTransformHierarchyWideFanoutDirtyFrontierReportTest();
    RunTransformHierarchyParallelFanoutDirtyFrontierReportTest();
    RunTransformVersioningTest();
    RunTransformSplitPayloadContractTest();
    RunSceneHierarchyRootCollectorUsesUnsafeHotQueryTest();
    RunSceneBehaviourIterationUsesUnsafeHotQueryTest();
    RunSceneBatchDuplicateTest();
    RunSceneHistoryUndoRedoTest();
    RunDestroyedEntityHandleDoesNotAffectNewEntityTest();
    RunSceneCameraLightVisitorBatchPathTest();
}

} // namespace kb::tests
