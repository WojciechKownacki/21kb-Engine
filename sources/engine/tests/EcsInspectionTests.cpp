#include "EcsTestSuites.hpp"
#include "EcsTestTypes.hpp"
#include "TestSupport.hpp"

#include "engine/ecs/World.hpp"
#include "engine/ecs/ComponentReflectionMacros.hpp"

#include <string_view>

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

[[nodiscard]] const kb::ecs::EditorEntityInspection* FindEditorEntity(const kb::ecs::EditorWorldInspection& inspection, std::string_view name) {
    for (const kb::ecs::EditorEntityInspection& entity : inspection.entities) {
        if (entity.name == name) {
            return &entity;
        }
    }
    return nullptr;
}

void RegisterPositionReflection(kb::ecs::World& world) {
    [[maybe_unused]] const kb::ecs::ComponentReflection* reflection = world.RegisterComponentReflection<EcsPosition>(
        "test.EcsPosition",
        {
            KB_ECS_FIELD(EcsPosition, x, kb::ecs::ComponentFieldType::Float32),
            KB_ECS_FIELD(EcsPosition, y, kb::ecs::ComponentFieldType::Float32),
        });
}

void RunEditorWorldInspectionTest() {
    kb::ecs::World world;
    RegisterPositionReflection(world);

    const kb::ecs::Entity empty = world.CreateEntity("Empty");
    const kb::ecs::Entity parent = world.CreateEntity("Parent");
    const kb::ecs::Entity child = world.CreateEntity("Child");
    world.SetParent(child, parent);
    world.Set(child, EcsPosition{ .x = 8.0F, .y = 13.0F });

    kb::ecs::EditorWorldInspection inspection;
    kb::tests::Require(world.CaptureEditorWorld(inspection), "ECS editor inspection failed");
    kb::tests::Require(inspection.entities.size() == 3, "ECS editor inspection did not include all created entities");

    const kb::ecs::EditorEntityInspection* emptyInspection = FindEditorEntity(inspection, "Empty");
    kb::tests::Require(emptyInspection != nullptr && emptyInspection->entity == empty, "ECS editor inspection did not include empty entity");
    kb::tests::Require(emptyInspection->components.empty(), "ECS editor inspection attached components to empty entity");

    const kb::ecs::EditorEntityInspection* childInspection = FindEditorEntity(inspection, "Child");
    kb::tests::Require(childInspection != nullptr && childInspection->parent == parent, "ECS editor inspection returned invalid child hierarchy");
    kb::tests::Require(childInspection->components.size() == 1, "ECS editor inspection returned invalid component count");
    kb::tests::Require(childInspection->components[0].fields.size() == 2, "ECS editor inspection did not serialize reflected fields");

    const float* x = std::get_if<float>(&childInspection->components[0].fields[0].value);
    const float* y = std::get_if<float>(&childInspection->components[0].fields[1].value);
    kb::tests::Require(x != nullptr && y != nullptr, "ECS editor inspection stored invalid field value type");
    kb::tests::Require(kb::tests::NearlyEqual(*x, 8.0F) && kb::tests::NearlyEqual(*y, 13.0F), "ECS editor inspection stored invalid field values");
}

void RunEditorComponentApplyTest() {
    kb::ecs::World world;
    RegisterPositionReflection(world);

    const kb::ecs::Entity entity = world.CreateEntity("Editable");
    world.Set(entity, EcsPosition{ .x = 1.0F, .y = 2.0F });

    kb::ecs::SerializedComponent component;
    kb::tests::Require(world.SerializeComponent(entity, world.Component<EcsPosition>(), component), "ECS editor component serialization failed");
    component.fields[0].value = 21.0F;

    kb::tests::Require(world.ApplySerializedComponent(entity, component), "ECS editor component apply failed");
    const EcsPosition* position = world.TryGet<EcsPosition>(entity);
    kb::tests::Require(position != nullptr, "ECS editor component apply removed component");
    kb::tests::Require(kb::tests::NearlyEqual(position->x, 21.0F), "ECS editor component apply did not update field");
    kb::tests::Require(kb::tests::NearlyEqual(position->y, 2.0F), "ECS editor component apply did not preserve untouched field");
}

void RunEditorEntityNamingAndChildrenTest() {
    kb::ecs::World world;

    const kb::ecs::Entity parent = world.CreateEntity("Parent");
    const kb::ecs::Entity child = world.CreateEntity("Child");
    world.SetParent(child, parent);
    world.SetName(child, "Renamed");

    const std::vector<kb::ecs::Entity> children = world.Children(parent);
    kb::tests::Require(children.size() == 1 && children[0] == child, "ECS hierarchy children API returned invalid result");
    kb::tests::Require(world.Name(child) == "Renamed", "ECS entity rename API did not update name");

    kb::ecs::EditorWorldInspection inspection;
    kb::tests::Require(world.CaptureEditorWorld(inspection), "ECS editor inspection after rename failed");
    kb::tests::Require(FindEditorEntity(inspection, "Renamed") != nullptr, "ECS editor inspection did not use renamed entity");
}

} // namespace

namespace kb::tests {

void RunEcsInspectionTests() {
    RunEntityInspectionTest();
    RunEditorWorldInspectionTest();
    RunEditorComponentApplyTest();
    RunEditorEntityNamingAndChildrenTest();
}

} // namespace kb::tests
