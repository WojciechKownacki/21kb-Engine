#include "EcsTestSuites.hpp"
#include "TestSupport.hpp"

#include "engine/ecs/World.hpp"

#include <algorithm>
#include <vector>

namespace {

struct EcsSelectedTag {};
struct EcsDependencyRelation {};

void RunTypedTagApiTest() {
    kb::ecs::World world;
    const kb::ecs::TagId selectedTag = world.RegisterTag<EcsSelectedTag>("test.Selected");
    const kb::ecs::TagId cachedSelectedTag = world.RegisterTag<EcsSelectedTag>("test.Selected");
    kb::tests::Require(selectedTag != 0, "Typed ECS tag registration failed");
    kb::tests::Require(selectedTag == cachedSelectedTag, "Typed ECS tag registration was not cached per type");

    const kb::ecs::Entity entity = world.CreateEntity("Selectable");
    world.AddTag<EcsSelectedTag>(entity);
    kb::tests::Require(world.HasTag<EcsSelectedTag>(entity), "Typed ECS tag was not assigned");

    world.RemoveTag<EcsSelectedTag>(entity);
    kb::tests::Require(!world.HasTag<EcsSelectedTag>(entity), "Typed ECS tag remove failed");
}

void RunTypedRelationApiTest() {
    kb::ecs::World world;
    const kb::ecs::RelationId dependencyRelation = world.RegisterRelation<EcsDependencyRelation>("test.DependsOn");
    const kb::ecs::RelationId cachedDependencyRelation = world.RegisterRelation<EcsDependencyRelation>("test.DependsOn");
    kb::tests::Require(dependencyRelation != 0, "Typed ECS relation registration failed");
    kb::tests::Require(dependencyRelation == cachedDependencyRelation, "Typed ECS relation registration was not cached per type");

    const kb::ecs::Entity system = world.CreateEntity("System");
    const kb::ecs::Entity dependency = world.CreateEntity("Dependency");
    world.AddRelation<EcsDependencyRelation>(system, dependency);

    kb::tests::Require(world.HasRelation<EcsDependencyRelation>(system, dependency), "Typed ECS relation was not assigned");
    kb::tests::Require(world.RelationTarget<EcsDependencyRelation>(system) == dependency, "Typed ECS relation target lookup failed");

    world.RemoveRelation<EcsDependencyRelation>(system, dependency);
    kb::tests::Require(!world.HasRelation<EcsDependencyRelation>(system, dependency), "Typed ECS relation remove failed");
}

void RunParentHierarchyApiTest() {
    kb::ecs::World world;
    const kb::ecs::Entity parent = world.CreateEntity("Parent");
    const kb::ecs::Entity child = world.CreateEntity("Child");
    const kb::ecs::Entity replacementParent = world.CreateEntity("ReplacementParent");

    world.SetParent(child, parent);
    kb::tests::Require(world.Parent(child) == parent, "ECS parent assignment failed");

    world.SetParent(child, replacementParent);
    kb::tests::Require(world.Parent(child) == replacementParent, "ECS parent replacement failed");

    world.SetParent(replacementParent, child);
    kb::tests::Require(world.Parent(replacementParent) != child, "ECS hierarchy accepted a parent cycle");

    world.SetParent(child, child);
    kb::tests::Require(world.Parent(child) == replacementParent, "ECS hierarchy accepted self-parenting");

    world.ClearParent(child);
    kb::tests::Require(!world.Parent(child).IsValid(), "ECS parent clear failed");
}

void RunBatchParentHierarchyApiTest() {
    kb::ecs::World world;
    const kb::ecs::Entity parent = world.CreateEntity("BatchParent");
    const kb::ecs::Entity replacementParent = world.CreateEntity("BatchReplacementParent");
    std::vector<kb::ecs::Entity> children;
    std::vector<kb::ecs::Entity> parents;
    children.reserve(5U);
    parents.reserve(5U);
    for (std::size_t index = 0; index < 5U; ++index) {
        children.push_back(world.CreateEntity());
        parents.push_back(parent);
    }

    world.SetParents(std::span<const kb::ecs::Entity>{ children }, std::span<const kb::ecs::Entity>{ parents });
    for (kb::ecs::Entity child : children) {
        kb::tests::Require(world.Parent(child) == parent, "ECS batch parent assignment failed");
    }

    std::fill(parents.begin(), parents.end(), replacementParent);
    world.SetParents(std::span<const kb::ecs::Entity>{ children }, std::span<const kb::ecs::Entity>{ parents });
    for (kb::ecs::Entity child : children) {
        kb::tests::Require(world.Parent(child) == replacementParent, "ECS batch parent replacement failed");
    }

    std::vector<kb::ecs::Entity> cycleChildren{ children[0], children[1] };
    std::vector<kb::ecs::Entity> cycleParents{ children[1], children[0] };
    world.SetParents(std::span<const kb::ecs::Entity>{ cycleChildren }, std::span<const kb::ecs::Entity>{ cycleParents });
    kb::tests::Require(world.Parent(children[0]) == children[1], "ECS batch parent did not apply an acyclic edge");
    kb::tests::Require(world.Parent(children[1]) == replacementParent, "ECS batch parent accepted a cycle edge");

    world.ClearParents(std::span<const kb::ecs::Entity>{ children });
    for (kb::ecs::Entity child : children) {
        kb::tests::Require(!world.Parent(child).IsValid(), "ECS batch parent clear failed");
    }

    world.ClearParents(std::span<const kb::ecs::Entity>{ children });
    for (kb::ecs::Entity child : children) {
        kb::tests::Require(!world.Parent(child).IsValid(), "ECS batch parent clear was not idempotent");
    }
}

void RunBatchParentTelemetryTest() {
    kb::ecs::World world;
    const kb::ecs::Entity parent = world.CreateEntity("TelemetryParent");
    std::vector<kb::ecs::Entity> children;
    std::vector<kb::ecs::Entity> parents;
    children.reserve(4U);
    parents.reserve(4U);
    for (std::size_t index = 0; index < 4U; ++index) {
        children.push_back(world.CreateEntity());
        parents.push_back(parent);
    }

    world.ResetTelemetryFrameCounters();
    world.SetParents(std::span<const kb::ecs::Entity>{ children }, std::span<const kb::ecs::Entity>{ parents });
    kb::ecs::WorldTelemetrySnapshot setSnapshot = world.TelemetrySnapshot();
    kb::tests::Require(setSnapshot.structuralChangesSinceReset == 1U, "ECS batch parent set telemetry was not grouped");

    world.ResetTelemetryFrameCounters();
    world.ClearParents(std::span<const kb::ecs::Entity>{ children });
    kb::ecs::WorldTelemetrySnapshot clearSnapshot = world.TelemetrySnapshot();
    kb::tests::Require(clearSnapshot.structuralChangesSinceReset == 1U, "ECS batch parent clear telemetry was not grouped");

    world.ResetTelemetryFrameCounters();
    world.ClearParents(std::span<const kb::ecs::Entity>{ children });
    kb::ecs::WorldTelemetrySnapshot unchangedSnapshot = world.TelemetrySnapshot();
    kb::tests::Require(unchangedSnapshot.structuralChangesSinceReset == 0U, "ECS empty batch parent clear recorded a structural change");
}

} // namespace

namespace kb::tests {

void RunEcsRelationTests() {
    RunTypedTagApiTest();
    RunTypedRelationApiTest();
    RunParentHierarchyApiTest();
    RunBatchParentHierarchyApiTest();
    RunBatchParentTelemetryTest();
}

} // namespace kb::tests
