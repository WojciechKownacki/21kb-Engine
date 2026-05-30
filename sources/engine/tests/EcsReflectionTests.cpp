#include "EcsTestSuites.hpp"
#include "EcsTestTypes.hpp"
#include "TestSupport.hpp"

#include "engine/ecs/ComponentFieldAccess.hpp"
#include "engine/ecs/ComponentReflectionMacros.hpp"
#include "engine/ecs/ComponentSerialization.hpp"
#include "engine/ecs/World.hpp"

#include <array>
#include <cstring>
#include <string_view>

namespace {

void RunComponentReflectionRegistrationTest() {
    kb::ecs::World world;

    const kb::ecs::ComponentReflection* reflection = world.RegisterComponentReflection<EcsPosition>(
        "test.EcsPosition",
        {
            KB_ECS_FIELD(EcsPosition, x, kb::ecs::ComponentFieldType::Float32),
            KB_ECS_FIELD(EcsPosition, y, kb::ecs::ComponentFieldType::Float32),
        });

    kb::tests::Require(reflection != nullptr, "ECS component reflection registration failed");
    kb::tests::Require(reflection->IsValid(), "ECS component reflection is invalid");
    kb::tests::Require(reflection->Name() == "test.EcsPosition", "ECS component reflection has invalid name");
    kb::tests::Require(reflection->Size() == sizeof(EcsPosition), "ECS component reflection has invalid size");
    kb::tests::Require(reflection->Fields().size() == 2, "ECS component reflection has invalid field count");

    const kb::ecs::ComponentFieldReflection* xField = reflection->FindField("x");
    const kb::ecs::ComponentFieldReflection* yField = reflection->FindField("y");
    kb::tests::Require(xField != nullptr && yField != nullptr, "ECS component reflection did not expose expected fields");
    kb::tests::Require(xField->offset == offsetof(EcsPosition, x), "ECS component reflection stored invalid x offset");
    kb::tests::Require(yField->offset == offsetof(EcsPosition, y), "ECS component reflection stored invalid y offset");
    kb::tests::Require(xField->type == kb::ecs::ComponentFieldType::Float32, "ECS component reflection stored invalid field type");

    const kb::ecs::ComponentReflection* cachedReflection = world.Reflection<EcsPosition>();
    kb::tests::Require(cachedReflection == reflection, "ECS component reflection lookup returned different registry entry");
}

void RunComponentReflectionValidationTest() {
    kb::ecs::World world;

    const kb::ecs::ComponentReflection* invalidReflection = world.RegisterComponentReflection<EcsVelocity>(
        "test.EcsVelocity",
        {
            kb::ecs::ComponentFieldDesc{
                .name = "invalid",
                .type = kb::ecs::ComponentFieldType::Float32,
                .offset = sizeof(EcsVelocity) + 4,
                .size = sizeof(float),
            },
        });

    kb::tests::Require(invalidReflection == nullptr, "ECS component reflection accepted out-of-bounds field");
    kb::tests::Require(world.Reflection<EcsVelocity>() == nullptr, "ECS component reflection registry stored invalid reflection");
}

void RunComponentFieldAccessTest() {
    kb::ecs::World world;
    const kb::ecs::ComponentReflection* reflection = world.RegisterComponentReflection<EcsPosition>(
        "test.EcsPosition",
        {
            KB_ECS_FIELD(EcsPosition, x, kb::ecs::ComponentFieldType::Float32),
            KB_ECS_FIELD(EcsPosition, y, kb::ecs::ComponentFieldType::Float32),
        });
    kb::tests::Require(reflection != nullptr, "ECS field access test could not register reflection");

    const kb::ecs::ComponentFieldReflection* xField = reflection->FindField("x");
    kb::tests::Require(xField != nullptr, "ECS field access test could not find field");

    EcsPosition position{ .x = 5.0F, .y = 9.0F };
    std::array<std::byte, sizeof(float)> buffer{};
    kb::tests::Require(kb::ecs::ComponentFieldAccess::Read(&position, sizeof(position), *xField, buffer), "ECS component field read failed");

    float readValue = 0.0F;
    std::memcpy(&readValue, buffer.data(), sizeof(float));
    kb::tests::Require(kb::tests::NearlyEqual(readValue, 5.0F), "ECS component field read returned invalid value");

    const float writtenValue = 11.0F;
    std::memcpy(buffer.data(), &writtenValue, sizeof(float));
    kb::tests::Require(kb::ecs::ComponentFieldAccess::Write(&position, sizeof(position), *xField, buffer), "ECS component field write failed");
    kb::tests::Require(kb::tests::NearlyEqual(position.x, 11.0F), "ECS component field write stored invalid value");
}

void RunComponentSerializerTest() {
    kb::ecs::World world;
    const kb::ecs::ComponentReflection* reflection = world.RegisterComponentReflection<EcsPosition>(
        "test.EcsPosition",
        {
            KB_ECS_FIELD(EcsPosition, x, kb::ecs::ComponentFieldType::Float32),
            KB_ECS_FIELD(EcsPosition, y, kb::ecs::ComponentFieldType::Float32),
        });
    kb::tests::Require(reflection != nullptr, "ECS serializer test could not register reflection");

    const EcsPosition source{ .x = 17.0F, .y = 23.0F };
    kb::ecs::SerializedComponent serialized;
    kb::tests::Require(kb::ecs::ComponentSerializer::Serialize(&source, *reflection, serialized), "ECS component serializer failed");
    kb::tests::Require(serialized.componentId == reflection->Id(), "ECS component serializer stored invalid component id");
    kb::tests::Require(serialized.componentName == "test.EcsPosition", "ECS component serializer stored invalid component name");
    kb::tests::Require(serialized.fields.size() == 2, "ECS component serializer stored invalid field count");

    const float* serializedX = std::get_if<float>(&serialized.fields[0].value);
    const float* serializedY = std::get_if<float>(&serialized.fields[1].value);
    kb::tests::Require(serializedX != nullptr && serializedY != nullptr, "ECS component serializer stored invalid value types");
    kb::tests::Require(kb::tests::NearlyEqual(*serializedX, 17.0F) && kb::tests::NearlyEqual(*serializedY, 23.0F), "ECS component serializer stored invalid values");

    EcsPosition restored{};
    kb::tests::Require(kb::ecs::ComponentSerializer::Apply(serialized, *reflection, &restored), "ECS component serializer apply failed");
    kb::tests::Require(kb::tests::NearlyEqual(restored.x, 17.0F) && kb::tests::NearlyEqual(restored.y, 23.0F), "ECS component serializer apply restored invalid values");
}

[[nodiscard]] const kb::ecs::SerializedEntity* FindSerializedEntityByName(const kb::ecs::SerializedWorld& world, std::string_view name) {
    for (const kb::ecs::SerializedEntity& entity : world.entities) {
        if (entity.name == name) {
            return &entity;
        }
    }
    return nullptr;
}

[[nodiscard]] const kb::ecs::SerializedEntity* FindSerializedEntityBySourceId(const kb::ecs::SerializedWorld& world, kb::ecs::Entity::IdType sourceId) {
    for (const kb::ecs::SerializedEntity& entity : world.entities) {
        if (entity.sourceId == sourceId) {
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

void RunWorldSerializationRestoreTest() {
    kb::ecs::World sourceWorld;
    RegisterPositionReflection(sourceWorld);

    const kb::ecs::Entity parent = sourceWorld.CreateEntity("Parent");
    const kb::ecs::Entity child = sourceWorld.CreateEntity("Child");
    sourceWorld.SetParent(child, parent);
    sourceWorld.Set(child, EcsPosition{ .x = 31.0F, .y = 41.0F });

    kb::ecs::SerializedWorld serializedWorld;
    kb::tests::Require(sourceWorld.SerializeWorld(serializedWorld), "ECS world serialization failed");
    kb::tests::Require(serializedWorld.entities.size() == 2, "ECS world serialization did not include parent stub and child");

    kb::ecs::World restoredWorld;
    RegisterPositionReflection(restoredWorld);
    kb::tests::Require(restoredWorld.RestoreSerializedWorld(serializedWorld), "ECS world restore failed");

    kb::ecs::SerializedWorld restoredSerializedWorld;
    kb::tests::Require(restoredWorld.SerializeWorld(restoredSerializedWorld), "ECS restored world serialization failed");

    const kb::ecs::SerializedEntity* restoredChild = FindSerializedEntityByName(restoredSerializedWorld, "Child");
    kb::tests::Require(restoredChild != nullptr, "ECS restored world is missing child entity");
    kb::tests::Require(restoredChild->components.size() == 1, "ECS restored child has invalid component count");
    kb::tests::Require(restoredChild->parentSourceId != 0, "ECS restored child is missing parent relation");

    const kb::ecs::SerializedEntity* restoredParent = FindSerializedEntityBySourceId(restoredSerializedWorld, restoredChild->parentSourceId);
    kb::tests::Require(restoredParent != nullptr && restoredParent->name == "Parent", "ECS restored child has invalid parent");

    const kb::ecs::SerializedComponent& restoredPosition = restoredChild->components[0];
    const float* restoredX = std::get_if<float>(&restoredPosition.fields[0].value);
    const float* restoredY = std::get_if<float>(&restoredPosition.fields[1].value);
    kb::tests::Require(restoredX != nullptr && restoredY != nullptr, "ECS restored component has invalid field value types");
    kb::tests::Require(kb::tests::NearlyEqual(*restoredX, 31.0F) && kb::tests::NearlyEqual(*restoredY, 41.0F), "ECS restored component has invalid field values");
}

} // namespace

namespace kb::tests {

void RunEcsReflectionTests() {
    RunComponentReflectionRegistrationTest();
    RunComponentReflectionValidationTest();
    RunComponentFieldAccessTest();
    RunComponentSerializerTest();
    RunWorldSerializationRestoreTest();
}

} // namespace kb::tests
