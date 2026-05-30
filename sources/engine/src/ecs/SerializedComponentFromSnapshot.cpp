#include "ecs/serialization/SerializedComponentFromSnapshot.hpp"

#include "engine/ecs/ComponentSerialization.hpp"

namespace kb::ecs {

bool SerializedComponentFromSnapshot::Serialize(const ComponentSnapshot& snapshot, const ComponentReflection& reflection, SerializedComponent& output) {
    if (snapshot.data.empty() || snapshot.componentId != reflection.Id()) {
        return false;
    }

    return ComponentSerializer::Serialize(snapshot.data.data(), reflection, output);
}

} // namespace kb::ecs
