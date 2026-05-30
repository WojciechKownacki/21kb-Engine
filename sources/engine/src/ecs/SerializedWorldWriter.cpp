#include "ecs/serialization/SerializedWorldWriter.hpp"

#include "ecs/serialization/SerializedComponentFromSnapshot.hpp"
#include "ecs/serialization/SerializedWorldEntityIndex.hpp"
#include "engine/ecs/World.hpp"
#include "engine/ecs/WorldSnapshot.hpp"

#include <utility>

namespace kb::ecs {

namespace {

void AddParentStub(const World& world, SerializedWorldEntityIndex& index, Entity parent) {
    if (!parent.IsValid()) {
        return;
    }

    static_cast<void>(index.Ensure(parent.Id(), world.Name(parent)));
}

} // namespace

SerializedWorldWriter::SerializedWorldWriter(const World& world) noexcept
    : world_(world) {}

bool SerializedWorldWriter::Write(SerializedWorld& output) const {
    output.entities.clear();

    SerializedWorldEntityIndex index{ output };
    const WorldSnapshot snapshot = world_.CaptureSnapshot();
    for (const EntitySnapshot& entitySnapshot : snapshot.entities) {
        const Entity parent = world_.Parent(Entity{ entitySnapshot.id });
        AddParentStub(world_, index, parent);

        SerializedEntity& entity = index.Ensure(entitySnapshot.id, entitySnapshot.name);
        entity.parentSourceId = parent.Id();
        entity.components.clear();
        entity.components.reserve(entitySnapshot.components.size());
        for (const ComponentSnapshot& componentSnapshot : entitySnapshot.components) {
            const ComponentReflection* reflection = world_.Reflection(componentSnapshot.componentId);
            if (reflection == nullptr) {
                return false;
            }

            SerializedComponent serializedComponent;
            if (!SerializedComponentFromSnapshot::Serialize(componentSnapshot, *reflection, serializedComponent)) {
                return false;
            }
            entity.components.push_back(std::move(serializedComponent));
        }
    }

    return true;
}

} // namespace kb::ecs
