#include "EcsTestSuites.hpp"
#include "EcsTestTypes.hpp"
#include "TestSupport.hpp"

#include "engine/ecs/World.hpp"

#include <cstring>

namespace {

template <typename T>
[[nodiscard]] T ReadSnapshotComponent(const kb::ecs::ComponentSnapshot& snapshot) {
    kb::tests::Require(snapshot.data.size() == sizeof(T), "ECS snapshot component size mismatch");

    T value{};
    std::memcpy(&value, snapshot.data.data(), sizeof(T));
    return value;
}

[[nodiscard]] const kb::ecs::EntitySnapshot* FindSnapshotEntity(const kb::ecs::WorldSnapshot& snapshot, kb::ecs::Entity entity) {
    for (const kb::ecs::EntitySnapshot& entitySnapshot : snapshot.entities) {
        if (entitySnapshot.id == entity.Id()) {
            return &entitySnapshot;
        }
    }
    return nullptr;
}

[[nodiscard]] const kb::ecs::ComponentSnapshot* FindSnapshotComponent(const kb::ecs::EntitySnapshot& snapshot, kb::ecs::ComponentId componentId) {
    for (const kb::ecs::ComponentSnapshot& componentSnapshot : snapshot.components) {
        if (componentSnapshot.componentId == componentId) {
            return &componentSnapshot;
        }
    }
    return nullptr;
}

void RunWorldSnapshotTest() {
    kb::ecs::World world;
    const kb::ecs::ComponentId positionComponent = world.RegisterComponent<EcsPosition>("test.EcsPosition");
    const kb::ecs::ComponentId velocityComponent = world.RegisterComponent<EcsVelocity>("test.EcsVelocity");

    const kb::ecs::Entity mover = world.CreateEntity("Mover");
    const kb::ecs::Entity staticEntity = world.CreateEntity("Static");
    world.Set(mover, EcsPosition{ .x = 3.0F, .y = 4.0F });
    world.Set(mover, EcsVelocity{ .x = 1.0F, .y = 2.0F });
    world.Set(staticEntity, EcsPosition{ .x = 9.0F, .y = 8.0F });

    const kb::ecs::WorldSnapshot snapshot = world.CaptureSnapshot();
    kb::tests::Require(snapshot.componentTypes.size() == 2, "ECS snapshot did not capture component type registry");
    kb::tests::Require(snapshot.entities.size() == 2, "ECS snapshot did not capture component-owning entities");

    const kb::ecs::EntitySnapshot* moverSnapshot = FindSnapshotEntity(snapshot, mover);
    kb::tests::Require(moverSnapshot != nullptr && moverSnapshot->name == "Mover", "ECS snapshot did not capture entity identity");

    const kb::ecs::ComponentSnapshot* positionSnapshot = FindSnapshotComponent(*moverSnapshot, positionComponent);
    const kb::ecs::ComponentSnapshot* velocitySnapshot = FindSnapshotComponent(*moverSnapshot, velocityComponent);
    kb::tests::Require(positionSnapshot != nullptr && velocitySnapshot != nullptr, "ECS snapshot did not capture all entity components");

    const EcsPosition position = ReadSnapshotComponent<EcsPosition>(*positionSnapshot);
    const EcsVelocity velocity = ReadSnapshotComponent<EcsVelocity>(*velocitySnapshot);
    kb::tests::Require(kb::tests::NearlyEqual(position.x, 3.0F) && kb::tests::NearlyEqual(position.y, 4.0F), "ECS snapshot captured invalid position data");
    kb::tests::Require(kb::tests::NearlyEqual(velocity.x, 1.0F) && kb::tests::NearlyEqual(velocity.y, 2.0F), "ECS snapshot captured invalid velocity data");
}

} // namespace

namespace kb::tests {

void RunEcsSnapshotTests() {
    RunWorldSnapshotTest();
}

} // namespace kb::tests
