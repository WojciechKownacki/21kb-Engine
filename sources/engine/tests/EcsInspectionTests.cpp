#include "EcsTestSuites.hpp"
#include "EcsTestTypes.hpp"
#include "TestSupport.hpp"

#include "engine/ecs/World.hpp"

namespace {

[[nodiscard]] bool HasInspectedComponent(const kb::ecs::EntityInspection& inspection, kb::ecs::ComponentId componentId) {
    for (const kb::ecs::EntityComponentInspection& component : inspection.components) {
        if (component.id == componentId) {
            return true;
        }
    }
    return false;
}

void RunEntityInspectionTest() {
    kb::ecs::World world;
    const kb::ecs::ComponentId positionComponent = world.RegisterComponent<EcsPosition>("test.EcsPosition");
    const kb::ecs::ComponentId velocityComponent = world.RegisterComponent<EcsVelocity>("test.EcsVelocity");

    const kb::ecs::Entity parent = world.CreateEntity("Parent");
    const kb::ecs::Entity child = world.CreateEntity("Child");
    world.SetParent(child, parent);
    world.Set(child, EcsPosition{ .x = 1.0F, .y = 2.0F });
    world.Set(child, EcsVelocity{ .x = 3.0F, .y = 4.0F });

    const kb::ecs::EntityInspection inspection = world.InspectEntity(child);
    kb::tests::Require(inspection.entity == child, "ECS inspection returned invalid entity");
    kb::tests::Require(inspection.name == "Child", "ECS inspection returned invalid entity name");
    kb::tests::Require(inspection.parent == parent, "ECS inspection returned invalid parent");
    kb::tests::Require(inspection.components.size() == 2, "ECS inspection returned invalid component count");
    kb::tests::Require(HasInspectedComponent(inspection, positionComponent), "ECS inspection did not include position component");
    kb::tests::Require(HasInspectedComponent(inspection, velocityComponent), "ECS inspection did not include velocity component");
}

} // namespace

namespace kb::tests {

void RunEcsInspectionTests() {
    RunEntityInspectionTest();
}

} // namespace kb::tests
