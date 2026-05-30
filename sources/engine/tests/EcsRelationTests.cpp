#include "EcsTestSuites.hpp"
#include "TestSupport.hpp"

#include "engine/ecs/World.hpp"

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

} // namespace

namespace kb::tests {

void RunEcsRelationTests() {
    RunTypedTagApiTest();
    RunTypedRelationApiTest();
    RunParentHierarchyApiTest();
}

} // namespace kb::tests
