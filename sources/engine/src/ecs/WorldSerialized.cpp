#include "engine/ecs/World.hpp"

#include "ecs/serialization/SerializedEntityComponentApplier.hpp"
#include "ecs/serialization/SerializedEntityComponentReader.hpp"
#include "ecs/serialization/SerializedWorldRestorer.hpp"
#include "ecs/serialization/SerializedWorldWriter.hpp"

namespace kb::ecs {

bool World::SerializeComponent(Entity entity, ComponentId componentId, SerializedComponent& output) const {
    return SerializedEntityComponentReader::Read(*this, entity, componentId, output);
}

bool World::ApplySerializedComponent(Entity entity, const SerializedComponent& component) {
    return SerializedEntityComponentApplier::Apply(*this, entity, component);
}

bool World::SerializeWorld(SerializedWorld& output) const {
    return SerializedWorldWriter{ *this }.Write(output);
}

bool World::RestoreSerializedWorld(const SerializedWorld& source) {
    return SerializedWorldRestorer{ *this }.Restore(source);
}

} // namespace kb::ecs
